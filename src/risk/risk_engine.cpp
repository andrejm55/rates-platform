#include "rates/risk/risk_engine.hpp"
#include "rates/pricing/pricers.hpp"

#include <algorithm>
#include <stdexcept>

namespace rates {
namespace {
MarketDataSnapshot with_curve(const MarketDataSnapshot& market, const std::string& id, const YieldCurve& curve) {
    MarketDataSnapshot shocked = market;
    shocked.curves[id] = curve;
    shocked.snapshot_id = market.snapshot_id + "-SHOCK";
    return shocked;
}
}

RiskReport RiskEngine::swap_risk(const InterestRateSwap& swap, const MarketDataSnapshot& market, double curve_bump) {
    if (curve_bump <= 0.0) throw std::invalid_argument("Curve bump must be positive");
    const double base = SwapPricer::price(swap, market).present_value;
    const auto& discount_curve = market.curve(swap.discount_curve_id);
    const auto& forward_curve = market.curve(swap.forward_curve_id);
    const double discount_up = SwapPricer::price(swap, with_curve(market, swap.discount_curve_id, discount_curve.bumped_parallel(curve_bump))).present_value;
    const double discount_down = SwapPricer::price(swap, with_curve(market, swap.discount_curve_id, discount_curve.bumped_parallel(-curve_bump))).present_value;
    const double forward_up = SwapPricer::price(swap, with_curve(market, swap.forward_curve_id, forward_curve.bumped_parallel(curve_bump))).present_value;
    const double forward_down = SwapPricer::price(swap, with_curve(market, swap.forward_curve_id, forward_curve.bumped_parallel(-curve_bump))).present_value;

    RiskReport report;
    report.base_present_value = base;
    report.sensitivities["discount_curve_parallel_pv01"] = (discount_up - discount_down) / 2.0;
    report.sensitivities["projection_curve_parallel_pv01"] = swap.discount_curve_id == swap.forward_curve_id
        ? 0.0
        : (forward_up - forward_down) / 2.0;
    report.sensitivities["parallel_pv01"] = report.sensitivities["discount_curve_parallel_pv01"] + report.sensitivities["projection_curve_parallel_pv01"];
    report.sensitivities["discount_curve_gamma_for_bump"] = discount_up - 2.0 * base + discount_down;
    report.scenario_pnl["discount_rates_up_100bp"] = SwapPricer::price(swap, with_curve(market, swap.discount_curve_id, discount_curve.bumped_parallel(0.01))).present_value - base;
    report.scenario_pnl["discount_rates_down_100bp"] = SwapPricer::price(swap, with_curve(market, swap.discount_curve_id, discount_curve.bumped_parallel(-0.01))).present_value - base;
    if (swap.discount_curve_id != swap.forward_curve_id) {
        report.scenario_pnl["projection_rates_up_100bp"] = SwapPricer::price(swap, with_curve(market, swap.forward_curve_id, forward_curve.bumped_parallel(0.01))).present_value - base;
        report.scenario_pnl["projection_rates_down_100bp"] = SwapPricer::price(swap, with_curve(market, swap.forward_curve_id, forward_curve.bumped_parallel(-0.01))).present_value - base;
    }

    for (std::size_t node = 0; node < discount_curve.times().size(); ++node) {
        const double node_up = SwapPricer::price(swap, with_curve(market, swap.discount_curve_id, discount_curve.bumped_node(node, curve_bump))).present_value;
        const double node_down = SwapPricer::price(swap, with_curve(market, swap.discount_curve_id, discount_curve.bumped_node(node, -curve_bump))).present_value;
        report.sensitivities["discount_bucketed_pv01_" + std::to_string(discount_curve.times()[node]) + "Y"] = (node_up - node_down) / 2.0;
    }
    if (swap.discount_curve_id != swap.forward_curve_id) {
        for (std::size_t node = 0; node < forward_curve.times().size(); ++node) {
            const double node_up = SwapPricer::price(swap, with_curve(market, swap.forward_curve_id, forward_curve.bumped_node(node, curve_bump))).present_value;
            const double node_down = SwapPricer::price(swap, with_curve(market, swap.forward_curve_id, forward_curve.bumped_node(node, -curve_bump))).present_value;
            report.sensitivities["projection_bucketed_pv01_" + std::to_string(forward_curve.times()[node]) + "Y"] = (node_up - node_down) / 2.0;
        }
    }
    report.warnings.push_back("Multi-curve risk bumps zero-rate nodes directly; production systems should rebuild discount and projection curves from shocked market quotes.");
    return report;
}

RiskReport RiskEngine::european_swaption_risk(const EuropeanSwaption& swaption, const MarketDataSnapshot& market,
                                              PricingModel model, double volatility,
                                              double curve_bump, double volatility_bump) {
    if (curve_bump <= 0.0 || volatility_bump <= 0.0) throw std::invalid_argument("Risk bumps must be positive");
    const auto base_result = EuropeanSwaptionPricer::price(swaption, market, model, volatility);
    const auto& discount_curve = market.curve(swaption.underlying.discount_curve_id);
    const auto& forward_curve = market.curve(swaption.underlying.forward_curve_id);
    const double discount_up = EuropeanSwaptionPricer::price(
        swaption, with_curve(market, swaption.underlying.discount_curve_id, discount_curve.bumped_parallel(curve_bump)), model, volatility).present_value;
    const double discount_down = EuropeanSwaptionPricer::price(
        swaption, with_curve(market, swaption.underlying.discount_curve_id, discount_curve.bumped_parallel(-curve_bump)), model, volatility).present_value;
    const double forward_up = EuropeanSwaptionPricer::price(
        swaption, with_curve(market, swaption.underlying.forward_curve_id, forward_curve.bumped_parallel(curve_bump)), model, volatility).present_value;
    const double forward_down = EuropeanSwaptionPricer::price(
        swaption, with_curve(market, swaption.underlying.forward_curve_id, forward_curve.bumped_parallel(-curve_bump)), model, volatility).present_value;
    const double vol_up = EuropeanSwaptionPricer::price(swaption, market, model, volatility + volatility_bump).present_value;
    const double vol_down = EuropeanSwaptionPricer::price(swaption, market, model, std::max(1e-10, volatility - volatility_bump)).present_value;

    RiskReport report;
    report.base_present_value = base_result.present_value;
    report.sensitivities["discount_curve_parallel_pv01"] = (discount_up - discount_down) / 2.0;
    report.sensitivities["projection_curve_parallel_pv01"] = swaption.underlying.discount_curve_id == swaption.underlying.forward_curve_id
        ? 0.0
        : (forward_up - forward_down) / 2.0;
    report.sensitivities["parallel_pv01"] = report.sensitivities["discount_curve_parallel_pv01"] + report.sensitivities["projection_curve_parallel_pv01"];
    report.sensitivities["vega_per_1bp_vol"] = (vol_up - vol_down) / 2.0;
    report.sensitivities["analytic_vega_per_1pct"] = base_result.sensitivities.at("vega_per_1pct");
    report.scenario_pnl["discount_rates_up_100bp"] = EuropeanSwaptionPricer::price(
        swaption, with_curve(market, swaption.underlying.discount_curve_id, discount_curve.bumped_parallel(0.01)), model, volatility).present_value - base_result.present_value;
    report.scenario_pnl["discount_rates_down_100bp"] = EuropeanSwaptionPricer::price(
        swaption, with_curve(market, swaption.underlying.discount_curve_id, discount_curve.bumped_parallel(-0.01)), model, volatility).present_value - base_result.present_value;
    if (swaption.underlying.discount_curve_id != swaption.underlying.forward_curve_id) {
        report.scenario_pnl["projection_rates_up_100bp"] = EuropeanSwaptionPricer::price(
            swaption, with_curve(market, swaption.underlying.forward_curve_id, forward_curve.bumped_parallel(0.01)), model, volatility).present_value - base_result.present_value;
        report.scenario_pnl["projection_rates_down_100bp"] = EuropeanSwaptionPricer::price(
            swaption, with_curve(market, swaption.underlying.forward_curve_id, forward_curve.bumped_parallel(-0.01)), model, volatility).present_value - base_result.present_value;
    }
    report.scenario_pnl["vol_up_20pct_relative"] = EuropeanSwaptionPricer::price(
        swaption, market, model, volatility * 1.2).present_value - base_result.present_value;
    report.scenario_pnl["vol_down_20pct_relative"] = EuropeanSwaptionPricer::price(
        swaption, market, model, volatility * 0.8).present_value - base_result.present_value;
    report.warnings.push_back("Finite-difference PV01 bumps zero-rate nodes directly and does not rebuild curves from instrument quotes.");
    return report;
}

}  // namespace rates

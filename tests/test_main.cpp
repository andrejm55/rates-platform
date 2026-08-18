#include "rates/calibration/curve_bootstrap.hpp"
#include "rates/core/date.hpp"
#include "rates/instruments/instruments.hpp"
#include "rates/market/market_snapshot.hpp"
#include "rates/models/option_models.hpp"
#include "rates/numerics/math.hpp"
#include "rates/pricing/pricers.hpp"
#include "rates/services/application_service.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check_close(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        ++failures;
        std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << '\n';
    }
}

double sample_mean_at_step(const std::vector<std::vector<double>>& paths, std::size_t step) {
    double total = 0.0;
    for (const auto& path : paths) total += path.at(step);
    return total / static_cast<double>(paths.size());
}

double sample_variance_at_step(const std::vector<std::vector<double>>& paths, std::size_t step, double mean) {
    double squared = 0.0;
    for (const auto& path : paths) {
        const double difference = path.at(step) - mean;
        squared += difference * difference;
    }
    return squared / static_cast<double>(paths.size() - 1);
}

rates::MarketDataSnapshot demo_market() {
    rates::MarketDataSnapshot market;
    market.valuation_date = rates::Date::parse("2026-07-27");
    market.snapshot_id = "TEST";
    market.curves.emplace("GBP-SONIA-DISCOUNT", rates::YieldCurve(
        "GBP-SONIA-DISCOUNT",
        {0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0},
        {0.040, 0.0395, 0.0390, 0.0380, 0.0370, 0.0365, 0.0360}
    ));
    market.curves.emplace("GBP-TERM-3M-FORWARD", rates::YieldCurve(
        "GBP-TERM-3M-FORWARD",
        {0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0},
        {0.0410, 0.0406, 0.0400, 0.0390, 0.0378, 0.0371, 0.0366}
    ));
    market.volatility_smiles.emplace("GBP-SWAPTION-NORMAL", rates::VolatilitySmile{
        "GBP-SWAPTION-NORMAL", {0.01, 0.03, 0.04, 0.05, 0.07}, {0.009, 0.0083, 0.0080, 0.0082, 0.0090}, true
    });
    market.volatility_cubes.emplace("GBP-SWAPTION-NORMAL-CUBE", rates::SwaptionVolatilityCube{
        "GBP-SWAPTION-NORMAL-CUBE",
        {1.0, 2.0},
        {5.0, 10.0},
        {0.03, 0.04, 0.05},
        {
            0.0080, 0.0078, 0.0081,
            0.0085, 0.0082, 0.0084,
            0.0088, 0.0084, 0.0086,
            0.0092, 0.0088, 0.0090
        },
        true
    });
    return market;
}
}

int main() {
    using namespace rates;

    const Date leap = Date::parse("2024-02-29");
    check(leap.add_years(1).str() == "2025-02-28", "Leap-day year addition");
    check(Date::parse("2026-01-31").add_months(1).str() == "2026-02-28", "End-of-month preservation");
    check_close(year_fraction(Date::parse("2026-01-01"), Date::parse("2027-01-01"), DayCountConvention::Actual365Fixed), 1.0, 1e-12, "ACT/365 year fraction");

    Calendar calendar;
    check(calendar.adjust(Date::parse("2026-07-25"), BusinessDayConvention::Following).str() == "2026-07-27", "Weekend adjustment");

    YieldCurve flat("FLAT", {1.0, 10.0}, {0.04, 0.04});
    check_close(flat.discount(5.0), std::exp(-0.2), 1e-12, "Continuous discounting");

    const auto bootstrapped = CurveBootstrapper::bootstrap("TEST-CURVE",
        {{0.25, 0.04}, {0.5, 0.0395}, {1.0, 0.039}},
        {{2.0, 0.038, 1.0}, {3.0, 0.0375, 1.0}, {5.0, 0.037, 1.0}});
    check(bootstrapped.converged, "Curve bootstrap converges");
    check(bootstrapped.curve.times().size() == 6, "Curve bootstrap creates all nodes");

    const auto multi_curve = CurveBootstrapper::bootstrap_multi_curve(
        "OIS", "TERM",
        {{0.25, 0.039}, {0.5, 0.0385}, {1.0, 0.038}},
        {{2.0, 0.0375, 1.0}, {3.0, 0.0372, 1.0}},
        {{0.25, 0.0405}},
        {{0.25, 0.5, 0.0410}, {0.5, 0.75, 0.0407}},
        {{0.75, 1.0, 95.95, 0.0001}},
        {{2.0, 0.039, 0.0008, 0.25}, {3.0, 0.0387, 0.0007, 0.25}});
    check(multi_curve.converged, "Multi-curve bootstrap converges");
    check(multi_curve.discount_curve.times().size() == 5, "OIS curve calibration creates discount nodes");
    check(multi_curve.projection_curve.times().size() == 6, "Projection curve calibration creates deposit/FRA/futures/basis nodes");

    const auto black_call = Black76::evaluate(0.04, 0.04, 0.20, 2.0, 1.0, OptionDirection::Payer);
    const auto black_put = Black76::evaluate(0.04, 0.04, 0.20, 2.0, 1.0, OptionDirection::Receiver);
    check_close(black_call.price, black_put.price, 1e-12, "ATM payer-receiver equality");
    check_close(
        Black76::evaluate(0.045, 0.04, 0.20, 2.0, 1.0, OptionDirection::Payer).price
            - Black76::evaluate(0.045, 0.04, 0.20, 2.0, 1.0, OptionDirection::Receiver).price,
        0.005,
        1e-12,
        "Black payer-receiver parity"
    );
    check_close(Black76::implied_volatility(black_call.price, 0.04, 0.04, 2.0, 1.0, OptionDirection::Payer), 0.20, 1e-8, "Black implied volatility inversion");

    const auto normal_call = Bachelier::evaluate(0.04, 0.04, 0.008, 2.0, 1.0, OptionDirection::Payer);
    check_close(normal_call.price, 0.008 * std::sqrt(2.0) * normal_pdf(0.0), 1e-12, "Bachelier ATM closed-form anchor");
    check_close(
        Bachelier::evaluate(0.045, 0.04, 0.008, 2.0, 1.0, OptionDirection::Payer).price
            - Bachelier::evaluate(0.045, 0.04, 0.008, 2.0, 1.0, OptionDirection::Receiver).price,
        0.005,
        1e-12,
        "Bachelier payer-receiver parity"
    );
    check_close(Bachelier::implied_volatility(normal_call.price, 0.04, 0.04, 2.0, 1.0, OptionDirection::Payer), 0.008, 1e-8, "Bachelier implied volatility inversion");

    const auto calibration = Sabr::calibrate(0.04, 2.0,
        {0.02, 0.03, 0.04, 0.05, 0.06}, {0.28, 0.235, 0.21, 0.205, 0.215}, 0.5, 0.03, 300);
    check(std::isfinite(calibration.root_mean_square_error), "SABR calibration produces finite error");
    check(calibration.model_volatilities.size() == 5, "SABR calibration returns model smile");

    SabrParameters normal_sabr{0.008, 0.0, -0.2, 0.5, 0.03};
    const double normal_sabr_vol = Sabr::normal_volatility(0.04, 0.04, 2.0, normal_sabr);
    check(normal_sabr_vol > 0.0 && normal_sabr_vol < 0.02, "Normal SABR returns rate-unit volatility");
    const auto normal_calibration = Sabr::calibrate(0.04, 2.0,
        {0.02, 0.03, 0.04, 0.05, 0.06}, {0.0089, 0.0083, 0.0080, 0.0082, 0.0088}, 0.0, 0.03, 200, true);
    check(normal_calibration.root_mean_square_error < 0.001, "Normal SABR calibration fits a normal smile");

    MarketDataSnapshot market = demo_market();
    InterestRateSwap swap;
    swap.effective_date = Date::parse("2027-07-29");
    swap.maturity_date = Date::parse("2032-07-29");
    swap.fixed_rate = 0.04;
    auto first_price = SwapPricer::price(swap, market);
    swap.fixed_rate = first_price.par_rate;
    auto par_price = SwapPricer::price(swap, market);
    check_close(par_price.present_value, 0.0, 1e-6, "Par swap has zero value");

    InterestRateSwap multi_curve_swap = swap;
    multi_curve_swap.discount_curve_id = "GBP-SONIA-DISCOUNT";
    multi_curve_swap.forward_curve_id = "GBP-TERM-3M-FORWARD";
    multi_curve_swap.floating_frequency_months = 3;
    multi_curve_swap.floating_day_count = DayCountConvention::Actual360;
    multi_curve_swap.fixed_rate = 0.04;
    const auto multi_curve_price = SwapPricer::price(multi_curve_swap, market);
    check(multi_curve_price.diagnostics.model == "DiscountProjectionCurve", "Swap uses separate discount and projection curves");
    check_close(multi_curve_price.diagnostics.metrics.at("floating_coupon_count"), 20.0, 1e-12, "Quarterly projected floating coupons are generated");
    check(std::abs(multi_curve_price.par_rate - first_price.par_rate) > 1e-5, "Projection curve changes the par rate");

    InterestRateSwap fixed_period_swap;
    fixed_period_swap.effective_date = Date::parse("2026-07-27");
    fixed_period_swap.maturity_date = Date::parse("2027-07-27");
    fixed_period_swap.forward_curve_id = "GBP-TERM-3M-FORWARD";
    fixed_period_swap.fixed_rate = 0.0;
    fixed_period_swap.floating_fixing_lag_business_days = 0;
    market.fixings["GBP-TERM-3M-FORWARD:2026-07-27"] = 0.055;
    const auto fixing_price = SwapPricer::price(fixed_period_swap, market);
    const double fixing_discount = market.curve("GBP-SONIA-DISCOUNT").discount(
        year_fraction(market.valuation_date, fixed_period_swap.maturity_date, DayCountConvention::Actual365Fixed));
    check_close(
        fixing_price.diagnostics.metrics.at("floating_leg_pv"),
        fixed_period_swap.notional * 0.055
            * year_fraction(fixed_period_swap.effective_date, fixed_period_swap.maturity_date, DayCountConvention::Actual365Fixed)
            * fixing_discount,
        1e-6,
        "Historical fixing overrides projected floating coupon"
    );

    RateIndex sonia;
    sonia.id = "GBP-SONIA";
    sonia.projection_curve_id = "GBP-SONIA-DISCOUNT";
    sonia.fixing_lag_business_days = 0;
    sonia.publication_lag_business_days = 0;
    sonia.lookback_business_days = 0;
    sonia.compounding = FloatingRateCompounding::OvernightCompounded;
    market.rate_indices.emplace(sonia.id, sonia);
    InterestRateSwap indexed_swap = fixed_period_swap;
    indexed_swap.floating_index_id = "GBP-SONIA";
    indexed_swap.forward_curve_id = "IGNORED-BY-INDEX";
    market.fixings["GBP-SONIA:2026-07-27"] = 0.052;
    const auto indexed_price = SwapPricer::price(indexed_swap, market);
    check(indexed_price.diagnostics.model == "SingleCurveProjection", "Rate index applies projection curve and compounding conventions");
    check_close(
        indexed_price.diagnostics.metrics.at("floating_leg_pv"),
        indexed_swap.notional * 0.052
            * year_fraction(indexed_swap.effective_date, indexed_swap.maturity_date, DayCountConvention::Actual365Fixed)
            * fixing_discount,
        1e-6,
        "Rate-index fixing overrides projected coupon"
    );

    EuropeanSwaption swaption;
    swaption.underlying = swap;
    swaption.exercise_date = Date::parse("2027-07-29");
    swaption.strike = swap.fixed_rate;
    const auto swaption_price = EuropeanSwaptionPricer::price(swaption, market, PricingModel::Bachelier, 0.008);
    check(swaption_price.present_value > 0.0, "European swaption has positive value");
    check(swaption_price.sensitivities.at("vega_per_1pct") > 0.0, "European swaption has positive vega");
    swaption.volatility_cube_id = "GBP-SWAPTION-NORMAL-CUBE";
    const auto cube_price = EuropeanSwaptionPricer::price(swaption, market, PricingModel::Bachelier);
    check(cube_price.implied_volatility > 0.007 && cube_price.implied_volatility < 0.009, "European swaption reads volatility cube");

    HullWhiteModel hull_white{0.03, 0.01};
    const auto paths = hull_white.simulate_factor_paths(1.0, 12, 100, 42);
    check(paths.size() == 100 && paths.front().size() == 13, "Hull-White simulation dimensions");
    check_close(hull_white.factor_variance(5.0), 0.00043196963219713696, 1e-14, "Hull-White exact variance anchor");
    check_close(hull_white.bond_loading(1.0, 6.0), 4.643067452498073, 1e-12, "Hull-White bond loading anchor");
    check(hull_white.normal_swaption_volatility(1.0, 5.0) > 0.0, "Hull-White analytic normal-vol benchmark");
    check(hull_white.factor_call_value(1.0, 0.0) > 0.0, "Hull-White factor option analytic benchmark");
    const auto hw_calibration = calibrate_hull_white_to_normal_volatilities(
        {1.0, 2.0, 5.0}, {5.0, 5.0, 10.0}, {0.045, 0.055, 0.075});
    check(hw_calibration.model.volatility > 0.0 && hw_calibration.root_mean_square_error < 0.01, "Hull-White calibration returns finite parameters");

    const auto convergence_paths = hull_white.simulate_factor_paths(5.0, 60, 20000, 7);
    const double simulated_mean = sample_mean_at_step(convergence_paths, 60);
    const double simulated_variance = sample_variance_at_step(convergence_paths, 60, simulated_mean);
    const double exact_variance = hull_white.factor_variance(5.0);
    check_close(simulated_mean, 0.0, 4.0e-4, "Hull-White Monte Carlo terminal mean convergence");
    check_close(simulated_variance, exact_variance, 8.0e-6, "Hull-White Monte Carlo terminal variance convergence");

    BermudanSwaption bermudan;
    bermudan.underlying = swap;
    bermudan.strike = swap.fixed_rate;
    bermudan.direction = OptionDirection::Payer;
    bermudan.exercise_dates = {
        Date::parse("2027-07-29"),
        Date::parse("2028-07-29"),
        Date::parse("2029-07-29")
    };
    const auto bermudan_low = BermudanSwaptionPricer::price(bermudan, market, hull_white, MonteCarloSettings{2000, 12, 99});
    const auto bermudan_high = BermudanSwaptionPricer::price(bermudan, market, hull_white, MonteCarloSettings{8000, 12, 99});
    check(bermudan_low.present_value > 0.0 && bermudan_high.present_value > 0.0, "Bermudan LSMC regression prices are positive");
    check(bermudan_high.diagnostics.standard_error < bermudan_low.diagnostics.standard_error * 0.8, "Bermudan LSMC standard error improves with more paths");
    check_close(
        bermudan_high.present_value,
        bermudan_low.present_value,
        4.0 * (bermudan_low.diagnostics.standard_error + bermudan_high.diagnostics.standard_error),
        "Bermudan LSMC path-count stability"
    );

    const std::string request_text = R"({
      "market": {
        "valuation_date": "2026-07-27",
        "snapshot_id": "JSON-TEST",
        "curves": [{"id":"GBP-SONIA-DISCOUNT","times":[0.25,1,5,10],"zero_rates":[0.04,0.039,0.037,0.0365]}],
        "volatility_smiles": [{"id":"GBP-SWAPTION-NORMAL","normal":true,"strikes":[0.01,0.04,0.07],"volatilities":[0.009,0.008,0.009]}]
      },
      "request": {
        "type": "swap",
        "effective_date": "2027-07-29",
        "maturity_date": "2032-07-29",
        "notional": 1000000,
        "fixed_rate": 0.04
      }
    })";
    const auto application_result = ApplicationService::execute(json::parse(request_text));
    check(application_result.at("success").as_bool(), "Application service JSON workflow");

    if (failures == 0) {
        std::cout << "All tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
}

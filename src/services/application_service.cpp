#include "rates/services/application_service.hpp"

#include "rates/calibration/curve_bootstrap.hpp"
#include "rates/instruments/instruments.hpp"
#include "rates/models/option_models.hpp"
#include "rates/portfolio/portfolio_pricer.hpp"
#include "rates/pricing/pricers.hpp"
#include "rates/risk/risk_engine.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace rates {
namespace {
using json::Value;

Value::Array number_array(const std::vector<double>& values) {
    Value::Array output;
    output.reserve(values.size());
    for (double value : values) output.emplace_back(value);
    return output;
}

Value curve_to_json(const YieldCurve& curve, const std::vector<double>& discount_factors) {
    Value result(Value::Object{});
    result["curve_id"] = curve.id();
    result["times"] = number_array(curve.times());
    result["zero_rates"] = number_array(curve.zero_rates());
    result["discount_factors"] = number_array(discount_factors);
    return result;
}

std::vector<double> parse_number_array(const Value& value) {
    std::vector<double> output;
    output.reserve(value.as_array().size());
    for (const auto& item : value.as_array()) output.push_back(item.as_number());
    return output;
}

std::vector<Date> parse_date_array(const Value& value) {
    std::vector<Date> output;
    output.reserve(value.as_array().size());
    for (const auto& item : value.as_array()) output.push_back(Date::parse(item.as_string()));
    return output;
}

DayCountConvention parse_day_count(const std::string& value) {
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (normalized == "act360" || normalized == "actual360" || normalized == "actual/360") return DayCountConvention::Actual360;
    if (normalized == "30/360" || normalized == "thirty360") return DayCountConvention::Thirty360;
    return DayCountConvention::Actual365Fixed;
}

FloatingRateCompounding parse_compounding(const std::string& value) {
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (normalized == "overnight_compounded" || normalized == "compounded" || normalized == "ois") {
        return FloatingRateCompounding::OvernightCompounded;
    }
    return FloatingRateCompounding::Simple;
}

InterestRateSwap parse_swap(const Value& request) {
    InterestRateSwap swap;
    swap.id = request.string_or("id", "SWAP");
    swap.currency = request.string_or("currency", "GBP");
    swap.discount_curve_id = request.string_or("discount_curve_id", "GBP-SONIA-DISCOUNT");
    swap.forward_curve_id = request.string_or("forward_curve_id", swap.discount_curve_id);
    swap.floating_index_id = request.string_or("floating_index_id", "");
    swap.effective_date = Date::parse(request.string_or("effective_date", "2026-07-29"));
    swap.maturity_date = Date::parse(request.string_or("maturity_date", "2031-07-29"));
    swap.notional = request.number_or("notional", 1'000'000.0);
    swap.fixed_rate = request.number_or("fixed_rate", 0.04);
    swap.pay_fixed = request.bool_or("pay_fixed", true);
    swap.fixed_frequency_months = request.int_or("fixed_frequency_months", 12);
    swap.floating_frequency_months = request.int_or("floating_frequency_months", swap.fixed_frequency_months);
    swap.floating_fixing_lag_business_days = request.int_or("floating_fixing_lag_business_days", 0);
    swap.floating_payment_lag_business_days = request.int_or("floating_payment_lag_business_days", 0);
    swap.floating_publication_lag_business_days = request.int_or("floating_publication_lag_business_days", 0);
    swap.floating_observation_shift_business_days = request.int_or("floating_observation_shift_business_days", 0);
    swap.floating_lookback_business_days = request.int_or("floating_lookback_business_days", 0);
    swap.floating_lockout_business_days = request.int_or("floating_lockout_business_days", 0);
    swap.fixed_day_count = parse_day_count(request.string_or("fixed_day_count", "actual365fixed"));
    swap.floating_day_count = parse_day_count(request.string_or("floating_day_count", "actual365fixed"));
    swap.floating_compounding = parse_compounding(request.string_or("floating_compounding", "simple"));
    return swap;
}

EuropeanSwaption parse_european_swaption(const Value& request) {
    EuropeanSwaption swaption;
    swaption.id = request.string_or("id", "EUROPEAN-SWAPTION");
    swaption.underlying = request.contains("underlying") ? parse_swap(request.at("underlying")) : parse_swap(request);
    swaption.exercise_date = Date::parse(request.string_or("exercise_date", "2027-07-29"));
    swaption.strike = request.number_or("strike", swaption.underlying.fixed_rate);
    swaption.underlying.fixed_rate = swaption.strike;
    swaption.direction = request.string_or("direction", "payer") == "receiver" ? OptionDirection::Receiver : OptionDirection::Payer;
    swaption.settlement = request.string_or("settlement", "physical") == "cash" ? SettlementType::Cash : SettlementType::Physical;
    swaption.long_position = request.bool_or("long_position", true);
    swaption.volatility_smile_id = request.string_or("volatility_smile_id", "GBP-SWAPTION-NORMAL");
    swaption.volatility_cube_id = request.string_or("volatility_cube_id", "");
    return swaption;
}

BermudanSwaption parse_bermudan_swaption(const Value& request) {
    BermudanSwaption swaption;
    swaption.id = request.string_or("id", "BERMUDAN-SWAPTION");
    swaption.underlying = request.contains("underlying") ? parse_swap(request.at("underlying")) : parse_swap(request);
    swaption.strike = request.number_or("strike", swaption.underlying.fixed_rate);
    swaption.underlying.fixed_rate = swaption.strike;
    swaption.direction = request.string_or("direction", "payer") == "receiver" ? OptionDirection::Receiver : OptionDirection::Payer;
    swaption.long_position = request.bool_or("long_position", true);
    if (request.contains("exercise_dates")) {
        swaption.exercise_dates = parse_date_array(request.at("exercise_dates"));
    } else {
        swaption.exercise_dates = {Date::parse("2027-07-29"), Date::parse("2028-07-29"), Date::parse("2029-07-29")};
    }
    return swaption;
}

CallableRangeAccrual parse_range_accrual(const Value& request) {
    CallableRangeAccrual product;
    product.id = request.string_or("id", "CALLABLE-RANGE-ACCRUAL");
    product.currency = request.string_or("currency", "GBP");
    product.discount_curve_id = request.string_or("discount_curve_id", "GBP-SONIA-DISCOUNT");
    product.effective_date = Date::parse(request.string_or("effective_date", "2026-07-29"));
    product.maturity_date = Date::parse(request.string_or("maturity_date", "2031-07-29"));
    product.notional = request.number_or("notional", 1'000'000.0);
    product.coupon_rate = request.number_or("coupon_rate", 0.06);
    product.lower_bound = request.number_or("lower_bound", 0.01);
    product.upper_bound = request.number_or("upper_bound", 0.06);
    product.observation_frequency_months = request.int_or("observation_frequency_months", 1);
    product.payment_frequency_months = request.int_or("payment_frequency_months", 12);
    product.redemption_fraction = request.number_or("redemption_fraction", 1.0);
    if (request.contains("issuer_call_dates")) product.issuer_call_dates = parse_date_array(request.at("issuer_call_dates"));
    return product;
}

PricingModel parse_model(const std::string& model) {
    if (model == "black" || model == "black76") return PricingModel::Black76;
    if (model == "sabr" || model == "sabr_black") return PricingModel::SabrBlack;
    if (model == "normal_sabr" || model == "sabr_normal") return PricingModel::SabrNormal;
    if (model == "hull_white" || model == "lsmc") return PricingModel::HullWhiteLsmc;
    return PricingModel::Bachelier;
}

MonteCarloSettings parse_mc_settings(const Value& request) {
    MonteCarloSettings settings;
    if (request.contains("monte_carlo")) {
        const auto& mc = request.at("monte_carlo");
        settings.paths = mc.int_or("paths", settings.paths);
        settings.steps_per_year = mc.int_or("steps_per_year", settings.steps_per_year);
        settings.seed = static_cast<unsigned>(mc.int_or("seed", static_cast<int>(settings.seed)));
    } else {
        settings.paths = request.int_or("paths", settings.paths);
        settings.steps_per_year = request.int_or("steps_per_year", settings.steps_per_year);
        settings.seed = static_cast<unsigned>(request.int_or("seed", static_cast<int>(settings.seed)));
    }
    return settings;
}

HullWhiteModel parse_hull_white(const Value& request) {
    HullWhiteModel model;
    if (request.contains("hull_white")) {
        const auto& hw = request.at("hull_white");
        model.mean_reversion = hw.number_or("mean_reversion", model.mean_reversion);
        model.volatility = hw.number_or("volatility", model.volatility);
    } else {
        model.mean_reversion = request.number_or("mean_reversion", model.mean_reversion);
        model.volatility = request.number_or("model_volatility", model.volatility);
    }
    return model;
}

Value risk_to_json(const RiskReport& report) {
    Value output(Value::Object{});
    output["base_present_value"] = report.base_present_value;
    Value sensitivities(Value::Object{});
    for (const auto& [name, value] : report.sensitivities) sensitivities[name] = value;
    output["sensitivities"] = sensitivities;
    Value scenarios(Value::Object{});
    for (const auto& [name, value] : report.scenario_pnl) scenarios[name] = value;
    output["scenario_pnl"] = scenarios;
    Value::Array warnings;
    for (const auto& warning : report.warnings) warnings.emplace_back(warning);
    output["warnings"] = warnings;
    return output;
}

Trade parse_trade(const Value& value) {
    Trade trade;
    trade.trade_id = value.string_or("trade_id", "TRADE");
    trade.book = value.string_or("book", "DEMO");
    trade.quantity = value.number_or("quantity", 1.0);
    const std::string type = value.at("instrument_type").as_string();
    const Value& instrument = value.contains("instrument") ? value.at("instrument") : value;
    if (type == "swap") trade.instrument = parse_swap(instrument);
    else if (type == "european_swaption") trade.instrument = parse_european_swaption(instrument);
    else if (type == "bermudan_swaption") trade.instrument = parse_bermudan_swaption(instrument);
    else if (type == "callable_range_accrual") trade.instrument = parse_range_accrual(instrument);
    else throw std::invalid_argument("Unsupported portfolio instrument type: " + type);
    return trade;
}
}

MarketDataSnapshot ApplicationService::parse_market(const Value& value) {
    MarketDataSnapshot market;
    market.valuation_date = Date::parse(value.string_or("valuation_date", "2026-07-27"));
    market.snapshot_id = value.string_or("snapshot_id", "DEMO-SNAPSHOT");
    if (!value.contains("curves")) throw std::invalid_argument("Market data requires curves");
    for (const auto& curve_value : value.at("curves").as_array()) {
        const std::string id = curve_value.at("id").as_string();
        market.curves.emplace(id, YieldCurve(id, parse_number_array(curve_value.at("times")), parse_number_array(curve_value.at("zero_rates"))));
    }
    if (value.contains("volatility_smiles")) {
        for (const auto& smile_value : value.at("volatility_smiles").as_array()) {
            VolatilitySmile smile;
            smile.id = smile_value.at("id").as_string();
            smile.normal = smile_value.bool_or("normal", false);
            smile.strikes = parse_number_array(smile_value.at("strikes"));
            smile.volatilities = parse_number_array(smile_value.at("volatilities"));
            market.volatility_smiles.emplace(smile.id, std::move(smile));
        }
    }
    if (value.contains("volatility_cubes")) {
        for (const auto& cube_value : value.at("volatility_cubes").as_array()) {
            SwaptionVolatilityCube cube;
            cube.id = cube_value.at("id").as_string();
            cube.normal = cube_value.bool_or("normal", false);
            cube.expiries = parse_number_array(cube_value.at("expiries"));
            cube.tenors = parse_number_array(cube_value.at("tenors"));
            cube.strikes = parse_number_array(cube_value.at("strikes"));
            cube.volatilities = parse_number_array(cube_value.at("volatilities"));
            market.volatility_cubes.emplace(cube.id, std::move(cube));
        }
    }
    if (value.contains("fixings")) {
        for (const auto& [key, fixing] : value.at("fixings").as_object()) {
            market.fixings.emplace(key, fixing.as_number());
        }
    }
    if (value.contains("rate_indices")) {
        for (const auto& index_value : value.at("rate_indices").as_array()) {
            RateIndex index;
            index.id = index_value.at("id").as_string();
            index.currency = index_value.string_or("currency", "GBP");
            index.projection_curve_id = index_value.string_or("projection_curve_id", "");
            index.fixing_lag_business_days = index_value.int_or("fixing_lag_business_days", 0);
            index.publication_lag_business_days = index_value.int_or("publication_lag_business_days", 0);
            index.observation_shift_business_days = index_value.int_or("observation_shift_business_days", 0);
            index.lookback_business_days = index_value.int_or("lookback_business_days", 0);
            index.lockout_business_days = index_value.int_or("lockout_business_days", 0);
            index.day_count = parse_day_count(index_value.string_or("day_count", "actual365fixed"));
            index.compounding = parse_compounding(index_value.string_or("compounding", "simple"));
            market.rate_indices.emplace(index.id, std::move(index));
        }
    }
    return market;
}

Value ApplicationService::pricing_result_to_json(const PricingResult& result) {
    Value output(Value::Object{});
    output["present_value"] = result.present_value;
    output["currency"] = result.currency;
    output["par_rate"] = result.par_rate;
    output["annuity"] = result.annuity;
    output["implied_volatility"] = result.implied_volatility;
    Value sensitivities(Value::Object{});
    for (const auto& [name, value] : result.sensitivities) sensitivities[name] = value;
    output["sensitivities"] = sensitivities;
    Value::Array cashflows;
    for (const auto& cashflow : result.cash_flows) {
        cashflows.emplace_back(Value::Object{
            {"payment_date", cashflow.payment_date},
            {"amount", cashflow.amount},
            {"discount_factor", cashflow.discount_factor},
            {"present_value", cashflow.present_value},
            {"description", cashflow.description}
        });
    }
    output["cash_flows"] = cashflows;
    Value diagnostics(Value::Object{});
    diagnostics["engine"] = result.diagnostics.engine;
    diagnostics["model"] = result.diagnostics.model;
    diagnostics["converged"] = result.diagnostics.converged;
    diagnostics["iterations"] = result.diagnostics.iterations;
    diagnostics["standard_error"] = result.diagnostics.standard_error;
    Value metrics(Value::Object{});
    for (const auto& [name, value] : result.diagnostics.metrics) metrics[name] = value;
    diagnostics["metrics"] = metrics;
    Value::Array warnings;
    for (const auto& warning : result.diagnostics.warnings) warnings.emplace_back(warning);
    diagnostics["warnings"] = warnings;
    output["diagnostics"] = diagnostics;
    return output;
}

Value ApplicationService::execute(const Value& root) {
    const MarketDataSnapshot market = parse_market(root.at("market"));
    const Value& request = root.at("request");
    const std::string type = request.at("type").as_string();

    Value output(Value::Object{});
    output["success"] = true;
    output["request_type"] = type;
    output["snapshot_id"] = market.snapshot_id;

    if (type == "curve_bootstrap") {
        std::vector<DepositQuote> deposits;
        std::vector<SwapQuote> swaps;
        if (request.contains("deposits")) {
            for (const auto& quote : request.at("deposits").as_array()) {
                deposits.push_back({quote.at("maturity").as_number(), quote.at("rate").as_number()});
            }
        }
        if (request.contains("swaps")) {
            for (const auto& quote : request.at("swaps").as_array()) {
                swaps.push_back({quote.at("maturity").as_number(), quote.at("par_rate").as_number(), quote.number_or("payment_interval", 1.0)});
            }
        }
        const auto calibration = CurveBootstrapper::bootstrap(request.string_or("curve_id", "BOOTSTRAPPED-CURVE"), deposits, swaps);
        Value result(Value::Object{});
        result["curve_id"] = calibration.curve.id();
        result["times"] = number_array(calibration.curve.times());
        result["zero_rates"] = number_array(calibration.curve.zero_rates());
        result["discount_factors"] = number_array(calibration.discount_factors);
        result["calibration_residuals"] = number_array(calibration.calibration_residuals);
        result["converged"] = calibration.converged;
        Value::Array warnings;
        for (const auto& warning : calibration.warnings) warnings.emplace_back(warning);
        result["warnings"] = warnings;
        output["result"] = result;
    } else if (type == "multi_curve_bootstrap") {
        std::vector<DepositQuote> ois_deposits;
        std::vector<SwapQuote> ois_swaps;
        std::vector<DepositQuote> projection_deposits;
        std::vector<FraQuote> fras;
        std::vector<FuturesQuote> futures;
        std::vector<BasisSwapQuote> basis_swaps;
        if (request.contains("ois_deposits")) {
            for (const auto& quote : request.at("ois_deposits").as_array()) {
                ois_deposits.push_back({quote.at("maturity").as_number(), quote.at("rate").as_number()});
            }
        }
        if (request.contains("ois_swaps")) {
            for (const auto& quote : request.at("ois_swaps").as_array()) {
                ois_swaps.push_back({quote.at("maturity").as_number(), quote.at("par_rate").as_number(), quote.number_or("payment_interval", 1.0)});
            }
        }
        if (request.contains("projection_deposits")) {
            for (const auto& quote : request.at("projection_deposits").as_array()) {
                projection_deposits.push_back({quote.at("maturity").as_number(), quote.at("rate").as_number()});
            }
        }
        if (request.contains("fras")) {
            for (const auto& quote : request.at("fras").as_array()) {
                fras.push_back({quote.at("start").as_number(), quote.at("end").as_number(), quote.at("rate").as_number()});
            }
        }
        if (request.contains("futures")) {
            for (const auto& quote : request.at("futures").as_array()) {
                futures.push_back({
                    quote.at("start").as_number(),
                    quote.at("end").as_number(),
                    quote.at("price").as_number(),
                    quote.number_or("convexity_adjustment", 0.0)
                });
            }
        }
        if (request.contains("basis_swaps")) {
            for (const auto& quote : request.at("basis_swaps").as_array()) {
                basis_swaps.push_back({
                    quote.at("maturity").as_number(),
                    quote.at("par_rate").as_number(),
                    quote.number_or("basis_spread", 0.0),
                    quote.number_or("payment_interval", 0.25)
                });
            }
        }
        const auto calibration = CurveBootstrapper::bootstrap_multi_curve(
            request.string_or("discount_curve_id", "OIS-DISCOUNT"),
            request.string_or("projection_curve_id", "TERM-PROJECTION"),
            ois_deposits, ois_swaps, projection_deposits, fras, futures, basis_swaps);
        Value result(Value::Object{});
        result["discount_curve"] = curve_to_json(calibration.discount_curve, calibration.discount_factors);
        result["projection_curve"] = curve_to_json(calibration.projection_curve, calibration.projection_discount_factors);
        result["discount_residuals"] = number_array(calibration.discount_residuals);
        result["projection_residuals"] = number_array(calibration.projection_residuals);
        result["converged"] = calibration.converged;
        Value::Array warnings;
        for (const auto& warning : calibration.warnings) warnings.emplace_back(warning);
        result["warnings"] = warnings;
        output["result"] = result;
    } else if (type == "swap") {
        output["result"] = pricing_result_to_json(SwapPricer::price(parse_swap(request), market));
    } else if (type == "european_swaption") {
        const auto swaption = parse_european_swaption(request);
        const PricingModel model = parse_model(request.string_or("model", "bachelier"));
        const double volatility = request.number_or("volatility", -1.0);
        SabrParameters sabr;
        if (request.contains("sabr")) {
            const auto& parameters = request.at("sabr");
            sabr.alpha = parameters.number_or("alpha", sabr.alpha);
            sabr.beta = parameters.number_or("beta", sabr.beta);
            sabr.rho = parameters.number_or("rho", sabr.rho);
            sabr.nu = parameters.number_or("nu", sabr.nu);
            sabr.shift = parameters.number_or("shift", sabr.shift);
        }
        output["result"] = pricing_result_to_json(EuropeanSwaptionPricer::price(swaption, market, model, volatility, sabr));
    } else if (type == "sabr_calibration") {
        const double forward = request.at("forward").as_number();
        const double expiry = request.at("expiry").as_number();
        const auto strikes = parse_number_array(request.at("strikes"));
        const auto volatilities = parse_number_array(request.at("volatilities"));
        const bool normal_volatilities = request.bool_or("normal", false);
        const auto calibration = Sabr::calibrate(forward, expiry, strikes, volatilities,
                                                 request.number_or("beta", 0.5), request.number_or("shift", 0.0),
                                                 request.int_or("max_iterations", 400), normal_volatilities);
        Value result(Value::Object{});
        result["alpha"] = calibration.parameters.alpha;
        result["beta"] = calibration.parameters.beta;
        result["rho"] = calibration.parameters.rho;
        result["nu"] = calibration.parameters.nu;
        result["shift"] = calibration.parameters.shift;
        result["rmse"] = calibration.root_mean_square_error;
        result["iterations"] = calibration.iterations;
        result["converged"] = calibration.converged;
        result["model_volatilities"] = number_array(calibration.model_volatilities);
        output["result"] = result;
    } else if (type == "hull_white_calibration") {
        const auto expiries = parse_number_array(request.at("expiries"));
        const auto tenors = parse_number_array(request.at("tenors"));
        const auto volatilities = parse_number_array(request.at("normal_volatilities"));
        const auto calibration = calibrate_hull_white_to_normal_volatilities(expiries, tenors, volatilities);
        Value result(Value::Object{});
        result["mean_reversion"] = calibration.model.mean_reversion;
        result["volatility"] = calibration.model.volatility;
        result["rmse"] = calibration.root_mean_square_error;
        result["converged"] = calibration.converged;
        result["model_volatilities"] = number_array(calibration.model_volatilities);
        output["result"] = result;
    } else if (type == "bermudan_swaption") {
        output["result"] = pricing_result_to_json(BermudanSwaptionPricer::price(
            parse_bermudan_swaption(request), market, parse_hull_white(request), parse_mc_settings(request)));
    } else if (type == "callable_range_accrual") {
        output["result"] = pricing_result_to_json(CallableRangeAccrualPricer::price(
            parse_range_accrual(request), market, parse_hull_white(request), parse_mc_settings(request)));
    } else if (type == "swap_risk") {
        output["result"] = risk_to_json(RiskEngine::swap_risk(parse_swap(request), market, request.number_or("curve_bump", 0.0001)));
    } else if (type == "swaption_risk") {
        const auto swaption = parse_european_swaption(request);
        output["result"] = risk_to_json(RiskEngine::european_swaption_risk(
            swaption, market, parse_model(request.string_or("model", "bachelier")),
            request.number_or("volatility", 0.008), request.number_or("curve_bump", 0.0001),
            request.number_or("volatility_bump", 0.0001)));
    } else if (type == "portfolio") {
        Portfolio portfolio;
        portfolio.portfolio_id = request.string_or("portfolio_id", "DEMO-PORTFOLIO");
        portfolio.reporting_currency = request.string_or("reporting_currency", "GBP");
        for (const auto& trade : request.at("trades").as_array()) portfolio.trades.push_back(parse_trade(trade));
        const auto valuation = PortfolioPricer::price(portfolio, market,
                                                      parse_model(request.string_or("european_swaption_model", "bachelier")),
                                                      request.number_or("default_swaption_volatility", 0.008),
                                                      parse_mc_settings(request));
        Value result(Value::Object{});
        result["portfolio_id"] = valuation.portfolio_id;
        result["total_present_value"] = valuation.total_present_value;
        Value by_book(Value::Object{});
        for (const auto& [book, pv] : valuation.present_value_by_book) by_book[book] = pv;
        result["present_value_by_book"] = by_book;
        Value::Array trades;
        for (const auto& trade : valuation.trades) {
            trades.emplace_back(Value::Object{
                {"trade_id", trade.trade_id}, {"book", trade.book}, {"quantity", trade.quantity},
                {"present_value", trade.present_value}, {"currency", trade.currency},
                {"success", trade.success}, {"error", trade.error}
            });
        }
        result["trades"] = trades;
        output["result"] = result;
    } else {
        throw std::invalid_argument("Unsupported request type: " + type);
    }
    return output;
}

}  // namespace rates

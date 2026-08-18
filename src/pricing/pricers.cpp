#include "rates/pricing/pricers.hpp"
#include "rates/numerics/math.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace rates {
namespace {
double time_from(const Date& valuation_date, const Date& future_date) {
    return std::max(0.0, year_fraction(valuation_date, future_date, DayCountConvention::Actual365Fixed));
}

struct SwapAnalytics {
    double annuity{0.0};
    double forward{0.0};
    double fixed_leg_pv{0.0};
    double floating_leg_pv{0.0};
    std::vector<CashFlowResult> fixed_cash_flows;
    std::vector<CashFlowResult> floating_cash_flows;
};

InterestRateSwap with_rate_index_conventions(InterestRateSwap swap, const MarketDataSnapshot& market) {
    if (swap.floating_index_id.empty()) return swap;
    const RateIndex* index = market.rate_index(swap.floating_index_id);
    if (!index) throw std::invalid_argument("Missing rate index: " + swap.floating_index_id);
    if (!index->projection_curve_id.empty()) swap.forward_curve_id = index->projection_curve_id;
    swap.floating_fixing_lag_business_days = index->fixing_lag_business_days;
    swap.floating_publication_lag_business_days = index->publication_lag_business_days;
    swap.floating_observation_shift_business_days = index->observation_shift_business_days;
    swap.floating_lookback_business_days = index->lookback_business_days;
    swap.floating_lockout_business_days = index->lockout_business_days;
    swap.floating_day_count = index->day_count;
    swap.floating_compounding = index->compounding;
    return swap;
}

Date effective_fixing_date(const InterestRateSwap& swap, const MarketDataSnapshot& market,
                           const SchedulePeriod& period) {
    Calendar calendar;
    Date fixing_date = period.fixing_date;
    const int observation_offset = swap.floating_observation_shift_business_days + swap.floating_lookback_business_days;
    if (observation_offset != 0) fixing_date = calendar.advance_business_days(fixing_date, -observation_offset);
    if (swap.floating_publication_lag_business_days > 0) {
        const Date publication_date = calendar.advance_business_days(fixing_date, swap.floating_publication_lag_business_days);
        if (publication_date > market.valuation_date) return period.fixing_date;
    }
    return fixing_date;
}

double floating_coupon_rate(const InterestRateSwap& swap, const MarketDataSnapshot& market,
                            const SchedulePeriod& period, const YieldCurve& forward_curve) {
    const Date fixing_date = effective_fixing_date(swap, market, period);
    if (fixing_date <= market.valuation_date) {
        const std::string index_key = swap.floating_index_id.empty() ? "" : swap.floating_index_id + ":" + fixing_date.str();
        if (!index_key.empty()) {
            const auto indexed = market.fixings.find(index_key);
            if (indexed != market.fixings.end()) return indexed->second;
        }
        const std::string dated_key = swap.forward_curve_id + ":" + fixing_date.str();
        const auto dated = market.fixings.find(dated_key);
        if (dated != market.fixings.end()) return dated->second;
        const auto generic = market.fixings.find(fixing_date.str());
        if (generic != market.fixings.end()) return generic->second;
    }
    const double start_time = time_from(market.valuation_date, period.accrual_start);
    const double end_time = time_from(market.valuation_date, period.accrual_end);
    if (swap.floating_compounding == FloatingRateCompounding::OvernightCompounded) {
        return forward_curve.forward_rate(start_time, end_time);
    }
    return forward_curve.forward_rate(start_time, end_time);
}

SwapAnalytics swap_analytics(const InterestRateSwap& swap, const MarketDataSnapshot& market) {
    const InterestRateSwap priced_swap = with_rate_index_conventions(swap, market);
    const auto& discount_curve = market.curve(priced_swap.discount_curve_id);
    const auto& forward_curve = market.curve(priced_swap.forward_curve_id);
    Calendar calendar;
    const auto fixed_schedule = generate_schedule(priced_swap.effective_date, priced_swap.maturity_date, priced_swap.fixed_frequency_months,
                                                  calendar, priced_swap.business_day_convention, priced_swap.fixed_day_count);
    const auto floating_schedule = generate_schedule(
        priced_swap.effective_date, priced_swap.maturity_date, priced_swap.floating_frequency_months, calendar,
        priced_swap.business_day_convention, priced_swap.floating_day_count, priced_swap.floating_fixing_lag_business_days,
        priced_swap.floating_payment_lag_business_days);
    SwapAnalytics analytics;
    for (const auto& period : fixed_schedule) {
        const double payment_time = time_from(market.valuation_date, period.payment_date);
        const double discount = discount_curve.discount(payment_time);
        analytics.annuity += period.year_fraction * discount;
        const double amount = priced_swap.notional * priced_swap.fixed_rate * period.year_fraction;
        analytics.fixed_leg_pv += amount * discount;
        analytics.fixed_cash_flows.push_back({period.payment_date.str(), amount, discount, amount * discount, "Fixed coupon"});
    }
    for (const auto& period : floating_schedule) {
        const double payment_time = time_from(market.valuation_date, period.payment_date);
        const double projected_rate = floating_coupon_rate(priced_swap, market, period, forward_curve);
        const double amount = priced_swap.notional * projected_rate * period.year_fraction;
        const double discount = discount_curve.discount(payment_time);
        analytics.floating_leg_pv += amount * discount;
        analytics.floating_cash_flows.push_back({
            period.payment_date.str(), amount, discount, amount * discount,
            "Floating coupon projected from " + priced_swap.forward_curve_id
        });
    }
    analytics.forward = analytics.annuity == 0.0 ? 0.0 : analytics.floating_leg_pv / (priced_swap.notional * analytics.annuity);
    return analytics;
}

double conditional_discount(const YieldCurve& curve, double current_time, double maturity_time,
                            double factor, const HullWhiteModel& model) {
    if (maturity_time <= current_time) return 1.0;
    const double ratio = curve.discount(maturity_time) / curve.discount(current_time);
    const double loading = model.bond_loading(current_time, maturity_time);
    return ratio * std::exp(-loading * factor);
}

std::size_t nearest_step(double time, double dt, int max_step) {
    return static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(time / dt)), 0, max_step));
}
}

PricingResult SwapPricer::price(const InterestRateSwap& swap, const MarketDataSnapshot& market) {
    const InterestRateSwap priced_swap = with_rate_index_conventions(swap, market);
    const auto analytics = swap_analytics(swap, market);
    const double payer_value = analytics.floating_leg_pv - analytics.fixed_leg_pv;

    PricingResult result;
    result.present_value = priced_swap.pay_fixed ? payer_value : -payer_value;
    result.currency = priced_swap.currency;
    result.par_rate = analytics.forward;
    result.annuity = priced_swap.notional * analytics.annuity;
    result.cash_flows = analytics.fixed_cash_flows;
    result.cash_flows.insert(result.cash_flows.end(), analytics.floating_cash_flows.begin(), analytics.floating_cash_flows.end());
    result.diagnostics.engine = "MultiCurveSwapEngine";
    result.diagnostics.model = priced_swap.discount_curve_id == priced_swap.forward_curve_id ? "SingleCurveProjection" : "DiscountProjectionCurve";
    result.diagnostics.metrics["floating_leg_pv"] = analytics.floating_leg_pv;
    result.diagnostics.metrics["fixed_leg_pv"] = analytics.fixed_leg_pv;
    result.diagnostics.metrics["fixed_leg_annuity"] = analytics.annuity;
    result.diagnostics.metrics["fixed_coupon_count"] = static_cast<double>(analytics.fixed_cash_flows.size());
    result.diagnostics.metrics["floating_coupon_count"] = static_cast<double>(analytics.floating_cash_flows.size());
    result.diagnostics.warnings.push_back("Floating coupons are projected coupon-by-coupon from the configured forward curve; historical fixings, publication lags and overnight compounding are simplified in this release.");
    return result;
}

double SwapPricer::conditional_swap_value(const InterestRateSwap& swap, const YieldCurve& discount_curve,
                                           const YieldCurve& forward_curve,
                                           const Date& valuation_date, const Date& exercise_date,
                                           double factor, const HullWhiteModel& model) {
    Calendar calendar;
    const auto fixed_schedule = generate_schedule(swap.effective_date, swap.maturity_date, swap.fixed_frequency_months,
                                                  calendar, swap.business_day_convention, swap.fixed_day_count);
    const auto floating_schedule = generate_schedule(
        swap.effective_date, swap.maturity_date, swap.floating_frequency_months, calendar,
        swap.business_day_convention, swap.floating_day_count, swap.floating_fixing_lag_business_days,
        swap.floating_payment_lag_business_days);
    const double exercise_time = time_from(valuation_date, exercise_date);
    double fixed = 0.0;
    double floating = 0.0;
    for (const auto& period : fixed_schedule) {
        if (period.payment_date <= exercise_date) continue;
        const double payment_time = time_from(valuation_date, period.payment_date);
        const double discount = conditional_discount(discount_curve, exercise_time, payment_time, factor, model);
        fixed += swap.notional * swap.fixed_rate * period.year_fraction * discount;
    }
    for (const auto& period : floating_schedule) {
        if (period.payment_date <= exercise_date) continue;
        const double start_time = time_from(valuation_date, period.accrual_start);
        const double end_time = time_from(valuation_date, period.accrual_end);
        const double payment_time = time_from(valuation_date, period.payment_date);
        const double projected_rate = forward_curve.forward_rate(start_time, end_time) + factor;
        const double discount = conditional_discount(discount_curve, exercise_time, payment_time, factor, model);
        floating += swap.notional * projected_rate * period.year_fraction * discount;
    }
    const double payer = floating - fixed;
    return swap.pay_fixed ? payer : -payer;
}

PricingResult EuropeanSwaptionPricer::price(const EuropeanSwaption& swaption, const MarketDataSnapshot& market,
                                            PricingModel model, double explicit_volatility,
                                            const SabrParameters& sabr_parameters) {
    InterestRateSwap underlying = swaption.underlying;
    underlying.fixed_rate = swaption.strike;
    const auto analytics = swap_analytics(underlying, market);
    const double expiry = time_from(market.valuation_date, swaption.exercise_date);
    const double tenor = time_from(swaption.exercise_date, swaption.underlying.maturity_date);
    if (expiry <= 0.0) throw std::invalid_argument("Swaption exercise must be after valuation date");

    double volatility = explicit_volatility;
    bool normal = false;
    if (volatility < 0.0) {
        if (!swaption.volatility_cube_id.empty()) {
            const auto& cube = market.volatility_cube(swaption.volatility_cube_id);
            volatility = cube.volatility(expiry, tenor, swaption.strike);
            normal = cube.normal;
        } else {
            const auto& smile = market.smile(swaption.volatility_smile_id);
            volatility = smile.volatility(swaption.strike);
            normal = smile.normal;
        }
    }

    const double discount_annuity = swaption.underlying.notional * analytics.annuity;
    OptionModelResult option;
    std::string model_name;
    if (model == PricingModel::Bachelier || normal || model == PricingModel::SabrNormal) {
        if (model == PricingModel::SabrNormal) {
            volatility = Sabr::normal_volatility(analytics.forward, swaption.strike, expiry, sabr_parameters);
        }
        option = Bachelier::evaluate(analytics.forward, swaption.strike, volatility, expiry,
                                     discount_annuity, swaption.direction);
        model_name = model == PricingModel::SabrNormal ? "SABR-Bachelier" : "Bachelier";
    } else {
        if (model == PricingModel::SabrBlack) {
            volatility = Sabr::lognormal_volatility(analytics.forward, swaption.strike, expiry, sabr_parameters);
            model_name = "SABR-Black76";
        } else {
            model_name = "Black76";
        }
        option = Black76::evaluate(analytics.forward, swaption.strike, volatility, expiry,
                                   discount_annuity, swaption.direction, sabr_parameters.shift);
    }

    PricingResult result;
    const double position_sign = swaption.long_position ? 1.0 : -1.0;
    result.present_value = position_sign * option.price;
    result.currency = swaption.underlying.currency;
    result.par_rate = analytics.forward;
    result.annuity = discount_annuity;
    result.implied_volatility = volatility;
    result.sensitivities["forward_delta"] = position_sign * option.delta;
    result.sensitivities["forward_gamma"] = position_sign * option.gamma;
    result.sensitivities["vega_per_1.00"] = position_sign * option.vega;
    result.sensitivities["vega_per_1pct"] = position_sign * option.vega * 0.01;
    result.diagnostics.engine = "AnalyticEuropeanSwaptionEngine";
    result.diagnostics.model = model_name;
    result.diagnostics.metrics["expiry_years"] = expiry;
    result.diagnostics.metrics["underlying_tenor_years"] = tenor;
    result.diagnostics.metrics["forward_swap_rate"] = analytics.forward;
    result.diagnostics.metrics["strike"] = swaption.strike;
    if (swaption.settlement == SettlementType::Cash) {
        result.diagnostics.warnings.push_back("Cash settlement uses the physical annuity approximation in this MVP.");
    }
    return result;
}

PricingResult BermudanSwaptionPricer::price(const BermudanSwaption& swaption, const MarketDataSnapshot& market,
                                            const HullWhiteModel& model, const MonteCarloSettings& settings) {
    if (swaption.exercise_dates.empty()) throw std::invalid_argument("Bermudan swaption requires exercise dates");
    const InterestRateSwap indexed_underlying = with_rate_index_conventions(swaption.underlying, market);
    const auto& discount_curve = market.curve(indexed_underlying.discount_curve_id);
    const auto& forward_curve = market.curve(indexed_underlying.forward_curve_id);
    std::vector<Date> exercise_dates = swaption.exercise_dates;
    std::sort(exercise_dates.begin(), exercise_dates.end());
    const double horizon = time_from(market.valuation_date, exercise_dates.back());
    const int steps = std::max(1, static_cast<int>(std::ceil(horizon * settings.steps_per_year)));
    const double dt = horizon / static_cast<double>(steps);
    const auto paths = model.simulate_factor_paths(horizon, steps, settings.paths, settings.seed);

    std::vector<std::size_t> exercise_steps;
    for (const Date& date : exercise_dates) exercise_steps.push_back(nearest_step(time_from(market.valuation_date, date), dt, steps));

    std::vector<double> values(static_cast<std::size_t>(settings.paths), 0.0);
    std::vector<int> exercise_index(static_cast<std::size_t>(settings.paths), -1);
    const double option_sign = swaption.direction == OptionDirection::Payer ? 1.0 : -1.0;

    for (int date_index = static_cast<int>(exercise_dates.size()) - 1; date_index >= 0; --date_index) {
        const std::size_t step = exercise_steps[static_cast<std::size_t>(date_index)];
        std::vector<double> states;
        std::vector<double> discounted_continuations;
        std::vector<double> immediate_values(static_cast<std::size_t>(settings.paths), 0.0);

        for (int path_index = 0; path_index < settings.paths; ++path_index) {
            const double state = paths[static_cast<std::size_t>(path_index)][step];
            InterestRateSwap remaining = indexed_underlying;
            remaining.fixed_rate = swaption.strike;
            const double swap_value = SwapPricer::conditional_swap_value(remaining, discount_curve, forward_curve, market.valuation_date,
                                                                          exercise_dates[static_cast<std::size_t>(date_index)],
                                                                          state, model);
            const double immediate = std::max(option_sign * swap_value, 0.0);
            immediate_values[static_cast<std::size_t>(path_index)] = immediate;

            if (date_index < static_cast<int>(exercise_dates.size()) - 1) {
                const double next_time = time_from(market.valuation_date, exercise_dates[static_cast<std::size_t>(date_index + 1)]);
                const double current_time = time_from(market.valuation_date, exercise_dates[static_cast<std::size_t>(date_index)]);
                const double discount = std::exp(-(discount_curve.zero_rate(current_time) + state) * (next_time - current_time));
                values[static_cast<std::size_t>(path_index)] *= discount;
            }
            if (immediate > 0.0) {
                states.push_back(state);
                discounted_continuations.push_back(values[static_cast<std::size_t>(path_index)]);
            }
        }

        std::vector<double> coefficients{0.0, 0.0, 0.0};
        if (states.size() >= 3) coefficients = least_squares_quadratic(states, discounted_continuations);
        for (int path_index = 0; path_index < settings.paths; ++path_index) {
            const double immediate = immediate_values[static_cast<std::size_t>(path_index)];
            const double state = paths[static_cast<std::size_t>(path_index)][step];
            const double estimated_continuation = std::max(0.0, evaluate_quadratic(coefficients, state));
            if (immediate > estimated_continuation && immediate > 0.0) {
                values[static_cast<std::size_t>(path_index)] = immediate;
                exercise_index[static_cast<std::size_t>(path_index)] = date_index;
            }
        }
    }

    const double first_time = time_from(market.valuation_date, exercise_dates.front());
    const double initial_discount = discount_curve.discount(first_time);
    for (double& value : values) value *= initial_discount;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    double squared = 0.0;
    for (double value : values) squared += (value - mean) * (value - mean);
    const double standard_error = std::sqrt(squared / std::max(1.0, static_cast<double>(values.size() - 1))) / std::sqrt(static_cast<double>(values.size()));

    PricingResult result;
    result.present_value = (swaption.long_position ? 1.0 : -1.0) * mean;
    result.currency = swaption.underlying.currency;
    result.diagnostics.engine = "LeastSquaresMonteCarloBermudanEngine";
    result.diagnostics.model = "HullWhiteOneFactorApproximation";
    result.diagnostics.standard_error = standard_error;
    result.diagnostics.metrics["paths"] = static_cast<double>(settings.paths);
    result.diagnostics.metrics["time_steps"] = static_cast<double>(steps);
    result.diagnostics.metrics["mean_reversion"] = model.mean_reversion;
    result.diagnostics.metrics["model_volatility"] = model.volatility;
    for (std::size_t index = 0; index < exercise_dates.size(); ++index) {
        const int count = static_cast<int>(std::count(exercise_index.begin(), exercise_index.end(), static_cast<int>(index)));
        result.diagnostics.metrics["exercise_probability_" + exercise_dates[index].str()] = static_cast<double>(count) / static_cast<double>(settings.paths);
    }
    result.diagnostics.warnings.push_back("Hull-White conditional bond prices use a curve-consistent Gaussian-factor approximation rather than a fully calibrated theta(t) implementation.");
    result.diagnostics.warnings.push_back("LSMC regression uses quadratic basis functions and should be convergence-tested for each product.");
    return result;
}

PricingResult CallableRangeAccrualPricer::price(const CallableRangeAccrual& product, const MarketDataSnapshot& market,
                                                const HullWhiteModel& model, const MonteCarloSettings& settings) {
    const auto& curve = market.curve(product.discount_curve_id);
    const double horizon = time_from(market.valuation_date, product.maturity_date);
    const int steps = std::max(1, static_cast<int>(std::ceil(horizon * settings.steps_per_year)));
    const double dt = horizon / static_cast<double>(steps);
    const auto paths = model.simulate_factor_paths(horizon, steps, settings.paths, settings.seed);

    std::vector<bool> is_call_step(static_cast<std::size_t>(steps + 1), false);
    std::map<std::size_t, std::string> call_labels;
    for (const Date& call_date : product.issuer_call_dates) {
        const auto step = nearest_step(time_from(market.valuation_date, call_date), dt, steps);
        is_call_step[step] = true;
        call_labels[step] = call_date.str();
    }

    std::vector<double> values(static_cast<std::size_t>(settings.paths), product.notional * product.redemption_fraction);
    std::map<std::size_t, int> call_counts;
    const double coupon_per_step = product.notional * product.coupon_rate * dt;

    for (int step = steps - 1; step >= 0; --step) {
        const double time = static_cast<double>(step) * dt;
        std::vector<double> states(static_cast<std::size_t>(settings.paths));
        std::vector<double> continuation(static_cast<std::size_t>(settings.paths));
        for (int path_index = 0; path_index < settings.paths; ++path_index) {
            const double state = paths[static_cast<std::size_t>(path_index)][static_cast<std::size_t>(step)];
            const double reference_rate = curve.zero_rate(std::max(time, 1e-8)) + state;
            const double coupon = (reference_rate >= product.lower_bound && reference_rate <= product.upper_bound)
                ? coupon_per_step : 0.0;
            const double discount = std::exp(-(curve.zero_rate(std::max(time, 1e-8)) + state) * dt);
            values[static_cast<std::size_t>(path_index)] = values[static_cast<std::size_t>(path_index)] * discount + coupon;
            states[static_cast<std::size_t>(path_index)] = state;
            continuation[static_cast<std::size_t>(path_index)] = values[static_cast<std::size_t>(path_index)];
        }

        if (is_call_step[static_cast<std::size_t>(step)] && step > 0) {
            const auto coefficients = least_squares_quadratic(states, continuation);
            for (int path_index = 0; path_index < settings.paths; ++path_index) {
                const double estimated_continuation = std::max(0.0, evaluate_quadratic(coefficients, states[static_cast<std::size_t>(path_index)]));
                const double call_value = product.notional * product.redemption_fraction;
                if (call_value < estimated_continuation) {
                    values[static_cast<std::size_t>(path_index)] = call_value;
                    ++call_counts[static_cast<std::size_t>(step)];
                }
            }
        }
    }

    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    double squared = 0.0;
    for (double value : values) squared += (value - mean) * (value - mean);
    const double standard_error = std::sqrt(squared / std::max(1.0, static_cast<double>(values.size() - 1))) / std::sqrt(static_cast<double>(values.size()));

    PricingResult result;
    result.present_value = mean;
    result.currency = product.currency;
    result.diagnostics.engine = "LeastSquaresMonteCarloCallableRangeAccrualEngine";
    result.diagnostics.model = "HullWhiteOneFactorApproximation";
    result.diagnostics.standard_error = standard_error;
    result.diagnostics.metrics["paths"] = static_cast<double>(settings.paths);
    result.diagnostics.metrics["time_steps"] = static_cast<double>(steps);
    for (const auto& [step, count] : call_counts) {
        result.diagnostics.metrics["issuer_call_probability_" + call_labels[step]] = static_cast<double>(count) / static_cast<double>(settings.paths);
    }
    result.diagnostics.warnings.push_back("Range accrual observations are discretised to the simulation grid rather than daily business-day observations.");
    result.diagnostics.warnings.push_back("Issuer call decisions use a quadratic one-factor regression and omit funding, credit, and smile effects.");
    return result;
}

}  // namespace rates

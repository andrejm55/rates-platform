#include "rates/calibration/curve_bootstrap.hpp"
#include "rates/numerics/math.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rates {
namespace {
double discount_from_nodes(const std::vector<double>& times, const std::vector<double>& zero_rates, double time) {
    if (time <= 0.0) return 1.0;
    if (times.empty()) throw std::invalid_argument("Cannot interpolate an empty bootstrap curve");
    return std::exp(-linear_interpolate(times, zero_rates, time) * time);
}

void append_projection_node(std::vector<double>& times, std::vector<double>& zero_rates,
                            std::vector<double>& discount_factors, double maturity, double discount) {
    if (maturity <= 0.0 || discount <= 0.0 || discount > 1.5) {
        throw std::invalid_argument("Invalid projection calibration node");
    }
    if (!times.empty() && maturity <= times.back() + 1e-10) {
        throw std::invalid_argument("Projection calibration maturities must be unique and increasing");
    }
    times.push_back(maturity);
    zero_rates.push_back(-std::log(discount) / maturity);
    discount_factors.push_back(discount);
}

double futures_rate(const FuturesQuote& quote) {
    if (quote.price > 1.0) return (100.0 - quote.price) / 100.0 - quote.convexity_adjustment;
    return quote.price - quote.convexity_adjustment;
}
}

CurveCalibrationResult CurveBootstrapper::bootstrap(
    const std::string& curve_id,
    std::vector<DepositQuote> deposits,
    std::vector<SwapQuote> swaps) {
    if (deposits.empty() && swaps.empty()) throw std::invalid_argument("At least one calibration quote is required");
    std::sort(deposits.begin(), deposits.end(), [](const auto& left, const auto& right) { return left.maturity < right.maturity; });
    std::sort(swaps.begin(), swaps.end(), [](const auto& left, const auto& right) { return left.maturity < right.maturity; });

    std::vector<double> times;
    std::vector<double> zero_rates;
    std::vector<double> discount_factors;
    std::vector<double> residuals;
    std::vector<std::string> warnings;

    for (const auto& quote : deposits) {
        if (quote.maturity <= 0.0 || 1.0 + quote.simple_rate * quote.maturity <= 0.0) {
            throw std::invalid_argument("Invalid deposit quote");
        }
        const double discount = 1.0 / (1.0 + quote.simple_rate * quote.maturity);
        const double zero = -std::log(discount) / quote.maturity;
        if (!times.empty() && quote.maturity <= times.back()) throw std::invalid_argument("Calibration maturities must be unique and increasing");
        times.push_back(quote.maturity);
        zero_rates.push_back(zero);
        discount_factors.push_back(discount);
        residuals.push_back(0.0);
    }

    for (const auto& quote : swaps) {
        if (quote.maturity <= 0.0 || quote.payment_interval <= 0.0 || quote.payment_interval > quote.maturity) {
            throw std::invalid_argument("Invalid swap quote");
        }
        if (!times.empty() && quote.maturity <= times.back()) {
            warnings.push_back("Swap quote at " + std::to_string(quote.maturity) + "Y was skipped because an equal or longer node already exists.");
            continue;
        }
        if (times.empty()) throw std::invalid_argument("At least one short deposit node is required before swap bootstrapping");

        const int payment_count = static_cast<int>(std::lround(quote.maturity / quote.payment_interval));
        if (std::abs(payment_count * quote.payment_interval - quote.maturity) > 1e-8) {
            throw std::invalid_argument("Swap maturity must be divisible by payment interval in the MVP bootstrapper");
        }
        double previous_annuity = 0.0;
        for (int payment = 1; payment < payment_count; ++payment) {
            const double payment_time = payment * quote.payment_interval;
            previous_annuity += quote.payment_interval * discount_from_nodes(times, zero_rates, payment_time);
        }
        const double numerator = 1.0 - quote.par_rate * previous_annuity;
        const double denominator = 1.0 + quote.par_rate * quote.payment_interval;
        const double discount = numerator / denominator;
        if (discount <= 0.0 || discount > 1.5) throw std::runtime_error("Bootstrap produced an invalid discount factor");
        const double zero = -std::log(discount) / quote.maturity;
        times.push_back(quote.maturity);
        zero_rates.push_back(zero);
        discount_factors.push_back(discount);

        double annuity = previous_annuity + quote.payment_interval * discount;
        const double reproduced_rate = (1.0 - discount) / annuity;
        residuals.push_back(reproduced_rate - quote.par_rate);
    }

    CurveCalibrationResult result;
    result.curve = YieldCurve(curve_id, times, zero_rates);
    result.discount_factors = discount_factors;
    result.calibration_residuals = residuals;
    result.converged = std::all_of(residuals.begin(), residuals.end(), [](double residual) { return std::abs(residual) < 1e-10; });
    result.warnings = std::move(warnings);
    result.warnings.push_back("Bootstrap uses simple deposit rates and regular fixed-for-floating par swaps with one shared discount curve.");
    return result;
}

MultiCurveCalibrationResult CurveBootstrapper::bootstrap_multi_curve(
    const std::string& discount_curve_id,
    const std::string& projection_curve_id,
    std::vector<DepositQuote> ois_deposits,
    std::vector<SwapQuote> ois_swaps,
    std::vector<DepositQuote> projection_deposits,
    std::vector<FraQuote> fras,
    std::vector<FuturesQuote> futures,
    std::vector<BasisSwapQuote> basis_swaps) {
    CurveCalibrationResult discount = bootstrap(discount_curve_id, std::move(ois_deposits), std::move(ois_swaps));

    std::sort(projection_deposits.begin(), projection_deposits.end(), [](const auto& left, const auto& right) { return left.maturity < right.maturity; });
    std::sort(fras.begin(), fras.end(), [](const auto& left, const auto& right) { return left.end < right.end; });
    std::sort(futures.begin(), futures.end(), [](const auto& left, const auto& right) { return left.end < right.end; });
    std::sort(basis_swaps.begin(), basis_swaps.end(), [](const auto& left, const auto& right) { return left.maturity < right.maturity; });

    std::vector<double> projection_times;
    std::vector<double> projection_zero_rates;
    std::vector<double> projection_discounts;
    std::vector<double> projection_residuals;
    std::vector<std::string> warnings;

    for (const auto& quote : projection_deposits) {
        if (quote.maturity <= 0.0 || 1.0 + quote.simple_rate * quote.maturity <= 0.0) {
            throw std::invalid_argument("Invalid projection deposit quote");
        }
        const double projection_discount = 1.0 / (1.0 + quote.simple_rate * quote.maturity);
        append_projection_node(projection_times, projection_zero_rates, projection_discounts, quote.maturity, projection_discount);
        projection_residuals.push_back(0.0);
    }

    auto add_forward_quote = [&](double start, double end, double rate, const std::string& label) {
        if (start < 0.0 || end <= start || 1.0 + rate * (end - start) <= 0.0) {
            throw std::invalid_argument("Invalid " + label + " quote");
        }
        if (projection_times.empty()) {
            throw std::invalid_argument(label + " calibration requires a preceding projection deposit node");
        }
        if (end <= projection_times.back() + 1e-10) {
            warnings.push_back(label + " ending at " + std::to_string(end) + "Y was skipped because an equal or longer projection node already exists.");
            return;
        }
        const double start_discount = discount_from_nodes(projection_times, projection_zero_rates, start);
        const double end_discount = start_discount / (1.0 + rate * (end - start));
        append_projection_node(projection_times, projection_zero_rates, projection_discounts, end, end_discount);
        const double reproduced = (start_discount / end_discount - 1.0) / (end - start);
        projection_residuals.push_back(reproduced - rate);
    };

    for (const auto& quote : fras) add_forward_quote(quote.start, quote.end, quote.rate, "FRA");
    for (const auto& quote : futures) add_forward_quote(quote.start, quote.end, futures_rate(quote), "Futures");

    const YieldCurve discount_curve = discount.curve;
    for (const auto& quote : basis_swaps) {
        if (quote.maturity <= 0.0 || quote.payment_interval <= 0.0 || quote.payment_interval > quote.maturity) {
            throw std::invalid_argument("Invalid basis swap quote");
        }
        if (projection_times.empty()) throw std::invalid_argument("Basis calibration requires at least one projection node");
        if (quote.maturity <= projection_times.back() + 1e-10) {
            warnings.push_back("Basis quote at " + std::to_string(quote.maturity) + "Y was skipped because an equal or longer projection node already exists.");
            continue;
        }
        const int payment_count = static_cast<int>(std::lround(quote.maturity / quote.payment_interval));
        if (std::abs(payment_count * quote.payment_interval - quote.maturity) > 1e-8) {
            throw std::invalid_argument("Basis swap maturity must be divisible by payment interval");
        }

        double fixed_leg = 0.0;
        double projected_leg = 0.0;
        double spread_annuity = 0.0;
        double previous_projection_discount = discount_from_nodes(projection_times, projection_zero_rates, 0.0);
        for (int payment = 1; payment <= payment_count; ++payment) {
            const double start = static_cast<double>(payment - 1) * quote.payment_interval;
            const double end = static_cast<double>(payment) * quote.payment_interval;
            const double discount_factor = discount_curve.discount(end);
            fixed_leg += quote.par_rate * quote.payment_interval * discount_factor;
            spread_annuity += quote.payment_interval * discount_factor;
            if (payment < payment_count) {
                const double start_projection_discount = discount_from_nodes(projection_times, projection_zero_rates, start);
                const double end_projection_discount = discount_from_nodes(projection_times, projection_zero_rates, end);
                const double forward = (start_projection_discount / end_projection_discount - 1.0) / quote.payment_interval;
                projected_leg += forward * quote.payment_interval * discount_factor;
                previous_projection_discount = end_projection_discount;
            }
        }

        const double terminal_discount_factor = discount_curve.discount(quote.maturity);
        const double required_forward = (fixed_leg - projected_leg - quote.basis_spread * spread_annuity)
            / (quote.payment_interval * terminal_discount_factor);
        const double terminal_projection_discount = previous_projection_discount / (1.0 + required_forward * quote.payment_interval);
        append_projection_node(projection_times, projection_zero_rates, projection_discounts, quote.maturity, terminal_projection_discount);

        const double reproduced_forward = (previous_projection_discount / terminal_projection_discount - 1.0) / quote.payment_interval;
        const double reproduced = projected_leg + reproduced_forward * quote.payment_interval * terminal_discount_factor
            + quote.basis_spread * spread_annuity - fixed_leg;
        projection_residuals.push_back(reproduced);
    }

    if (projection_times.empty()) {
        throw std::invalid_argument("At least one projection calibration quote is required");
    }

    MultiCurveCalibrationResult result;
    result.discount_curve = discount.curve;
    result.projection_curve = YieldCurve(projection_curve_id, projection_times, projection_zero_rates);
    result.discount_factors = discount.discount_factors;
    result.projection_discount_factors = projection_discounts;
    result.discount_residuals = discount.calibration_residuals;
    result.projection_residuals = projection_residuals;
    result.warnings = std::move(warnings);
    result.warnings.insert(result.warnings.end(), discount.warnings.begin(), discount.warnings.end());
    result.warnings.push_back("Multi-curve calibration supports simple OIS discount bootstrapping and projection nodes from deposits, FRAs, futures and regular basis swaps; it does not yet implement a global nonlinear solve.");
    result.converged = discount.converged && std::all_of(projection_residuals.begin(), projection_residuals.end(), [](double residual) {
        return std::abs(residual) < 1e-8;
    });
    return result;
}

}  // namespace rates

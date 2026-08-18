#include "rates/models/option_models.hpp"
#include "rates/numerics/math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace rates {
namespace {
double sign_for(OptionDirection direction) {
    return direction == OptionDirection::Payer ? 1.0 : -1.0;
}

double intrinsic(double forward, double strike, double annuity, OptionDirection direction) {
    return annuity * std::max(sign_for(direction) * (forward - strike), 0.0);
}

double sabr_model_volatility(double forward, double strike, double expiry, const SabrParameters& parameters, bool normal) {
    return normal ? Sabr::normal_volatility(forward, strike, expiry, parameters)
                  : Sabr::lognormal_volatility(forward, strike, expiry, parameters);
}

double sabr_error(double forward, double expiry, const std::vector<double>& strikes,
                  const std::vector<double>& market, const SabrParameters& parameters,
                  bool normal, std::vector<double>* model = nullptr) {
    double squared_error = 0.0;
    if (model) model->clear();
    for (std::size_t i = 0; i < strikes.size(); ++i) {
        double value;
        try {
            value = sabr_model_volatility(forward, strikes[i], expiry, parameters, normal);
        } catch (...) {
            return std::numeric_limits<double>::infinity();
        }
        if (!std::isfinite(value) || value <= 0.0) return std::numeric_limits<double>::infinity();
        if (model) model->push_back(value);
        const double difference = value - market[i];
        squared_error += difference * difference;
    }
    return std::sqrt(squared_error / static_cast<double>(strikes.size()));
}

SabrCalibrationResult calibrate_from_seed(double forward, double expiry, const std::vector<double>& strikes,
                                          const std::vector<double>& market_volatilities, SabrParameters parameters,
                                          int max_iterations, bool normal_volatilities) {
    double step_alpha = std::max(0.002, parameters.alpha * 0.35);
    double step_rho = 0.25;
    double step_nu = 0.25;
    double best_error = sabr_error(forward, expiry, strikes, market_volatilities, parameters, normal_volatilities);
    int iteration = 0;

    for (; iteration < max_iterations; ++iteration) {
        bool improved = false;
        const SabrParameters base = parameters;
        for (int dimension = 0; dimension < 3; ++dimension) {
            for (int direction : {-1, 1}) {
                SabrParameters candidate = base;
                if (dimension == 0) candidate.alpha = std::max(1e-6, base.alpha + direction * step_alpha);
                if (dimension == 1) candidate.rho = std::clamp(base.rho + direction * step_rho, -0.999, 0.999);
                if (dimension == 2) candidate.nu = std::max(1e-6, base.nu + direction * step_nu);
                const double candidate_error = sabr_error(forward, expiry, strikes, market_volatilities, candidate, normal_volatilities);
                if (candidate_error < best_error) {
                    best_error = candidate_error;
                    parameters = candidate;
                    improved = true;
                }
            }
        }
        if (!improved) {
            step_alpha *= 0.55;
            step_rho *= 0.55;
            step_nu *= 0.55;
        }
        if (std::max({step_alpha, step_rho, step_nu}) < 1e-8 || best_error < 1e-9) break;
    }

    SabrCalibrationResult result;
    result.parameters = parameters;
    result.root_mean_square_error = sabr_error(forward, expiry, strikes, market_volatilities, parameters, normal_volatilities, &result.model_volatilities);
    result.iterations = iteration + 1;
    result.converged = std::isfinite(result.root_mean_square_error) && result.root_mean_square_error < (normal_volatilities ? 5e-4 : 5e-3);
    return result;
}
}

OptionModelResult Black76::evaluate(double forward, double strike, double volatility, double expiry,
                                    double discount_annuity, OptionDirection direction, double shift) {
    const double shifted_forward = forward + shift;
    const double shifted_strike = strike + shift;
    if (discount_annuity < 0.0 || expiry < 0.0 || volatility < 0.0) {
        throw std::invalid_argument("Black inputs must be non-negative where applicable");
    }
    if (shifted_forward <= 0.0 || shifted_strike <= 0.0) {
        throw std::invalid_argument("Shifted forward and strike must be positive");
    }
    if (expiry == 0.0 || volatility == 0.0) {
        return {intrinsic(forward, strike, discount_annuity, direction), 0.0, 0.0, 0.0};
    }
    const double sqrt_expiry = std::sqrt(expiry);
    const double stddev = volatility * sqrt_expiry;
    const double d1 = (std::log(shifted_forward / shifted_strike) + 0.5 * stddev * stddev) / stddev;
    const double d2 = d1 - stddev;
    const double sign = sign_for(direction);
    const double price = discount_annuity * sign * (
        shifted_forward * normal_cdf(sign * d1) - shifted_strike * normal_cdf(sign * d2));
    const double delta = discount_annuity * sign * normal_cdf(sign * d1);
    const double gamma = discount_annuity * normal_pdf(d1) / (shifted_forward * stddev);
    const double vega = discount_annuity * shifted_forward * normal_pdf(d1) * sqrt_expiry;
    return {price, delta, gamma, vega};
}

double Black76::implied_volatility(double target_price, double forward, double strike, double expiry,
                                   double discount_annuity, OptionDirection direction, double shift) {
    const double minimum = intrinsic(forward, strike, discount_annuity, direction);
    if (target_price < minimum - 1e-10) throw std::invalid_argument("Target price violates intrinsic value");
    if (std::abs(target_price - minimum) < 1e-12) return 0.0;
    auto objective = [&](double volatility) {
        return evaluate(forward, strike, volatility, expiry, discount_annuity, direction, shift).price - target_price;
    };
    double upper = 0.5;
    while (objective(upper) < 0.0 && upper < 10.0) upper *= 2.0;
    return bisection(objective, 1e-10, upper, 1e-10, 300);
}

OptionModelResult Bachelier::evaluate(double forward, double strike, double normal_volatility, double expiry,
                                      double discount_annuity, OptionDirection direction) {
    if (discount_annuity < 0.0 || expiry < 0.0 || normal_volatility < 0.0) {
        throw std::invalid_argument("Bachelier inputs must be non-negative where applicable");
    }
    if (expiry == 0.0 || normal_volatility == 0.0) {
        return {intrinsic(forward, strike, discount_annuity, direction), 0.0, 0.0, 0.0};
    }
    const double sign = sign_for(direction);
    const double stddev = normal_volatility * std::sqrt(expiry);
    const double d = (forward - strike) / stddev;
    const double price = discount_annuity * (sign * (forward - strike) * normal_cdf(sign * d) + stddev * normal_pdf(d));
    const double delta = discount_annuity * sign * normal_cdf(sign * d);
    const double gamma = discount_annuity * normal_pdf(d) / stddev;
    const double vega = discount_annuity * std::sqrt(expiry) * normal_pdf(d);
    return {price, delta, gamma, vega};
}

double Bachelier::implied_volatility(double target_price, double forward, double strike, double expiry,
                                     double discount_annuity, OptionDirection direction) {
    const double minimum = intrinsic(forward, strike, discount_annuity, direction);
    if (target_price < minimum - 1e-10) throw std::invalid_argument("Target price violates intrinsic value");
    if (std::abs(target_price - minimum) < 1e-12) return 0.0;
    auto objective = [&](double volatility) {
        return evaluate(forward, strike, volatility, expiry, discount_annuity, direction).price - target_price;
    };
    double upper = 0.01;
    while (objective(upper) < 0.0 && upper < 5.0) upper *= 2.0;
    return bisection(objective, 1e-12, upper, 1e-10, 300);
}

double Sabr::lognormal_volatility(double forward, double strike, double expiry, const SabrParameters& p) {
    const double f = forward + p.shift;
    const double k = strike + p.shift;
    if (f <= 0.0 || k <= 0.0 || p.alpha <= 0.0 || p.nu < 0.0 || p.beta < 0.0 || p.beta > 1.0 || std::abs(p.rho) >= 1.0) {
        throw std::invalid_argument("Invalid SABR inputs");
    }
    const double one_minus_beta = 1.0 - p.beta;
    const double fk = f * k;
    const double fk_beta = std::pow(fk, 0.5 * one_minus_beta);
    const double log_fk = std::log(f / k);
    const double log_sq = log_fk * log_fk;
    const double log_fourth = log_sq * log_sq;

    const double denominator_correction = 1.0 + (one_minus_beta * one_minus_beta / 24.0) * log_sq
        + (std::pow(one_minus_beta, 4) / 1920.0) * log_fourth;

    const double time_correction = 1.0 + expiry * (
        (one_minus_beta * one_minus_beta * p.alpha * p.alpha) / (24.0 * fk_beta * fk_beta)
        + (p.rho * p.beta * p.nu * p.alpha) / (4.0 * fk_beta)
        + ((2.0 - 3.0 * p.rho * p.rho) * p.nu * p.nu) / 24.0);

    if (std::abs(log_fk) < 1e-10) {
        const double f_beta = std::pow(f, one_minus_beta);
        return (p.alpha / f_beta) * (1.0 + expiry * (
            (one_minus_beta * one_minus_beta * p.alpha * p.alpha) / (24.0 * f_beta * f_beta)
            + (p.rho * p.beta * p.nu * p.alpha) / (4.0 * f_beta)
            + ((2.0 - 3.0 * p.rho * p.rho) * p.nu * p.nu) / 24.0));
    }

    const double z = (p.nu / p.alpha) * fk_beta * log_fk;
    const double xz = std::log((std::sqrt(1.0 - 2.0 * p.rho * z + z * z) + z - p.rho) / (1.0 - p.rho));
    const double z_over_xz = std::abs(xz) < 1e-12 ? 1.0 : z / xz;
    return (p.alpha / (fk_beta * denominator_correction)) * z_over_xz * time_correction;
}

double Sabr::normal_volatility(double forward, double strike, double expiry, const SabrParameters& parameters) {
    const double shifted_forward = forward + parameters.shift;
    const double shifted_strike = strike + parameters.shift;
    const double black_volatility = lognormal_volatility(forward, strike, expiry, parameters);
    if (std::abs(shifted_forward - shifted_strike) < 1e-10) return black_volatility * shifted_forward;
    const double equivalent_level = (shifted_forward - shifted_strike) / std::log(shifted_forward / shifted_strike);
    return black_volatility * equivalent_level;
}

SabrCalibrationResult Sabr::calibrate(double forward, double expiry, const std::vector<double>& strikes,
                                      const std::vector<double>& market_volatilities, double beta,
                                      double shift, int max_iterations, bool normal_volatilities) {
    if (strikes.size() != market_volatilities.size() || strikes.size() < 3) {
        throw std::invalid_argument("SABR calibration requires at least three matching strike and volatility points");
    }
    auto closest = std::min_element(strikes.begin(), strikes.end(), [&](double left, double right) {
        return std::abs(left - forward) < std::abs(right - forward);
    });
    const std::size_t atm_index = static_cast<std::size_t>(closest - strikes.begin());
    const double atm_scale = normal_volatilities ? 1.0 : std::pow(forward + shift, 1.0 - beta);
    SabrCalibrationResult best;
    best.root_mean_square_error = std::numeric_limits<double>::infinity();
    int total_iterations = 0;
    for (double rho_seed : {-0.6, -0.2, 0.2, 0.6}) {
        for (double nu_seed : {0.15, 0.4, 0.8, 1.2}) {
            SabrParameters seed;
            seed.beta = beta;
            seed.shift = shift;
            seed.alpha = std::max(1e-5, market_volatilities[atm_index] * atm_scale);
            seed.rho = rho_seed;
            seed.nu = nu_seed;
            auto candidate = calibrate_from_seed(forward, expiry, strikes, market_volatilities, seed, max_iterations, normal_volatilities);
            total_iterations += candidate.iterations;
            if (candidate.root_mean_square_error < best.root_mean_square_error) best = candidate;
        }
    }
    best.iterations = total_iterations;
    return best;
}

double HullWhiteModel::factor_variance(double time) const {
    if (time <= 0.0) return 0.0;
    if (std::abs(mean_reversion) < 1e-12) return volatility * volatility * time;
    return volatility * volatility * (1.0 - std::exp(-2.0 * mean_reversion * time)) / (2.0 * mean_reversion);
}

double HullWhiteModel::bond_loading(double start, double end) const {
    const double tenor = end - start;
    if (tenor <= 0.0) return 0.0;
    if (std::abs(mean_reversion) < 1e-12) return tenor;
    return (1.0 - std::exp(-mean_reversion * tenor)) / mean_reversion;
}

double HullWhiteModel::normal_swaption_volatility(double expiry, double tenor) const {
    if (expiry <= 0.0 || tenor <= 0.0) throw std::invalid_argument("Hull-White volatility benchmark requires positive expiry and tenor");
    const double loading = bond_loading(expiry, expiry + tenor);
    const double variance_average = factor_variance(expiry) / expiry;
    return std::abs(loading) * std::sqrt(std::max(variance_average, 0.0));
}

double HullWhiteModel::factor_call_value(double expiry, double strike) const {
    if (expiry <= 0.0) throw std::invalid_argument("Factor option expiry must be positive");
    const double standard_deviation = std::sqrt(factor_variance(expiry));
    if (standard_deviation == 0.0) return std::max(-strike, 0.0);
    const double d = -strike / standard_deviation;
    return -strike * normal_cdf(d) + standard_deviation * normal_pdf(d);
}

std::vector<std::vector<double>> HullWhiteModel::simulate_factor_paths(double horizon, int steps, int paths, unsigned seed) const {
    if (horizon <= 0.0 || steps <= 0 || paths <= 0) throw std::invalid_argument("Invalid Hull-White simulation dimensions");
    const double dt = horizon / static_cast<double>(steps);
    const double decay = std::exp(-mean_reversion * dt);
    const double variance = std::abs(mean_reversion) < 1e-12
        ? volatility * volatility * dt
        : volatility * volatility * (1.0 - std::exp(-2.0 * mean_reversion * dt)) / (2.0 * mean_reversion);
    const double standard_deviation = std::sqrt(std::max(variance, 0.0));
    std::mt19937 generator(seed);
    std::normal_distribution<double> normal(0.0, 1.0);
    const auto path_count = static_cast<std::size_t>(paths);
    const auto step_count = static_cast<std::size_t>(steps);
    std::vector<std::vector<double>> output(path_count, std::vector<double>(step_count + 1, 0.0));
    for (std::size_t path = 0; path < path_count; ++path) {
        for (std::size_t step = 1; step <= step_count; ++step) {
            output[path][step] = output[path][step - 1] * decay + standard_deviation * normal(generator);
        }
    }
    return output;
}

HullWhiteCalibrationResult calibrate_hull_white_to_normal_volatilities(
    const std::vector<double>& expiries,
    const std::vector<double>& tenors,
    const std::vector<double>& normal_volatilities) {
    if (expiries.size() != tenors.size() || expiries.size() != normal_volatilities.size() || expiries.empty()) {
        throw std::invalid_argument("Hull-White calibration requires matching non-empty expiry, tenor and volatility arrays");
    }
    HullWhiteCalibrationResult best;
    best.root_mean_square_error = std::numeric_limits<double>::infinity();
    for (double mean_reversion : {0.005, 0.01, 0.02, 0.03, 0.05, 0.08, 0.12}) {
        std::vector<double> loadings;
        loadings.reserve(expiries.size());
        double numerator = 0.0;
        double denominator = 0.0;
        for (std::size_t i = 0; i < expiries.size(); ++i) {
            HullWhiteModel unit{mean_reversion, 1.0};
            const double loading = unit.normal_swaption_volatility(expiries[i], tenors[i]);
            loadings.push_back(loading);
            numerator += loading * normal_volatilities[i];
            denominator += loading * loading;
        }
        if (denominator <= 0.0) continue;
        const double sigma = std::max(1e-8, numerator / denominator);
        HullWhiteModel model{mean_reversion, sigma};
        std::vector<double> model_vols;
        model_vols.reserve(expiries.size());
        double squared_error = 0.0;
        for (std::size_t i = 0; i < expiries.size(); ++i) {
            const double model_vol = model.normal_swaption_volatility(expiries[i], tenors[i]);
            model_vols.push_back(model_vol);
            const double error = model_vol - normal_volatilities[i];
            squared_error += error * error;
        }
        const double rmse = std::sqrt(squared_error / static_cast<double>(expiries.size()));
        if (rmse < best.root_mean_square_error) {
            best.model = model;
            best.model_volatilities = std::move(model_vols);
            best.root_mean_square_error = rmse;
            best.converged = rmse < 5e-4;
        }
    }
    return best;
}

}  // namespace rates

#pragma once

#include "rates/instruments/instruments.hpp"

#include <vector>

namespace rates {

struct OptionModelResult {
    double price{0.0};
    double delta{0.0};
    double gamma{0.0};
    double vega{0.0};
};

class Black76 {
public:
    static OptionModelResult evaluate(double forward, double strike, double volatility, double expiry,
                                      double discount_annuity, OptionDirection direction, double shift = 0.0);
    static double implied_volatility(double target_price, double forward, double strike, double expiry,
                                     double discount_annuity, OptionDirection direction, double shift = 0.0);
};

class Bachelier {
public:
    static OptionModelResult evaluate(double forward, double strike, double normal_volatility, double expiry,
                                      double discount_annuity, OptionDirection direction);
    static double implied_volatility(double target_price, double forward, double strike, double expiry,
                                     double discount_annuity, OptionDirection direction);
};

struct SabrParameters {
    double alpha{0.02};
    double beta{0.5};
    double rho{-0.2};
    double nu{0.5};
    double shift{0.0};
};

struct SabrCalibrationResult {
    SabrParameters parameters;
    double root_mean_square_error{0.0};
    int iterations{0};
    bool converged{false};
    std::vector<double> model_volatilities;
};

class Sabr {
public:
    static double lognormal_volatility(double forward, double strike, double expiry, const SabrParameters& parameters);
    static double normal_volatility(double forward, double strike, double expiry, const SabrParameters& parameters);
    static SabrCalibrationResult calibrate(double forward, double expiry, const std::vector<double>& strikes,
                                           const std::vector<double>& market_volatilities, double beta = 0.5,
                                           double shift = 0.0, int max_iterations = 400, bool normal_volatilities = false);
};

struct HullWhiteModel {
    double mean_reversion{0.03};
    double volatility{0.01};

    [[nodiscard]] double factor_variance(double time) const;
    [[nodiscard]] double bond_loading(double start, double end) const;
    [[nodiscard]] double normal_swaption_volatility(double expiry, double tenor) const;
    [[nodiscard]] double factor_call_value(double expiry, double strike) const;
    [[nodiscard]] std::vector<std::vector<double>> simulate_factor_paths(
        double horizon, int steps, int paths, unsigned seed = 42) const;

};

struct HullWhiteCalibrationResult {
    HullWhiteModel model;
    double root_mean_square_error{0.0};
    bool converged{false};
    std::vector<double> model_volatilities;
};

HullWhiteCalibrationResult calibrate_hull_white_to_normal_volatilities(
    const std::vector<double>& expiries,
    const std::vector<double>& tenors,
    const std::vector<double>& normal_volatilities);

}  // namespace rates

#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace rates {

inline double normal_pdf(double x) {
    static constexpr double inv_sqrt_2pi = 0.39894228040143267794;
    return inv_sqrt_2pi * std::exp(-0.5 * x * x);
}

inline double normal_cdf(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

inline double clamp_probability(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double linear_interpolate(const std::vector<double>& x, const std::vector<double>& y, double target);
double bisection(const std::function<double(double)>& function, double lower, double upper, double tolerance = 1e-10, int max_iterations = 200);
std::vector<double> least_squares_quadratic(const std::vector<double>& x, const std::vector<double>& y);
double evaluate_quadratic(const std::vector<double>& coefficients, double x);

}  // namespace rates

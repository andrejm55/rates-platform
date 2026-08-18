#include "rates/numerics/math.hpp"

namespace rates {

double linear_interpolate(const std::vector<double>& x, const std::vector<double>& y, double target) {
    if (x.size() != y.size() || x.empty()) {
        throw std::invalid_argument("Interpolation vectors must have equal non-zero size");
    }
    if (x.size() == 1 || target <= x.front()) {
        return y.front();
    }
    if (target >= x.back()) {
        return y.back();
    }
    auto upper = std::upper_bound(x.begin(), x.end(), target);
    const std::size_t right = static_cast<std::size_t>(upper - x.begin());
    const std::size_t left = right - 1;
    const double weight = (target - x[left]) / (x[right] - x[left]);
    return y[left] + weight * (y[right] - y[left]);
}

double bisection(const std::function<double(double)>& function, double lower, double upper, double tolerance, int max_iterations) {
    double f_lower = function(lower);
    double f_upper = function(upper);
    if (f_lower == 0.0) return lower;
    if (f_upper == 0.0) return upper;
    if (f_lower * f_upper > 0.0) {
        throw std::invalid_argument("Bisection requires a sign change");
    }
    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        const double midpoint = 0.5 * (lower + upper);
        const double f_midpoint = function(midpoint);
        if (std::abs(f_midpoint) < tolerance || std::abs(upper - lower) < tolerance) {
            return midpoint;
        }
        if (f_lower * f_midpoint <= 0.0) {
            upper = midpoint;
            f_upper = f_midpoint;
        } else {
            lower = midpoint;
            f_lower = f_midpoint;
        }
    }
    return 0.5 * (lower + upper);
}

std::vector<double> least_squares_quadratic(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.empty()) {
        throw std::invalid_argument("Least-squares inputs must have equal non-zero size");
    }
    double s0 = static_cast<double>(x.size());
    double s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;
    double t0 = 0.0, t1 = 0.0, t2 = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double x2 = x[i] * x[i];
        s1 += x[i];
        s2 += x2;
        s3 += x2 * x[i];
        s4 += x2 * x2;
        t0 += y[i];
        t1 += x[i] * y[i];
        t2 += x2 * y[i];
    }

    double matrix[3][4] = {
        {s0, s1, s2, t0},
        {s1, s2, s3, t1},
        {s2, s3, s4, t2}
    };
    for (int pivot = 0; pivot < 3; ++pivot) {
        int best = pivot;
        for (int row = pivot + 1; row < 3; ++row) {
            if (std::abs(matrix[row][pivot]) > std::abs(matrix[best][pivot])) best = row;
        }
        for (int column = pivot; column < 4; ++column) std::swap(matrix[pivot][column], matrix[best][column]);
        if (std::abs(matrix[pivot][pivot]) < 1e-14) {
            return {t0 / s0, 0.0, 0.0};
        }
        const double divisor = matrix[pivot][pivot];
        for (int column = pivot; column < 4; ++column) matrix[pivot][column] /= divisor;
        for (int row = 0; row < 3; ++row) {
            if (row == pivot) continue;
            const double factor = matrix[row][pivot];
            for (int column = pivot; column < 4; ++column) matrix[row][column] -= factor * matrix[pivot][column];
        }
    }
    return {matrix[0][3], matrix[1][3], matrix[2][3]};
}

double evaluate_quadratic(const std::vector<double>& coefficients, double x) {
    if (coefficients.size() < 3) throw std::invalid_argument("Quadratic requires three coefficients");
    return coefficients[0] + coefficients[1] * x + coefficients[2] * x * x;
}

}  // namespace rates

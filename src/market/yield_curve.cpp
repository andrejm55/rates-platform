#include "rates/market/yield_curve.hpp"
#include "rates/numerics/math.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace rates {

YieldCurve::YieldCurve(std::string id, std::vector<double> times, std::vector<double> zero_rates)
    : id_(std::move(id)), times_(std::move(times)), zero_rates_(std::move(zero_rates)) {
    if (times_.size() != zero_rates_.size() || times_.empty()) {
        throw std::invalid_argument("Curve requires matching non-empty time and rate vectors");
    }
    for (std::size_t i = 1; i < times_.size(); ++i) {
        if (times_[i] <= times_[i - 1]) throw std::invalid_argument("Curve times must be strictly increasing");
    }
}

double YieldCurve::zero_rate(double time) const {
    if (time <= 0.0) return zero_rates_.front();
    return linear_interpolate(times_, zero_rates_, time);
}

double YieldCurve::discount(double time) const {
    if (time <= 0.0) return 1.0;
    return std::exp(-zero_rate(time) * time);
}

double YieldCurve::forward_rate(double start, double end) const {
    if (end <= start) throw std::invalid_argument("Forward end must exceed start");
    return (discount(start) / discount(end) - 1.0) / (end - start);
}

YieldCurve YieldCurve::bumped_parallel(double bump) const {
    auto bumped = zero_rates_;
    for (double& rate : bumped) rate += bump;
    return YieldCurve(id_ + "-BUMPED", times_, bumped);
}

YieldCurve YieldCurve::bumped_node(std::size_t node, double bump) const {
    if (node >= zero_rates_.size()) throw std::out_of_range("Curve node out of range");
    auto bumped = zero_rates_;
    bumped[node] += bump;
    return YieldCurve(id_ + "-NODE-BUMPED", times_, bumped);
}

double VolatilitySmile::volatility(double strike) const {
    return linear_interpolate(strikes, volatilities, strike);
}

double SwaptionVolatilityCube::volatility(double expiry, double tenor, double strike) const {
    if (expiries.empty() || tenors.empty() || strikes.empty()) throw std::invalid_argument("Volatility cube axes cannot be empty");
    const std::size_t expected = expiries.size() * tenors.size() * strikes.size();
    if (volatilities.size() != expected) throw std::invalid_argument("Volatility cube has inconsistent dimensions");

    auto bracket = [](const std::vector<double>& axis, double x) {
        if (axis.size() == 1 || x <= axis.front()) return std::pair<std::size_t, std::size_t>{0, 0};
        if (x >= axis.back()) {
            const std::size_t last = axis.size() - 1;
            return std::pair<std::size_t, std::size_t>{last, last};
        }
        auto upper = std::upper_bound(axis.begin(), axis.end(), x);
        const std::size_t high = static_cast<std::size_t>(upper - axis.begin());
        return std::pair<std::size_t, std::size_t>{high - 1, high};
    };
    auto weight = [](const std::vector<double>& axis, std::size_t low, std::size_t high, double x) {
        if (low == high) return 0.0;
        return (x - axis[low]) / (axis[high] - axis[low]);
    };
    auto at = [&](std::size_t expiry_index, std::size_t tenor_index, std::size_t strike_index) {
        return volatilities[(expiry_index * tenors.size() + tenor_index) * strikes.size() + strike_index];
    };

    const auto [e0, e1] = bracket(expiries, expiry);
    const auto [t0, t1] = bracket(tenors, tenor);
    const auto [k0, k1] = bracket(strikes, strike);
    const double we = weight(expiries, e0, e1, expiry);
    const double wt = weight(tenors, t0, t1, tenor);
    const double wk = weight(strikes, k0, k1, strike);

    double value = 0.0;
    for (int ei = 0; ei < 2; ++ei) {
        const std::size_t e = ei == 0 ? e0 : e1;
        const double ew = ei == 0 ? 1.0 - we : we;
        for (int ti = 0; ti < 2; ++ti) {
            const std::size_t t = ti == 0 ? t0 : t1;
            const double tw = ti == 0 ? 1.0 - wt : wt;
            for (int ki = 0; ki < 2; ++ki) {
                const std::size_t k = ki == 0 ? k0 : k1;
                const double kw = ki == 0 ? 1.0 - wk : wk;
                value += ew * tw * kw * at(e, t, k);
            }
        }
    }
    return value;
}

}  // namespace rates

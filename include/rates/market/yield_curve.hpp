#pragma once

#include <string>
#include <vector>

namespace rates {

class YieldCurve {
public:
    YieldCurve() = default;
    YieldCurve(std::string id, std::vector<double> times, std::vector<double> zero_rates);

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::vector<double>& times() const noexcept { return times_; }
    [[nodiscard]] const std::vector<double>& zero_rates() const noexcept { return zero_rates_; }
    [[nodiscard]] double zero_rate(double time) const;
    [[nodiscard]] double discount(double time) const;
    [[nodiscard]] double forward_rate(double start, double end) const;
    [[nodiscard]] YieldCurve bumped_parallel(double bump) const;
    [[nodiscard]] YieldCurve bumped_node(std::size_t node, double bump) const;

private:
    std::string id_{"UNSPECIFIED"};
    std::vector<double> times_;
    std::vector<double> zero_rates_;
};

struct VolatilitySmile {
    std::string id;
    std::vector<double> strikes;
    std::vector<double> volatilities;
    bool normal{false};

    [[nodiscard]] double volatility(double strike) const;
};

struct SwaptionVolatilityCube {
    std::string id;
    std::vector<double> expiries;
    std::vector<double> tenors;
    std::vector<double> strikes;
    std::vector<double> volatilities;
    bool normal{false};

    [[nodiscard]] double volatility(double expiry, double tenor, double strike) const;
};

}  // namespace rates

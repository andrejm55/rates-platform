#pragma once

#include "rates/core/date.hpp"
#include "rates/instruments/instruments.hpp"
#include "rates/market/yield_curve.hpp"

#include <map>
#include <stdexcept>
#include <string>

namespace rates {

struct HullWhiteParameters {
    double mean_reversion{0.03};
    double volatility{0.01};
};

struct RateIndex {
    std::string id;
    std::string currency{"GBP"};
    std::string projection_curve_id;
    int fixing_lag_business_days{0};
    int publication_lag_business_days{0};
    int observation_shift_business_days{0};
    int lookback_business_days{0};
    int lockout_business_days{0};
    DayCountConvention day_count{DayCountConvention::Actual365Fixed};
    FloatingRateCompounding compounding{FloatingRateCompounding::Simple};
};

class MarketDataSnapshot {
public:
    Date valuation_date{2026, 1, 1};
    std::string snapshot_id{"DEMO"};
    std::map<std::string, YieldCurve> curves;
    std::map<std::string, VolatilitySmile> volatility_smiles;
    std::map<std::string, SwaptionVolatilityCube> volatility_cubes;
    std::map<std::string, double> fixings;
    std::map<std::string, RateIndex> rate_indices;
    std::map<std::string, HullWhiteParameters> hull_white_parameters;

    [[nodiscard]] const YieldCurve& curve(const std::string& id) const {
        auto iterator = curves.find(id);
        if (iterator == curves.end()) throw std::out_of_range("Missing curve: " + id);
        return iterator->second;
    }

    [[nodiscard]] const VolatilitySmile& smile(const std::string& id) const {
        auto iterator = volatility_smiles.find(id);
        if (iterator == volatility_smiles.end()) throw std::out_of_range("Missing volatility smile: " + id);
        return iterator->second;
    }

    [[nodiscard]] const SwaptionVolatilityCube& volatility_cube(const std::string& id) const {
        auto iterator = volatility_cubes.find(id);
        if (iterator == volatility_cubes.end()) throw std::out_of_range("Missing volatility cube: " + id);
        return iterator->second;
    }

    [[nodiscard]] const RateIndex* rate_index(const std::string& id) const {
        auto iterator = rate_indices.find(id);
        return iterator == rate_indices.end() ? nullptr : &iterator->second;
    }
};

}  // namespace rates

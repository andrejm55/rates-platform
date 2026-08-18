#pragma once

#include "rates/instruments/instruments.hpp"
#include "rates/market/market_snapshot.hpp"
#include "rates/pricing/pricing_result.hpp"

#include <map>
#include <string>

namespace rates {

struct RiskReport {
    double base_present_value{0.0};
    std::map<std::string, double> sensitivities;
    std::map<std::string, double> scenario_pnl;
    std::vector<std::string> warnings;
};

class RiskEngine {
public:
    static RiskReport swap_risk(const InterestRateSwap& swap, const MarketDataSnapshot& market,
                                double curve_bump = 0.0001);
    static RiskReport european_swaption_risk(const EuropeanSwaption& swaption, const MarketDataSnapshot& market,
                                             PricingModel model, double volatility,
                                             double curve_bump = 0.0001, double volatility_bump = 0.0001);
};

}  // namespace rates

#pragma once

#include "rates/instruments/instruments.hpp"
#include "rates/market/market_snapshot.hpp"
#include "rates/models/option_models.hpp"
#include "rates/pricing/pricing_result.hpp"

namespace rates {

struct MonteCarloSettings {
    int paths{20000};
    int steps_per_year{12};
    unsigned seed{42};
};

class SwapPricer {
public:
    static PricingResult price(const InterestRateSwap& swap, const MarketDataSnapshot& market);
    static double conditional_swap_value(const InterestRateSwap& swap, const YieldCurve& discount_curve,
                                         const YieldCurve& forward_curve,
                                         const Date& valuation_date, const Date& exercise_date,
                                         double factor, const HullWhiteModel& model);
};

class EuropeanSwaptionPricer {
public:
    static PricingResult price(const EuropeanSwaption& swaption, const MarketDataSnapshot& market,
                               PricingModel model, double explicit_volatility = -1.0,
                               const SabrParameters& sabr_parameters = {});
};

class BermudanSwaptionPricer {
public:
    static PricingResult price(const BermudanSwaption& swaption, const MarketDataSnapshot& market,
                               const HullWhiteModel& model, const MonteCarloSettings& settings = {});
};

class CallableRangeAccrualPricer {
public:
    static PricingResult price(const CallableRangeAccrual& product, const MarketDataSnapshot& market,
                               const HullWhiteModel& model, const MonteCarloSettings& settings = {});
};

}  // namespace rates

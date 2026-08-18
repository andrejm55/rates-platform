#pragma once

#include "rates/instruments/instruments.hpp"
#include "rates/market/market_snapshot.hpp"
#include "rates/pricing/pricing_result.hpp"
#include "rates/pricing/pricers.hpp"

#include <map>
#include <string>
#include <vector>

namespace rates {

struct TradeValuation {
    std::string trade_id;
    std::string book;
    double quantity{0.0};
    double present_value{0.0};
    std::string currency;
    bool success{true};
    std::string error;
};

struct PortfolioValuation {
    std::string portfolio_id;
    double total_present_value{0.0};
    std::map<std::string, double> present_value_by_book;
    std::vector<TradeValuation> trades;
};

class PortfolioPricer {
public:
    static PortfolioValuation price(const Portfolio& portfolio, const MarketDataSnapshot& market,
                                    PricingModel european_swaption_model = PricingModel::Bachelier,
                                    double default_swaption_volatility = 0.008,
                                    const MonteCarloSettings& monte_carlo_settings = {});
};

}  // namespace rates

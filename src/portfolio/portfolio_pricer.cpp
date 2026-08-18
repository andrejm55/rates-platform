#include "rates/portfolio/portfolio_pricer.hpp"
#include "rates/pricing/pricers.hpp"

#include <future>
#include <type_traits>
#include <utility>

namespace rates {

PortfolioValuation PortfolioPricer::price(const Portfolio& portfolio, const MarketDataSnapshot& market,
                                          PricingModel european_swaption_model,
                                          double default_swaption_volatility,
                                          const MonteCarloSettings& monte_carlo_settings) {
    PortfolioValuation output;
    output.portfolio_id = portfolio.portfolio_id;

    auto price_trade = [&](const Trade& trade) {
        TradeValuation valuation;
        valuation.trade_id = trade.trade_id;
        valuation.book = trade.book;
        valuation.quantity = trade.quantity;
        try {
            const PricingResult result = std::visit([&](const auto& instrument) -> PricingResult {
                using T = std::decay_t<decltype(instrument)>;
                if constexpr (std::is_same_v<T, InterestRateSwap>) {
                    return SwapPricer::price(instrument, market);
                } else if constexpr (std::is_same_v<T, EuropeanSwaption>) {
                    return EuropeanSwaptionPricer::price(instrument, market, european_swaption_model, default_swaption_volatility);
                } else if constexpr (std::is_same_v<T, BermudanSwaption>) {
                    HullWhiteModel model;
                    return BermudanSwaptionPricer::price(instrument, market, model, monte_carlo_settings);
                } else {
                    HullWhiteModel model;
                    return CallableRangeAccrualPricer::price(instrument, market, model, monte_carlo_settings);
                }
            }, trade.instrument);
            valuation.present_value = trade.quantity * result.present_value;
            valuation.currency = result.currency;
            output.total_present_value += valuation.present_value;
            output.present_value_by_book[trade.book] += valuation.present_value;
        } catch (const std::exception& error) {
            valuation.success = false;
            valuation.error = error.what();
        }
        return valuation;
    };

    if (portfolio.trades.size() <= 1) {
        for (const auto& trade : portfolio.trades) output.trades.push_back(price_trade(trade));
    } else {
        std::vector<std::future<TradeValuation>> futures;
        futures.reserve(portfolio.trades.size());
        for (const auto& trade : portfolio.trades) {
            futures.push_back(std::async(std::launch::async, price_trade, std::cref(trade)));
        }
        output.trades.reserve(portfolio.trades.size());
        for (auto& future : futures) output.trades.push_back(future.get());
    }

    for (const auto& valuation : output.trades) {
        if (!valuation.success) continue;
        output.total_present_value += valuation.present_value;
        output.present_value_by_book[valuation.book] += valuation.present_value;
    }
    return output;
}

}  // namespace rates

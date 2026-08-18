// This file is intentionally dependency-free. Compile manually or adapt it to Google Benchmark.
#include "rates/market/market_snapshot.hpp"
#include "rates/pricing/pricers.hpp"

#include <chrono>
#include <iostream>

int main() {
    rates::MarketDataSnapshot market;
    market.valuation_date = rates::Date::parse("2026-07-27");
    market.curves.emplace("GBP-SONIA-DISCOUNT", rates::YieldCurve("GBP-SONIA-DISCOUNT", {0.25,1,2,5,10,20}, {0.04,0.039,0.038,0.037,0.0365,0.036}));
    rates::InterestRateSwap swap;
    swap.effective_date = rates::Date::parse("2027-07-29");
    swap.maturity_date = rates::Date::parse("2037-07-29");

    constexpr int iterations = 100000;
    const auto start = std::chrono::steady_clock::now();
    double accumulator = 0.0;
    for (int index = 0; index < iterations; ++index) accumulator += rates::SwapPricer::price(swap, market).present_value;
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "iterations=" << iterations << " seconds=" << elapsed << " prices_per_second=" << iterations / elapsed << " checksum=" << accumulator << '\n';
}

#pragma once

#include "rates/market/market_snapshot.hpp"
#include "rates/persistence/json.hpp"
#include "rates/pricing/pricing_result.hpp"

namespace rates {

class ApplicationService {
public:
    static json::Value execute(const json::Value& root);
    static MarketDataSnapshot parse_market(const json::Value& value);
    static json::Value pricing_result_to_json(const PricingResult& result);
};

}  // namespace rates

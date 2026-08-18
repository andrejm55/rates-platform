#pragma once

#include <map>
#include <string>
#include <vector>

namespace rates {

struct CashFlowResult {
    std::string payment_date;
    double amount{0.0};
    double discount_factor{0.0};
    double present_value{0.0};
    std::string description;
};

struct PricingDiagnostics {
    std::string engine;
    std::string model;
    bool converged{true};
    int iterations{0};
    double standard_error{0.0};
    std::vector<std::string> warnings;
    std::map<std::string, double> metrics;
};

struct PricingResult {
    double present_value{0.0};
    std::string currency{"GBP"};
    double par_rate{0.0};
    double annuity{0.0};
    double implied_volatility{0.0};
    std::map<std::string, double> sensitivities;
    std::vector<CashFlowResult> cash_flows;
    PricingDiagnostics diagnostics;
};

}  // namespace rates

#pragma once

#include "rates/market/yield_curve.hpp"

#include <string>
#include <vector>

namespace rates {

struct DepositQuote {
    double maturity{0.0};
    double simple_rate{0.0};
};

struct SwapQuote {
    double maturity{0.0};
    double par_rate{0.0};
    double payment_interval{1.0};
};

struct FraQuote {
    double start{0.0};
    double end{0.0};
    double rate{0.0};
};

struct FuturesQuote {
    double start{0.0};
    double end{0.0};
    double price{0.0};
    double convexity_adjustment{0.0};
};

struct BasisSwapQuote {
    double maturity{0.0};
    double par_rate{0.0};
    double basis_spread{0.0};
    double payment_interval{0.25};
};

struct CurveCalibrationResult {
    YieldCurve curve;
    std::vector<double> discount_factors;
    std::vector<double> calibration_residuals;
    bool converged{true};
    std::vector<std::string> warnings;
};

struct MultiCurveCalibrationResult {
    YieldCurve discount_curve;
    YieldCurve projection_curve;
    std::vector<double> discount_factors;
    std::vector<double> projection_discount_factors;
    std::vector<double> discount_residuals;
    std::vector<double> projection_residuals;
    bool converged{true};
    std::vector<std::string> warnings;
};

class CurveBootstrapper {
public:
    static CurveCalibrationResult bootstrap(
        const std::string& curve_id,
        std::vector<DepositQuote> deposits,
        std::vector<SwapQuote> swaps
    );

    static MultiCurveCalibrationResult bootstrap_multi_curve(
        const std::string& discount_curve_id,
        const std::string& projection_curve_id,
        std::vector<DepositQuote> ois_deposits,
        std::vector<SwapQuote> ois_swaps,
        std::vector<DepositQuote> projection_deposits,
        std::vector<FraQuote> fras,
        std::vector<FuturesQuote> futures,
        std::vector<BasisSwapQuote> basis_swaps
    );
};

}  // namespace rates

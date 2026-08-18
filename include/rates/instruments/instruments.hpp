#pragma once

#include "rates/core/date.hpp"

#include <string>
#include <variant>
#include <vector>

namespace rates {

enum class OptionDirection { Payer, Receiver };
enum class SettlementType { Physical, Cash };
enum class FloatingRateCompounding { Simple, OvernightCompounded };

enum class PricingModel { Black76, Bachelier, SabrBlack, SabrNormal, HullWhiteLsmc };

struct InterestRateSwap {
    std::string id{"SWAP"};
    std::string currency{"GBP"};
    std::string discount_curve_id{"GBP-SONIA-DISCOUNT"};
    std::string forward_curve_id{"GBP-SONIA-DISCOUNT"};
    std::string floating_index_id;
    Date effective_date{2026, 7, 29};
    Date maturity_date{2031, 7, 29};
    double notional{1'000'000.0};
    double fixed_rate{0.04};
    bool pay_fixed{true};
    int fixed_frequency_months{12};
    int floating_frequency_months{12};
    int floating_fixing_lag_business_days{0};
    int floating_payment_lag_business_days{0};
    int floating_publication_lag_business_days{0};
    int floating_observation_shift_business_days{0};
    int floating_lookback_business_days{0};
    int floating_lockout_business_days{0};
    DayCountConvention fixed_day_count{DayCountConvention::Actual365Fixed};
    DayCountConvention floating_day_count{DayCountConvention::Actual365Fixed};
    FloatingRateCompounding floating_compounding{FloatingRateCompounding::Simple};
    BusinessDayConvention business_day_convention{BusinessDayConvention::ModifiedFollowing};
};

struct EuropeanSwaption {
    std::string id{"EUROPEAN-SWAPTION"};
    InterestRateSwap underlying;
    Date exercise_date{2027, 7, 29};
    double strike{0.04};
    OptionDirection direction{OptionDirection::Payer};
    SettlementType settlement{SettlementType::Physical};
    bool long_position{true};
    std::string volatility_smile_id{"GBP-SWAPTION-NORMAL"};
    std::string volatility_cube_id;
};

struct BermudanSwaption {
    std::string id{"BERMUDAN-SWAPTION"};
    InterestRateSwap underlying;
    std::vector<Date> exercise_dates;
    double strike{0.04};
    OptionDirection direction{OptionDirection::Payer};
    bool long_position{true};
};

struct CallableRangeAccrual {
    std::string id{"CALLABLE-RANGE-ACCRUAL"};
    std::string currency{"GBP"};
    std::string discount_curve_id{"GBP-SONIA-DISCOUNT"};
    Date effective_date{2026, 7, 29};
    Date maturity_date{2031, 7, 29};
    double notional{1'000'000.0};
    double coupon_rate{0.06};
    double lower_bound{0.01};
    double upper_bound{0.06};
    int observation_frequency_months{1};
    int payment_frequency_months{12};
    std::vector<Date> issuer_call_dates;
    double redemption_fraction{1.0};
};

using Instrument = std::variant<InterestRateSwap, EuropeanSwaption, BermudanSwaption, CallableRangeAccrual>;

struct Trade {
    std::string trade_id;
    std::string book{"DEMO"};
    double quantity{1.0};
    Instrument instrument;
};

struct Portfolio {
    std::string portfolio_id{"DEMO-PORTFOLIO"};
    std::string reporting_currency{"GBP"};
    std::vector<Trade> trades;
};

}  // namespace rates

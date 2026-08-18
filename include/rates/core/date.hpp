#pragma once

#include <compare>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace rates {

enum class BusinessDayConvention {
    Unadjusted,
    Following,
    ModifiedFollowing,
    Preceding,
    ModifiedPreceding
};

enum class DayCountConvention {
    Actual360,
    Actual365Fixed,
    Thirty360
};

class Date {
public:
    Date() = default;
    Date(int year, unsigned month, unsigned day);

    static Date parse(const std::string& text);
    static Date from_serial(std::int64_t serial);

    [[nodiscard]] int year() const noexcept { return year_; }
    [[nodiscard]] unsigned month() const noexcept { return month_; }
    [[nodiscard]] unsigned day() const noexcept { return day_; }
    [[nodiscard]] std::string str() const;
    [[nodiscard]] std::int64_t serial() const;
    [[nodiscard]] bool is_weekend() const;
    [[nodiscard]] Date add_days(int days) const;
    [[nodiscard]] Date add_months(int months, bool preserve_end_of_month = true) const;
    [[nodiscard]] Date add_years(int years) const;
    [[nodiscard]] bool is_end_of_month() const;

    auto operator<=>(const Date&) const = default;

private:
    int year_{1970};
    unsigned month_{1};
    unsigned day_{1};
};

class Calendar {
public:
    Calendar() = default;
    explicit Calendar(std::set<std::int64_t> holidays);

    void add_holiday(const Date& date);
    [[nodiscard]] bool is_business_day(const Date& date) const;
    [[nodiscard]] Date adjust(const Date& date, BusinessDayConvention convention) const;
    [[nodiscard]] Date advance_business_days(const Date& date, int business_days) const;

private:
    std::set<std::int64_t> holidays_;
};

struct SchedulePeriod {
    Date accrual_start;
    Date accrual_end;
    Date payment_date;
    Date fixing_date;
    double year_fraction{0.0};
};

[[nodiscard]] double year_fraction(const Date& start, const Date& end, DayCountConvention convention);

[[nodiscard]] std::vector<SchedulePeriod> generate_schedule(
    const Date& effective_date,
    const Date& maturity_date,
    int frequency_months,
    const Calendar& calendar,
    BusinessDayConvention business_day_convention,
    DayCountConvention day_count_convention,
    int fixing_lag_business_days = 0,
    int payment_lag_business_days = 0
);

}  // namespace rates

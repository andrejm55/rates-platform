#include "rates/core/date.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace rates {
namespace {
using namespace std::chrono;

year_month_day to_ymd(const Date& date) {
    return year{date.year()} / month{date.month()} / day{date.day()};
}

Date from_ymd(const year_month_day& ymd) {
    if (!ymd.ok()) {
        throw std::invalid_argument("Invalid calendar date");
    }
    return Date(static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()));
}

unsigned last_day_of_month(int year_value, unsigned month_value) {
    const year_month_day_last last = year{year_value} / month_day_last{month{month_value}};
    return static_cast<unsigned>(last.day());
}
}

Date::Date(int year_value, unsigned month_value, unsigned day_value)
    : year_(year_value), month_(month_value), day_(day_value) {
    if (!to_ymd(*this).ok()) {
        throw std::invalid_argument("Invalid date: " + str());
    }
}

Date Date::parse(const std::string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        throw std::invalid_argument("Date must be YYYY-MM-DD: " + text);
    }
    return Date(std::stoi(text.substr(0, 4)), static_cast<unsigned>(std::stoi(text.substr(5, 2))),
                static_cast<unsigned>(std::stoi(text.substr(8, 2))));
}

Date Date::from_serial(std::int64_t serial_value) {
    const sys_days epoch = std::chrono::year{1970} / std::chrono::January / 1;
    return from_ymd(year_month_day{epoch + days{serial_value}});
}

std::string Date::str() const {
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << year_ << '-' << std::setw(2) << month_ << '-' << std::setw(2) << day_;
    return output.str();
}

std::int64_t Date::serial() const {
    const sys_days epoch = std::chrono::year{1970} / std::chrono::January / 1;
    return (sys_days{to_ymd(*this)} - epoch).count();
}

bool Date::is_weekend() const {
    const std::chrono::weekday weekday{std::chrono::sys_days{to_ymd(*this)}};
    return weekday == std::chrono::Saturday || weekday == std::chrono::Sunday;
}

Date Date::add_days(int day_count) const {
    return from_ymd(year_month_day{sys_days{to_ymd(*this)} + days{day_count}});
}

Date Date::add_months(int month_count, bool preserve_end_of_month) const {
    const bool was_end = is_end_of_month();
    year_month target = std::chrono::year{year_} / std::chrono::month{month_} + std::chrono::months{month_count};
    unsigned target_day = day_;
    const unsigned month_end = last_day_of_month(static_cast<int>(target.year()), static_cast<unsigned>(target.month()));
    if ((preserve_end_of_month && was_end) || target_day > month_end) {
        target_day = month_end;
    }
    return from_ymd(target / std::chrono::day{target_day});
}

Date Date::add_years(int years_count) const {
    return add_months(years_count * 12);
}

bool Date::is_end_of_month() const {
    return day_ == last_day_of_month(year_, month_);
}

Calendar::Calendar(std::set<std::int64_t> holidays) : holidays_(std::move(holidays)) {}

void Calendar::add_holiday(const Date& date) {
    holidays_.insert(date.serial());
}

bool Calendar::is_business_day(const Date& date) const {
    return !date.is_weekend() && !holidays_.contains(date.serial());
}

Date Calendar::adjust(const Date& date, BusinessDayConvention convention) const {
    if (convention == BusinessDayConvention::Unadjusted || is_business_day(date)) {
        return date;
    }

    auto following = [&]() {
        Date candidate = date;
        while (!is_business_day(candidate)) {
            candidate = candidate.add_days(1);
        }
        return candidate;
    };
    auto preceding = [&]() {
        Date candidate = date;
        while (!is_business_day(candidate)) {
            candidate = candidate.add_days(-1);
        }
        return candidate;
    };

    if (convention == BusinessDayConvention::Following) {
        return following();
    }
    if (convention == BusinessDayConvention::Preceding) {
        return preceding();
    }
    if (convention == BusinessDayConvention::ModifiedFollowing) {
        Date candidate = following();
        return candidate.month() == date.month() ? candidate : preceding();
    }
    Date candidate = preceding();
    return candidate.month() == date.month() ? candidate : following();
}

Date Calendar::advance_business_days(const Date& date, int business_days) const {
    if (business_days == 0) {
        return adjust(date, BusinessDayConvention::Following);
    }
    const int direction = business_days > 0 ? 1 : -1;
    int remaining = business_days > 0 ? business_days : -business_days;
    Date candidate = date;
    while (remaining > 0) {
        candidate = candidate.add_days(direction);
        if (is_business_day(candidate)) {
            --remaining;
        }
    }
    return candidate;
}

double year_fraction(const Date& start, const Date& end, DayCountConvention convention) {
    if (end < start) {
        return -year_fraction(end, start, convention);
    }
    if (convention == DayCountConvention::Actual360) {
        return static_cast<double>(end.serial() - start.serial()) / 360.0;
    }
    if (convention == DayCountConvention::Actual365Fixed) {
        return static_cast<double>(end.serial() - start.serial()) / 365.0;
    }
    const int d1 = std::min<int>(30, static_cast<int>(start.day()));
    const int d2 = (d1 == 30) ? std::min<int>(30, static_cast<int>(end.day())) : static_cast<int>(end.day());
    const int days360 = 360 * (end.year() - start.year()) + 30 * (static_cast<int>(end.month()) - static_cast<int>(start.month())) + (d2 - d1);
    return static_cast<double>(days360) / 360.0;
}

std::vector<SchedulePeriod> generate_schedule(
    const Date& effective_date,
    const Date& maturity_date,
    int frequency_months,
    const Calendar& calendar,
    BusinessDayConvention business_day_convention,
    DayCountConvention day_count_convention,
    int fixing_lag_business_days,
    int payment_lag_business_days) {
    if (frequency_months <= 0) {
        throw std::invalid_argument("Schedule frequency must be positive");
    }
    if (maturity_date <= effective_date) {
        throw std::invalid_argument("Maturity must follow effective date");
    }

    std::vector<Date> boundaries{effective_date};
    Date cursor = effective_date;
    while (cursor < maturity_date) {
        Date next = cursor.add_months(frequency_months);
        if (next > maturity_date) {
            next = maturity_date;
        }
        boundaries.push_back(next);
        cursor = next;
    }

    std::vector<SchedulePeriod> periods;
    periods.reserve(boundaries.size() - 1);
    for (std::size_t index = 1; index < boundaries.size(); ++index) {
        const Date start = boundaries[index - 1];
        const Date end = boundaries[index];
        const Date adjusted_end = calendar.adjust(end, business_day_convention);
        const Date payment = payment_lag_business_days == 0
            ? adjusted_end
            : calendar.advance_business_days(adjusted_end, payment_lag_business_days);
        const Date fixing = fixing_lag_business_days == 0
            ? calendar.adjust(start, BusinessDayConvention::Preceding)
            : calendar.advance_business_days(calendar.adjust(start, BusinessDayConvention::Preceding), -fixing_lag_business_days);
        periods.push_back({start, end, payment, fixing, year_fraction(start, end, day_count_convention)});
    }
    return periods;
}

}  // namespace rates

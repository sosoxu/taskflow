#pragma once

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/result/result.h"

namespace taskflow::scheduler::engine {

class CronParser {
public:
    CronParser() = delete;

    // Parse a 6-field cron expression (sec min hour day month weekday)
    // and compute the next trigger time after the given 'from' time
    // Returns the next trigger time as "YYYY-MM-DD HH:MM:SS" string
    static common::result::Result<std::string> getNextTrigger(
        const std::string& cron_expression, const std::string& from_time) {
        // Split the cron expression into 6 fields
        std::vector<std::string> fields;
        std::istringstream iss(cron_expression);
        std::string field;
        while (iss >> field) {
            fields.push_back(field);
        }
        if (fields.size() != 6) {
            return common::result::Result<std::string>::failure(
                "Cron expression must have 6 fields (sec min hour day month weekday), got " +
                std::to_string(fields.size()));
        }

        // Parse each field
        auto seconds = parseField(fields[0], 0, 59);
        if (!seconds.ok()) return common::result::Result<std::string>::failure(seconds.error());

        auto minutes = parseField(fields[1], 0, 59);
        if (!minutes.ok()) return common::result::Result<std::string>::failure(minutes.error());

        auto hours = parseField(fields[2], 0, 23);
        if (!hours.ok()) return common::result::Result<std::string>::failure(hours.error());

        auto days = parseField(fields[3], 1, 31);
        if (!days.ok()) return common::result::Result<std::string>::failure(days.error());

        auto months = parseField(fields[4], 1, 12);
        if (!months.ok()) return common::result::Result<std::string>::failure(months.error());

        auto weekdays = parseField(fields[5], 0, 6);
        if (!weekdays.ok()) return common::result::Result<std::string>::failure(weekdays.error());

        // Parse the from_time string
        std::tm tm_from{};
        std::istringstream time_iss(from_time);
        time_iss >> std::get_time(&tm_from, "%Y-%m-%d %H:%M:%S");
        if (time_iss.fail()) {
            return common::result::Result<std::string>::failure(
                "Invalid from_time format, expected YYYY-MM-DD HH:MM:SS");
        }
        tm_from.tm_isdst = 0;
        auto from_time_t = timegm(&tm_from);

        // Start searching from from_time + 1 second
        auto search_time_t = from_time_t + 1;

        // Search up to 4 years (account for leap year)
        const auto max_search_time_t = from_time_t + (4 * 365 + 1) * 86400;

        const auto& sec_vals = seconds.value();
        const auto& min_vals = minutes.value();
        const auto& hour_vals = hours.value();
        const auto& day_vals = days.value();
        const auto& month_vals = months.value();
        const auto& wday_vals = weekdays.value();

        while (search_time_t <= max_search_time_t) {
            std::tm tm_search{};
            gmtime_r(&search_time_t, &tm_search);

            // Check month
            bool month_wrapped = false;
            int next_month = findNext(month_vals, tm_search.tm_mon + 1, month_wrapped);
            if (month_wrapped) {
                // Advance to next year, January
                tm_search.tm_year++;
                tm_search.tm_mon = next_month - 1;
                tm_search.tm_mday = 1;
                tm_search.tm_hour = 0;
                tm_search.tm_min = 0;
                tm_search.tm_sec = 0;
                tm_search.tm_isdst = 0;
                search_time_t = timegm(&tm_search);
                continue;
            }
            if (next_month != tm_search.tm_mon + 1) {
                // Advance to the matching month
                tm_search.tm_mon = next_month - 1;
                tm_search.tm_mday = 1;
                tm_search.tm_hour = 0;
                tm_search.tm_min = 0;
                tm_search.tm_sec = 0;
                tm_search.tm_isdst = 0;
                search_time_t = timegm(&tm_search);
                continue;
            }

            // Check day and weekday together
            bool day_match = false;
            bool wday_match = false;
            {
                // Fix #276: 删除无用的 findNext 调用（返回值和 wrapped 参数均未使用）
                // 以及冗余的 front()/back() 范围检查（binary_search 已足够判断成员关系）
                day_match = std::binary_search(day_vals.begin(), day_vals.end(), tm_search.tm_mday);
                wday_match = std::binary_search(wday_vals.begin(), wday_vals.end(), tm_search.tm_wday);
            }

            // If both day and weekday fields are specified (not just *),
            // standard cron behavior: match either one.
            // If only one is specified, match that one.
            bool day_field_is_star = (fields[3] == "*");
            bool wday_field_is_star = (fields[5] == "*");

            bool day_ok = false;
            if (!day_field_is_star && !wday_field_is_star) {
                // Both specified: match either
                day_ok = day_match || wday_match;
            } else if (day_field_is_star && wday_field_is_star) {
                // Both are *: match any day
                day_ok = true;
            } else if (!day_field_is_star) {
                day_ok = day_match;
            } else {
                day_ok = wday_match;
            }

            if (!day_ok) {
                // Advance to next day
                tm_search.tm_mday++;
                tm_search.tm_hour = 0;
                tm_search.tm_min = 0;
                tm_search.tm_sec = 0;
                tm_search.tm_isdst = 0;
                search_time_t = timegm(&tm_search);
                continue;
            }

            // Check hour
            bool hour_wrapped = false;
            int next_hour = findNext(hour_vals, tm_search.tm_hour, hour_wrapped);
            if (hour_wrapped) {
                // Advance to next day
                tm_search.tm_mday++;
                tm_search.tm_hour = 0;
                tm_search.tm_min = 0;
                tm_search.tm_sec = 0;
                tm_search.tm_isdst = 0;
                search_time_t = timegm(&tm_search);
                continue;
            }
            if (next_hour != tm_search.tm_hour) {
                tm_search.tm_hour = next_hour;
                tm_search.tm_min = 0;
                tm_search.tm_sec = 0;
                tm_search.tm_isdst = 0;
                search_time_t = timegm(&tm_search);
                continue;
            }

            // Check minute
            bool min_wrapped = false;
            int next_min = findNext(min_vals, tm_search.tm_min, min_wrapped);
            if (min_wrapped) {
                // Advance to next hour
                tm_search.tm_hour++;
                tm_search.tm_min = 0;
                tm_search.tm_sec = 0;
                tm_search.tm_isdst = 0;
                search_time_t = timegm(&tm_search);
                continue;
            }
            if (next_min != tm_search.tm_min) {
                tm_search.tm_min = next_min;
                tm_search.tm_sec = 0;
                tm_search.tm_isdst = 0;
                search_time_t = timegm(&tm_search);
                continue;
            }

            // Check second
            bool sec_wrapped = false;
            int next_sec = findNext(sec_vals, tm_search.tm_sec, sec_wrapped);
            if (sec_wrapped) {
                // Advance to next minute
                tm_search.tm_min++;
                tm_search.tm_sec = 0;
                tm_search.tm_isdst = 0;
                search_time_t = timegm(&tm_search);
                continue;
            }
            if (next_sec != tm_search.tm_sec) {
                tm_search.tm_sec = next_sec;
                tm_search.tm_isdst = 0;
                search_time_t = timegm(&tm_search);
                continue;
            }

            // All fields match!
            std::ostringstream oss;
            oss << std::put_time(&tm_search, "%Y-%m-%d %H:%M:%S");
            return oss.str();
        }

        return common::result::Result<std::string>::failure(
            "No matching time found within 4 years for cron expression: " + cron_expression);
    }

private:
    // Parse a single field of the cron expression
    // Returns sorted list of valid values for that field
    static common::result::Result<std::vector<int>> parseField(
        const std::string& field, int min_val, int max_val) {
        std::vector<int> result;

        // Split by comma (list elements)
        std::vector<std::string> parts;
        std::istringstream iss(field);
        std::string part;
        while (std::getline(iss, part, ',')) {
            parts.push_back(part);
        }

        for (const auto& p : parts) {
            // Check for step
            auto slash_pos = p.find('/');
            std::string range_part = p;
            int step = 1;

            if (slash_pos != std::string::npos) {
                range_part = p.substr(0, slash_pos);
                try {
                    step = std::stoi(p.substr(slash_pos + 1));
                } catch (...) {
                    return common::result::Result<std::vector<int>>::failure(
                        "Invalid step value in cron field: " + p);
                }
                if (step <= 0) {
                    return common::result::Result<std::vector<int>>::failure(
                        "Step must be positive in cron field: " + p);
                }
            }

            // Determine range
            int range_start = min_val;
            int range_end = max_val;

            if (range_part == "*") {
                // All values in range
            } else {
                auto dash_pos = range_part.find('-');
                if (dash_pos != std::string::npos) {
                    try {
                        range_start = std::stoi(range_part.substr(0, dash_pos));
                        range_end = std::stoi(range_part.substr(dash_pos + 1));
                    } catch (...) {
                        return common::result::Result<std::vector<int>>::failure(
                            "Invalid range in cron field: " + range_part);
                    }
                } else {
                    try {
                        range_start = std::stoi(range_part);
                        range_end = range_start;
                    } catch (...) {
                        return common::result::Result<std::vector<int>>::failure(
                            "Invalid value in cron field: " + range_part);
                    }
                }
            }

            // Validate range
            if (range_start < min_val || range_end > max_val || range_start > range_end) {
                return common::result::Result<std::vector<int>>::failure(
                    "Range out of bounds in cron field: " + p +
                    " (valid: " + std::to_string(min_val) + "-" + std::to_string(max_val) + ")");
            }

            // Generate values
            // Fix #291: 用 long long 防止 v += step 整数溢出导致无限循环
            // 例如 step=2147483647 时，int 的 v += step 会溢出为负数，导致 v <= range_end 恒真
            for (long long v = range_start; v <= range_end; v += step) {
                result.push_back(static_cast<int>(v));
            }
        }

        // Sort and deduplicate
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());

        if (result.empty()) {
            return common::result::Result<std::vector<int>>::failure(
                "No valid values in cron field: " + field);
        }

        return result;
    }

    // Find the smallest value in 'values' that is >= target, or wrap around
    static int findNext(const std::vector<int>& values, int target, bool& wrapped) {
        wrapped = false;
        auto it = std::lower_bound(values.begin(), values.end(), target);
        if (it != values.end()) {
            return *it;
        }
        wrapped = true;
        return values.front();
    }
};

}  // namespace taskflow::scheduler::engine

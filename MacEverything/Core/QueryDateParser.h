#pragma once
#include <ctime>
#include <cstdint>
#include <string>
#include <cctype>
#include <algorithm>
#include "QueryAST.h"

/// Parse date filter arguments (dm:, dc:, da:) into structured time ranges.
/// Fills node.op, node.numVal1 (start epoch), node.numVal2 (end epoch).
///
/// Supported formats:
///   Keywords:   today, yesterday, thisweek, lastweek, thismonth, lastmonth,
///               thisyear, lastyear
///   Relative:   last2days, last7days, last30days, last3months, last6months, last2years
///   ISO dates:  2024, 2024-01, 2024-01-15
///   Comparison: >2024-01-01, >=yesterday, <2024-06, <=today
///   Range:      2023..2024, 2024-01..2024-06, yesterday..today
class QueryDateParser {
    static int safeStoi(const std::string& s, bool& ok) {
        try { ok = true; return std::stoi(s); }
        catch (...) { ok = false; return 0; }
    }

    static time_t safeMktime(struct tm* t) {
        time_t result = mktime(t);
        return (result == static_cast<time_t>(-1)) ? 0 : result;
    }

    static uint64_t toEpoch(time_t t) {
        return (t < 0) ? 0 : static_cast<uint64_t>(t);
    }

public:
    /// Parse a date filter argument and populate node fields.
    /// Uses RANGE op with numVal1=start, numVal2=end (both inclusive, epoch seconds).
    /// For comparisons (GT/LT/GE/LE), uses the corresponding CompareOp.
    static void parse(QueryNode& node, const std::string& arg) {
        if (arg.empty()) return;

        // Check for range: A..B
        auto dotdot = arg.find("..");
        if (dotdot != std::string::npos) {
            std::string left = arg.substr(0, dotdot);
            std::string right = arg.substr(dotdot + 2);
            auto [ls, le] = parseDateExpr(left);
            auto [rs, re] = parseDateExpr(right);
            node.op = CompareOp::RANGE;
            node.numVal1 = toEpoch(ls);   // range start
            node.numVal2 = toEpoch(re);   // range end
            return;
        }

        // Check for comparison operators
        size_t valStart = 0;
        CompareOp cmpOp = CompareOp::RANGE; // default = range (keyword/date gives a range)
        if (arg.size() >= 2 && arg[0] == '>' && arg[1] == '=') {
            cmpOp = CompareOp::GE;
            valStart = 2;
        } else if (arg.size() >= 2 && arg[0] == '<' && arg[1] == '=') {
            cmpOp = CompareOp::LE;
            valStart = 2;
        } else if (arg[0] == '>') {
            cmpOp = CompareOp::GT;
            valStart = 1;
        } else if (arg[0] == '<') {
            cmpOp = CompareOp::LT;
            valStart = 1;
        }

        std::string dateStr = arg.substr(valStart);
        auto [start, end] = parseDateExpr(dateStr);

        if (cmpOp == CompareOp::GE || cmpOp == CompareOp::GT ||
            cmpOp == CompareOp::LE || cmpOp == CompareOp::LT) {
            node.op = cmpOp;
            // For GT/GE: compare against the start of the period
            // For LT/LE: compare against the end of the period
            if (cmpOp == CompareOp::GT) {
                node.numVal1 = toEpoch(end);
            } else if (cmpOp == CompareOp::GE) {
                node.numVal1 = toEpoch(start);
            } else if (cmpOp == CompareOp::LT) {
                node.numVal1 = toEpoch(start);
            } else { // LE
                node.numVal1 = toEpoch(end);
            }
        } else {
            node.op = CompareOp::RANGE;
            node.numVal1 = toEpoch(start);
            node.numVal2 = toEpoch(end);
        }
    }

private:
    /// Returns {start_epoch, end_epoch} for a date expression.
    static std::pair<time_t, time_t> parseDateExpr(const std::string& expr) {
        std::string lower = me::toLower(expr);

        // Keywords
        if (lower == "today")      return todayRange();
        if (lower == "yesterday")  return yesterdayRange();
        if (lower == "thisweek")   return thisWeekRange();
        if (lower == "lastweek")   return lastWeekRange();
        if (lower == "thismonth")  return thisMonthRange();
        if (lower == "lastmonth")  return lastMonthRange();
        if (lower == "thisyear")   return thisYearRange();
        if (lower == "lastyear")   return lastYearRange();

        // Relative: lastNdays, lastNmonths, lastNyears, lastNweeks
        if (lower.substr(0, 4) == "last") {
            return parseRelative(lower.substr(4));
        }

        // ISO date: YYYY, YYYY-MM, YYYY-MM-DD
        return parseISODate(lower);
    }

    // ── Keyword ranges ──

    static std::pair<time_t, time_t> todayRange() {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t start = safeMktime(&t);
        t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
        time_t end = safeMktime(&t);
        return {start, end};
    }

    static std::pair<time_t, time_t> yesterdayRange() {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_mday -= 1;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t start = safeMktime(&t); // mktime normalizes
        t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
        time_t end = safeMktime(&t);
        return {start, end};
    }

    static std::pair<time_t, time_t> thisWeekRange() {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        int wday = t.tm_wday; // 0=Sun
        t.tm_mday -= wday;    // back to Sunday
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t start = safeMktime(&t);
        t.tm_mday += 6; // Saturday
        t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
        time_t end = safeMktime(&t);
        return {start, end};
    }

    static std::pair<time_t, time_t> lastWeekRange() {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        int wday = t.tm_wday;
        t.tm_mday -= wday + 7; // back to last Sunday
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t start = safeMktime(&t);
        t.tm_mday += 6;
        t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
        time_t end = safeMktime(&t);
        return {start, end};
    }

    static std::pair<time_t, time_t> thisMonthRange() {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_mday = 1;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t start = safeMktime(&t);
        // End of month: go to 1st of next month - 1 second
        t.tm_mon += 1;
        time_t nextMonth = safeMktime(&t);
        time_t end = nextMonth - 1;
        return {start, end};
    }

    static std::pair<time_t, time_t> lastMonthRange() {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_mon -= 1;
        t.tm_mday = 1;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t start = safeMktime(&t);
        t.tm_mon += 1;
        time_t nextMonth = safeMktime(&t);
        time_t end = nextMonth - 1;
        return {start, end};
    }

    static std::pair<time_t, time_t> thisYearRange() {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_mon = 0; t.tm_mday = 1;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t start = safeMktime(&t);
        t.tm_mon = 11; t.tm_mday = 31;
        t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
        time_t end = safeMktime(&t);
        return {start, end};
    }

    static std::pair<time_t, time_t> lastYearRange() {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_year -= 1;
        t.tm_mon = 0; t.tm_mday = 1;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t start = safeMktime(&t);
        t.tm_mon = 11; t.tm_mday = 31;
        t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
        time_t end = safeMktime(&t);
        return {start, end};
    }

    // ── Relative date parsing ──
    // Parses: "Ndays", "Nmonths", "Nyears", "Nweeks"
    // Returns range from N periods ago to now.
    static std::pair<time_t, time_t> parseRelative(const std::string& s) {
        // Extract leading digits
        size_t numEnd = 0;
        while (numEnd < s.size() && std::isdigit(static_cast<unsigned char>(s[numEnd])))
            numEnd++;

        if (numEnd == 0) return {0, 0};

        bool ok = false;
        int n = safeStoi(s.substr(0, numEnd), ok);
        if (!ok || n <= 0) return {0, 0};
        std::string unit = s.substr(numEnd);

        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);

        if (unit == "days" || unit == "day") {
            t.tm_mday -= n;
        } else if (unit == "weeks" || unit == "week") {
            t.tm_mday -= n * 7;
        } else if (unit == "months" || unit == "month") {
            t.tm_mon -= n;
        } else if (unit == "years" || unit == "year") {
            t.tm_year -= n;
        } else {
            return {0, 0};
        }

        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t start = safeMktime(&t);
        return {start, now};
    }

    // ── ISO date parsing ──
    // Supports: YYYY, YYYY-MM, YYYY-MM-DD
    static std::pair<time_t, time_t> parseISODate(const std::string& s) {
        if (s.size() < 4) return {0, 0};

        // Check for YYYY-MM-DD
        if (s.size() >= 10 && s[4] == '-' && s[7] == '-') {
            bool ok1, ok2, ok3;
            int y = safeStoi(s.substr(0, 4), ok1);
            int m = safeStoi(s.substr(5, 2), ok2);
            int d = safeStoi(s.substr(8, 2), ok3);
            if (!ok1 || !ok2 || !ok3) return {0, 0};
            struct tm t = {};
            t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
            t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
            t.tm_isdst = -1;
            time_t start = safeMktime(&t);
            t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
            time_t end = safeMktime(&t);
            return {start, end};
        }

        // Check for YYYY-MM
        if (s.size() >= 7 && s[4] == '-') {
            bool ok1, ok2;
            int y = safeStoi(s.substr(0, 4), ok1);
            int m = safeStoi(s.substr(5, 2), ok2);
            if (!ok1 || !ok2) return {0, 0};
            struct tm t = {};
            t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = 1;
            t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
            t.tm_isdst = -1;
            time_t start = safeMktime(&t);
            t.tm_mon += 1; // first of next month
            time_t end = safeMktime(&t) - 1;
            return {start, end};
        }

        // YYYY only
        if (s.size() >= 4 && std::isdigit(static_cast<unsigned char>(s[0]))) {
            bool ok;
            int y = safeStoi(s.substr(0, 4), ok);
            if (!ok) return {0, 0};
            struct tm t = {};
            t.tm_year = y - 1900; t.tm_mon = 0; t.tm_mday = 1;
            t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
            t.tm_isdst = -1;
            time_t start = safeMktime(&t);
            t.tm_mon = 11; t.tm_mday = 31;
            t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
            time_t end = safeMktime(&t);
            return {start, end};
        }

        return {0, 0};
    }
};

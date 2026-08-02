#pragma once
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <sys/stat.h>
#include <utime.h>

// ── Part 57: Query Date Filter Tests ──
// Tests for Phase 3 date filters: dm:, dc:, datemodified:, datecreated:
// Tests both the date parser and the integrated search engine evaluation.

static void runQueryDateFilterTests() {
    std::cout << "── Part 57: Query Date Filters (Phase 3) ──\n";
    int localPassed = 0, localFailed = 0;

    auto check = [&](bool cond, const std::string& msg) {
        if (cond) { localPassed++; passed++; }
        else { localFailed++; failed++; std::cout << "  FAIL: " << msg << "\n"; }
    };

    // ═══════════════════════════════════════════
    // Section A: QueryDateParser unit tests
    // ═══════════════════════════════════════════

    // A1: Parse "today" — should produce RANGE with today's start/end
    {
        auto node = QueryNode::makeFilter("dm", "today");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A1: dm:today → RANGE op");

        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t todayStart = mktime(&t);
        t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
        time_t todayEnd = mktime(&t);
        check(node->numVal1 == static_cast<uint64_t>(todayStart), "A1: dm:today start");
        check(node->numVal2 == static_cast<uint64_t>(todayEnd), "A1: dm:today end");
    }

    // A2: Parse "yesterday"
    {
        auto node = QueryNode::makeFilter("dm", "yesterday");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A2: dm:yesterday → RANGE");

        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_mday -= 1;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t ydayStart = mktime(&t);
        check(node->numVal1 == static_cast<uint64_t>(ydayStart), "A2: yesterday start");
    }

    // A3: Parse ISO date "2024-01-15"
    {
        auto node = QueryNode::makeFilter("dm", "2024-01-15");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A3: ISO date → RANGE");

        struct tm t = {};
        t.tm_year = 2024 - 1900; t.tm_mon = 0; t.tm_mday = 15;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        t.tm_isdst = -1;
        time_t expected = mktime(&t);
        check(node->numVal1 == static_cast<uint64_t>(expected), "A3: 2024-01-15 start");
    }

    // A4: Parse ISO month "2024-06"
    {
        auto node = QueryNode::makeFilter("dm", "2024-06");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A4: ISO month → RANGE");

        struct tm t = {};
        t.tm_year = 2024 - 1900; t.tm_mon = 5; t.tm_mday = 1;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        t.tm_isdst = -1;
        time_t expected = mktime(&t);
        check(node->numVal1 == static_cast<uint64_t>(expected), "A4: 2024-06 start");
    }

    // A5: Parse ISO year "2024"
    {
        auto node = QueryNode::makeFilter("dm", "2024");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A5: ISO year → RANGE");

        struct tm t = {};
        t.tm_year = 2024 - 1900; t.tm_mon = 0; t.tm_mday = 1;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        t.tm_isdst = -1;
        time_t start = mktime(&t);
        t.tm_mon = 11; t.tm_mday = 31;
        t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
        time_t end = mktime(&t);
        check(node->numVal1 == static_cast<uint64_t>(start), "A5: 2024 start");
        check(node->numVal2 == static_cast<uint64_t>(end), "A5: 2024 end");
    }

    // A6: Parse comparison ">2024-01-01"
    {
        auto node = QueryNode::makeFilter("dm", ">2024-01-01");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::GT, "A6: >date → GT op");

        // GT means "after end of the date period"
        struct tm t = {};
        t.tm_year = 2024 - 1900; t.tm_mon = 0; t.tm_mday = 1;
        t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
        t.tm_isdst = -1;
        time_t expected = mktime(&t);
        check(node->numVal1 == static_cast<uint64_t>(expected), "A6: >2024-01-01 threshold");
    }

    // A7: Parse comparison ">=today"
    {
        auto node = QueryNode::makeFilter("dm", ">=today");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::GE, "A7: >=today → GE op");

        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t todayStart = mktime(&t);
        check(node->numVal1 == static_cast<uint64_t>(todayStart), "A7: >=today threshold");
    }

    // A8: Parse comparison "<2024-06"
    {
        auto node = QueryNode::makeFilter("dm", "<2024-06");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::LT, "A8: <date → LT op");

        struct tm t = {};
        t.tm_year = 2024 - 1900; t.tm_mon = 5; t.tm_mday = 1;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        t.tm_isdst = -1;
        time_t expected = mktime(&t);
        check(node->numVal1 == static_cast<uint64_t>(expected), "A8: <2024-06 threshold");
    }

    // A9: Parse range "2024-01..2024-06"
    {
        auto node = QueryNode::makeFilter("dm", "2024-01..2024-06");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A9: range → RANGE op");

        struct tm t1 = {};
        t1.tm_year = 2024 - 1900; t1.tm_mon = 0; t1.tm_mday = 1;
        t1.tm_hour = 0; t1.tm_min = 0; t1.tm_sec = 0;
        t1.tm_isdst = -1;
        time_t start = mktime(&t1);

        struct tm t2 = {};
        t2.tm_year = 2024 - 1900; t2.tm_mon = 5; t2.tm_mday = 1;
        t2.tm_isdst = -1;
        // End of June = July 1 - 1 second
        t2.tm_mon = 6;
        t2.tm_mday = 1;
        t2.tm_hour = 0; t2.tm_min = 0; t2.tm_sec = 0;
        time_t end = mktime(&t2) - 1;

        check(node->numVal1 == static_cast<uint64_t>(start), "A9: range start");
        check(node->numVal2 == static_cast<uint64_t>(end), "A9: range end");
    }

    // A10: Parse relative "last7days"
    {
        auto node = QueryNode::makeFilter("dm", "last7days");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A10: last7days → RANGE");

        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_mday -= 7;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t start = mktime(&t);
        check(node->numVal1 == static_cast<uint64_t>(start), "A10: last7days start");
        // End should be approximately now
        int64_t diff = static_cast<int64_t>(node->numVal2) - static_cast<int64_t>(now);
        check(diff >= -2 && diff <= 2, "A10: last7days end ≈ now");
    }

    // A11: Parse relative "last3months"
    {
        auto node = QueryNode::makeFilter("dm", "last3months");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A11: last3months → RANGE");
        check(node->numVal1 > 0, "A11: last3months has valid start");
    }

    // A12: Aliases — "datemodified" same as "dm"
    {
        auto node = QueryNode::makeFilter("datemodified", "today");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A12: datemodified:today works");
        check(node->numVal1 > 0, "A12: datemodified has valid start");
    }

    // A13: "thisweek"
    {
        auto node = QueryNode::makeFilter("dm", "thisweek");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A13: thisweek → RANGE");

        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        // numVal1 should be Sunday start, numVal2 should be Saturday end
        check(node->numVal1 <= static_cast<uint64_t>(now), "A13: thisweek start <= now");
        check(node->numVal2 >= static_cast<uint64_t>(now), "A13: thisweek end >= now");
    }

    // A14: "thismonth"
    {
        auto node = QueryNode::makeFilter("dm", "thismonth");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A14: thismonth → RANGE");

        time_t now = ::time(nullptr);
        check(node->numVal1 <= static_cast<uint64_t>(now), "A14: thismonth start <= now");
        check(node->numVal2 >= static_cast<uint64_t>(now), "A14: thismonth end >= now");
    }

    // A15: "thisyear"
    {
        auto node = QueryNode::makeFilter("dm", "thisyear");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A15: thisyear → RANGE");

        time_t now = ::time(nullptr);
        check(node->numVal1 <= static_cast<uint64_t>(now), "A15: thisyear start <= now");
        check(node->numVal2 >= static_cast<uint64_t>(now), "A15: thisyear end >= now");
    }

    // A16: "lastweek"
    {
        auto node = QueryNode::makeFilter("dm", "lastweek");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A16: lastweek → RANGE");

        time_t now = ::time(nullptr);
        check(node->numVal2 < static_cast<uint64_t>(now), "A16: lastweek end < now");
    }

    // A17: "lastmonth"
    {
        auto node = QueryNode::makeFilter("dm", "lastmonth");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A17: lastmonth → RANGE");
        check(node->numVal1 > 0, "A17: lastmonth has valid start");
    }

    // A18: "lastyear"
    {
        auto node = QueryNode::makeFilter("dm", "lastyear");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::RANGE, "A18: lastyear → RANGE");
        check(node->numVal1 > 0, "A18: lastyear has valid start");

        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        // numVal2 should be Dec 31 of last year
        check(node->numVal2 < static_cast<uint64_t>(now), "A18: lastyear end < now");
    }

    // A19: "<=today"
    {
        auto node = QueryNode::makeFilter("dm", "<=today");
        QueryFilterParser::parse(*node);
        check(node->op == CompareOp::LE, "A19: <=today → LE op");
    }

    // ═══════════════════════════════════════════
    // Section B: Integrated search engine tests
    // ═══════════════════════════════════════════

    // Create a temp dir with files at different modification times
    auto tmpDir = std::filesystem::temp_directory_path() / "me_date_filter_tests";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir);

    auto writeFile = [](const std::filesystem::path& p, size_t sz) {
        std::ofstream f(p, std::ios::binary);
        std::string data(sz, 'x');
        f.write(data.data(), data.size());
    };

    // Create test files
    writeFile(tmpDir / "recent.cpp", 100);   // modtime = now (recent)
    writeFile(tmpDir / "older.cpp", 200);    // will be backdated

    // Backdate "older.cpp" to 2023-06-15
    {
        struct tm t = {};
        t.tm_year = 2023 - 1900; t.tm_mon = 5; t.tm_mday = 15;
        t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0;
        t.tm_isdst = -1;
        time_t old = mktime(&t);
        struct utimbuf ut;
        ut.actime = old;
        ut.modtime = old;
        utime((tmpDir / "older.cpp").c_str(), &ut);
    }

    // Also create a file backdated to yesterday
    writeFile(tmpDir / "yesterday_file.txt", 50);
    {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        t.tm_mday -= 1;
        t.tm_hour = 14; t.tm_min = 30; t.tm_sec = 0;
        time_t yday = mktime(&t);
        struct utimbuf ut;
        ut.actime = yday;
        ut.modtime = yday;
        utime((tmpDir / "yesterday_file.txt").c_str(), &ut);
    }

    // Scan into SearchEngine
    SearchEngine engine;
    {
        DirectoryScanner scanner;
        scanner.scan(tmpDir.string());
        auto records = scanner.takeResults();
        engine.loadRecords(std::move(records));
    }

    // Helper: query and collect result names
    auto queryNames = [&](const std::string& q) -> std::vector<std::string> {
        QueryTimingInfo timing;
        auto indices = engine.query(q, 100, false, timing);
        std::vector<std::string> names;
        engine.forEachRecordWithPath(indices, [&](uint32_t, const FileRecord& r, const std::string&) {
            names.push_back(r.name);
        });
        std::sort(names.begin(), names.end());
        return names;
    };

    auto containsName = [](const std::vector<std::string>& v, const std::string& n) {
        return std::find(v.begin(), v.end(), n) != v.end();
    };

    // B1: dm:today should match recent.cpp (created today) but not older.cpp
    {
        auto results = queryNames("dm:today");
        check(containsName(results, "recent.cpp"), "B1: dm:today matches recent.cpp");
        check(!containsName(results, "older.cpp"), "B1: dm:today excludes older.cpp");
    }

    // B2: dm:2023 should match older.cpp but not recent.cpp
    {
        auto results = queryNames("dm:2023");
        check(containsName(results, "older.cpp"), "B2: dm:2023 matches older.cpp");
        check(!containsName(results, "recent.cpp"), "B2: dm:2023 excludes recent.cpp");
    }

    // B3: dm:2023-06 should match older.cpp
    {
        auto results = queryNames("dm:2023-06");
        check(containsName(results, "older.cpp"), "B3: dm:2023-06 matches older.cpp");
    }

    // B4: dm:2023-06-15 should match older.cpp
    {
        auto results = queryNames("dm:2023-06-15");
        check(containsName(results, "older.cpp"), "B4: dm:2023-06-15 matches older.cpp");
    }

    // B5: dm:yesterday should match yesterday_file.txt
    {
        auto results = queryNames("dm:yesterday");
        check(containsName(results, "yesterday_file.txt"), "B5: dm:yesterday matches yesterday_file.txt");
        check(!containsName(results, "older.cpp"), "B5: dm:yesterday excludes older.cpp");
    }

    // B6: dm:>2024-01-01 — should match recent files
    {
        auto results = queryNames("dm:>2024-01-01");
        check(containsName(results, "recent.cpp"), "B6: dm:>2024-01-01 matches recent.cpp");
        check(!containsName(results, "older.cpp"), "B6: dm:>2024-01-01 excludes older.cpp (2023)");
    }

    // B7: dm:<2024-01-01 — should match older files
    {
        auto results = queryNames("dm:<2024-01-01");
        check(containsName(results, "older.cpp"), "B7: dm:<2024-01-01 matches older.cpp");
        check(!containsName(results, "recent.cpp"), "B7: dm:<2024-01-01 excludes recent.cpp");
    }

    // B8: Combine date with ext — dm:today ext:cpp
    {
        auto results = queryNames("dm:today ext:cpp");
        check(containsName(results, "recent.cpp"), "B8: dm:today ext:cpp matches recent.cpp");
        check(!containsName(results, "yesterday_file.txt"), "B8: ext:cpp excludes .txt files");
    }

    // B9: Combine date with text — older dm:2023
    {
        auto results = queryNames("older dm:2023");
        check(containsName(results, "older.cpp"), "B9: text+date combo works");
    }

    // B10: NOT date — !dm:today should exclude today's files
    {
        auto results = queryNames("!dm:today");
        check(!containsName(results, "recent.cpp"), "B10: !dm:today excludes recent.cpp");
        check(containsName(results, "older.cpp"), "B10: !dm:today includes older.cpp");
    }

    // B11: OR with date — dm:today | dm:2023
    {
        auto results = queryNames("dm:today | dm:2023");
        check(containsName(results, "recent.cpp"), "B11: dm:today|dm:2023 matches recent");
        check(containsName(results, "older.cpp"), "B11: dm:today|dm:2023 matches older");
    }

    // B12: datemodified: alias works same as dm:
    {
        auto results = queryNames("datemodified:today");
        check(containsName(results, "recent.cpp"), "B12: datemodified:today works");
    }

    // B13: dm:last7days — should match recent and yesterday but not 2023 file
    {
        auto results = queryNames("dm:last7days");
        check(containsName(results, "recent.cpp"), "B13: dm:last7days matches recent.cpp");
        check(containsName(results, "yesterday_file.txt"), "B13: dm:last7days matches yesterday");
        check(!containsName(results, "older.cpp"), "B13: dm:last7days excludes 2023 file");
    }

    // B14: dm range — dm:2023-01..2023-12
    {
        auto results = queryNames("dm:2023-01..2023-12");
        check(containsName(results, "older.cpp"), "B14: dm:range matches 2023 file");
        check(!containsName(results, "recent.cpp"), "B14: dm:range excludes current file");
    }

    // B15: dc: (date created) — falls back to modTime, so should behave like dm:
    {
        auto results = queryNames("dc:today");
        check(containsName(results, "recent.cpp"), "B15: dc:today works (modTime fallback)");
    }

    // B16: Everything-style ranges may omit either endpoint.
    {
        auto before = QueryParser::parse("dm:..2023-12-31");
        check(before && before->filterValid && before->numVal1 == 0,
              "B16: open-start date range begins at epoch");
        auto beforeResults = queryNames("dm:..2023-12-31");
        check(containsName(beforeResults, "older.cpp"),
              "B16: open-start range matches an older file");
        check(!containsName(beforeResults, "recent.cpp"),
              "B16: open-start range excludes a recent file");

        auto after = QueryParser::parse("dm:2024-01-01..");
        check(after && after->filterValid && after->numVal2 == UINT64_MAX,
              "B16: open-end date range has an unbounded upper endpoint");
        auto afterResults = queryNames("dm:2024-01-01..");
        check(containsName(afterResults, "recent.cpp"),
              "B16: open-end range matches a recent file");
        check(!containsName(afterResults, "older.cpp"),
              "B16: open-end range excludes an older file");
    }

    // Cleanup
    {
        auto invalidMonth = QueryParser::parse("dm:2024-13-01");
        auto invalidDay = QueryParser::parse("dm:2024-02-30");
        check(invalidMonth && invalidMonth->numVal1 == 0 && invalidMonth->numVal2 == 0,
              "B17: invalid month is rejected");
        check(invalidDay && invalidDay->numVal1 == 0 && invalidDay->numVal2 == 0,
              "B17: invalid day is rejected");
        check(invalidMonth && !invalidMonth->filterValid,
              "B17: invalid month marks the filter invalid");
        check(invalidDay && !invalidDay->filterValid,
              "B17: invalid day marks the filter invalid");

        auto invalidComparison = QueryParser::parse("dm:>2024-01-01T12:00");
        check(invalidComparison && !invalidComparison->filterValid,
              "B17: invalid comparison marks the filter invalid");
        auto results = queryNames("dm:>2024-01-01T12:00");
        check(results.empty(), "B17: invalid comparison cannot broaden results");
    }

    std::filesystem::remove_all(tmpDir);

    std::cout << "  Part 57 result: " << localPassed << " passed, " << localFailed << " failed\n";
}

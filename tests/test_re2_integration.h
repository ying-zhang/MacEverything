#pragma once
// Part 75: RE2 integration tests
// Validates that RE2 regex engine works correctly for search queries.

#include <re2/re2.h>
#include <string>
#include <memory>

extern int passed, failed;

static void runRE2IntegrationTests() {
    std::cout << "\n── Part 75: RE2 Integration Tests ──\n\n";

    // --- Test 1: RE2 compilation of valid pattern ---
    {
        re2::RE2 re("a.*b");
        bool ok = re.ok();
        std::cout << "  RE2 valid pattern compiles: " << (ok ? "PASS" : "FAIL") << "\n";
        ok ? ++passed : ++failed;
    }

    // --- Test 2: RE2 compilation of invalid pattern ---
    {
        re2::RE2::Options opts;
        opts.set_log_errors(false);
        re2::RE2 re("a[unclosed", opts);
        bool ok = !re.ok();
        std::cout << "  RE2 invalid pattern rejected: " << (ok ? "PASS" : "FAIL") << "\n";
        ok ? ++passed : ++failed;
    }

    // --- Test 3: PartialMatch basic ---
    {
        re2::RE2 re("hello");
        bool match = RE2::PartialMatch("say hello world", re);
        std::cout << "  RE2 PartialMatch basic: " << (match ? "PASS" : "FAIL") << "\n";
        match ? ++passed : ++failed;
    }

    // --- Test 4: PartialMatch no match ---
    {
        re2::RE2 re("xyz");
        bool match = RE2::PartialMatch("hello world", re);
        bool ok = !match;
        std::cout << "  RE2 PartialMatch no match: " << (ok ? "PASS" : "FAIL") << "\n";
        ok ? ++passed : ++failed;
    }

    // --- Test 5: Case-insensitive matching ---
    {
        re2::RE2::Options opts;
        opts.set_case_sensitive(false);
        re2::RE2 re("hello", opts);
        bool match = RE2::PartialMatch("HELLO WORLD", re);
        std::cout << "  RE2 case-insensitive match: " << (match ? "PASS" : "FAIL") << "\n";
        match ? ++passed : ++failed;
    }

    // --- Test 6: Case-sensitive matching ---
    {
        re2::RE2::Options opts;
        opts.set_case_sensitive(true);
        re2::RE2 re("hello", opts);
        bool match = RE2::PartialMatch("HELLO WORLD", re);
        bool ok = !match;
        std::cout << "  RE2 case-sensitive no match: " << (ok ? "PASS" : "FAIL") << "\n";
        ok ? ++passed : ++failed;
    }

    // --- Test 7: Complex pattern a.*b.*c (the 17s regression case) ---
    {
        re2::RE2 re("a.*b.*c");
        bool m1 = RE2::PartialMatch("axbxc", re);
        bool m2 = RE2::PartialMatch("abc", re);
        bool m3 = !RE2::PartialMatch("xyz", re);
        bool m4 = RE2::PartialMatch("a_long_string_b_another_c", re);
        bool ok = m1 && m2 && m3 && m4;
        std::cout << "  RE2 complex a.*b.*c pattern: " << (ok ? "PASS" : "FAIL") << "\n";
        ok ? ++passed : ++failed;
    }

    // --- Test 8: Character class [^abc]de ---
    {
        re2::RE2 re("[^abc]de");
        bool m1 = RE2::PartialMatch("xde", re);
        bool m2 = !RE2::PartialMatch("ade", re);
        bool m3 = RE2::PartialMatch("1de", re);
        bool ok = m1 && m2 && m3;
        std::cout << "  RE2 character class [^abc]de: " << (ok ? "PASS" : "FAIL") << "\n";
        ok ? ++passed : ++failed;
    }

    // --- Test 9: StringPiece zero-copy matching ---
    {
        re2::RE2 re("test");
        const char* data = "this is a test string";
        re2::StringPiece sp(data, 21);
        bool match = RE2::PartialMatch(sp, re);
        std::cout << "  RE2 StringPiece zero-copy: " << (match ? "PASS" : "FAIL") << "\n";
        match ? ++passed : ++failed;
    }

    // --- Test 10: Empty string ---
    {
        re2::RE2 re(".*");
        bool match = RE2::PartialMatch("", re);
        std::cout << "  RE2 empty string match: " << (match ? "PASS" : "FAIL") << "\n";
        match ? ++passed : ++failed;
    }

    // --- Test 11: Special chars in pattern (escaped) ---
    {
        re2::RE2 re("file\\.txt");
        bool m1 = RE2::PartialMatch("file.txt", re);
        bool m2 = !RE2::PartialMatch("filextxt", re);
        bool ok = m1 && m2;
        std::cout << "  RE2 escaped special chars: " << (ok ? "PASS" : "FAIL") << "\n";
        ok ? ++passed : ++failed;
    }

    // --- Test 12: unique_ptr ownership (mimics RegexCache usage) ---
    {
        re2::RE2::Options opts;
        opts.set_case_sensitive(false);
        opts.set_log_errors(false);
        auto re = std::make_unique<re2::RE2>("test.*pattern", opts);
        bool compiled = re->ok();
        bool match = RE2::PartialMatch("test_foo_pattern", *re);
        bool ok = compiled && match;
        std::cout << "  RE2 unique_ptr ownership: " << (ok ? "PASS" : "FAIL") << "\n";
        ok ? ++passed : ++failed;
    }

    // --- Test 13: Performance sanity -- a.*b.*c on long string should be fast ---
    {
        // Build a 10KB string with no 'c' to force full scan
        std::string longStr(10000, 'a');
        longStr += "b";
        longStr += std::string(10000, 'a');
        // Should NOT match (no 'c' after 'b')
        re2::RE2 re("a.*b.*c");
        auto t0 = std::chrono::steady_clock::now();
        bool match = RE2::PartialMatch(longStr, re);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        bool ok = !match && ms < 100.0; // must not match and must be fast
        std::cout << "  RE2 perf sanity (20KB, a.*b.*c): " << ms << "ms "
                  << (ok ? "PASS" : "FAIL") << "\n";
        ok ? ++passed : ++failed;
    }

    std::cout << "\n";
}

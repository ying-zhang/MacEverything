#pragma once
#include "test_helpers.h"
#include <string>
#include <vector>
#include <cstring>
#include <random>

inline void runSIMDSearchTests() {
    std::cout << "\n── Part 51: SIMDSearch Tests ──\n\n";

    // 51a: simdFind — basic match
    {
        std::string hay = "hello world";
        CHECK(me::simdFind(hay.data(), hay.size(), "world", 5) == 6);
        CHECK(me::simdFind(hay.data(), hay.size(), "hello", 5) == 0);
        CHECK(me::simdFind(hay.data(), hay.size(), "xyz", 3) == hay.size());
        std::cout << "  51a: simdFind basic              PASS\n";
    }

    // 51b: simdFind — empty needle returns 0
    {
        std::string hay = "abc";
        CHECK(me::simdFind(hay.data(), hay.size(), "", 0) == 0);
        std::cout << "  51b: simdFind empty needle       PASS\n";
    }

    // 51c: simdFind — needle longer than haystack
    {
        std::string hay = "ab";
        CHECK(me::simdFind(hay.data(), hay.size(), "abcdef", 6) == hay.size());
        std::cout << "  51c: simdFind needle > haystack  PASS\n";
    }

    // 51d: simdFind — single char needle
    {
        std::string hay = "abcdef";
        CHECK(me::simdFind(hay.data(), hay.size(), "d", 1) == 3);
        CHECK(me::simdFind(hay.data(), hay.size(), "z", 1) == hay.size());
        std::cout << "  51d: simdFind single char        PASS\n";
    }

    // 51e: simdFind — match at end
    {
        std::string hay = "the quick brown fox";
        CHECK(me::simdFind(hay.data(), hay.size(), "fox", 3) == 16);
        std::cout << "  51e: simdFind match at end       PASS\n";
    }

    // 51f: simdFind — long haystack crossing NEON boundaries (>32 bytes)
    {
        // Build a 200-byte haystack with needle near the end
        std::string hay(200, 'a');
        hay[180] = 'X';
        hay[181] = 'Y';
        hay[182] = 'Z';
        CHECK(me::simdFind(hay.data(), hay.size(), "XYZ", 3) == 180);
        // Needle at very start
        hay[0] = 'P'; hay[1] = 'Q';
        CHECK(me::simdFind(hay.data(), hay.size(), "PQ", 2) == 0);
        std::cout << "  51f: simdFind cross NEON boundary PASS\n";
    }

    // 51g: simdContains
    {
        std::string hay = "hello world";
        CHECK(me::simdContains(hay.data(), hay.size(), "world", 5));
        CHECK(!me::simdContains(hay.data(), hay.size(), "xyz", 3));
        // Empty needle: simdFind returns 0 (found at position 0), which is < hayLen, so contains=true
        CHECK(me::simdContains(hay.data(), hay.size(), "", 0) == true);
        std::cout << "  51g: simdContains                PASS\n";
    }

    // 51h: simdFindAll — multiple occurrences
    {
        std::string hay = "abcabcabc";
        std::vector<size_t> results;
        me::simdFindAll(hay.data(), hay.size(), "abc", 3, results);
        CHECK(results.size() == 3);
        CHECK(results[0] == 0);
        CHECK(results[1] == 3);
        CHECK(results[2] == 6);
        std::cout << "  51h: simdFindAll multiple        PASS\n";
    }

    // 51i: simdFindAll — no match
    {
        std::string hay = "hello world";
        std::vector<size_t> results;
        me::simdFindAll(hay.data(), hay.size(), "xyz", 3, results);
        CHECK(results.empty());
        std::cout << "  51i: simdFindAll no match        PASS\n";
    }

    // 51j: simdFindAll — empty needle returns nothing
    {
        std::string hay = "abc";
        std::vector<size_t> results;
        me::simdFindAll(hay.data(), hay.size(), "", 0, results);
        CHECK(results.empty());
        std::cout << "  51j: simdFindAll empty needle    PASS\n";
    }

    // 51k: simdFindAll — overlapping matches (single char)
    {
        std::string hay = "aaaa";
        std::vector<size_t> results;
        me::simdFindAll(hay.data(), hay.size(), "a", 1, results);
        CHECK(results.size() == 4);
        std::cout << "  51k: simdFindAll single char     PASS\n";
    }

    // 51l: simdFindAll — long buffer >64 bytes
    {
        std::string hay(100, '.');
        hay[10] = 'X'; hay[11] = 'Y';
        hay[50] = 'X'; hay[51] = 'Y';
        hay[90] = 'X'; hay[91] = 'Y';
        std::vector<size_t> results;
        me::simdFindAll(hay.data(), hay.size(), "XY", 2, results);
        CHECK(results.size() == 3);
        CHECK(results[0] == 10);
        CHECK(results[1] == 50);
        CHECK(results[2] == 90);
        std::cout << "  51l: simdFindAll long buffer     PASS\n";
    }

    // 51m: simdToLowerAscii — basic
    {
        std::string s = "Hello World ABC xyz 123";
        me::simdToLowerAscii(s.data(), s.size());
        CHECK(s == "hello world abc xyz 123");
        std::cout << "  51m: simdToLowerAscii basic      PASS\n";
    }

    // 51n: simdToLowerAscii — all uppercase
    {
        std::string s = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        me::simdToLowerAscii(s.data(), s.size());
        CHECK(s == "abcdefghijklmnopqrstuvwxyz");
        std::cout << "  51n: simdToLowerAscii all upper  PASS\n";
    }

    // 51o: simdToLowerAscii — already lowercase
    {
        std::string s = "already lowercase 123!@#";
        std::string expected = s;
        me::simdToLowerAscii(s.data(), s.size());
        CHECK(s == expected);
        std::cout << "  51o: simdToLowerAscii no-op      PASS\n";
    }

    // 51p: simdToLowerAscii — empty string
    {
        std::string s;
        me::simdToLowerAscii(s.data(), s.size()); // should not crash
        CHECK(s.empty());
        std::cout << "  51p: simdToLowerAscii empty      PASS\n";
    }

    // 51q: simdToLowerAscii — long string crossing NEON boundary
    {
        std::string s(100, 'A');
        me::simdToLowerAscii(s.data(), s.size());
        CHECK(s == std::string(100, 'a'));
        std::cout << "  51q: simdToLowerAscii long       PASS\n";
    }

    // 51r: simdToLowerAscii — preserves non-alpha bytes
    {
        std::string s = "FILE.TXT";
        me::simdToLowerAscii(s.data(), s.size());
        CHECK(s == "file.txt");
        std::cout << "  51r: simdToLowerAscii non-alpha  PASS\n";
    }

    // 51s: Consistency with strstr for random inputs
    {
        std::mt19937 rng(42);
        bool allMatch = true;
        for (int trial = 0; trial < 200; trial++) {
            size_t hayLen = rng() % 300 + 2;
            size_t ndlLen = rng() % 5 + 2;
            std::string hay(hayLen, '\0');
            std::string ndl(ndlLen, '\0');
            for (auto& c : hay) c = 'a' + (rng() % 4);
            for (auto& c : ndl) c = 'a' + (rng() % 4);

            size_t simdPos = me::simdFind(hay.data(), hay.size(), ndl.data(), ndl.size());
            const char* stdPos = strstr(hay.c_str(), ndl.c_str());
            size_t expected = stdPos ? static_cast<size_t>(stdPos - hay.c_str()) : hay.size();
            if (simdPos != expected) { allMatch = false; break; }
        }
        CHECK(allMatch);
        std::cout << "  51s: Random consistency check    PASS\n";
    }
}

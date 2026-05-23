#pragma once
#include "test_helpers.h"
#include <string>
#include <vector>
#include <cstring>

inline void runStringPoolTests() {
    std::cout << "\n── Part 50: StringPool Tests ──\n\n";

    // 50a: Basic append and retrieval
    {
        StringPool pool;
        uint32_t i0 = pool.append("hello");
        uint32_t i1 = pool.append("world");
        CHECK(i0 == 0);
        CHECK(i1 == 1);
        CHECK(pool.length(i0) == 5);
        CHECK(pool.length(i1) == 5);
        CHECK(std::memcmp(pool.data(i0), "hello", 5) == 0);
        CHECK(std::memcmp(pool.data(i1), "world", 5) == 0);
        CHECK(pool.str(i0) == "hello");
        CHECK(pool.str(i1) == "world");
        CHECK(pool.view(i0) == "hello");
        CHECK(pool.entryCount() == 2);
        CHECK(pool.rawSize() == 10);
        std::cout << "  50a: Basic append/retrieval      PASS\n";
    }

    // 50b: Tombstone
    {
        StringPool pool;
        pool.append("aaa");
        pool.append("bbb");
        pool.append("ccc");
        CHECK(pool.isLive(1));
        pool.tombstone(1);
        CHECK(!pool.isLive(1));
        CHECK(pool.length(1) == 0);
        // Other entries unaffected
        CHECK(pool.isLive(0));
        CHECK(pool.isLive(2));
        CHECK(pool.str(0) == "aaa");
        CHECK(pool.str(2) == "ccc");
        std::cout << "  50b: Tombstone                   PASS\n";
    }

    // 50c: Compact
    {
        StringPool pool;
        pool.append("alpha");
        pool.append("beta");
        pool.append("gamma");
        pool.append("delta");
        pool.tombstone(1); // remove "beta"
        pool.tombstone(3); // remove "delta"

        std::vector<bool> liveMask = {true, false, true, false};
        auto result = pool.compact(liveMask);
        CHECK(result.compacted.entryCount() == 2);
        CHECK(result.compacted.str(0) == "alpha");
        CHECK(result.compacted.str(1) == "gamma");
        CHECK(result.remap.size() == 4);
        CHECK(result.remap[0] == 0);
        CHECK(result.remap[1] == UINT32_MAX);
        CHECK(result.remap[2] == 1);
        CHECK(result.remap[3] == UINT32_MAX);
        CHECK(result.compacted.rawSize() == 10); // "alpha" + "gamma"
        std::cout << "  50c: Compact                     PASS\n";
    }

    // 50d: loadBulk
    {
        StringPool pool;
        std::vector<std::string> strings = {"foo", "bar", "baz", "qux"};
        pool.loadBulk(strings);
        CHECK(pool.entryCount() == 4);
        CHECK(pool.str(0) == "foo");
        CHECK(pool.str(1) == "bar");
        CHECK(pool.str(2) == "baz");
        CHECK(pool.str(3) == "qux");
        CHECK(pool.rawSize() == 12);
        std::cout << "  50d: loadBulk                    PASS\n";
    }

    // 50e: Empty string
    {
        StringPool pool;
        uint32_t i0 = pool.append("");
        CHECK(pool.length(i0) == 0);
        // Note: empty string has length 0 same as tombstone, but isLive checks both
        CHECK(pool.str(i0) == "");
        std::cout << "  50e: Empty string                PASS\n";
    }

    // 50f: Reserve and large batch
    {
        StringPool pool;
        pool.reserve(10000, 1000);
        for (int i = 0; i < 1000; i++) {
            pool.append(std::to_string(i));
        }
        CHECK(pool.entryCount() == 1000);
        CHECK(pool.str(0) == "0");
        CHECK(pool.str(999) == "999");
        std::cout << "  50f: Reserve + large batch       PASS\n";
    }

    // 50g: Contiguous buffer property
    {
        StringPool pool;
        pool.append("AAAA");
        pool.append("BBBB");
        pool.append("CCCC");
        // All data should be contiguous in rawBuffer
        const char* buf = pool.rawBuffer();
        CHECK(std::memcmp(buf, "AAAABBBBCCCC", 12) == 0);
        CHECK(pool.rawSize() == 12);
        std::cout << "  50g: Contiguous buffer           PASS\n";
    }

    // 50h: Clear
    {
        StringPool pool;
        pool.append("test");
        pool.clear();
        CHECK(pool.entryCount() == 0);
        CHECK(pool.rawSize() == 0);
        std::cout << "  50h: Clear                       PASS\n";
    }

    // 50i: Copy semantics
    {
        StringPool pool;
        pool.append("original");
        StringPool copy = pool;
        CHECK(copy.str(0) == "original");
        copy.append("added");
        CHECK(copy.entryCount() == 2);
        CHECK(pool.entryCount() == 1); // original unchanged
        std::cout << "  50i: Copy semantics              PASS\n";
    }

    // 50j: Move semantics
    {
        StringPool pool;
        pool.append("moveme");
        StringPool moved = std::move(pool);
        CHECK(moved.str(0) == "moveme");
        CHECK(moved.entryCount() == 1);
        std::cout << "  50j: Move semantics              PASS\n";
    }
}

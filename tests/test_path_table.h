#pragma once
// ═══════════════════════════════════════════════════════
//  Part 20: PathTable Unit Tests
// ═══════════════════════════════════════════════════════

static void runPathTableTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 20: PathTable Unit Tests\n";
    std::cout << "========================================\n\n";

    // -- Test 1: Basic intern and resolve --
    std::cout << "  --- Basic intern/resolve ---\n";
    {
        PathTable table;
        uint32_t idx0 = table.intern("/usr/local/bin");
        uint32_t idx1 = table.intern("/home/user");
        uint32_t idx2 = table.intern("/tmp");

        check(idx0 == 0, "PathTable: first intern returns 0");
        check(idx1 == 1, "PathTable: second intern returns 1");
        check(idx2 == 2, "PathTable: third intern returns 2");

        check(table.resolve(idx0) == "/usr/local/bin", "PathTable: resolve(0) correct");
        check(table.resolve(idx1) == "/home/user", "PathTable: resolve(1) correct");
        check(table.resolve(idx2) == "/tmp", "PathTable: resolve(2) correct");
        check(table.size() == 3, "PathTable: size == 3");
    }

    // -- Test 2: Deduplication --
    std::cout << "\n  --- Deduplication ---\n";
    {
        PathTable table;
        uint32_t idx0 = table.intern("/shared/path");
        uint32_t idx1 = table.intern("/other/path");
        uint32_t idx2 = table.intern("/shared/path");  // duplicate
        uint32_t idx3 = table.intern("/shared/path");  // duplicate again

        check(idx0 == idx2, "PathTable: duplicate path returns same index");
        check(idx0 == idx3, "PathTable: triple duplicate returns same index");
        check(idx0 != idx1, "PathTable: different paths get different indices");
        check(table.size() == 2, "PathTable: size == 2 (dedup'd)");
    }

    // -- Test 3: Empty path --
    std::cout << "\n  --- Empty path ---\n";
    {
        PathTable table;
        uint32_t idx = table.intern("");
        check(table.resolve(idx).empty(), "PathTable: empty path interned and resolved");
        uint32_t idx2 = table.intern("");
        check(idx == idx2, "PathTable: duplicate empty path dedup'd");
        check(table.size() == 1, "PathTable: size == 1 for single empty path");
    }

    // -- Test 4: Clear --
    std::cout << "\n  --- Clear ---\n";
    {
        PathTable table;
        table.intern("/a");
        table.intern("/b");
        check(table.size() == 2, "PathTable: size before clear");
        table.clear();
        check(table.size() == 0, "PathTable: size == 0 after clear");

        // Re-intern should start from 0
        uint32_t idx = table.intern("/c");
        check(idx == 0, "PathTable: re-intern after clear starts from 0");
        check(table.resolve(idx) == "/c", "PathTable: resolve after clear correct");
    }

    // -- Test 5: Many unique paths --
    std::cout << "\n  --- Many unique paths ---\n";
    if (gSkipPerformanceTests) {
        std::cout << "    [SKIP] Large PathTable workload skipped in --fast mode\n";
    } else {
        PathTable table;
        const uint32_t N = 10000;
        for (uint32_t i = 0; i < N; i++) {
            uint32_t idx = table.intern("/dir/" + std::to_string(i));
            check(idx == i, ("PathTable: sequential index for path " + std::to_string(i)).c_str());
            if (idx != i) break;  // stop on first failure to avoid spam
        }
        check(table.size() == N, "PathTable: 10000 unique paths stored");

        // Verify all resolve correctly
        bool allCorrect = true;
        for (uint32_t i = 0; i < N; i++) {
            if (table.resolve(i) != "/dir/" + std::to_string(i)) {
                allCorrect = false;
                break;
            }
        }
        check(allCorrect, "PathTable: all 10000 paths resolve correctly");
    }

    // -- Test 6: High dedup ratio (simulating real workload) --
    std::cout << "\n  --- High dedup ratio ---\n";
    {
        PathTable table;
        // 1000 files across 10 directories
        for (int dir = 0; dir < 10; dir++) {
            std::string path = "/volume/projects/project_" + std::to_string(dir) + "/src";
            for (int file = 0; file < 100; file++) {
                table.intern(path);
            }
        }
        check(table.size() == 10, "PathTable: 1000 interns -> 10 unique paths (100:1 dedup)");
    }

    // -- Test 7: Copy semantics --
    std::cout << "\n  --- Copy semantics ---\n";
    {
        PathTable original;
        original.intern("/path/a");
        original.intern("/path/b");

        PathTable copy = original;
        check(copy.size() == 2, "PathTable copy: size preserved");
        check(copy.resolve(0) == "/path/a", "PathTable copy: resolve(0) correct");
        check(copy.resolve(1) == "/path/b", "PathTable copy: resolve(1) correct");

        // Mutating copy doesn't affect original
        copy.intern("/path/c");
        check(copy.size() == 3, "PathTable copy: new intern in copy");
        check(original.size() == 2, "PathTable copy: original unchanged");
    }

    std::cout << "\n";
}

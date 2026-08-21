#pragma once
// Part 46: Large-Scale Query Performance Benchmark (10M records)
//
// Generates a deterministic 10M-record dataset that mimics real macOS filesystem
// distribution, then runs a comprehensive query matrix covering every code path:
//   - Trigram path (good/medium/poor selectivity)
//   - Layer 1 bypass (trigram candidates > 1% of total)
//   - Linear scan (short keywords, glob patterns)
//   - Phase 2 path-only matching
//   - maxResults early exit (limit=10, 100, 1000, unlimited)
//   - No-match (trigram early exit)
//   - Lock contention simulation (concurrent writes during queries)
//
// Run:  ./test_all --part 46

#include <random>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <thread>

static void runLargeScalePerfBenchmarks() {
    std::cout << "========================================\n";
    std::cout << "  Part 46: 10M-Record Query Performance\n";
    std::cout << "========================================\n\n";

    // ═══════════════════════════════════════════
    //  Phase 1: Build 10M-record dataset
    // ═══════════════════════════════════════════
    const uint32_t N = 10'000'000;
    std::cout << "  Building " << N << " record dataset...\n";

    double memBefore = getMemoryUsageMB();
    auto buildStart = std::chrono::steady_clock::now();

    std::vector<FileRecord> records;
    records.reserve(N);

    std::mt19937 rng(12345); // deterministic seed, different from Part 44

    // Realistic macOS directory pool (30 dirs for variety)
    const std::string dirs[] = {
        "/Users/dev/Documents/projects/frontend/src/components",
        "/Users/dev/Documents/projects/backend/src/api",
        "/Users/dev/Documents/projects/backend/src/models",
        "/Users/dev/Library/Caches/com.apple.dt.Xcode",
        "/Users/dev/Library/Developer/Xcode/DerivedData",
        "/Applications/Xcode.app/Contents/Frameworks",
        "/Applications/Safari.app/Contents/Resources",
        "/System/Library/Frameworks/CoreFoundation.framework",
        "/System/Library/PrivateFrameworks",
        "/usr/local/lib/node_modules",
        "/usr/local/Cellar/python/3.12/lib",
        "/opt/homebrew/lib/pkgconfig",
        "/opt/homebrew/include/boost",
        "/tmp/build/output/Release",
        "/var/folders/xy/abc123/T/com.apple.finder",
        "/Library/Developer/CommandLineTools/SDKs",
        "/Library/Preferences",
        "/private/var/db/receipts",
        "/Users/dev/Desktop/work/reports",
        "/Users/dev/Downloads",
        "/Users/dev/.config/nvim",
        "/Users/dev/.npm/_cacache/content-v2",
        "/Users/dev/go/pkg/mod/github.com",
        "/Users/dev/data/project/mac_everything/MacEverything/Core",
        "/Users/dev/data/project/mac_everything/tests",
        "/Volumes/ExternalDrive/Backups",
        "/Volumes/ExternalDrive/Photos",
        "/Users/dev/Music/Logic Pro/Projects",
        "/Users/dev/Movies/Final Cut Pro",
        "/Users/dev/Pictures/Screenshots",
    };
    const int NUM_DIRS = 30;

    const std::string extensions[] = {
        ".txt", ".cpp", ".h", ".py", ".js", ".ts", ".json", ".md",
        ".log", ".xml", ".csv", ".plist", ".swift", ".m", ".mm",
        ".dylib", ".so", ".o", ".a", ".png", ".jpg", ".pdf",
        ".yaml", ".toml", ".rs", ".go", ".java", ".rb", ".sh", ".zsh"
    };
    const int NUM_EXT = 30;

    const std::string prefixes[] = {
        "file", "data", "config", "module", "lib", "src", "doc",
        "test", "build", "cache", "index", "main", "util", "helper",
        "view", "model", "controller", "service", "handler", "manager"
    };
    const int NUM_PREFIX = 20;

    // Distribution (by bucket out of 1000):
    //   0-19   (2.0%): "test_NNN.*"       — high trigram candidate, poor selectivity
    //   20-39  (2.0%): "*_test_NNN.*"     — embedded "test", different trigram profile
    //   40-49  (1.0%): "SearchEngine*.*"  — realistic filename, medium selectivity
    //   50-99  (5.0%): "*.cpp"            — extension match
    //  100-109 (1.0%): "*.swift"          — extension match (less common)
    //  110-114 (0.5%): "unique_xyz_NNN.*" — excellent selectivity (rare)
    //  115-119 (0.5%): "EXACT_MATCH_NNN"  — for exact name matching tests
    //  120-129 (1.0%): "README.md"        — duplicate common names (path-only differentiation)
    //  130-139 (1.0%): "index.js"         — duplicate common names
    //  140-149 (1.0%): deep nested paths  — long path testing
    //  150-999 (85%):  generic names      — random prefix_NNN.ext

    for (uint32_t i = 0; i < N; i++) {
        std::string name;
        std::string dir;
        uint32_t bucket = i % 1000;

        if (bucket < 20) {
            // 2%: "test_NNN.ext" — triggers Layer 1 bypass at 10M scale
            name = "test_" + std::to_string(i) + extensions[rng() % NUM_EXT];
            dir = dirs[rng() % NUM_DIRS];
        } else if (bucket < 40) {
            // 2%: "prefix_test_NNN.txt" — "test" embedded
            name = prefixes[rng() % NUM_PREFIX] + "_test_" + std::to_string(i) + ".txt";
            dir = dirs[rng() % NUM_DIRS];
        } else if (bucket < 50) {
            // 1%: "SearchEngine_NNN.ext" — medium selectivity multi-word
            name = "SearchEngine_" + std::to_string(i) + extensions[rng() % 5]; // .txt/.cpp/.h/.py/.js
            dir = dirs[rng() % NUM_DIRS];
        } else if (bucket < 100) {
            // 5%: forced .cpp extension
            name = prefixes[rng() % NUM_PREFIX] + "_" + std::to_string(i) + ".cpp";
            dir = dirs[rng() % NUM_DIRS];
        } else if (bucket < 110) {
            // 1%: forced .swift extension
            name = prefixes[rng() % NUM_PREFIX] + "_" + std::to_string(i) + ".swift";
            dir = dirs[rng() % NUM_DIRS];
        } else if (bucket < 115) {
            // 0.5%: rare "unique_xyz_NNN.dat"
            name = "unique_xyz_" + std::to_string(i) + ".dat";
            dir = dirs[rng() % NUM_DIRS];
        } else if (bucket < 120) {
            // 0.5%: exact match candidates "EXACT_MATCH_NNN"
            name = "EXACT_MATCH_" + std::to_string(i);
            dir = dirs[rng() % NUM_DIRS];
        } else if (bucket < 130) {
            // 1%: "README.md" — same name, different paths (path-only differentiation)
            name = "README.md";
            dir = dirs[i % NUM_DIRS] + "/subdir_" + std::to_string(i / 1000);
        } else if (bucket < 140) {
            // 1%: "index.js" — same name, different paths
            name = "index.js";
            dir = dirs[i % NUM_DIRS] + "/pkg_" + std::to_string(i / 1000);
        } else if (bucket < 150) {
            // 1%: deep nested path (6-8 levels)
            name = prefixes[rng() % NUM_PREFIX] + "_" + std::to_string(i) + extensions[rng() % NUM_EXT];
            dir = dirs[rng() % NUM_DIRS] + "/level1/level2/level3/level4/deep_" + std::to_string(i % 100);
        } else {
            // 85%: generic "prefix_NNN.ext"
            name = prefixes[rng() % NUM_PREFIX] + "_" + std::to_string(i) + extensions[rng() % NUM_EXT];
            dir = dirs[rng() % NUM_DIRS];
        }

        records.push_back({
            name, dir,
            uint8_t(1 + (rng() % 2)),              // type: file or dir
            uint64_t(rng() % 10'000'000),           // size: 0-10MB
            time_t(1700000000 + rng() % 10'000'000) // modTime: ~4 months range
        });
    }

    SearchEngine engine;
    engine.loadRecords(std::move(records));

    auto buildEnd = std::chrono::steady_clock::now();
    double buildMs = std::chrono::duration<double, std::milli>(buildEnd - buildStart).count();
    double memAfter = getMemoryUsageMB();

    std::cout << "  Dataset built + loaded in " << std::fixed << std::setprecision(0)
              << buildMs << "ms\n";
    std::cout << "  Records: " << engine.recordCount() << " total, "
              << engine.liveRecordCount() << " live\n";
    std::cout << "  Memory: " << std::setprecision(1) << memBefore << "MB -> "
              << memAfter << "MB (delta " << (memAfter - memBefore) << "MB)\n\n";

    // ═══════════════════════════════════════════
    //  Benchmark infrastructure
    // ═══════════════════════════════════════════
    struct BenchResult {
        std::string label;
        std::string keyword;
        uint32_t maxResults;
        double avgMs;
        double minMs;
        double maxMs;
        double p50Ms;
        double p95Ms;
        uint32_t resultCount;
    };

    auto benchmark = [&](const std::string& label, const std::string& keyword,
                         uint32_t maxResults, int iterations) -> BenchResult {
        // Warm-up
        auto warmup = engine.query(keyword, maxResults);
        uint32_t resultCount = static_cast<uint32_t>(warmup.size());

        std::vector<double> times;
        times.reserve(iterations);
        for (int i = 0; i < iterations; i++) {
            auto t0 = std::chrono::steady_clock::now();
            auto res = engine.query(keyword, maxResults);
            auto t1 = std::chrono::steady_clock::now();
            times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }

        std::sort(times.begin(), times.end());
        double sum = 0;
        for (double t : times) sum += t;
        double avg = sum / iterations;
        double p50 = times[iterations / 2];
        double p95 = times[static_cast<int>(iterations * 0.95)];

        return {label, keyword, maxResults, avg, times.front(), times.back(), p50, p95, resultCount};
    };

    // Table printer
    auto printTableHeader = []() {
        std::cout << "    " << std::left << std::setw(36) << "Scenario"
                  << std::right << std::setw(7) << "Avg"
                  << std::setw(7) << "Min"
                  << std::setw(7) << "Max"
                  << std::setw(7) << "P50"
                  << std::setw(7) << "P95"
                  << std::setw(10) << "Results"
                  << std::setw(7) << "Limit"
                  << "\n";
        std::cout << "    " << std::string(88, '-') << "\n";
    };

    auto printRow = [](const BenchResult& r) {
        std::cout << "    " << std::left << std::setw(36) << r.label
                  << std::right << std::fixed << std::setprecision(1)
                  << std::setw(6) << r.avgMs << "ms"
                  << std::setprecision(1)
                  << std::setw(6) << r.minMs << "ms"
                  << std::setw(6) << r.maxMs << "ms"
                  << std::setw(6) << r.p50Ms << "ms"
                  << std::setw(6) << r.p95Ms << "ms"
                  << std::setw(10) << r.resultCount
                  << std::setw(7) << r.maxResults
                  << "\n";
    };

    const int RUNS = 10; // 10 iterations for stable results at this scale

    std::vector<BenchResult> allResults;
    auto run = [&](const std::string& label, const std::string& keyword,
                   uint32_t maxResults, int runs = RUNS) {
        auto r = benchmark(label, keyword, maxResults, runs);
        printRow(r);
        allResults.push_back(r);
        return r;
    };

    // ═══════════════════════════════════════════
    //  Scenario 1: Trigram path — varying selectivity
    // ═══════════════════════════════════════════
    std::cout << "  === Scenario 1: Trigram selectivity ===\n";
    printTableHeader();

    // "test" — 4% match rate, ~400K candidates → Layer 1 bypass triggers
    run("\"test\" (4%, Layer1 bypass)", "test", 100);
    run("\"test\" limit=10", "test", 10);
    run("\"test\" limit=1000", "test", 1000);
    run("\"test\" unlimited", "test", 0);

    // "SearchEngine" — 1%, ~100K candidates, long keyword = many trigrams
    run("\"SearchEngine\" (1%)", "SearchEngine", 100);
    run("\"SearchEngine\" limit=10", "SearchEngine", 10);

    // "unique_xyz" — 0.5%, ~50K candidates, excellent selectivity
    run("\"unique_xyz\" (0.5%)", "unique_xyz", 100);
    run("\"unique_xyz\" limit=10", "unique_xyz", 10);
    run("\"unique_xyz\" unlimited", "unique_xyz", 0);

    // Very specific: should have near-zero candidates
    run("\"unique_xyz_12345\" (exact)", "unique_xyz_12345", 100);

    std::cout << "\n";

    // ═══════════════════════════════════════════
    //  Scenario 2: Linear scan — short keywords
    // ═══════════════════════════════════════════
    std::cout << "  === Scenario 2: Short keywords (linear scan) ===\n";
    printTableHeader();

    run("\"te\" (2 chars, linear)", "te", 100);
    run("\"te\" limit=10", "te", 10);
    run("\"a\" (1 char, very common)", "a", 100);
    run("\"a\" limit=10", "a", 10);
    run("\"x\" (1 char, rare)", "x", 100);

    std::cout << "\n";

    // ═══════════════════════════════════════════
    //  Scenario 3: Glob patterns
    // ═══════════════════════════════════════════
    std::cout << "  === Scenario 3: Glob patterns ===\n";
    printTableHeader();

    run("\"*.cpp\" (5% match)", "*.cpp", 100);
    run("\"*.cpp\" limit=10", "*.cpp", 10);
    run("\"*.swift\" (1% match)", "*.swift", 100);
    run("\"test_*\" (prefix glob)", "test_*", 100);
    run("\"*.nonexistent\" (no match)", "*.nonexistent", 100);

    std::cout << "\n";

    // ═══════════════════════════════════════════
    //  Scenario 4: No match — trigram early exit
    // ═══════════════════════════════════════════
    std::cout << "  === Scenario 4: No match ===\n";
    printTableHeader();

    // Trigram miss: no posting list match → instant return
    run("\"qzqzqz_nothing\" (trigram miss)", "qzqzqz_nothing", 100);
    // Short keyword no match → full linear scan
    run("\"zz\" (2 chars, no match)", "zz", 100);
    // Glob no match → full linear scan
    run("\"*.zzz\" (glob, no match)", "*.zzz", 100);

    std::cout << "\n";

    // ═══════════════════════════════════════════
    //  Scenario 5: Path-only matching
    // ═══════════════════════════════════════════
    std::cout << "  === Scenario 5: Path-only matching ===\n";
    printTableHeader();

    // Keyword that matches path but not name
    run("\"homebrew\" (path-only)", "homebrew", 100);
    run("\"DerivedData\" (path-only)", "DerivedData", 100);
    run("\"level4\" (deep path)", "level4", 100);

    std::cout << "\n";

    // ═══════════════════════════════════════════
    //  Scenario 6: Duplicate name differentiation
    // ═══════════════════════════════════════════
    std::cout << "  === Scenario 6: Duplicate names ===\n";
    printTableHeader();

    // "README.md" — 1% of records share this name
    run("\"README.md\" (1% dupes)", "README.md", 100);
    run("\"README.md\" limit=10", "README.md", 10);
    run("\"index.js\" (1% dupes)", "index.js", 100);

    std::cout << "\n";

    // ═══════════════════════════════════════════
    //  Scenario 7: Exact match prioritization
    // ═══════════════════════════════════════════
    std::cout << "  === Scenario 7: Exact match & ranking ===\n";
    printTableHeader();

    run("\"EXACT_MATCH\" (0.5%)", "EXACT_MATCH", 100);
    run("\"EXACT_MATCH\" limit=10", "EXACT_MATCH", 10);

    std::cout << "\n";

    // ═══════════════════════════════════════════
    //  Scenario 8: maxResults scaling
    // ═══════════════════════════════════════════
    std::cout << "  === Scenario 8: maxResults scaling ===\n";
    printTableHeader();

    run("\"test\" limit=1", "test", 1);
    run("\"test\" limit=5", "test", 5);
    run("\"test\" limit=10", "test", 10);
    run("\"test\" limit=50", "test", 50);
    run("\"test\" limit=100", "test", 100);
    run("\"test\" limit=500", "test", 500);
    run("\"test\" limit=1000", "test", 1000);
    run("\"test\" limit=10000", "test", 10000);
    run("\"test\" unlimited", "test", 0);

    std::cout << "\n";

    // ═══════════════════════════════════════════
    //  Scenario 9: Concurrent write contention
    // ═══════════════════════════════════════════
    std::cout << "  === Scenario 9: Query under write contention ===\n";
    printTableHeader();
    {
        // Start a background thread that continuously adds records
        std::atomic<bool> stopWriter(false);
        std::atomic<uint64_t> writtenCount(0);
        std::thread writer([&]() {
            uint32_t seq = N;
            while (!stopWriter.load(std::memory_order_relaxed)) {
                engine.addRecord({"contention_" + std::to_string(seq++) + ".tmp",
                                  "/tmp/contention", 1, 100, 1700000000});
                writtenCount.fetch_add(1, std::memory_order_relaxed);
                // Brief pause to avoid overwhelming the engine
                if (seq % 50 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }
        });

        // Let writer warm up
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        run("\"test\" +writes limit=100", "test", 100);
        run("\"unique_xyz\" +writes lim=100", "unique_xyz", 100);
        run("\"*.cpp\" +writes limit=100", "*.cpp", 100);

        stopWriter.store(true, std::memory_order_relaxed);
        writer.join();

        std::cout << "    (background writer added " << writtenCount.load() << " records during test)\n";
    }

    std::cout << "\n";

    // ═══════════════════════════════════════════
    //  Summary: performance matrix
    // ═══════════════════════════════════════════
    std::cout << "  === Performance Summary ===\n\n";

    // Find key results for summary
    auto findResult = [&](const std::string& label) -> const BenchResult* {
        for (const auto& r : allResults) {
            if (r.label == label) return &r;
        }
        return nullptr;
    };

    auto testL100  = findResult("\"test\" (4%, Layer1 bypass)");
    auto testL10   = findResult("\"test\" limit=10");
    auto searchEng = findResult("\"SearchEngine\" (1%)");
    auto uniq100   = findResult("\"unique_xyz\" (0.5%)");
    auto te100     = findResult("\"te\" (2 chars, linear)");
    auto glob100   = findResult("\"*.cpp\" (5% match)");
    auto noMatch   = findResult("\"qzqzqz_nothing\" (trigram miss)");
    auto readme    = findResult("\"README.md\" (1% dupes)");

    std::cout << "    Query paths at 10M records:\n";
    if (testL100)  std::cout << "      Layer1 bypass (\"test\" L=100):     " << std::fixed << std::setprecision(1) << testL100->avgMs << "ms\n";
    if (testL10)   std::cout << "      Layer1 bypass (\"test\" L=10):      " << testL10->avgMs << "ms\n";
    if (searchEng) std::cout << "      Trigram medium (\"SearchEngine\"):  " << searchEng->avgMs << "ms\n";
    if (uniq100)   std::cout << "      Trigram good (\"unique_xyz\"):      " << uniq100->avgMs << "ms\n";
    if (te100)     std::cout << "      Linear scan (\"te\" L=100):         " << te100->avgMs << "ms\n";
    if (glob100)   std::cout << "      Glob scan (\"*.cpp\" L=100):        " << glob100->avgMs << "ms\n";
    if (noMatch)   std::cout << "      Trigram miss (no match):          " << noMatch->avgMs << "ms\n";
    if (readme)    std::cout << "      Dupe names (\"README.md\" L=100):   " << readme->avgMs << "ms\n";

    // Ratios
    if (testL100 && te100 && te100->avgMs > 0) {
        double ratio = testL100->avgMs / te100->avgMs;
        std::cout << "\n    Ratio (Layer1-bypass / linear-scan): " << std::setprecision(2) << ratio << "x\n";
    }
    if (testL100 && testL10 && testL10->avgMs > 0) {
        double ratio = testL100->avgMs / testL10->avgMs;
        std::cout << "    Ratio (limit=100 / limit=10):        " << std::setprecision(2) << ratio << "x\n";
    }

    // Correctness checks
    std::cout << "\n  === Correctness Checks ===\n";

    // Verify result counts are reasonable
    {
        auto res = engine.query("test", 0);
        // "test" matches filenames (~4%) AND paths containing "test" (e.g. /tests/),
        // so actual count is ~1M-1.3M
        check(res.size() > 800'000 && res.size() < 1'500'000,
              "\"test\" unlimited result count in expected range (800K-1.5M)");

        auto resLimited = engine.query("test", 100);
        check(resLimited.size() == 100, "\"test\" limit=100 returns exactly 100");

        auto resUnique = engine.query("unique_xyz", 0);
        // ~0.5% of 10M = ~50K
        check(resUnique.size() > 40'000 && resUnique.size() < 60'000,
              "\"unique_xyz\" unlimited result count in expected range (40K-60K)");

        auto resNo = engine.query("qzqzqz_nothing", 100);
        check(resNo.empty(), "No-match returns empty");

        auto resCpp = engine.query("*.cpp", 0);
        check(resCpp.size() > 400'000, "\"*.cpp\" matches >400K files");

        // Verify exact match ranking
        auto resExact = engine.query("README.md", 10);
        if (!resExact.empty()) {
            auto rec = engine.getRecord(resExact[0]);
            check(rec.name == "README.md", "Exact match 'README.md' ranked first");
        }

        auto resLimit1 = engine.query("test", 1);
        check(resLimit1.size() == 1, "limit=1 returns exactly 1 result");
    }

    std::cout << "\n  Memory after all benchmarks: " << std::setprecision(1) << getMemoryUsageMB() << "MB\n";
    std::cout << "\n";
}

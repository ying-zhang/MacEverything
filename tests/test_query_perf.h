#pragma once
// Part 44: Query Performance Benchmarks (data-driven optimization validation)
// Tests the query() path under various selectivity conditions to measure:
// - Trigram intersection time
// - Phase 1 (candidate verification) time
// - Phase 2 (path-only scan) time
// - Total query time
// Each scenario is run multiple times and averaged.

#include <random>
#include <sstream>

static void runQueryPerfBenchmarks() {
    std::cout << "========================================\n";
    std::cout << "  Part 44: Query Performance Benchmarks\n";
    std::cout << "========================================\n\n";

    // ── Build a realistic-sized dataset ──
    // Goal: ~500K records to exercise the trigram path at scale.
    // Records have diverse names that create different selectivity profiles.
    const uint32_t N = 500000;
    std::cout << "  Building " << N << " record dataset...\n";

    auto buildStart = std::chrono::steady_clock::now();
    std::vector<FileRecord> records;
    records.reserve(N);

    std::mt19937 rng(42); // deterministic seed for reproducibility

    // Name pools for different selectivity bands:
    // - "test" substring: ~2% of files (high candidate count, poor selectivity)
    // - ".cpp" extension: ~5% (medium, path-only match)
    // - "unique_xyz": <0.01% (low candidate count, excellent selectivity)
    // - Short common names: "a", "README" etc.
    std::string extensions[] = {".txt", ".cpp", ".h", ".py", ".js", ".json", ".md", ".log", ".xml", ".csv"};
    std::string prefixes[] = {"file", "data", "config", "module", "lib", "src", "doc", "test", "build", "cache"};
    std::string dirs[] = {"/usr/local/share", "/Users/someone/Documents", "/Applications/App.app/Contents",
                          "/System/Library/Frameworks", "/tmp/build/output", "/var/log",
                          "/opt/homebrew/lib", "/Library/Developer/CommandLineTools",
                          "/Users/someone/Desktop/project/src", "/private/var/folders"};

    for (uint32_t i = 0; i < N; i++) {
        std::string name;
        uint32_t bucket = i % 100;

        if (bucket < 2) {
            // ~2%: names containing "test" (high trigram candidate count)
            name = "test_" + std::to_string(i) + extensions[rng() % 10];
        } else if (bucket < 4) {
            // ~2%: names with "test" embedded differently
            name = prefixes[rng() % 10] + std::string("_test_") + std::to_string(i) + ".txt";
        } else if (bucket < 9) {
            // ~5%: .cpp files (for extension search)
            name = prefixes[rng() % 10] + "_" + std::to_string(i) + ".cpp";
        } else if (bucket == 9) {
            // ~1%: unique identifiers (excellent selectivity)
            name = "unique_xyz_" + std::to_string(i) + ".dat";
        } else {
            // ~91%: generic filenames
            name = prefixes[rng() % 10] + "_" + std::to_string(i) + extensions[rng() % 10];
        }

        std::string dir = dirs[rng() % 10];
        records.push_back({name, dir, uint8_t(1 + (rng() % 2)), uint64_t(rng() % 1000000), time_t(1700000000 + rng() % 1000000)});
    }

    SearchEngine engine;
    engine.loadRecords(std::move(records));

    auto buildEnd = std::chrono::steady_clock::now();
    double buildMs = std::chrono::duration<double, std::milli>(buildEnd - buildStart).count();
    std::cout << "  Dataset built + loaded in " << std::fixed << std::setprecision(1) << buildMs << "ms\n";
    std::cout << "  Total records: " << engine.recordCount() << ", live: " << engine.liveRecordCount() << "\n\n";

    // ── Benchmark helper ──
    struct BenchResult {
        std::string name;
        double avgMs;
        double minMs;
        double maxMs;
        uint32_t resultCount;
    };

    auto benchmark = [&](const std::string& keyword, uint32_t maxResults, int iterations) -> BenchResult {
        // Warm-up run
        auto warmup = engine.query(keyword, maxResults);
        uint32_t resultCount = static_cast<uint32_t>(warmup.size());

        double totalMs = 0;
        double minMs = 1e9, maxMs = 0;
        for (int i = 0; i < iterations; i++) {
            auto t0 = std::chrono::steady_clock::now();
            auto res = engine.query(keyword, maxResults);
            auto t1 = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            totalMs += ms;
            if (ms < minMs) minMs = ms;
            if (ms > maxMs) maxMs = ms;
        }
        return {keyword, totalMs / iterations, minMs, maxMs, resultCount};
    };

    auto printResult = [](const BenchResult& r) {
        std::cout << "    Query \"" << r.name << "\": avg=" << std::fixed << std::setprecision(3)
                  << r.avgMs << "ms  min=" << r.minMs << "ms  max=" << r.maxMs
                  << "ms  results=" << r.resultCount << "\n";
    };

    const int RUNS = 20;

    // ── Scenario 1: High trigram candidate count (poor selectivity) ──
    // "test" appears in ~4% of filenames -> candidates >> 1% of total
    // This is the bottleneck case: Phase 1 single-threaded random access
    std::cout << "  --- Scenario 1: High candidate count (poor selectivity) ---\n";
    {
        auto r1 = benchmark("test", 100, RUNS);
        printResult(r1);
        auto r2 = benchmark("test", 0, RUNS);
        printResult(r2);
        check(r1.resultCount > 0, "High candidate count benchmark returned results");
    }

    // ── Scenario 2: Medium selectivity ──
    // ".cpp" is 3 chars but common -> moderate candidate count
    std::cout << "\n  --- Scenario 2: Medium selectivity ---\n";
    {
        auto r1 = benchmark("config", 100, RUNS);
        printResult(r1);
        auto r2 = benchmark("module", 100, RUNS);
        printResult(r2);
        check(r1.resultCount > 0 && r2.resultCount > 0, "Medium selectivity benchmark returned results");
    }

    // ── Scenario 3: Excellent selectivity (rare keyword) ──
    // "unique_xyz" appears in ~1% -> few candidates, trigram is fast
    std::cout << "\n  --- Scenario 3: Excellent selectivity (rare keyword) ---\n";
    {
        auto r1 = benchmark("unique_xyz", 100, RUNS);
        printResult(r1);
        auto r2 = benchmark("unique_xyz_12345", 100, RUNS);
        printResult(r2);
        check(r1.resultCount > 0, "Excellent selectivity benchmark returned results");
    }

    // ── Scenario 4: Short keyword (no trigram, linear scan) ──
    // Keywords < 3 chars always use parallel linear scan
    std::cout << "\n  --- Scenario 4: Short keyword (parallel linear scan) ---\n";
    {
        auto r1 = benchmark("te", 100, RUNS);
        printResult(r1);
        auto r2 = benchmark("a", 100, RUNS);
        printResult(r2);
        check(r1.resultCount > 0, "Short keyword benchmark returned results");
    }

    // ── Scenario 5: Glob pattern (parallel linear scan) ──
    std::cout << "\n  --- Scenario 5: Glob pattern ---\n";
    {
        auto r1 = benchmark("*.cpp", 100, RUNS);
        printResult(r1);
        auto r2 = benchmark("test_*", 100, RUNS);
        printResult(r2);
        check(r1.resultCount > 0, "Glob pattern benchmark returned results");
    }

    // ── Scenario 6: No match (trigram early exit) ──
    std::cout << "\n  --- Scenario 6: No match (trigram early exit) ---\n";
    {
        auto r1 = benchmark("qzqzqz_nonexistent", 100, RUNS);
        printResult(r1);
        check(r1.resultCount == 0, "No-match benchmark returned zero results");
    }

    // ── Scenario 7: Correctness validation ──
    // Ensure query results are correct across all paths
    std::cout << "\n  --- Scenario 7: Correctness validation ---\n";
    {
        // "test" should find all test_* and *_test_* files
        auto res = engine.query("test", 0);
        check(res.size() > 0, "Correctness: 'test' returns results");

        // Check that exact matches are ranked first
        auto resLimited = engine.query("test", 100);
        check(resLimited.size() <= 100, "Correctness: maxResults limit respected");

        // "unique_xyz" should match specific files
        auto resUnique = engine.query("unique_xyz", 0);
        check(resUnique.size() > 0 && resUnique.size() < N / 10, "Correctness: 'unique_xyz' has reasonable result count");

        // Glob pattern correctness
        auto resGlob = engine.query("*.cpp", 0);
        bool allCpp = true;
        for (uint32_t i = 0; i < std::min((size_t)10, resGlob.size()); i++) {
            auto rec = engine.getRecord(resGlob[i]);
            if (rec.name.size() < 4 || rec.name.substr(rec.name.size() - 4) != ".cpp") {
                allCpp = false;
                break;
            }
        }
        check(allCpp, "Correctness: glob '*.cpp' only returns .cpp files");

        // No-match returns empty
        auto resNo = engine.query("qzqzqz_nonexistent", 0);
        check(resNo.empty(), "Correctness: non-existent keyword returns empty");
    }

    // ── Performance summary ──
    std::cout << "\n  --- Performance Summary ---\n";
    {
        auto highCand = benchmark("test", 100, RUNS);
        auto lowCand = benchmark("unique_xyz", 100, RUNS);
        auto linearScan = benchmark("te", 100, RUNS);
        auto globScan = benchmark("*.cpp", 100, RUNS);

        std::cout << "    High candidates (\"test\", maxR=100):    " << std::fixed << std::setprecision(3) << highCand.avgMs << "ms\n";
        std::cout << "    Low candidates  (\"unique_xyz\", maxR=100): " << lowCand.avgMs << "ms\n";
        std::cout << "    Linear scan     (\"te\", maxR=100):      " << linearScan.avgMs << "ms\n";
        std::cout << "    Glob scan       (\"*.cpp\", maxR=100):   " << globScan.avgMs << "ms\n";

        // After optimization, high candidates should be closer to linear scan
        // (not 100x slower as in production with 4.8M records)
        double ratio = highCand.avgMs / linearScan.avgMs;
        std::cout << "    Ratio (high_cand / linear_scan): " << std::setprecision(1) << ratio << "x\n";
        check(ratio > 0, "Performance summary: ratio is valid");
    }

    std::cout << "\n";
}

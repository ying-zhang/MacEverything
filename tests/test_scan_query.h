#pragma once
// ═══════════════════════════════════════════════════════
//  Part 1 + 2: Full Disk Scan + Query Benchmark
// ═══════════════════════════════════════════════════════

static SearchEngine globalEngine;

static void runScanAndQueryBenchmark(const std::string& rootPath) {
    size_t memBefore = getMemoryUsageMB();

    std::cout << "========================================\n";
    std::cout << "  Part 1: Full Disk Scan\n";
    std::cout << "========================================\n\n";
    std::cout << "  Root path: " << rootPath << "\n";
    std::cout << "  Memory before scan: " << memBefore << " MB\n\n";

    auto t0 = std::chrono::steady_clock::now();
    DirectoryScanner scanner;
    scanner.scan(rootPath);
    auto t1 = std::chrono::steady_clock::now();
    double scanTime = std::chrono::duration<double>(t1 - t0).count();

    const auto& stats = scanner.getStats();
    uint64_t files = stats.fileCount.load();
    uint64_t dirs = stats.dirCount.load();
    uint64_t symlinks = stats.symlinkCount.load();
    uint64_t others = stats.otherCount.load();
    uint64_t errors = stats.errorCount.load();
    uint64_t total = files + dirs + symlinks + others;

    size_t memAfterScan = getMemoryUsageMB();

    std::cout << "  Files:       " << files << "\n";
    std::cout << "  Directories: " << dirs << "\n";
    std::cout << "  Symlinks:    " << symlinks << "\n";
    std::cout << "  Other:       " << others << "\n";
    std::cout << "  Errors:      " << errors << "\n";
    std::cout << "  Total:       " << total << "\n";
    std::cout << "  Scan time:   " << std::fixed << std::setprecision(3) << scanTime << "s\n";
    std::cout << "  Throughput:  " << std::fixed << std::setprecision(0) << (total / scanTime) << " entries/s\n";
    std::cout << "  Memory:      " << memAfterScan << " MB\n\n";

    // Build index
    std::cout << "  Building Search Index...\n";
    auto results = scanner.takeResults();
    auto t2 = std::chrono::steady_clock::now();
    globalEngine.loadRecords(std::move(results));
    auto t3 = std::chrono::steady_clock::now();
    double indexTime = std::chrono::duration<double>(t3 - t2).count();

    size_t memAfterIndex = getMemoryUsageMB();
    std::cout << "  Records:     " << globalEngine.recordCount() << "\n";
    std::cout << "  Live:        " << globalEngine.liveRecordCount() << "\n";
    std::cout << "  Index time:  " << std::fixed << std::setprecision(3) << indexTime << "s\n";
    std::cout << "  Memory:      " << memAfterIndex << " MB\n";
    std::cout << "  Per-entry:   ~" << std::setprecision(0) << (memAfterIndex * 1024.0 * 1024.0 / total) << " bytes\n";
    std::cout << "  Total startup: " << std::fixed << std::setprecision(3) << (scanTime + indexTime) << "s\n\n";

    // ── Part 2: Query Performance ──
    std::cout << "========================================\n";
    std::cout << "  Part 2: Query Performance\n";
    std::cout << "========================================\n\n";

    struct QueryTest {
        std::string keyword;
        uint32_t maxResults;
        std::string description;
    };

    std::vector<QueryTest> tests = {
        {"README",       0,     "Common filename (README)"},
        {"readme",       0,     "Case-insensitive (readme)"},
        {".cpp",         0,     "File extension (.cpp)"},
        {".swift",       0,     "File extension (.swift)"},
        {".json",        0,     "File extension (.json)"},
        {".py",          0,     "File extension (.py)"},
        {"node_modules", 0,     "Directory name (node_modules)"},
        {"index",        0,     "Common name (index)"},
        {"test",         0,     "Common name (test)"},
        {"a",            0,     "Single char (a) - many matches"},
        {"xyzxyzxyz",    0,     "No match (xyzxyzxyz)"},
        {".cpp",         100,   ".cpp maxResults=100"},
        {".cpp",         1000,  ".cpp maxResults=1000"},
        {"test",         50000, "test maxResults=50000"},
    };

    // Warm up
    globalEngine.query("warmup", 10);

    std::cout << std::left << std::setw(40) << "  Query"
              << std::right << std::setw(10) << "Matches"
              << std::setw(12) << "Time(ms)" << "\n";
    std::cout << "  " << std::string(60, '-') << "\n";

    double totalQueryTime = 0;
    int unlimitedCount = 0;

    for (const auto& test : tests) {
        double bestTime = 1e9;
        size_t matchCount = 0;

        for (int run = 0; run < 3; run++) {
            auto qs = std::chrono::steady_clock::now();
            auto indices = globalEngine.query(test.keyword, test.maxResults);
            auto qe = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(qe - qs).count() * 1000;
            if (elapsed < bestTime) bestTime = elapsed;
            matchCount = indices.size();
        }

        std::cout << "  " << std::left << std::setw(40) << test.description
                  << std::right << std::setw(10) << matchCount
                  << std::setw(10) << std::fixed << std::setprecision(2) << bestTime << "ms\n";

        if (test.maxResults == 0) {
            totalQueryTime += bestTime;
            unlimitedCount++;
        }
    }

    if (unlimitedCount > 0) {
        std::cout << "\n  Avg query time (unlimited): "
                  << std::fixed << std::setprecision(2) << (totalQueryTime / unlimitedCount) << "ms\n";
    }

    // Record fetch
    std::cout << "\n  Record Fetch Benchmark:\n";
    auto fetchIndices = globalEngine.query("a", 10000);
    auto f0 = std::chrono::steady_clock::now();
    for (uint32_t idx : fetchIndices) {
        const auto& r = globalEngine.getRecord(idx);
        (void)r.name;
    }
    auto f1 = std::chrono::steady_clock::now();
    double fetchTime = std::chrono::duration<double>(f1 - f0).count() * 1000;
    std::cout << "  Fetched " << fetchIndices.size() << " records in "
              << std::fixed << std::setprecision(2) << fetchTime << "ms"
              << " (" << std::setprecision(1) << (fetchTime / fetchIndices.size() * 1000) << "us/record)\n\n";
}

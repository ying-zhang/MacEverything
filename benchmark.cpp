// MacEverything Performance Benchmark
// Compile: clang++ -std=c++20 -O2 MacEverything/Core/*.cpp benchmark.cpp -o benchmark

#include "MacEverything/Core/DirectoryScanner.h"
#include "MacEverything/Core/SearchEngine.h"
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <mach/mach.h>

static size_t getMemoryUsageMB() {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return info.resident_size / (1024 * 1024);
    }
    return 0;
}

struct BenchResult {
    std::string label;
    double timeMs;
    size_t matchCount;
};

int main(int argc, char* argv[]) {
    std::string rootPath = (argc > 1) ? argv[1] : "/";

    int testfd = open(rootPath.c_str(), O_RDONLY | O_DIRECTORY);
    if (testfd < 0) {
        std::cerr << "Error: cannot open '" << rootPath << "': " << strerror(errno) << "\n";
        return 1;
    }
    close(testfd);

    size_t memBefore = getMemoryUsageMB();

    // ===================== Phase 1: Scan =====================
    std::cout << "========================================\n";
    std::cout << "  MacEverything Performance Benchmark\n";
    std::cout << "========================================\n\n";
    std::cout << "Root path: " << rootPath << "\n";
    std::cout << "Memory before scan: " << memBefore << " MB\n\n";

    std::cout << "[1/4] Full Disk Scan...\n";
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

    // ===================== Phase 2: Index Build =====================
    std::cout << "[2/4] Building Search Index...\n";
    auto results = scanner.takeResults();

    SearchEngine engine;
    auto t2 = std::chrono::steady_clock::now();
    engine.loadRecords(std::move(results));
    auto t3 = std::chrono::steady_clock::now();
    double indexTime = std::chrono::duration<double>(t3 - t2).count();

    size_t memAfterIndex = getMemoryUsageMB();
    std::cout << "  Records:     " << engine.recordCount() << "\n";
    std::cout << "  Index time:  " << std::fixed << std::setprecision(3) << indexTime << "s\n";
    std::cout << "  Memory:      " << memAfterIndex << " MB\n\n";

    // ===================== Phase 3: Query Benchmarks =====================
    std::cout << "[3/4] Query Performance Benchmarks...\n\n";

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
        {".cpp",         100,   ".cpp with maxResults=100"},
        {".cpp",         1000,  ".cpp with maxResults=1000"},
        {"test",         50000, "test with maxResults=50000"},
    };

    std::vector<BenchResult> benchResults;
    // Warm up
    engine.query("warmup", 10);

    std::cout << std::left << std::setw(40) << "  Query"
              << std::right << std::setw(10) << "Matches"
              << std::setw(12) << "Time(ms)" << "\n";
    std::cout << "  " << std::string(60, '-') << "\n";

    for (const auto& test : tests) {
        // Run each query 3 times, take the best
        double bestTime = 1e9;
        size_t matchCount = 0;

        for (int run = 0; run < 3; run++) {
            auto qs = std::chrono::steady_clock::now();
            auto indices = engine.query(test.keyword, test.maxResults);
            auto qe = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(qe - qs).count() * 1000;
            if (elapsed < bestTime) bestTime = elapsed;
            matchCount = indices.size();
        }

        std::string label = test.description;
        if (test.maxResults > 0) {
            label += " (max=" + std::to_string(test.maxResults) + ")";
        }

        std::cout << "  " << std::left << std::setw(40) << label
                  << std::right << std::setw(10) << matchCount
                  << std::setw(10) << std::fixed << std::setprecision(2) << bestTime << "ms\n";

        benchResults.push_back({label, bestTime, matchCount});
    }

    // ===================== Phase 4: Record Fetch =====================
    std::cout << "\n[4/4] Record Fetch Benchmark...\n";

    // Fetch 10000 records by index
    auto fetchIndices = engine.query("a", 10000);
    auto f0 = std::chrono::steady_clock::now();
    for (uint32_t idx : fetchIndices) {
        const auto& r = engine.getRecord(idx);
        (void)r.name; // prevent optimization
    }
    auto f1 = std::chrono::steady_clock::now();
    double fetchTime = std::chrono::duration<double>(f1 - f0).count() * 1000;
    std::cout << "  Fetched " << fetchIndices.size() << " records in "
              << std::fixed << std::setprecision(2) << fetchTime << "ms"
              << " (" << std::setprecision(1) << (fetchTime / fetchIndices.size() * 1000) << "µs/record)\n";

    // ===================== Summary =====================
    size_t memFinal = getMemoryUsageMB();

    std::cout << "\n========================================\n";
    std::cout << "  Summary\n";
    std::cout << "========================================\n";
    std::cout << "  Total entries:      " << total << "\n";
    std::cout << "  Scan time:          " << std::fixed << std::setprecision(3) << scanTime << "s\n";
    std::cout << "  Index build time:   " << std::fixed << std::setprecision(3) << indexTime << "s\n";
    std::cout << "  Total startup time: " << std::fixed << std::setprecision(3) << (scanTime + indexTime) << "s\n";
    std::cout << "  Memory usage:       " << memFinal << " MB\n";
    std::cout << "  Per-entry memory:   ~" << std::setprecision(0)
              << (memFinal * 1024.0 * 1024.0 / total) << " bytes/entry\n";

    // Average query time for unlimited queries
    double totalQueryTime = 0;
    int queryCount = 0;
    for (const auto& br : benchResults) {
        if (br.label.find("max=") == std::string::npos) {
            totalQueryTime += br.timeMs;
            queryCount++;
        }
    }
    if (queryCount > 0) {
        std::cout << "  Avg query time:     " << std::fixed << std::setprecision(2)
                  << (totalQueryTime / queryCount) << "ms (unlimited)\n";
    }

    std::cout << "========================================\n";

    return 0;
}

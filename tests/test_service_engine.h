#pragma once
// Part 40: ServiceEngine unit tests
// Verifies ServiceEngine construction, full scan on a tmpdir, query, and shutdown.

// ServiceEngine.h is included via test_all.cpp
#include <fstream>

static bool runPart40() {
    std::cout << "\n=== Part 40: ServiceEngine unit tests ===\n";
    bool allOk = true;

    // Create a tmpdir with some test files
    auto tmpBase = fs::temp_directory_path() / "maceverything_test_se";
    fs::remove_all(tmpBase);
    fs::create_directories(tmpBase / "subdir");

    // Create test files
    { std::ofstream f(tmpBase / "hello.txt"); f << "hello world"; }
    { std::ofstream f(tmpBase / "test.cpp"); f << "int main() {}"; }
    { std::ofstream f(tmpBase / "subdir" / "nested.log"); f << "log entry"; }

    std::string cachePath = (tmpBase / "cache").string();
    fs::create_directories(cachePath);

    // Test 1: Construction
    {
        ServiceConfig config;
        config.scanRoot = tmpBase.string();
        config.cachePath = cachePath;
        config.logPath = (tmpBase / "logs").string();

        auto engine = std::make_unique<ServiceEngine>(config);
        check(engine != nullptr, "ServiceEngine construction succeeds");

        check(engine->recordCount() == 0, "Initial recordCount is 0");
        check(!engine->isScanning(), "Not scanning initially");
        check(!engine->isMonitoring(), "Not monitoring initially");
    }

    // Test 2: Full scan on tmpdir
    {
        ServiceConfig config;
        config.scanRoot = tmpBase.string();
        config.cachePath = cachePath;
        config.logPath = (tmpBase / "logs").string();

        auto se = std::make_unique<ServiceEngine>(config);
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        uint32_t finalCount = 0;
        bool didFullScan = false;

        se->startFullScan([&](uint32_t count, bool fullScan) {
            finalCount = count;
            didFullScan = fullScan;
            dispatch_semaphore_signal(sem);
        });

        long result = dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
        check(result == 0, "Full scan completed within timeout");
        check(didFullScan, "Reported as full scan");
        // Should have at least: hello.txt, test.cpp, subdir, nested.log, tmpdir itself
        check(finalCount >= 4, "Found at least 4 records");

        // Test 3: Query on scanned data
        auto searchEngine = se->safeEngine();
        check(searchEngine != nullptr, "safeEngine returns non-null after scan");

        if (searchEngine) {
            auto indices = searchEngine->query("hello", 10);
            check(!indices.empty(), "Query 'hello' finds hello.txt");

            auto indices2 = searchEngine->query("nested", 10);
            check(!indices2.empty(), "Query 'nested' finds nested.log");

            auto indices3 = searchEngine->query("nonexistent_xyz", 10);
            check(indices3.empty(), "Query 'nonexistent_xyz' returns empty");
        }

        // Test 4: State queries
        check(!se->isScanning(), "Not scanning after scan completion");
        check(se->liveRecordCount() == finalCount, "liveRecordCount matches finalCount");

        // Test 5: Controlled rebuild immediately exposes an empty engine, then repopulates it.
        dispatch_semaphore_t rebuildSem = dispatch_semaphore_create(0);
        uint32_t rebuiltCount = 0;
        se->rebuildIndex([&](uint32_t count, bool) {
            rebuiltCount = count;
            dispatch_semaphore_signal(rebuildSem);
        });
        check(se->liveRecordCount() == 0, "Rebuild immediately clears visible index");
        long rebuildResult = dispatch_semaphore_wait(
            rebuildSem, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC));
        check(rebuildResult == 0, "Rebuild completed within timeout");
        check(rebuiltCount >= 4, "Rebuild repopulated scanned records");

        // Test 6: Shutdown
        se->shutdown();
        check(!se->isScanning(), "Not scanning after shutdown");
    }

    // Test 7: Incremental load (cold start = full scan fallback)
    {
        // Use a fresh cache so there's no cached index
        std::string freshCache = (tmpBase / "cache_incr").string();
        fs::create_directories(freshCache);

        ServiceConfig config;
        config.scanRoot = tmpBase.string();
        config.cachePath = freshCache;
        config.logPath = (tmpBase / "logs").string();

        auto se = std::make_unique<ServiceEngine>(config);
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        uint32_t finalCount = 0;

        se->startIncremental([&](uint32_t count, bool) {
            finalCount = count;
            dispatch_semaphore_signal(sem);
        });

        long result = dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC));
        check(result == 0, "Incremental (cold) completed within timeout");
        check(finalCount >= 4, "Incremental found at least 4 records");

        se->shutdown();
    }

    // Clean up
    fs::remove_all(tmpBase);

    return allOk;
}

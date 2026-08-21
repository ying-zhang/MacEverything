#pragma once
// Part 80: Instance lock enforcement at the ServiceEngine level.
// Proves that a second ServiceEngine pointing at the same cache path does
// NOT load, replay or write any index file when the lock is already held.
// Also covers snapshot+WAL persistence scenarios: after Engine A flushes
// to disk, Engine B must fail without touching the persisted files.

#include <fstream>
#include <sys/stat.h>

namespace {
    // Collect checksums of all files matching a pattern in a directory.
    struct FileSnapshot {
        std::string path;
        off_t size = 0;
        time_t mtime = 0;
    };
    std::vector<FileSnapshot> snapshotDir(const std::string& dir) {
        std::vector<FileSnapshot> files;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            struct stat st;
            if (::stat(entry.path().c_str(), &st) == 0) {
                files.push_back({entry.path().string(), st.st_size, st.st_mtime});
            }
        }
        return files;
    }
}

static void runInstanceLockEnforcementTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 80 — Instance Lock Enforcement\n";
    std::cout << "========================================\n\n";

    auto tmpBase = fs::temp_directory_path() / "maceverything_test_lock_enf";
    fs::remove_all(tmpBase);
    fs::create_directories(tmpBase / "subdir");

    // Create a handful of test files so the first engine has real records.
    { std::ofstream f(tmpBase / "alpha.txt"); f << "alpha"; }
    { std::ofstream f(tmpBase / "beta.cpp"); f << "int main() { return 0; }"; }
    { std::ofstream f(tmpBase / "subdir" / "gamma.log"); f << "gamma"; }

    std::string cachePath = (tmpBase / "cache").string();

    // ═══════════════════════════════════════════════════════
    //  Test 1 — second engine must fail fast, leave no trace
    // ═══════════════════════════════════════════════════════

    {
        // --- Engine A: startFullScan (acquires lock, scans only into memory) ---
        ServiceConfig configA;
        configA.scanRoot = tmpBase.string();
        configA.cachePath = cachePath;
        configA.logPath = (tmpBase / "logs").string();
        configA.contentIndexingEnabled = false;   // keep the test fast & disk-quiet
        configA.realtimeMonitoring = false;

        auto seA = std::make_unique<ServiceEngine>(configA);
        dispatch_semaphore_t semA = dispatch_semaphore_create(0);
        uint32_t aCount = 0;
        bool aFull = false;

        seA->startFullScan([&](uint32_t count, bool fullScan) {
            aCount = count;
            aFull = fullScan;
            dispatch_semaphore_signal(semA);
        });

        long waitA = dispatch_semaphore_wait(semA, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
        check(waitA == 0, "Engine A completed within timeout");
        check(!seA->isStartupFatal(), "Engine A is not startup-fatal");
        check(aFull, "Engine A reports full scan");
        check(aCount >= 4, "Engine A has at least 4 records");
        check(seA->liveRecordCount() >= 4, "Engine A liveRecordCount >= 4");

        // --- Engine B: same cache → must fail the lock BEFORE touching index ---
        ServiceConfig configB;
        configB.scanRoot = tmpBase.string();
        configB.cachePath = cachePath;
        configB.logPath = (tmpBase / "logs").string();
        configB.contentIndexingEnabled = false;
        configB.realtimeMonitoring = false;

        auto seB = std::make_unique<ServiceEngine>(configB);
        std::string bFailReason;
        bool bGotCallback = false;
        uint32_t bCount = 0xFFFF;
        bool bFullFlag = true;

        seB->onStartupFailed = [&](const std::string& reason) {
            bFailReason = reason;
            bGotCallback = true;
        };

        seB->startIncremental([&](uint32_t count, bool fullScan) {
            bCount = count;
            bFullFlag = fullScan;
        });

        // The lock check is synchronous — state is final right after the call.
        check(seB->isStartupFatal(), "Engine B reports startup-fatal");
        check(bGotCallback, "Engine B fired onStartupFailed");
        check(!bFailReason.empty(), "onStartupFailed reason is non-empty");
        check(bCount == 0, "Engine B completion reports 0 records");
        check(!bFullFlag, "Engine B completion reports NOT a full scan");
        check(seB->liveRecordCount() == 0, "Engine B loaded no live records");

        // Neither engine created on-disk index files (A is in-memory only,
        // B never reached the persistence layer).
        check(!fs::exists(cachePath + "/index.wal"), "No index.wal created");
        check(!fs::exists(cachePath + "/index.v6"), "No index.v6 created");
        check(!fs::exists(cachePath + "/index.bin"), "No index.bin created");
        // The lock file itself should exist (A created it).
        check(fs::exists(cachePath + "/.instance.lock"), "Lock file .instance.lock exists");

        // Clean up
        seA->shutdown();
        seB->shutdown();
    }

    fs::remove_all(tmpBase);

    // ═══════════════════════════════════════════════════════
    //  Test 2 — Engine B must not touch existing snapshot+WAL
    // ═══════════════════════════════════════════════════════
    // Engine A starts, scans, saves a snapshot+WAL, and keeps the
    // lock held. Engine B then attempts to start on the same cache;
    // it must fail WITHOUT loading, replaying, or modifying any
    // persisted file that A wrote.

    {
        auto tmp2 = fs::temp_directory_path() / "maceverything_test_lock_snap";
        fs::remove_all(tmp2);
        fs::create_directories(tmp2 / "data");
        // Create enough files so the engine definitely writes something.
        for (int i = 0; i < 20; ++i) {
            std::ofstream f(tmp2 / "data" / ("file" + std::to_string(i) + ".txt"));
            f << "content " << i;
        }

        std::string cache2 = (tmp2 / "cache").string();
        fs::create_directories(cache2);

        // --- Engine A: incremental startup (writes snapshot to disk) ---
        // startIncremental creates a persistence object and flushes via its
        // completion callback, so persisted files will exist on disk.
        ServiceConfig cfgA;
        cfgA.scanRoot = (tmp2 / "data").string();
        cfgA.cachePath = cache2;
        cfgA.logPath = (tmp2 / "logs").string();
        cfgA.contentIndexingEnabled = false;
        cfgA.realtimeMonitoring = false;
        cfgA.automaticMaintenanceEnabled = false;

        auto seA = std::make_unique<ServiceEngine>(cfgA);
        dispatch_semaphore_t semA = dispatch_semaphore_create(0);
        bool aDidFull = false;
        uint32_t aCount = 0;
        seA->startIncremental([&](uint32_t count, bool didFull) {
            aCount = count;
            aDidFull = didFull;
            dispatch_semaphore_signal(semA);
        });
        long waitA = dispatch_semaphore_wait(semA, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC));
        check(waitA == 0, "Engine A incremental startup completed");
        check(!seA->isStartupFatal(), "Engine A is not startup-fatal");
        check(aCount >= 20, "Engine A has records from scan");

        // Give persistence a moment to flush to disk
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Verify snapshot/WAL files exist (from incremental's persistence)
        bool hasIndex = fs::exists(cache2 + "/index.v6") ||
                        fs::exists(cache2 + "/index.bin") ||
                        fs::exists(cache2 + "/index.wal");
        check(hasIndex, "Engine A produced on-disk snapshot or WAL");

        // Snapshot the cache directory state BEFORE B attempts to start.
        auto beforeFiles = snapshotDir(cache2);
        check(!beforeFiles.empty(), "Cache directory is non-empty after A's persistence");

        // --- Engine B: same cache, Engine A is STILL ALIVE (lock held) ---
        // Engine B must fail without reading or modifying A's index files.
        ServiceConfig cfgB;
        cfgB.scanRoot = (tmp2 / "data").string();
        cfgB.cachePath = cache2;
        cfgB.logPath = (tmp2 / "logs").string();
        cfgB.contentIndexingEnabled = false;
        cfgB.realtimeMonitoring = false;

        auto seB = std::make_unique<ServiceEngine>(cfgB);
        bool bFailed = false;
        seB->onStartupFailed = [&](const std::string&) { bFailed = true; };

        seB->startIncremental([](uint32_t, bool) {});

        check(seB->isStartupFatal(), "Engine B reports startup-fatal with existing snapshot");
        check(bFailed, "Engine B fired onStartupFailed with existing snapshot");
        check(seB->liveRecordCount() == 0,
              "Engine B loaded 0 records despite existing snapshot");

        // Engine A should still be functional
        check(seA->liveRecordCount() >= 20, "Engine A still has its records");

        // Verify cache files are unchanged after B's failed startup
        auto afterFiles = snapshotDir(cache2);
        check(beforeFiles.size() == afterFiles.size(),
              "Cache file count unchanged after B's failed startup");
        for (const auto& after : afterFiles) {
            bool found = false;
            for (const auto& before : beforeFiles) {
                if (before.path == after.path) {
                    found = true;
                    check(before.size == after.size,
                          ("File size unchanged: " + after.path).c_str());
                    check(before.mtime == after.mtime,
                          ("File mtime unchanged: " + after.path).c_str());
                    break;
                }
            }
            if (!found) {
                check(false, ("Unexpected new file after B startup: " + after.path).c_str());
            }
        }

        seB->shutdown();
        seA->shutdown();
        fs::remove_all(tmp2);
    }

    // ═══════════════════════════════════════════════════════
    //  Test 3 — duplicate GUI process mutex simulation
    // ═══════════════════════════════════════════════════════
    // Simulates two app processes pointed at the same cache directory. The
    // second process must refuse to start before opening any index state.

    {
        auto tmp3 = fs::temp_directory_path() / "maceverything_test_dual";
        fs::remove_all(tmp3);
        fs::create_directories(tmp3 / "files");
        { std::ofstream f(tmp3 / "files" / "shared.txt"); f << "shared"; }

        std::string cache3 = (tmp3 / "cache").string();

        // Simulate GUI starting first
        ServiceConfig guiCfg;
        guiCfg.scanRoot = (tmp3 / "files").string();
        guiCfg.cachePath = cache3;
        guiCfg.logPath = (tmp3 / "logs").string();
        guiCfg.processType = "gui";
        guiCfg.contentIndexingEnabled = false;
        guiCfg.realtimeMonitoring = false;

        auto guiEngine = std::make_unique<ServiceEngine>(guiCfg);
        dispatch_semaphore_t semG = dispatch_semaphore_create(0);
        guiEngine->startFullScan([&](uint32_t, bool) {
            dispatch_semaphore_signal(semG);
        });
        dispatch_semaphore_wait(semG, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
        check(!guiEngine->isStartupFatal(), "GUI engine started successfully");
        check(guiEngine->liveRecordCount() > 0, "GUI engine has records");

        // Simulate a second app instance trying to use the same cache.
        ServiceConfig secondCfg;
        secondCfg.scanRoot = (tmp3 / "files").string();
        secondCfg.cachePath = cache3;
        secondCfg.logPath = (tmp3 / "logs").string();
        secondCfg.processType = "gui";
        secondCfg.contentIndexingEnabled = false;
        secondCfg.realtimeMonitoring = false;

        auto secondEngine = std::make_unique<ServiceEngine>(secondCfg);
        bool secondFailed = false;
        std::string secondReason;
        secondEngine->onStartupFailed = [&](const std::string& reason) {
            secondFailed = true;
            secondReason = reason;
        };

        secondEngine->startIncremental([](uint32_t, bool) {});

        check(secondEngine->isStartupFatal(),
              "Second GUI engine reports startup-fatal when lock is held");
        check(secondFailed, "Second GUI engine fired onStartupFailed");
        check(secondReason.find("another instance") != std::string::npos,
              "Second GUI failure reason mentions another instance");
        check(secondEngine->liveRecordCount() == 0,
              "Second GUI engine loaded 0 records");

        // GUI still functional
        check(guiEngine->liveRecordCount() > 0,
              "First GUI engine remains functional after duplicate rejected");

        guiEngine->shutdown();
        secondEngine->shutdown();
        fs::remove_all(tmp3);
    }

    std::cout << "\n";
}

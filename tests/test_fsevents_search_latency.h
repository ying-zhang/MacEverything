#pragma once
// ═══════════════════════════════════════════════════════
//  Part 76: FSEvents → SearchEngine Latency Test
//  Measures the end-to-end time from file creation on disk
//  to the file being searchable via SearchEngine::query().
//  Pipeline: file create → FSEvents → ServiceEngine → batchMutate → queryable
//
//  NOTE: FileSystemWatcher uses kFSEventStreamCreateFlagIgnoreSelf,
//  so files must be created from a child process to generate events.
// ═══════════════════════════════════════════════════════

#include <signal.h>
#include <spawn.h>

extern char** environ;

static void runFSEventsSearchLatencyTest() {
    std::cout << "========================================\n";
    std::cout << "  Part 76: FSEvents → Search Latency\n";
    std::cout << "========================================\n\n";

    const int NUM_FILES = 100;
    const int POLL_INTERVAL_MS = 5;       // 5ms poll interval
    const int TIMEOUT_MS = 5000;          // 5s timeout per file
    const double MAX_ACCEPTABLE_P99_MS = 1000.0;  // P99 should be < 1s

    // ── Setup: create tmpdir with some seed files, start ServiceEngine ──
    // NOTE: fs::temp_directory_path() on macOS resolves to /var/folders/.../T,
    // which is both a symlink (/var -> /private/var) and a system-filtered path.
    // Use the (real, non-system) current working directory so FSEvents reports
    // paths that match the configured scan root and are not dropped as system.
    auto tmpBase = fs::current_path() / ("me_latency_test_" + std::to_string(getpid()));
    fs::remove_all(tmpBase);
    fs::create_directories(tmpBase);

    // Seed a file so the directory is non-empty (created before monitoring starts)
    { std::ofstream f(tmpBase / "seed.txt"); f << "seed\n"; }

    std::string cachePath = (tmpBase / "cache").string();
    fs::create_directories(cachePath);

    ServiceConfig config;
    config.scanRoot = tmpBase.string();
    config.cachePath = cachePath;
    config.logPath = (tmpBase / "logs").string();

    auto se = std::make_unique<ServiceEngine>(config);

    // Full scan first
    dispatch_semaphore_t scanSem = dispatch_semaphore_create(0);
    se->startFullScan([&](uint32_t, bool) {
        dispatch_semaphore_signal(scanSem);
    });
    long scanResult = dispatch_semaphore_wait(scanSem,
        dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
    check(scanResult == 0, "Full scan completed");

    // startMonitoring() is called automatically after full scan completes
    // Wait for FSEvents to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    check(se->isMonitoring(), "FSEvents monitoring started");

    auto engine = se->safeEngine();
    check(engine != nullptr, "SearchEngine available");

    if (!engine) {
        se->shutdown();
        fs::remove_all(tmpBase);
        return;
    }

    // ── Run latency measurements ──
    // Files must be created from a child process because FileSystemWatcher
    // uses kFSEventStreamCreateFlagIgnoreSelf (events from same PID are dropped).
    std::vector<double> latencies;
    int timeouts = 0;

    std::cout << "  Creating " << NUM_FILES << " files via child process, polling every "
              << POLL_INTERVAL_MS << "ms (timeout " << TIMEOUT_MS << "ms)...\n\n";

    for (int i = 0; i < NUM_FILES; i++) {
        std::string fileName = "latency_probe_" + std::to_string(i) + ".txt";
        std::string filePath = (tmpBase / fileName).string();

        // Create the file from a child process using /bin/sh -c
        auto t0 = std::chrono::steady_clock::now();

        pid_t pid;
        std::string cmd = "echo probe > " + filePath;
        char* argv[] = {(char*)"/bin/sh", (char*)"-c", (char*)cmd.c_str(), nullptr};
        int spawnErr = posix_spawn(&pid, "/bin/sh", nullptr, nullptr, argv, environ);
        if (spawnErr != 0) {
            std::cout << "    [error] posix_spawn failed: " << strerror(spawnErr) << "\n";
            timeouts++;
            continue;
        }
        int status;
        waitpid(pid, &status, 0);

        // Poll until searchable or timeout
        bool found = false;
        while (true) {
            auto elapsed = std::chrono::steady_clock::now() - t0;
            double elapsedMs = std::chrono::duration<double, std::milli>(elapsed).count();
            if (elapsedMs > TIMEOUT_MS) break;

            auto curEngine = se->safeEngine();
            auto results = curEngine->query(fileName, 1);
            if (!results.empty()) {
                found = true;
                latencies.push_back(elapsedMs);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
        }

        if (!found) {
            timeouts++;
        }

        // Print progress every 20 files
        if ((i + 1) % 20 == 0) {
            double lastMs = found ? latencies.back() : -1;
            std::cout << "    [" << std::setw(3) << (i + 1) << "/" << NUM_FILES << "] "
                      << (found ? std::to_string((int)lastMs) + " ms" : "TIMEOUT")
                      << "\n";
        }
    }

    // ── Statistics ──
    std::cout << "\n  --- Results ---\n";
    std::cout << "    Files tested:  " << NUM_FILES << "\n";
    std::cout << "    Found:         " << latencies.size() << "\n";
    std::cout << "    Timed out:     " << timeouts << "\n";

    check(timeouts == 0, "All files found within timeout");
    check(!latencies.empty(), "At least one latency measurement");

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        double sum = 0;
        for (double v : latencies) sum += v;
        double mean = sum / latencies.size();
        double minMs = latencies.front();
        double maxMs = latencies.back();
        double median = latencies[latencies.size() / 2];
        double p90 = latencies[(size_t)(latencies.size() * 0.90)];
        double p99 = latencies[(size_t)(latencies.size() * 0.99)];

        std::cout << std::fixed << std::setprecision(1);
        std::cout << "    Min:     " << std::setw(8) << minMs << " ms\n";
        std::cout << "    Max:     " << std::setw(8) << maxMs << " ms\n";
        std::cout << "    Mean:    " << std::setw(8) << mean << " ms\n";
        std::cout << "    Median:  " << std::setw(8) << median << " ms\n";
        std::cout << "    P90:     " << std::setw(8) << p90 << " ms\n";
        std::cout << "    P99:     " << std::setw(8) << p99 << " ms\n";

        // Distribution
        std::cout << "\n    Distribution:\n";
        int buckets[] = {0, 50, 100, 200, 300, 500, 1000, 2000, 5000};
        int numBuckets = sizeof(buckets) / sizeof(buckets[0]);
        for (int b = 0; b < numBuckets - 1; b++) {
            int lo = buckets[b], hi = buckets[b + 1];
            int count = 0;
            for (double v : latencies) {
                if (v >= lo && v < hi) count++;
            }
            if (count > 0) {
                std::string bar(count, '#');
                std::cout << "      " << std::setw(5) << lo << "-" << std::setw(5) << hi
                          << " ms: " << std::setw(3) << count << " " << bar << "\n";
            }
        }

        // Assertions: P99 < 1s (FSEvents latency is ~300ms, allow generous margin)
        check(p99 < MAX_ACCEPTABLE_P99_MS,
              ("P99 latency < " + std::to_string((int)MAX_ACCEPTABLE_P99_MS) + "ms (actual: "
               + std::to_string((int)p99) + "ms)").c_str());

        // Median should be around 300ms (FSEvents coalesce window)
        check(median < 600.0,
              ("Median latency < 600ms (actual: " + std::to_string((int)median) + "ms)").c_str());
    }

    // ── Cleanup ──
    se->shutdown();
    fs::remove_all(tmpBase);
    std::cout << "\n";
}

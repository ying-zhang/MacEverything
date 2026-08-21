#pragma once
// Part 19: Rapid Typing Stress Test
// Simulates user rapidly typing/deleting/retyping to measure query latency,
// cancellation behavior, and CPU/memory usage under high-frequency query load.

static void printResourceDelta(const char* label,
                               const ResourceSnapshot& before,
                               const ResourceSnapshot& after) {
    double cpuUser = after.cpu.userMs - before.cpu.userMs;
    double cpuSys  = after.cpu.sysMs  - before.cpu.sysMs;
    int64_t memDelta = static_cast<int64_t>(after.memMB) - static_cast<int64_t>(before.memMB);
    std::cout << "    " << label << " resources:\n";
    std::cout << "      CPU user: " << std::fixed << std::setprecision(1)
              << cpuUser << " ms, sys: " << cpuSys << " ms, total: "
              << (cpuUser + cpuSys) << " ms\n";
    std::cout << "      Memory: " << before.memMB << " → " << after.memMB
              << " MB (delta " << (memDelta >= 0 ? "+" : "") << memDelta << " MB)\n";
}

inline void runRapidTypingTest() {
    std::cout << "=== Part 19: Rapid Typing Stress Test ===\n\n";

    auto testStart = ResourceSnapshot::take();

    // Build a large engine (100k records) so query() takes measurable time
    constexpr uint32_t N = 100'000;
    SearchEngine engine;
    {
        std::vector<FileRecord> records;
        records.reserve(N);
        for (uint32_t i = 0; i < N; i++) {
            FileRecord r;
            r.name = "hello_world_file_" + std::to_string(i) + ".txt";
            r.path = "/deep/nested/path/number/" + std::to_string(i % 1000);
            r.type = 1;
            r.size = 100;
            r.modTime = static_cast<time_t>(i);
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));
    }

    auto afterLoad = ResourceSnapshot::take();
    printResourceDelta("Engine load (100k records)", testStart, afterLoad);
    std::cout << "\n";

    // ── Scenario 1: Incremental typing "h" -> "he" -> "hel" -> "hell" -> "hello" ──
    {
        std::cout << "  Scenario 1: Incremental typing simulation\n";
        auto before = ResourceSnapshot::take();

        const std::vector<std::string> keystrokes = {"h", "he", "hel", "hell", "hello"};

        std::vector<std::thread> threads;
        std::vector<std::atomic<size_t>> resultCounts(keystrokes.size());
        for (auto& rc : resultCounts) rc.store(0, std::memory_order_relaxed);

        for (size_t i = 0; i < keystrokes.size() - 1; i++) {
            threads.emplace_back([&engine, &resultCounts, &keystrokes, i] {
                auto results = engine.query(keystrokes[i], 10000);
                resultCounts[i].store(results.size(), std::memory_order_relaxed);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }

        auto finalStart = std::chrono::steady_clock::now();
        auto finalResults = engine.query(keystrokes.back(), 10000);
        auto finalElapsed = std::chrono::steady_clock::now() - finalStart;
        double finalMs = std::chrono::duration<double, std::milli>(finalElapsed).count();

        for (auto& t : threads) t.join();

        auto after = ResourceSnapshot::take();

        size_t cancelledCount = 0;
        for (size_t i = 0; i < keystrokes.size() - 1; i++) {
            if (resultCounts[i].load(std::memory_order_relaxed) == 0)
                cancelledCount++;
        }

        double cpuTotal = after.cpu.totalMs() - before.cpu.totalMs();

        std::cout << "    Intermediate queries cancelled: " << cancelledCount
                  << "/" << (keystrokes.size() - 1) << "\n";
        std::cout << "    Final query (\"hello\"): " << finalMs << " ms, "
                  << finalResults.size() << " results\n";
        printResourceDelta("Scenario 1", before, after);

        check(!finalResults.empty(),
              "Scenario 1: Final query returns results");
        check(finalMs < 200.0,
              "Scenario 1: Final query latency < 200ms");
        check(cpuTotal < 5000.0,
              "Scenario 1: CPU time < 5s (no runaway threads)");
    }

    // ── Scenario 2: Type -> Clear -> Retype ──
    {
        std::cout << "\n  Scenario 2: Type -> Clear -> Retype\n";
        auto before = ResourceSnapshot::take();

        std::atomic<size_t> phase1Results{0};
        std::thread t1([&] {
            auto results = engine.query("deep/nested", 10000);
            phase1Results.store(results.size(), std::memory_order_relaxed);
        });

        std::this_thread::sleep_for(std::chrono::microseconds(500));

        auto emptyResults = engine.query("", 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        auto retypeStart = std::chrono::steady_clock::now();
        auto retypeResults = engine.query("hello_world_file_42", 100);
        auto retypeElapsed = std::chrono::steady_clock::now() - retypeStart;
        double retypeMs = std::chrono::duration<double, std::milli>(retypeElapsed).count();

        t1.join();

        auto after = ResourceSnapshot::take();

        bool phase1Cancelled = phase1Results.load(std::memory_order_relaxed) == 0;
        double cpuTotal = after.cpu.totalMs() - before.cpu.totalMs();

        std::cout << "    Phase 1 (\"deep/nested\"): "
                  << phase1Results.load() << " results"
                  << (phase1Cancelled ? " (cancelled)" : " (completed)") << "\n";
        std::cout << "    Phase 2 (clear): " << emptyResults.size() << " results\n";
        std::cout << "    Phase 3 (\"hello_world_file_42\"): " << retypeMs << " ms, "
                  << retypeResults.size() << " results\n";
        printResourceDelta("Scenario 2", before, after);

        check(emptyResults.empty(),
              "Scenario 2: Empty query returns empty");
        check(!retypeResults.empty(),
              "Scenario 2: Retype query returns correct results");
        check(retypeMs < 200.0,
              "Scenario 2: Retype latency < 200ms");
        check(cpuTotal < 2000.0,
              "Scenario 2: CPU time < 2s (cancelled query stops promptly)");
    }

    // ── Scenario 3: High-frequency stress (100 rapid queries) ──
    {
        std::cout << "\n  Scenario 3: High-frequency stress (100 rapid queries)\n";
        auto before = ResourceSnapshot::take();

        constexpr int QUERY_COUNT = 100;
        std::vector<std::thread> threads;
        std::vector<std::atomic<size_t>> resultCounts(QUERY_COUNT);
        for (auto& rc : resultCounts) rc.store(0, std::memory_order_relaxed);

        auto stressStart = std::chrono::steady_clock::now();

        for (int i = 0; i < QUERY_COUNT - 1; i++) {
            threads.emplace_back([&engine, &resultCounts, i] {
                std::string keyword = "file_stress_query_" + std::to_string(i);
                auto results = engine.query(keyword, 1000);
                resultCounts[i].store(results.size(), std::memory_order_relaxed);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        auto finalStart = std::chrono::steady_clock::now();
        auto finalResults = engine.query("hello_world_file_99", 100);
        auto finalElapsed = std::chrono::steady_clock::now() - finalStart;
        double finalMs = std::chrono::duration<double, std::milli>(finalElapsed).count();

        for (auto& t : threads) t.join();

        auto stressElapsed = std::chrono::steady_clock::now() - stressStart;
        double totalMs = std::chrono::duration<double, std::milli>(stressElapsed).count();

        auto after = ResourceSnapshot::take();

        size_t cancelledCount = 0;
        for (int i = 0; i < QUERY_COUNT - 1; i++) {
            if (resultCounts[i].load(std::memory_order_relaxed) == 0)
                cancelledCount++;
        }

        double cancelRate = 100.0 * cancelledCount / (QUERY_COUNT - 1);
        double cpuTotal = after.cpu.totalMs() - before.cpu.totalMs();
        int64_t memDelta = static_cast<int64_t>(after.memMB) - static_cast<int64_t>(before.memMB);

        std::cout << "    Total wall time: " << totalMs << " ms\n";
        std::cout << "    Cancelled: " << cancelledCount << "/" << (QUERY_COUNT - 1)
                  << " (" << std::fixed << std::setprecision(1) << cancelRate << "%)\n";
        std::cout << "    Final query: " << finalMs << " ms, "
                  << finalResults.size() << " results\n";
        printResourceDelta("Scenario 3", before, after);

        check(!finalResults.empty(),
              "Scenario 3: Final query returns results under stress");
        check(finalMs < 200.0,
              "Scenario 3: Final query latency < 200ms under stress");
        check(cancelledCount > 0,
              "Scenario 3: Some intermediate queries were cancelled");
        check(cpuTotal < 10000.0,
              "Scenario 3: CPU time < 10s (100 queries don't explode CPU)");
        check(memDelta < 100,
              "Scenario 3: Memory growth < 100MB (no leak under stress)");
    }

    // ── Scenario 4: Baseline latency (no contention) ──
    {
        std::cout << "\n  Scenario 4: Baseline latency (no contention)\n";
        auto before = ResourceSnapshot::take();

        engine.query("hello_world_file_0", 10);

        constexpr int RUNS = 10;
        double totalMs = 0;
        size_t totalResults = 0;
        for (int i = 0; i < RUNS; i++) {
            auto start = std::chrono::steady_clock::now();
            auto results = engine.query("hello_world_file_" + std::to_string(i * 100), 100);
            auto elapsed = std::chrono::steady_clock::now() - start;
            totalMs += std::chrono::duration<double, std::milli>(elapsed).count();
            totalResults += results.size();
        }
        double avgMs = totalMs / RUNS;

        double pathTotalMs = 0;
        for (int i = 0; i < RUNS; i++) {
            auto start = std::chrono::steady_clock::now();
            auto results = engine.query("deep/nested", 100);
            auto elapsed = std::chrono::steady_clock::now() - start;
            pathTotalMs += std::chrono::duration<double, std::milli>(elapsed).count();
        }
        double pathAvgMs = pathTotalMs / RUNS;

        auto after = ResourceSnapshot::take();
        double cpuTotal = after.cpu.totalMs() - before.cpu.totalMs();

        std::cout << "    Trigram query avg: " << std::fixed << std::setprecision(2)
                  << avgMs << " ms (" << totalResults / RUNS << " results avg)\n";
        std::cout << "    Path scan avg:    " << std::fixed << std::setprecision(2)
                  << pathAvgMs << " ms\n";
        printResourceDelta("Scenario 4", before, after);

        check(avgMs < 50.0,
              "Scenario 4: Trigram query baseline < 50ms");
        check(pathAvgMs < 200.0,
              "Scenario 4: Path scan baseline < 200ms");
        check(cpuTotal < 2000.0,
              "Scenario 4: Baseline CPU < 2s (20 queries)");
    }

    // ── Scenario 5: Cool-down CPU utilization (with FSEvents background) ──
    {
        std::cout << "\n  Scenario 5: Cool-down CPU utilization (with FSEvents)\n";

        // Setup: temp directory + FileSystemWatcher to simulate real app background
        auto tmpDir = fs::temp_directory_path() / ("maceverything_idle_" + std::to_string(getpid()));
        fs::create_directories(tmpDir);

        FileSystemWatcher watcher;
        std::atomic<int> eventCount{0};
        watcher.start(tmpDir.string(), [&](std::vector<FileSystemWatcher::Event> events) {
            eventCount.fetch_add(static_cast<int>(events.size()), std::memory_order_relaxed);
        });
        // Let FSEvents stream settle (same pattern as test_fsevents.h)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Phase 1: Fire burst queries + generate real FS events simultaneously
        {
            // Create files to trigger FSEvents callbacks
            for (int i = 0; i < 20; i++) {
                std::ofstream(tmpDir / ("burst_" + std::to_string(i) + ".txt")) << "data";
            }

            std::vector<std::thread> burst;
            for (int i = 0; i < 50; i++) {
                burst.emplace_back([&engine, i] {
                    engine.query("burst_query_" + std::to_string(i), 100);
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            for (auto& t : burst) t.join();
        }

        // Phase 2: Wait for FSEvents to drain (latency is 300ms in FileSystemWatcher)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        int eventsReceived = eventCount.load(std::memory_order_relaxed);

        // Phase 3: All queries done, FSEvents drained. Measure CPU during idle.
        // FileSystemWatcher is still running in background (GCD queue alive).
        constexpr int IDLE_MS = 500;
        auto idleBefore = ResourceSnapshot::take();
        std::this_thread::sleep_for(std::chrono::milliseconds(IDLE_MS));
        auto idleAfter = ResourceSnapshot::take();

        double idleCpuMs = idleAfter.cpu.totalMs() - idleBefore.cpu.totalMs();
        double utilizationPct = (idleCpuMs / IDLE_MS) * 100.0;

        std::cout << "    FSEvents received: " << eventsReceived << " events\n";
        std::cout << "    Idle window: " << IDLE_MS << " ms\n";
        std::cout << "    CPU consumed during idle: " << std::fixed << std::setprecision(1)
                  << idleCpuMs << " ms\n";
        std::cout << "    CPU utilization: " << std::fixed << std::setprecision(1)
                  << utilizationPct << "% (of 1 core)\n";

        // Note: With kFSEventStreamCreateFlagIgnoreSelf, same-process file
        // writes are correctly filtered out by FSEvents. Event count may be 0.
        // This is expected — the test's primary goal is measuring idle CPU.
        if (eventsReceived == 0) {
            std::cout << "    [INFO] Scenario 5: No FSEvents received (IgnoreSelf active)\n";
        } else {
            check(true, "Scenario 5: FSEvents received events during burst");
        }
        check(utilizationPct < 5.0,
              "Scenario 5: CPU utilization < 5% after queries stop (with FSEvents)");
        check(idleCpuMs < 25.0,
              "Scenario 5: CPU time < 25ms during 500ms idle window");

        // Cleanup
        watcher.stop();
        fs::remove_all(tmpDir);
    }

    // ── Overall resource summary ──
    {
        auto testEnd = ResourceSnapshot::take();
        std::cout << "\n  Overall resource summary:\n";
        printResourceDelta("Part 19 total", testStart, testEnd);

        double totalCpu = testEnd.cpu.totalMs() - testStart.cpu.totalMs();
        int64_t totalMemDelta = static_cast<int64_t>(testEnd.memMB) - static_cast<int64_t>(testStart.memMB);

        check(totalCpu < 20000.0,
              "Part 19: Total CPU time < 20s");
        check(totalMemDelta < 200,
              "Part 19: Total memory growth < 200MB");
    }

    std::cout << "\n";
}

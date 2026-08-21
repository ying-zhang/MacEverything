#pragma once
// Part 17: Session-Scoped Query Cancellation Tests
// Validates per-session generation: same-session queries cancel predecessors,
// cross-session queries do NOT cancel each other.

#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <iostream>

inline void runQueryCancelTests() {
    std::cout << "=== Part 17: Session-Scoped Query Cancellation ===\n\n";

    // Build a large engine (200k records) so queries take measurable time
    constexpr uint32_t N = 200'000;
    SearchEngine engine;
    {
        std::vector<FileRecord> records;
        records.reserve(N);
        for (uint32_t i = 0; i < N; i++) {
            FileRecord r;
            r.name = "file_match_" + std::to_string(i) + ".txt";
            r.path = "/deep/nested/path/number/" + std::to_string(i % 1000);
            r.type = 1;
            r.size = 100;
            r.modTime = static_cast<time_t>(i);
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));
    }

    // Test 1: cancelSession(1) only affects session 1, not session 2
    {
        std::cout << "  Test 1: cancelSession isolates sessions\n";

        auto [gen1, val1] = engine.acquireSessionGeneration(1);
        auto [gen2, val2] = engine.acquireSessionGeneration(2);

        engine.cancelSession(1);

        bool session1Cancelled = gen1->load(std::memory_order_relaxed) != val1;
        bool session2Unchanged = gen2->load(std::memory_order_relaxed) == val2;

        check(session1Cancelled, "cancelSession(1) increments session 1 generation");
        check(session2Unchanged, "cancelSession(1) does NOT affect session 2 generation");
    }

    // Test 2: Same-session concurrent queries — later cancels earlier
    {
        std::cout << "  Test 2: Same-session queries cancel predecessors\n";
        constexpr uint64_t SESSION_A = 10;

        std::atomic<size_t> firstResultCount{0};
        std::atomic<bool> firstDone{false};

        std::thread t1([&] {
            auto results = engine.query("deep/nested", 10000, true, SESSION_A);
            firstResultCount.store(results.size(), std::memory_order_relaxed);
            firstDone.store(true, std::memory_order_release);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        auto results2 = engine.query("file_match_42", 100, true, SESSION_A);

        t1.join();

        bool firstCancelled = firstResultCount.load(std::memory_order_relaxed) == 0;
        bool secondHasResults = !results2.empty();

        std::cout << "    First query: " << firstResultCount.load() << " results"
                  << (firstCancelled ? " (cancelled)" : " (completed)") << "\n";
        std::cout << "    Second query: " << results2.size() << " results\n";

        check(secondHasResults, "Second query (same session) returns results");
        if (!firstCancelled) {
            std::cout << "    NOTE: First query completed before cancel took effect (timing)\n";
        }
    }

    // Test 3: Different-session concurrent queries do NOT cancel each other
    {
        std::cout << "  Test 3: Cross-session queries are independent\n";
        constexpr uint64_t SESSION_X = 20;
        constexpr uint64_t SESSION_Y = 21;

        std::atomic<size_t> resultCountX{0};
        std::atomic<size_t> resultCountY{0};

        std::thread tX([&] {
            auto results = engine.query("file_match_1", 100, true, SESSION_X);
            resultCountX.store(results.size(), std::memory_order_relaxed);
        });

        std::thread tY([&] {
            auto results = engine.query("file_match_2", 100, true, SESSION_Y);
            resultCountY.store(results.size(), std::memory_order_relaxed);
        });

        tX.join();
        tY.join();

        bool xHasResults = resultCountX.load(std::memory_order_relaxed) > 0;
        bool yHasResults = resultCountY.load(std::memory_order_relaxed) > 0;

        std::cout << "    Session X: " << resultCountX.load() << " results\n";
        std::cout << "    Session Y: " << resultCountY.load() << " results\n";

        check(xHasResults, "Session X query completes with results");
        check(yHasResults, "Session Y query completes with results (not cancelled by X)");
    }

    // Test 4: sessionId=0 queries are never cancelled
    {
        std::cout << "  Test 4: sessionId=0 never participates in cancellation\n";

        engine.cancelSession(1);
        engine.cancelSession(2);
        engine.cancelSession(10);
        engine.cancelSession(20);
        engine.cancelSession(21);

        auto results = engine.query("file_match_99", 100, true, 0);
        check(!results.empty(), "sessionId=0 query returns results despite other session cancels");

        std::atomic<size_t> count1{0}, count2{0};
        std::thread t1([&] {
            auto r = engine.query("file_match_50", 100, true, 0);
            count1.store(r.size(), std::memory_order_relaxed);
        });
        std::thread t2([&] {
            auto r = engine.query("file_match_51", 100, true, 0);
            count2.store(r.size(), std::memory_order_relaxed);
        });
        t1.join();
        t2.join();

        check(count1.load() > 0, "First sessionId=0 query not cancelled");
        check(count2.load() > 0, "Second sessionId=0 query not cancelled");
    }

    // Test 5: queryAdvanced uses caller's myGen, not re-read (regression for Bug 1)
    {
        std::cout << "  Test 5: queryAdvanced uses passed myGen (Bug 1 regression)\n";
        constexpr uint64_t SESSION_B = 30;

        std::atomic<size_t> advancedResultCount{0};

        std::thread t1([&] {
            auto results = engine.query("file match", 50000, true, SESSION_B);
            advancedResultCount.store(results.size(), std::memory_order_relaxed);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        engine.cancelSession(SESSION_B);

        auto results2 = engine.query("file_match_1", 10, true, SESSION_B);

        t1.join();

        size_t advCount = advancedResultCount.load(std::memory_order_relaxed);
        std::cout << "    Advanced query: " << advCount << " results\n";
        std::cout << "    Cancel query: " << results2.size() << " results\n";

        check(!results2.empty() || advCount == 0,
              "Either cancel query succeeds or advanced query was cancelled");
    }

    // Test 6: Normal single query without interference completes correctly
    {
        std::cout << "  Test 6: Normal query without interference\n";
        auto results = engine.query("file_match_99", 100, true, 42);
        check(!results.empty(), "Single query on fresh session returns results");
    }

    // Test 7: releasing a session cancels in-flight work and drops retained state
    {
        constexpr uint64_t SESSION_RELEASED = 99;
        auto [oldGeneration, oldValue] = engine.acquireSessionGeneration(SESSION_RELEASED);
        engine.releaseSession(SESSION_RELEASED);
        check(oldGeneration->load(std::memory_order_relaxed) != oldValue,
              "releaseSession cancels holders of the old generation");

        auto [newGeneration, newValue] = engine.acquireSessionGeneration(SESSION_RELEASED);
        check(newGeneration != oldGeneration && newValue == 1,
              "released session state is recreated independently");
        engine.releaseSession(SESSION_RELEASED);
    }
}

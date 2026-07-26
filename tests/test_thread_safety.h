#pragma once
// ═══════════════════════════════════════════════════════
//  Part 5: Thread Safety Stress Test
// ═══════════════════════════════════════════════════════

static void runThreadSafetyTest() {
    std::cout << "========================================\n";
    std::cout << "  Part 5: Thread Safety Stress Test\n";
    std::cout << "========================================\n\n";

    SearchEngine engine;

    // Load initial data
    std::vector<FileRecord> initial;
    for (int i = 0; i < 10000; i++) {
        initial.push_back({"stress_" + std::to_string(i) + ".txt", "/stress", 1, (uint64_t)i, (time_t)i});
    }
    engine.loadRecords(std::move(initial));

    std::atomic<bool> running{true};
    std::atomic<uint64_t> readOps{0};
    std::atomic<uint64_t> writeOps{0};
    std::atomic<bool> errorDetected{false};

    constexpr int NUM_READERS = 4;
    constexpr int NUM_WRITERS = 2;
    constexpr int DURATION_SECS = 2;

    // Reader threads
    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_READERS; t++) {
        threads.emplace_back([&] {
            int queries = 0;
            while (running.load(std::memory_order_relaxed)) {
                try {
                    auto res = engine.query("stress_" + std::to_string(queries % 1000), 100);
                    // Access returned records
                    for (uint32_t idx : res) {
                        const auto& r = engine.getRecord(idx);
                        (void)r.name;
                    }
                    // Also read live count
                    (void)engine.liveRecordCount();
                    readOps.fetch_add(1, std::memory_order_relaxed);
                    queries++;
                } catch (...) {
                    errorDetected.store(true);
                }
            }
        });
    }

    // Writer threads
    for (int t = 0; t < NUM_WRITERS; t++) {
        threads.emplace_back([&, t] {
            int ops = 0;
            while (running.load(std::memory_order_relaxed)) {
                try {
                    std::string name = "w" + std::to_string(t) + "_" + std::to_string(ops) + ".txt";
                    std::string path = "/stress/w" + std::to_string(t);

                    // Add
                    engine.addRecord({name, path, 1, 100, 1000});
                    writeOps.fetch_add(1, std::memory_order_relaxed);

                    // Update
                    engine.updateByPath(path + "/" + name,
                                        {name + ".bak", path, 1, 200, 2000});
                    writeOps.fetch_add(1, std::memory_order_relaxed);

                    // Remove
                    engine.removeByPath(path + "/" + name + ".bak");
                    writeOps.fetch_add(1, std::memory_order_relaxed);

                    ops++;
                } catch (...) {
                    errorDetected.store(true);
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(DURATION_SECS));
    running.store(false, std::memory_order_relaxed);

    for (auto& th : threads) {
        th.join();
    }

    uint64_t totalReads = readOps.load();
    uint64_t totalWrites = writeOps.load();

    check(!errorDetected.load(), "No exceptions during concurrent access");
    check(totalReads > 0, "Reader threads performed queries");
    check(totalWrites > 0, "Writer threads performed mutations");

    std::cout << "\n    Duration:     " << DURATION_SECS << "s\n";
    std::cout << "    Readers:      " << NUM_READERS << " threads\n";
    std::cout << "    Writers:      " << NUM_WRITERS << " threads\n";
    std::cout << "    Read ops:     " << totalReads << " (" << totalReads / DURATION_SECS << " ops/s)\n";
    std::cout << "    Write ops:    " << totalWrites << " (" << totalWrites / DURATION_SECS << " ops/s)\n";
    std::cout << "    Total ops:    " << (totalReads + totalWrites) << "\n";
    std::cout << "    Live records: " << engine.liveRecordCount() << "\n\n";
}

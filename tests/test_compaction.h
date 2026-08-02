#pragma once
// ═══════════════════════════════════════════════════════
//  Part 3d: Compact Records Tests
// ═══════════════════════════════════════════════════════

static void runCompactionTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 3d: Compact Records Tests\n";
    std::cout << "========================================\n\n";

    SearchEngine engine;
    std::vector<FileRecord> records;
    for (int i = 0; i < 100; i++) {
        records.push_back({"compact_" + std::to_string(i) + ".txt", "/test", 1, (uint64_t)i, (time_t)i});
    }
    engine.loadRecords(std::move(records));

    check(engine.recordCount() == 100, "Compact: initial recordCount == 100");
    check(engine.liveRecordCount() == 100, "Compact: initial liveRecordCount == 100");

    // Remove half the records
    for (int i = 0; i < 50; i++) {
        engine.removeByPath("/test/compact_" + std::to_string(i) + ".txt");
    }
    check(engine.liveRecordCount() == 50, "Compact: after removing 50, liveRecordCount == 50");
    check(engine.recordCount() == 100, "Compact: after removing 50, recordCount still 100 (tombstones)");

    // Compact
    engine.compactRecords();
    check(engine.recordCount() == 50, "Compact: after compaction, recordCount == 50");
    check(engine.liveRecordCount() == 50, "Compact: after compaction, liveRecordCount == 50");

    // Verify remaining records are still queryable
    auto res = engine.query("compact_50");
    check(res.size() == 1, "Compact: compact_50.txt still queryable after compaction");
    check(engine.getRecord(res[0]).name == "compact_50.txt", "Compact: correct record data after compaction");

    // Verify removed records are gone
    res = engine.query("compact_0");
    check(res.empty(), "Compact: compact_0.txt not queryable after compaction");

    // Compact when nothing to compact
    engine.compactRecords();
    check(engine.recordCount() == 50, "Compact: no-op compaction preserves recordCount");

    // Exercise repeated COW swaps while readers validate generation-stable results.
    {
        SearchEngine concurrentEngine;
        std::vector<FileRecord> concurrentRecords;
        for (int i = 0; i < 100; ++i) {
            concurrentRecords.push_back({"survivor_marker_" + std::to_string(i) + ".txt",
                                         "/stable", 1, 1, 1});
        }
        for (int i = 0; i < 4'000; ++i) {
            concurrentRecords.push_back({"churn_" + std::to_string(i) + ".dat",
                                         "/mutable", 1, 1, 1});
        }
        concurrentEngine.loadRecords(std::move(concurrentRecords));

        std::atomic<bool> stopReaders{false};
        std::atomic<bool> inconsistent{false};
        std::atomic<uint64_t> stableReads{0};
        std::vector<std::thread> readers;
        for (int threadIndex = 0; threadIndex < 4; ++threadIndex) {
            readers.emplace_back([&] {
                while (!stopReaders.load(std::memory_order_acquire)) {
                    uint64_t generation = concurrentEngine.compactionGeneration();
                    auto indices = concurrentEngine.query("survivor_marker", 200);
                    size_t visited = 0;
                    bool validNames = true;
                    bool stable = concurrentEngine.forEachRecordWithPathIfGeneration(
                        indices, generation,
                        [&](uint32_t, const FileRecord& record, const std::string&) {
                            ++visited;
                            if (record.name.rfind("survivor_marker_", 0) != 0) validNames = false;
                        });
                    if (stable) {
                        stableReads.fetch_add(1, std::memory_order_relaxed);
                        if (!validNames || visited != 100) {
                            inconsistent.store(true, std::memory_order_release);
                        }
                    }
                }
            });
        }

        for (int round = 0; round < 6; ++round) {
            for (int i = 0; i < 500; ++i) {
                FileRecord replacement{"churn_" + std::to_string(i) + ".dat",
                                       "/mutable", 1, static_cast<uint64_t>(round + 2), round + 2};
                concurrentEngine.updateByPath("/mutable/churn_" + std::to_string(i) + ".dat",
                                              std::move(replacement));
            }
            concurrentEngine.compactRecords();
        }
        stopReaders.store(true, std::memory_order_release);
        for (auto& reader : readers) reader.join();

        check(stableReads.load(std::memory_order_relaxed) > 0,
              "Compact concurrent stress completed stable reader snapshots");
        check(!inconsistent.load(std::memory_order_acquire),
              "Compact concurrent stress preserved query consistency");
    }

    std::cout << "\n";
}

#pragma once
// Part 7d: H-1 — IndexPersistence wal_ data race (P0-2)
// Verifies that concurrent compact() + attachWAL() operations on IndexPersistence
// don't produce data races on the wal_ shared_ptr.

static void runWalRaceIndexPersistenceTest() {
    std::cout << "═══ Part 7d: IndexPersistence WAL Race Safety ═══\n\n";

    std::string tmpDir = "/tmp/maceverything_h1_test_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    auto engine = std::make_shared<SearchEngine>();
    // Add some records so compact() has data to work with
    std::vector<FileRecord> records;
    for (int i = 0; i < 100; i++) {
        records.push_back({
            "file" + std::to_string(i) + ".txt",
            "/test/dir",
            1,
            static_cast<uint64_t>(i * 100),
            static_cast<time_t>(1000000 + i)
        });
    }
    engine->loadRecords(std::move(records));

    std::string basePath = tmpDir + "/idx.bin";
    std::string walPath = tmpDir + "/idx.wal";
    auto persistence = std::make_shared<IndexPersistence>(
        engine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
    persistence->attachWAL();

    FileRecord seed;
    seed.name = "wal-seed.txt";
    seed.path = "/test/dir";
    seed.type = 1;
    seed.size = 1;
    seed.modTime = time(nullptr);
    engine->addRecord(std::move(seed));

    // Test 1: Concurrent compact() calls should not race on wal_
    std::atomic<bool> stop{false};
    std::atomic<int> compactCount{0};

    std::vector<std::thread> compactors;
    for (int t = 0; t < 3; t++) {
        compactors.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                persistence->compact(static_cast<uint64_t>(
                    compactCount.load(std::memory_order_relaxed)), /*force=*/true);
                compactCount.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    // Let compactors run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : compactors) t.join();

    check(compactCount.load() > 0, "H1: Concurrent compacts completed without crash");
    size_t activeWalCount = 0;
    for (const auto& entry : fs::directory_iterator(tmpDir)) {
        const auto name = entry.path().filename().string();
        if (name == "idx.wal" || name.rfind("idx.wal.seg.", 0) == 0) activeWalCount++;
    }
    check(activeWalCount == 1, "H1: Concurrent compacts retain exactly one active WAL");

    // Test 2: attachWAL + compact() interleaving
    persistence->attachWAL();
    persistence->compact(999, /*force=*/true);
    check(engine->liveRecordCount() == 101, "H1: attachWAL + compact interleaving preserved all records");

    persistence.reset();
    fs::remove_all(tmpDir);
    std::cout << "\n";
}

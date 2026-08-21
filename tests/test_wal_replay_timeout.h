#pragma once
// Part 14: WAL Batch Replay (formerly WAL Replay Timeout)
// Tests the batch WAL replay path that replaced per-entry replay.

static void runWalReplayTimeoutTest() {
    std::cout << "═══ Part 14: WAL Batch Replay ═══\n\n";

    std::string tmpDir = "/tmp/maceverything_wal_timeout_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string basePath = tmpDir + "/index.bin";
    std::string walPath = tmpDir + "/index.wal";

    // Create an engine with some records and save
    auto engine = std::make_shared<SearchEngine>();
    {
        std::vector<FileRecord> records;
        for (int i = 0; i < 100; i++) {
            records.push_back({"file" + std::to_string(i) + ".txt", "/tmp", 1, 100, time(nullptr), static_cast<uint64_t>(i + 1), 1});
        }
        engine->loadRecords(std::move(records));
        IndexMetadata meta;
        meta.lastEventId = 42;
        engine->saveToFile(basePath, meta);
    }

    // Write a WAL with Add entries
    {
        IndexWAL wal;
        wal.open(walPath);
        for (int i = 0; i < 1000; i++) {
            FileRecord r{"walfile" + std::to_string(i) + ".txt", "/tmp/wal", 1, 50, time(nullptr), static_cast<uint64_t>(10000 + i), 1};
            wal.append(WALOp::Add, "/tmp/wal/walfile" + std::to_string(i) + ".txt", r);
        }
        wal.close();
    }

    // Test: batch WAL replay merges correctly
    {
        auto testEngine = std::make_shared<SearchEngine>();
        IndexPersistence persistence(testEngine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        uint64_t eventId = persistence.load();
        check(eventId == 42, "C14: WAL batch replay succeeded, eventId preserved");
        check(testEngine->liveRecordCount() == 1100, "C14: All records loaded (100 base + 1000 WAL)");
    }

    // Test: verify loadRecords({}) still works for engine reset
    {
        auto testEngine = std::make_shared<SearchEngine>();
        std::vector<FileRecord> records;
        records.push_back({"test.txt", "/tmp", 1, 100, time(nullptr), 1, 1});
        testEngine->loadRecords(std::move(records));
        check(testEngine->liveRecordCount() == 1, "C14: Engine has 1 record before reset");
        testEngine->loadRecords({}); // reset
        check(testEngine->liveRecordCount() == 0, "C14: Engine reset via loadRecords({}) works");
    }

    fs::remove_all(tmpDir);
    std::cout << "\n";
}

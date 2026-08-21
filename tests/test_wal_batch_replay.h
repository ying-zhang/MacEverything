#pragma once
// Part 27: WAL Batch Replay — correctness and performance tests
// Verifies that batch merge of WAL entries (Add/Remove/Update) into base
// records produces the same result as sequential replay, and that large
// WALs replay within acceptable time.

static void runWalBatchReplayTests() {
    std::cout << "═══ Part 27: WAL Batch Replay ═══\n\n";

    std::string tmpDir = "/tmp/maceverything_wal_batch_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string basePath = tmpDir + "/index.bin";
    std::string walPath  = tmpDir + "/index.wal";

    // Helper: clean up base and WAL files between sub-tests
    auto cleanup = [&]() {
        fs::remove(basePath);
        fs::remove(walPath);
        fs::remove(pagesPathFor(basePath));
        fs::remove(ptablePathFor(basePath));
        fs::remove(basePath + ".v6");
        fs::remove(basePath + ".v6.tmp");
    };

    // ── Test 1: Add entries merge correctly ──
    {
        cleanup();
        auto engine = std::make_shared<SearchEngine>();
        std::vector<FileRecord> base;
        for (int i = 0; i < 50; i++) {
            base.push_back({"base" + std::to_string(i) + ".txt", "/data", 1, 100,
                            time(nullptr), static_cast<uint64_t>(i + 1), 1});
        }
        engine->loadRecords(std::move(base));
        IndexMetadata meta;
        meta.lastEventId = 10;
        engine->saveToFile(basePath, meta);

        IndexWAL wal;
        wal.open(walPath);
        for (int i = 0; i < 30; i++) {
            FileRecord r{"added" + std::to_string(i) + ".txt", "/data/new", 1, 200,
                         time(nullptr), static_cast<uint64_t>(1000 + i), 1};
            wal.append(WALOp::Add, "/data/new/added" + std::to_string(i) + ".txt", r);
        }
        wal.close();

        auto testEngine = std::make_shared<SearchEngine>();
        IndexPersistence persistence(testEngine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        uint64_t eventId = persistence.load();
        check(eventId == 10, "C27: Add-only WAL preserves eventId");
        check(testEngine->liveRecordCount() == 80, "C27: Add-only WAL: 50 base + 30 added = 80");
    }

    // ── Test 2: Remove entries work correctly ──
    {
        cleanup();
        auto engine = std::make_shared<SearchEngine>();
        std::vector<FileRecord> base;
        for (int i = 0; i < 50; i++) {
            base.push_back({"file" + std::to_string(i) + ".txt", "/rem", 1, 100,
                            time(nullptr), static_cast<uint64_t>(i + 1), 1});
        }
        engine->loadRecords(std::move(base));
        IndexMetadata meta;
        meta.lastEventId = 20;
        engine->saveToFile(basePath, meta);

        IndexWAL wal;
        wal.open(walPath);
        for (int i = 0; i < 10; i++) {
            wal.append(WALOp::Remove, "/rem/file" + std::to_string(i) + ".txt");
        }
        wal.close();

        auto testEngine = std::make_shared<SearchEngine>();
        IndexPersistence persistence(testEngine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        persistence.load();
        check(testEngine->liveRecordCount() == 40, "C27: Remove WAL: 50 - 10 = 40");

        // Verify removed files are not searchable
        auto results = testEngine->query("file0.txt", 100);
        bool foundRemoved = false;
        for (uint32_t idx : results) {
            auto rec = testEngine->getRecord(idx);
            if (rec.name == "file0.txt") { foundRemoved = true; break; }
        }
        check(!foundRemoved, "C27: Removed file0.txt not in search results");
    }

    // ── Test 3: Update entries replace existing records ──
    {
        cleanup();
        auto engine = std::make_shared<SearchEngine>();
        std::vector<FileRecord> base;
        for (int i = 0; i < 20; i++) {
            base.push_back({"doc" + std::to_string(i) + ".txt", "/upd", 1, 100,
                            time(nullptr), static_cast<uint64_t>(i + 1), 1});
        }
        engine->loadRecords(std::move(base));
        IndexMetadata meta;
        meta.lastEventId = 30;
        engine->saveToFile(basePath, meta);

        IndexWAL wal;
        wal.open(walPath);
        for (int i = 0; i < 5; i++) {
            FileRecord r{"doc" + std::to_string(i) + ".txt", "/upd", 1, 999,
                         time(nullptr), static_cast<uint64_t>(i + 1), 1};
            wal.append(WALOp::Update, "/upd/doc" + std::to_string(i) + ".txt", r);
        }
        wal.close();

        auto testEngine = std::make_shared<SearchEngine>();
        IndexPersistence persistence(testEngine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        persistence.load();
        check(testEngine->liveRecordCount() == 20, "C27: Update WAL preserves record count (20)");

        auto results = testEngine->query("doc0.txt", 10);
        bool foundUpdated = false;
        for (uint32_t idx : results) {
            auto rec = testEngine->getRecord(idx);
            if (rec.name == "doc0.txt" && rec.size == 999) { foundUpdated = true; break; }
        }
        check(foundUpdated, "C27: Updated doc0.txt has new size=999");
    }

    // ── Test 4: Mixed Add/Remove/Update ──
    {
        cleanup();
        auto engine = std::make_shared<SearchEngine>();
        std::vector<FileRecord> base;
        for (int i = 0; i < 100; i++) {
            base.push_back({"mix" + std::to_string(i) + ".txt", "/mix", 1, 100,
                            time(nullptr), static_cast<uint64_t>(i + 1), 1});
        }
        engine->loadRecords(std::move(base));
        IndexMetadata meta;
        meta.lastEventId = 40;
        engine->saveToFile(basePath, meta);

        IndexWAL wal;
        wal.open(walPath);
        for (int i = 0; i < 20; i++) {
            wal.append(WALOp::Remove, "/mix/mix" + std::to_string(i) + ".txt");
        }
        for (int i = 20; i < 30; i++) {
            FileRecord r{"mix" + std::to_string(i) + ".txt", "/mix", 1, 555,
                         time(nullptr), static_cast<uint64_t>(i + 1), 1};
            wal.append(WALOp::Update, "/mix/mix" + std::to_string(i) + ".txt", r);
        }
        for (int i = 0; i < 15; i++) {
            FileRecord r{"new" + std::to_string(i) + ".txt", "/mix/sub", 1, 300,
                         time(nullptr), static_cast<uint64_t>(2000 + i), 1};
            wal.append(WALOp::Add, "/mix/sub/new" + std::to_string(i) + ".txt", r);
        }
        wal.close();

        auto testEngine = std::make_shared<SearchEngine>();
        IndexPersistence persistence(testEngine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        persistence.load();
        // 100 - 20 removed + 15 added = 95 (updates don't change count)
        check(testEngine->liveRecordCount() == 95, "C27: Mixed WAL: 100 - 20 + 15 = 95");
    }

    // ── Test 5: Idempotent Add (duplicate path) ──
    {
        cleanup();
        auto engine = std::make_shared<SearchEngine>();
        std::vector<FileRecord> base;
        base.push_back({"exists.txt", "/dup", 1, 100, time(nullptr), 1, 1});
        engine->loadRecords(std::move(base));
        IndexMetadata meta;
        meta.lastEventId = 50;
        engine->saveToFile(basePath, meta);

        IndexWAL wal;
        wal.open(walPath);
        FileRecord r{"exists.txt", "/dup", 1, 777, time(nullptr), 1, 1};
        wal.append(WALOp::Add, "/dup/exists.txt", r);
        wal.close();

        auto testEngine = std::make_shared<SearchEngine>();
        IndexPersistence persistence(testEngine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        persistence.load();
        check(testEngine->liveRecordCount() == 1, "C27: Idempotent Add keeps count at 1");

        auto results = testEngine->query("exists.txt", 10);
        bool sizeUpdated = false;
        for (uint32_t idx : results) {
            auto rec = testEngine->getRecord(idx);
            if (rec.name == "exists.txt" && rec.size == 777) { sizeUpdated = true; break; }
        }
        check(sizeUpdated, "C27: Idempotent Add updates existing record size to 777");
    }

    // ── Test 6: Remove non-existent path is a no-op ──
    {
        cleanup();
        auto engine = std::make_shared<SearchEngine>();
        std::vector<FileRecord> base;
        base.push_back({"only.txt", "/nop", 1, 100, time(nullptr), 1, 1});
        engine->loadRecords(std::move(base));
        IndexMetadata meta;
        meta.lastEventId = 60;
        engine->saveToFile(basePath, meta);

        IndexWAL wal;
        wal.open(walPath);
        wal.append(WALOp::Remove, "/nop/nonexistent.txt");
        wal.close();

        auto testEngine = std::make_shared<SearchEngine>();
        IndexPersistence persistence(testEngine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        persistence.load();
        check(testEngine->liveRecordCount() == 1, "C27: Remove non-existent path is a no-op");
    }

    // ── Test 7: Empty base + WAL-only ──
    {
        std::string emptyBase = tmpDir + "/nobase.bin";
        std::string walOnlyPath = tmpDir + "/walonly.wal";
        fs::remove(emptyBase);
        fs::remove(walOnlyPath);
        fs::remove(pagesPathFor(emptyBase));
        fs::remove(ptablePathFor(emptyBase));

        IndexWAL wal;
        wal.open(walOnlyPath);
        for (int i = 0; i < 50; i++) {
            FileRecord r{"fresh" + std::to_string(i) + ".txt", "/fresh", 1, 200,
                         time(nullptr), static_cast<uint64_t>(i + 1), 1};
            wal.append(WALOp::Add, "/fresh/fresh" + std::to_string(i) + ".txt", r);
        }
        wal.close();

        auto testEngine = std::make_shared<SearchEngine>();
        IndexPersistence persistence(testEngine, emptyBase, walOnlyPath, pagesPathFor(emptyBase), ptablePathFor(emptyBase), emptyBase + ".v6");
        uint64_t eventId = persistence.load();
        check(eventId == 0, "C27: No base file returns eventId=0");
        check(testEngine->liveRecordCount() == 50, "C27: WAL-only loads 50 records");
    }

    // ── Test 8: Case-insensitive matching for Remove/Update ──
    {
        cleanup();
        auto engine = std::make_shared<SearchEngine>();
        std::vector<FileRecord> base;
        base.push_back({"CaseFile.TXT", "/Case", 1, 100, time(nullptr), 1, 1});
        engine->loadRecords(std::move(base));
        IndexMetadata meta;
        meta.lastEventId = 70;
        engine->saveToFile(basePath, meta);

        IndexWAL wal;
        wal.open(walPath);
        wal.append(WALOp::Remove, "/case/casefile.txt");
        wal.close();

        auto testEngine = std::make_shared<SearchEngine>();
        IndexPersistence persistence(testEngine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        persistence.load();
        check(testEngine->liveRecordCount() == 0, "C27: Case-insensitive Remove works");
    }

    // ── Test 9: Large WAL performance (10K entries) ──
    {
        cleanup();
        auto engine = std::make_shared<SearchEngine>();
        std::vector<FileRecord> base;
        for (int i = 0; i < 5000; i++) {
            base.push_back({"perf" + std::to_string(i) + ".txt", "/perf", 1, 100,
                            time(nullptr), static_cast<uint64_t>(i + 1), 1});
        }
        engine->loadRecords(std::move(base));
        IndexMetadata meta;
        meta.lastEventId = 80;
        engine->saveToFile(basePath, meta);

        IndexWAL wal;
        wal.open(walPath);
        for (int i = 0; i < 5000; i++) {
            FileRecord r{"walperf" + std::to_string(i) + ".txt", "/perf/wal", 1, 200,
                         time(nullptr), static_cast<uint64_t>(50000 + i), 1};
            wal.append(WALOp::Add, "/perf/wal/walperf" + std::to_string(i) + ".txt", r);
        }
        for (int i = 0; i < 2500; i++) {
            wal.append(WALOp::Remove, "/perf/perf" + std::to_string(i) + ".txt");
        }
        for (int i = 2500; i < 5000; i++) {
            FileRecord r{"perf" + std::to_string(i) + ".txt", "/perf", 1, 888,
                         time(nullptr), static_cast<uint64_t>(i + 1), 1};
            wal.append(WALOp::Update, "/perf/perf" + std::to_string(i) + ".txt", r);
        }
        wal.close();

        auto testEngine = std::make_shared<SearchEngine>();
        IndexPersistence persistence(testEngine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");

        auto start = std::chrono::steady_clock::now();
        persistence.load();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        // 5000 base - 2500 removed + 5000 added = 7500
        check(testEngine->liveRecordCount() == 7500,
              "C27: Large WAL: 5000 - 2500 + 5000 = 7500");
        check(elapsed < 5000,
              "C27: 10K WAL entries replayed in <5s (batch mode)");
        std::cout << "    [INFO] 10K WAL batch replay: " << elapsed << "ms\n";
    }

    // ── Test 10: Batch replay matches sequential replay (equivalence) ──
    {
        cleanup();
        auto engine = std::make_shared<SearchEngine>();
        std::vector<FileRecord> base;
        for (int i = 0; i < 30; i++) {
            base.push_back({"eq" + std::to_string(i) + ".txt", "/eq", 1, 100,
                            time(nullptr), static_cast<uint64_t>(i + 1), 1});
        }
        engine->loadRecords(std::move(base));
        IndexMetadata meta;
        meta.lastEventId = 90;
        engine->saveToFile(basePath, meta);

        IndexWAL wal;
        wal.open(walPath);
        for (int i = 0; i < 5; i++) {
            wal.append(WALOp::Remove, "/eq/eq" + std::to_string(i) + ".txt");
        }
        for (int i = 5; i < 10; i++) {
            FileRecord r{"eq" + std::to_string(i) + ".txt", "/eq", 1, 333,
                         time(nullptr), static_cast<uint64_t>(i + 1), 1};
            wal.append(WALOp::Update, "/eq/eq" + std::to_string(i) + ".txt", r);
        }
        for (int i = 0; i < 10; i++) {
            FileRecord r{"eqnew" + std::to_string(i) + ".txt", "/eq/new", 1, 444,
                         time(nullptr), static_cast<uint64_t>(5000 + i), 1};
            wal.append(WALOp::Add, "/eq/new/eqnew" + std::to_string(i) + ".txt", r);
        }
        wal.close();

        // Batch replay (our new code path)
        auto batchEngine = std::make_shared<SearchEngine>();
        IndexPersistence batchPersist(batchEngine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        batchPersist.load();

        // Sequential replay (manual simulation)
        auto seqEngine = std::make_shared<SearchEngine>();
        seqEngine->loadFromFile(basePath, (uint64_t*)nullptr);
        auto entries = IndexWAL::readAll(walPath);
        for (auto& entry : entries) {
            switch (entry.op) {
                case WALOp::Add:
                    seqEngine->addRecord(std::move(entry.record));
                    break;
                case WALOp::Remove:
                    seqEngine->removeByPath(entry.fullPath);
                    break;
                case WALOp::Update:
                    seqEngine->updateByPath(entry.fullPath, std::move(entry.record));
                    break;
            }
        }

        check(batchEngine->liveRecordCount() == seqEngine->liveRecordCount(),
              "C27: Batch vs sequential: same live record count");

        // Verify search results match for a few queries
        bool searchMatch = true;
        for (const auto& kw : {"eq5", "eqnew", "eq0"}) {
            auto batchResults = batchEngine->query(kw, 100);
            auto seqResults = seqEngine->query(kw, 100);
            if (batchResults.size() != seqResults.size()) {
                searchMatch = false;
                break;
            }
        }
        check(searchMatch, "C27: Batch vs sequential: search results match");
    }

    fs::remove_all(tmpDir);
    std::cout << "\n";
}

#pragma once
// ═══════════════════════════════════════════════════════
//  Part 61 — FSEvents batch mutation tests
// ═══════════════════════════════════════════════════════
//
// Validates SearchEngine::batchMutate() which applies multiple
// MutationOps (REMOVE / UPDATE) under a single unique_lock,
// reducing lock contention from FSEvents callbacks.

static void runFSEventsBatchTests() {
    std::cout << "\n═══════════════════════════════════════════════════════\n";
    std::cout << "  Part 61 — FSEvents batch mutation\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";

    // ── Test 1: batchMutate with empty ops — no side effects ──
    {
        std::cout << "  Test 1: batchMutate empty ops\n";
        SearchEngine engine;
        FileRecord r; r.name = "a.txt"; r.path = "/tmp"; r.type = 1; r.size = 10; r.modTime = 1000;
        engine.updateByPath("/tmp/a.txt", std::move(r));
        check(engine.liveRecordCount() == 1u, "pre-check: 1 record");

        std::vector<SearchEngine::MutationOp> ops;
        engine.batchMutate(std::move(ops));
        check(engine.liveRecordCount() == 1u, "after empty batchMutate: still 1 record");
    }

    // ── Test 2: batchMutate mixed REMOVE + UPDATE ──
    {
        std::cout << "\n  Test 2: batchMutate mixed REMOVE + UPDATE\n";
        SearchEngine engine;

        // Add 3 records
        for (int i = 0; i < 3; i++) {
            FileRecord r;
            r.name = "file" + std::to_string(i) + ".txt";
            r.path = "/data";
            r.type = 1; r.size = 100; r.modTime = 2000 + i;
            engine.updateByPath("/data/" + r.name, std::move(r));
        }
        check(engine.liveRecordCount() == 3u, "pre-check: 3 records");

        // Batch: remove file0.txt, update file1.txt (new size), add file3.txt
        std::vector<SearchEngine::MutationOp> ops;
        ops.push_back({SearchEngine::MutationOp::REMOVE, "/data/file0.txt", {}});

        FileRecord updated;
        updated.name = "file1.txt"; updated.path = "/data";
        updated.type = 1; updated.size = 999; updated.modTime = 3000;
        ops.push_back({SearchEngine::MutationOp::UPDATE, "/data/file1.txt", std::move(updated)});

        FileRecord newRec;
        newRec.name = "file3.txt"; newRec.path = "/data";
        newRec.type = 1; newRec.size = 50; newRec.modTime = 4000;
        ops.push_back({SearchEngine::MutationOp::UPDATE, "/data/file3.txt", std::move(newRec)});

        engine.batchMutate(std::move(ops));

        check(engine.liveRecordCount() == 3u, "3 live after batch");

        // file0 removed
        check(engine.indexForPath("/data/file0.txt") == UINT32_MAX, "file0 removed");
        // file1 updated
        uint32_t idx1 = engine.indexForPath("/data/file1.txt");
        check(idx1 != UINT32_MAX, "file1 exists");
        if (idx1 != UINT32_MAX) {
            auto rec1 = engine.getRecord(idx1);
            check(rec1.size == 999u, "file1 updated size == 999");
        }
        // file2 unchanged
        check(engine.indexForPath("/data/file2.txt") != UINT32_MAX, "file2 still exists");
        // file3 added
        uint32_t idx3 = engine.indexForPath("/data/file3.txt");
        check(idx3 != UINT32_MAX, "file3 exists");
        if (idx3 != UINT32_MAX) {
            auto rec3 = engine.getRecord(idx3);
            check(rec3.size == 50u, "file3 size == 50");
        }
    }

    // ── Test 3: batchMutate + query correctness ──
    {
        std::cout << "\n  Test 3: batchMutate + query correctness\n";
        SearchEngine engine;

        // Add records with distinctive names
        FileRecord r1; r1.name = "alpha_report.txt"; r1.path = "/docs"; r1.type = 1; r1.size = 10; r1.modTime = 1000;
        engine.updateByPath("/docs/alpha_report.txt", std::move(r1));

        FileRecord r2; r2.name = "beta_report.txt"; r2.path = "/docs"; r2.type = 1; r2.size = 20; r2.modTime = 1001;
        engine.updateByPath("/docs/beta_report.txt", std::move(r2));

        FileRecord r3; r3.name = "gamma_notes.txt"; r3.path = "/docs"; r3.type = 1; r3.size = 30; r3.modTime = 1002;
        engine.updateByPath("/docs/gamma_notes.txt", std::move(r3));

        // Batch: remove alpha, add delta_report
        std::vector<SearchEngine::MutationOp> ops;
        ops.push_back({SearchEngine::MutationOp::REMOVE, "/docs/alpha_report.txt", {}});

        FileRecord r4; r4.name = "delta_report.txt"; r4.path = "/docs"; r4.type = 1; r4.size = 40; r4.modTime = 1003;
        ops.push_back({SearchEngine::MutationOp::UPDATE, "/docs/delta_report.txt", std::move(r4)});

        engine.batchMutate(std::move(ops));

        // Query "report" should find beta_report and delta_report (not alpha_report)
        auto results = engine.query("report", 10, false);
        std::set<std::string> names;
        for (auto idx : results) {
            names.insert(engine.getRecord(idx).name);
        }
        check(names.count("beta_report.txt") > 0, "beta_report found in query");
        check(names.count("delta_report.txt") > 0, "delta_report found in query");
        check(names.count("alpha_report.txt") == 0, "alpha_report NOT found in query");
        check(results.size() == 2u, "exactly 2 report matches");
    }

    // ── Test 4: batchMutate remove non-existent path — no crash ──
    {
        std::cout << "\n  Test 4: batchMutate remove non-existent path\n";
        SearchEngine engine;
        FileRecord r; r.name = "keep.txt"; r.path = "/tmp"; r.type = 1; r.size = 1; r.modTime = 100;
        engine.updateByPath("/tmp/keep.txt", std::move(r));

        std::vector<SearchEngine::MutationOp> ops;
        ops.push_back({SearchEngine::MutationOp::REMOVE, "/tmp/nonexistent.txt", {}});
        engine.batchMutate(std::move(ops));

        check(engine.liveRecordCount() == 1u, "keep.txt still live after removing nonexistent");
    }

    // ── Test 5: concurrent batchMutate and query (thread safety) ──
    {
        std::cout << "\n  Test 5: concurrent batchMutate + query\n";
        SearchEngine engine;

        // Seed some records
        for (int i = 0; i < 100; i++) {
            FileRecord r;
            r.name = "item_" + std::to_string(i) + ".dat";
            r.path = "/pool";
            r.type = 1; r.size = i; r.modTime = 5000 + i;
            engine.updateByPath("/pool/" + r.name, std::move(r));
        }

        std::atomic<bool> done{false};
        std::atomic<int> queryCount{0};
        std::atomic<bool> queryOk{true};

        // Reader thread: queries continuously
        std::thread reader([&]() {
            while (!done.load(std::memory_order_relaxed)) {
                auto results = engine.query("item", 200, false);
                if (results.empty()) {
                    queryOk.store(false, std::memory_order_relaxed);
                }
                queryCount.fetch_add(1, std::memory_order_relaxed);
            }
        });

        // Writer thread: batch mutations
        for (int batch = 0; batch < 10; batch++) {
            std::vector<SearchEngine::MutationOp> ops;
            for (int j = 0; j < 10; j++) {
                int idx = batch * 10 + j;
                // Remove old
                ops.push_back({SearchEngine::MutationOp::REMOVE,
                               "/pool/item_" + std::to_string(idx) + ".dat", {}});
                // Add replacement
                FileRecord r;
                r.name = "item_" + std::to_string(idx) + "_v2.dat";
                r.path = "/pool";
                r.type = 1; r.size = idx + 1000; r.modTime = 9000 + idx;
                ops.push_back({SearchEngine::MutationOp::UPDATE,
                               "/pool/" + r.name, std::move(r)});
            }
            engine.batchMutate(std::move(ops));
        }

        done.store(true, std::memory_order_relaxed);
        reader.join();

        check(engine.liveRecordCount() == 100u, "still 100 live records after concurrent ops");
        check(queryOk.load(), "queries always found results during concurrent ops");
        check(queryCount.load() > 0, "queries actually ran");

        std::cout << "    (queries executed: " << queryCount.load() << ")\n";
    }

    // ── Test 6: batchMutate WAL logging ──
    {
        std::cout << "\n  Test 6: batchMutate WAL logging\n";
        std::string walPath = "/tmp/test_batchmutate_wal_" + std::to_string(getpid()) + ".wal";

        SearchEngine engine;
        auto wal = std::make_shared<IndexWAL>();
        wal->open(walPath);
        engine.attachWAL(wal);

        FileRecord r; r.name = "wal_test.txt"; r.path = "/wal"; r.type = 1; r.size = 1; r.modTime = 100;
        engine.updateByPath("/wal/wal_test.txt", std::move(r));

        // batchMutate: remove + add
        std::vector<SearchEngine::MutationOp> ops;
        ops.push_back({SearchEngine::MutationOp::REMOVE, "/wal/wal_test.txt", {}});
        FileRecord r2; r2.name = "wal_new.txt"; r2.path = "/wal"; r2.type = 1; r2.size = 2; r2.modTime = 200;
        ops.push_back({SearchEngine::MutationOp::UPDATE, "/wal/wal_new.txt", std::move(r2)});
        engine.batchMutate(std::move(ops));

        engine.detachWAL();
        wal->close();

        // Replay WAL entries
        auto entries = IndexWAL::readAll(walPath);
        // Should have at least 3 entries: initial updateByPath, remove, update from batch
        check(entries.size() >= 3, "WAL has >= 3 entries from batchMutate");

        // Check we have Remove and Update entries
        bool hasRemove = false, hasUpdate = false;
        for (const auto& e : entries) {
            if (e.op == WALOp::Remove) hasRemove = true;
            if (e.op == WALOp::Update) hasUpdate = true;
        }
        check(hasRemove, "WAL has Remove entry");
        check(hasUpdate, "WAL has Update entry");

        std::remove(walPath.c_str());
    }

    // ── Test 7: mutations remain correct across 300-op chunk boundaries ──
    {
        std::cout << "\n  Test 7: batchMutate chunk boundaries\n";
        SearchEngine engine;
        std::vector<SearchEngine::MutationOp> ops;
        ops.reserve(650);
        for (int i = 0; i < 650; ++i) {
            FileRecord record;
            record.name = "chunked_" + std::to_string(i) + ".txt";
            record.path = "/chunks";
            record.type = 1;
            record.size = static_cast<uint64_t>(i);
            record.modTime = 10'000 + i;
            ops.push_back({SearchEngine::MutationOp::UPDATE,
                           "/chunks/" + record.name, std::move(record)});
        }
        engine.batchMutate(std::move(ops));
        check(engine.liveRecordCount() == 650u,
              "650 updates survive multiple mutation chunks");
        check(engine.query("chunked_649", 10, false).size() == 1,
              "record after second chunk boundary is queryable");
    }

    std::cout << "\n";
}

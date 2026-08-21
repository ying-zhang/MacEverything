#pragma once
// Tests for SearchEngine::replayWALEntries() — in-place WAL replay (P0 optimization)

static void runWalInplaceReplayTests() {
    std::cout << "\n═══ Part 42: WAL In-Place Replay ═══\n\n";

    namespace fs = std::filesystem;
    std::string tmpDir = "/tmp/test_wal_inplace_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    // --- Test 1: Add operation inserts record and updates pathIndex_ ---
    {
        SearchEngine engine;
        // Load some initial records
        std::vector<FileRecord> initial;
        {
            FileRecord r; r.type = 1; r.name = "existing.txt";
            r.path = "/Users/test"; r.size = 100; r.modTime = 1000;
            initial.push_back(std::move(r));
        }
        engine.loadRecords(std::move(initial));
        check(engine.liveRecordCount() == 1, "T1: 1 record after loadRecords");

        // Replay a WAL Add entry
        std::vector<WALEntry> entries;
        {
            WALEntry e;
            e.op = WALOp::Add;
            e.fullPath = "/Users/test/newfile.txt";
            e.record.type = 1;
            e.record.name = "newfile.txt";
            e.record.path = "/Users/test";
            e.record.size = 200;
            e.record.modTime = 2000;
            entries.push_back(std::move(e));
        }
        engine.replayWALEntries(std::move(entries));

        check(engine.liveRecordCount() == 2, "T1: 2 records after replay Add");
        uint32_t idx = engine.indexForPath("/Users/test/newfile.txt");
        check(idx != UINT32_MAX, "T1: new record findable via indexForPath");
        FileRecord r = engine.getRecord(idx);
        check(r.name == "newfile.txt", "T1: new record has correct name");
        check(r.size == 200, "T1: new record has correct size");
    }

    // --- Test 2: Remove operation tombstones record and removes from pathIndex_ ---
    {
        SearchEngine engine;
        std::vector<FileRecord> initial;
        {
            FileRecord r; r.type = 1; r.name = "toremove.txt";
            r.path = "/Users/test"; r.size = 100; r.modTime = 1000;
            initial.push_back(std::move(r));
        }
        {
            FileRecord r; r.type = 1; r.name = "keep.txt";
            r.path = "/Users/test"; r.size = 50; r.modTime = 900;
            initial.push_back(std::move(r));
        }
        engine.loadRecords(std::move(initial));
        check(engine.liveRecordCount() == 2, "T2: 2 records initially");

        std::vector<WALEntry> entries;
        {
            WALEntry e;
            e.op = WALOp::Remove;
            e.fullPath = "/Users/test/toremove.txt";
            entries.push_back(std::move(e));
        }
        engine.replayWALEntries(std::move(entries));

        check(engine.liveRecordCount() == 1, "T2: 1 record after replay Remove");
        check(engine.indexForPath("/Users/test/toremove.txt") == UINT32_MAX,
              "T2: removed record not findable");
        check(engine.indexForPath("/Users/test/keep.txt") != UINT32_MAX,
              "T2: kept record still findable");
    }

    // --- Test 3: Update operation replaces old record ---
    {
        SearchEngine engine;
        std::vector<FileRecord> initial;
        {
            FileRecord r; r.type = 1; r.name = "update.txt";
            r.path = "/Users/test"; r.size = 100; r.modTime = 1000;
            initial.push_back(std::move(r));
        }
        engine.loadRecords(std::move(initial));

        std::vector<WALEntry> entries;
        {
            WALEntry e;
            e.op = WALOp::Update;
            e.fullPath = "/Users/test/update.txt";
            e.record.type = 1;
            e.record.name = "update.txt";
            e.record.path = "/Users/test";
            e.record.size = 999;
            e.record.modTime = 5000;
            entries.push_back(std::move(e));
        }
        engine.replayWALEntries(std::move(entries));

        check(engine.liveRecordCount() == 1, "T3: still 1 live record after Update");
        uint32_t idx = engine.indexForPath("/Users/test/update.txt");
        check(idx != UINT32_MAX, "T3: updated record findable");
        FileRecord r = engine.getRecord(idx);
        check(r.size == 999, "T3: record size updated to 999");
        check(r.modTime == 5000, "T3: record modTime updated");
    }

    // --- Test 4: Mixed operations (Add + Update + Remove) ---
    {
        SearchEngine engine;
        std::vector<FileRecord> initial;
        {
            FileRecord r; r.type = 1; r.name = "a.txt";
            r.path = "/d"; r.size = 10; r.modTime = 100;
            initial.push_back(std::move(r));
        }
        {
            FileRecord r; r.type = 1; r.name = "b.txt";
            r.path = "/d"; r.size = 20; r.modTime = 200;
            initial.push_back(std::move(r));
        }
        engine.loadRecords(std::move(initial));
        check(engine.liveRecordCount() == 2, "T4: 2 records initially");

        std::vector<WALEntry> entries;
        // Add c.txt
        {
            WALEntry e;
            e.op = WALOp::Add;
            e.fullPath = "/d/c.txt";
            e.record.type = 1; e.record.name = "c.txt";
            e.record.path = "/d"; e.record.size = 30; e.record.modTime = 300;
            entries.push_back(std::move(e));
        }
        // Update a.txt
        {
            WALEntry e;
            e.op = WALOp::Update;
            e.fullPath = "/d/a.txt";
            e.record.type = 1; e.record.name = "a.txt";
            e.record.path = "/d"; e.record.size = 111; e.record.modTime = 400;
            entries.push_back(std::move(e));
        }
        // Remove b.txt
        {
            WALEntry e;
            e.op = WALOp::Remove;
            e.fullPath = "/d/b.txt";
            entries.push_back(std::move(e));
        }
        engine.replayWALEntries(std::move(entries));

        check(engine.liveRecordCount() == 2, "T4: 2 live records after mixed ops");
        check(engine.indexForPath("/d/c.txt") != UINT32_MAX, "T4: c.txt added");
        check(engine.indexForPath("/d/b.txt") == UINT32_MAX, "T4: b.txt removed");
        uint32_t aIdx = engine.indexForPath("/d/a.txt");
        check(aIdx != UINT32_MAX, "T4: a.txt still present");
        FileRecord aRec = engine.getRecord(aIdx);
        check(aRec.size == 111, "T4: a.txt updated size");
    }

    // --- Test 5: Empty entries list is a no-op ---
    {
        SearchEngine engine;
        std::vector<FileRecord> initial;
        {
            FileRecord r; r.type = 1; r.name = "x.txt";
            r.path = "/d"; r.size = 1; r.modTime = 1;
            initial.push_back(std::move(r));
        }
        engine.loadRecords(std::move(initial));

        std::vector<WALEntry> empty;
        engine.replayWALEntries(std::move(empty));
        check(engine.liveRecordCount() == 1, "T5: no change with empty WAL entries");
        check(engine.indexForPath("/d/x.txt") != UINT32_MAX, "T5: record intact");
    }

    // --- Test 6: Trigram index updated correctly after replay ---
    {
        SearchEngine engine;
        std::vector<FileRecord> initial;
        {
            FileRecord r; r.type = 1; r.name = "report.pdf";
            r.path = "/docs"; r.size = 500; r.modTime = 100;
            initial.push_back(std::move(r));
        }
        engine.loadRecords(std::move(initial));

        // Query for "report" — should find it
        auto results1 = engine.query("report", 10);
        check(results1.size() == 1, "T6: 'report' found before replay");

        // Replay: remove report.pdf, add summary.pdf
        std::vector<WALEntry> entries;
        {
            WALEntry e;
            e.op = WALOp::Remove;
            e.fullPath = "/docs/report.pdf";
            entries.push_back(std::move(e));
        }
        {
            WALEntry e;
            e.op = WALOp::Add;
            e.fullPath = "/docs/summary.pdf";
            e.record.type = 1; e.record.name = "summary.pdf";
            e.record.path = "/docs"; e.record.size = 600; e.record.modTime = 200;
            entries.push_back(std::move(e));
        }
        engine.replayWALEntries(std::move(entries));

        // "report" should not be found anymore
        auto results2 = engine.query("report", 10);
        check(results2.empty(), "T6: 'report' not found after remove");

        // "summary" should be found
        auto results3 = engine.query("summary", 10);
        check(results3.size() == 1, "T6: 'summary' found after add");
    }

    // --- Test 7: Remove non-existent path is silently ignored ---
    {
        SearchEngine engine;
        std::vector<FileRecord> initial;
        {
            FileRecord r; r.type = 1; r.name = "only.txt";
            r.path = "/d"; r.size = 1; r.modTime = 1;
            initial.push_back(std::move(r));
        }
        engine.loadRecords(std::move(initial));

        std::vector<WALEntry> entries;
        {
            WALEntry e;
            e.op = WALOp::Remove;
            e.fullPath = "/d/nonexistent.txt";
            entries.push_back(std::move(e));
        }
        engine.replayWALEntries(std::move(entries));

        check(engine.liveRecordCount() == 1, "T7: removing nonexistent path doesn't crash");
        check(engine.indexForPath("/d/only.txt") != UINT32_MAX, "T7: existing record untouched");
    }

    fs::remove_all(tmpDir);
    std::cout << "\n  Part 42 done.\n";
}

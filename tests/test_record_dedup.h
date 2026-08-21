// test_record_dedup.h — Tests for record deduplication in SearchEngine
// Verifies that duplicate paths never produce multiple live records.

#pragma once

inline void runRecordDedupTests() {
    std::cout << "\n══════════════════════════════════════════\n";
    std::cout << "  Part 45: Record Deduplication\n";
    std::cout << "══════════════════════════════════════════\n";

    // ── Test 1: addRecord deduplicates same path ──
    {
        std::cout << "\n  --- addRecord dedup ---\n";
        SearchEngine engine;
        std::vector<FileRecord> base;
        base.push_back({"file.txt", "/dir", 1, 100, 1000});
        base.push_back({"other.txt", "/dir", 1, 200, 2000});
        engine.loadRecords(std::move(base));
        check(engine.liveRecordCount() == 2, "C45: initial liveCount == 2");

        // Add duplicate path with different size
        engine.addRecord({"file.txt", "/dir", 1, 999, 3000});
        check(engine.liveRecordCount() == 2, "C45: addRecord dup keeps liveCount == 2");

        auto results = engine.query("file.txt", 10);
        check(results.size() == 1, "C45: query returns exactly 1 result for dup path");
        auto rec = engine.getRecord(results[0]);
        check(rec.size == 999, "C45: addRecord dup uses latest record (size=999)");
    }

    // ── Test 2: loadRecords deduplicates same path ──
    {
        std::cout << "\n  --- loadRecords dedup ---\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Same path appears 3 times
        records.push_back({"dup.txt", "/dir", 1, 100, 1000});
        records.push_back({"unique.txt", "/dir", 1, 200, 2000});
        records.push_back({"dup.txt", "/dir", 1, 300, 3000});  // duplicate 1
        records.push_back({"dup.txt", "/dir", 1, 400, 4000});  // duplicate 2 (latest)
        engine.loadRecords(std::move(records));

        // Only 2 unique paths: dup.txt and unique.txt
        check(engine.liveRecordCount() == 2, "C45: loadRecords dedup: liveCount == 2 (not 4)");

        auto results = engine.query("dup.txt", 10);
        check(results.size() == 1, "C45: loadRecords dedup: query returns 1 for dup path");
        auto rec = engine.getRecord(results[0]);
        check(rec.size == 400, "C45: loadRecords dedup: latest record wins (size=400)");
    }

    // ── Test 3: compactRecords removes orphaned duplicates ──
    {
        std::cout << "\n  --- compactRecords dedup ---\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"a.txt", "/dir", 1, 100, 1000});
        records.push_back({"a.txt", "/dir", 1, 200, 2000});  // dup
        records.push_back({"b.txt", "/dir", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        check(engine.liveRecordCount() == 2, "C45: before compact: liveCount == 2");
        uint32_t totalBefore = engine.recordCount();

        auto remap = engine.compactRecords();
        check(engine.liveRecordCount() == 2, "C45: after compact: liveCount == 2");
        check(engine.recordCount() <= totalBefore,
              "C45: compaction does not increase record count");
        // After compaction, total should equal live (no tombstones or orphans)
        check(engine.recordCount() == engine.liveRecordCount(),
              "C45: after compact: recordCount == liveRecordCount (no orphans)");

        // Verify query still works
        auto results = engine.query("a.txt", 10);
        check(results.size() == 1, "C45: after compact: query returns 1 for a.txt");
        auto rec = engine.getRecord(results[0]);
        check(rec.size == 200, "C45: after compact: latest a.txt preserved (size=200)");
    }

    // ── Test 4: addRecord dedup consistency with replayWALEntries ──
    {
        std::cout << "\n  --- addRecord vs replayWAL consistency ---\n";

        // Engine A: using addRecord
        SearchEngine engineA;
        std::vector<FileRecord> base;
        base.push_back({"file.txt", "/dir", 1, 100, 1000});
        engineA.loadRecords(std::move(base));
        engineA.addRecord({"file.txt", "/dir", 1, 999, 5000});

        // Engine B: using replayWALEntries
        SearchEngine engineB;
        std::vector<FileRecord> base2;
        base2.push_back({"file.txt", "/dir", 1, 100, 1000});
        engineB.loadRecords(std::move(base2));
        std::vector<WALEntry> entries;
        WALEntry e;
        e.op = WALOp::Add;
        e.fullPath = "/dir/file.txt";
        e.record = {"file.txt", "/dir", 1, 999, 5000};
        entries.push_back(std::move(e));
        engineB.replayWALEntries(std::move(entries));

        check(engineA.liveRecordCount() == engineB.liveRecordCount(),
              "C45: addRecord and replayWAL produce same liveCount");

        auto resA = engineA.query("file.txt", 10);
        auto resB = engineB.query("file.txt", 10);
        check(resA.size() == resB.size(),
              "C45: addRecord and replayWAL produce same query result count");
        if (!resA.empty() && !resB.empty()) {
            check(engineA.getRecord(resA[0]).size == engineB.getRecord(resB[0]).size,
                  "C45: addRecord and replayWAL produce same record data");
        }
    }

    // ── Test 5: query returns no duplicate paths ──
    {
        std::cout << "\n  --- query no duplicate paths ---\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        // 50 files, 10 of which appear 3 times each = 70 records, 50 unique paths
        for (int i = 0; i < 50; i++) {
            std::string name = "test_" + std::to_string(i) + ".txt";
            records.push_back({name, "/data", 1, static_cast<uint64_t>(i * 100), static_cast<time_t>(i)});
        }
        // Add duplicates for first 10
        for (int i = 0; i < 10; i++) {
            std::string name = "test_" + std::to_string(i) + ".txt";
            records.push_back({name, "/data", 1, static_cast<uint64_t>(i * 100 + 1), static_cast<time_t>(1000 + i)});
            records.push_back({name, "/data", 1, static_cast<uint64_t>(i * 100 + 2), static_cast<time_t>(2000 + i)});
        }
        engine.loadRecords(std::move(records));

        check(engine.liveRecordCount() == 50, "C45: 70 records with 50 unique paths -> liveCount == 50");

        auto results = engine.query("test_", 100);
        // Check no duplicate paths in results
        std::set<std::string> seenPaths;
        bool hasDup = false;
        for (uint32_t idx : results) {
            auto rec = engine.getRecord(idx);
            std::string fullPath = rec.path + "/" + rec.name;
            if (seenPaths.count(fullPath)) {
                hasDup = true;
                break;
            }
            seenPaths.insert(fullPath);
        }
        check(!hasDup, "C45: query results contain no duplicate paths");
        check(results.size() == 50, "C45: query returns all 50 unique files");
    }

    // ── Test 6: addRecord multiple dups in sequence ──
    {
        std::cout << "\n  --- addRecord multiple sequential dups ---\n";
        SearchEngine engine;
        engine.loadRecords({});

        // Add same path 5 times
        for (int i = 0; i < 5; i++) {
            engine.addRecord({"repeated.txt", "/dir", 1, static_cast<uint64_t>(i * 100), static_cast<time_t>(i)});
        }
        check(engine.liveRecordCount() == 1, "C45: 5 addRecord same path -> liveCount == 1");

        auto results = engine.query("repeated.txt", 10);
        check(results.size() == 1, "C45: 5 addRecord same path -> query returns 1");
        auto rec = engine.getRecord(results[0]);
        check(rec.size == 400, "C45: 5 addRecord same path -> latest size == 400");
    }

    std::cout << "\n  Part 45 complete.\n";
}

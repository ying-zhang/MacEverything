#pragma once

static void runFlatDeltaFixTests() {
    std::cout << "\n=== Part 81: Flat+Delta Index Bug Fixes ===\n\n";

    // 81.1 estimateTrigramCost returns non-zero for delta-only trigrams
    {
        std::cout << "  [81.1] estimateTrigramCost sees delta adds\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 200; i++) {
            FileRecord r;
            r.name = "baseline_" + std::to_string(i) + ".dat";
            r.path = "/base";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        // After loadRecords, flat index is active and nameTrigramIndex_ is cleared.
        // Add a record with a unique trigram pattern only in delta.
        FileRecord unique;
        unique.name = "xqzalpha.txt";
        unique.path = "/base";
        unique.type = 1;
        unique.size = 50;
        unique.modTime = 2000;
        engine.addRecord(std::move(unique));

        // Query via structured path (slash triggers SEGMENTS mode)
        auto res = engine.query("/base/xqzalpha");
        check(res.size() >= 1, "structured query finds delta-only record 'xqzalpha'");
        bool found = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "xqzalpha.txt") found = true;
        }
        check(found, "correct record returned for delta-only trigram");
    }

    // 81.2 structured query still works for records in flat index
    {
        std::cout << "  [81.2] structured query works with flat index\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 300; i++) {
            FileRecord r;
            r.name = "generic_" + std::to_string(i) + ".log";
            r.path = "/project/logs";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        for (int i = 0; i < 5; i++) {
            FileRecord r;
            r.name = "vqwreport_" + std::to_string(i) + ".csv";
            r.path = "/project/reports";
            r.type = 1;
            r.size = 200;
            r.modTime = 2000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        auto res = engine.query("/reports/vqwreport");
        check(res.size() == 5, "structured query finds 5 flat-index records");
    }

    // 81.3 regex trigram prefilter works after flat index is built
    {
        std::cout << "  [81.3] regex trigram works with flat index\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 500; i++) {
            FileRecord r;
            r.name = "ordinary_file_" + std::to_string(i) + ".txt";
            r.path = "/data";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        for (int i = 0; i < 5; i++) {
            FileRecord r;
            r.name = "jkwtestcase_" + std::to_string(i) + ".pyc";
            r.path = "/data";
            r.type = 1;
            r.size = 200;
            r.modTime = 2000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("regex:jkwtestcase.*\\.pyc", 100, true, timing);
        check(results.size() == 5, "regex query finds 5 matches via flat index");
    }

    // 81.4 regex trigram works for records added after flat index (delta)
    {
        std::cout << "  [81.4] regex trigram finds delta-added records\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 500; i++) {
            FileRecord r;
            r.name = "filler_item_" + std::to_string(i) + ".bin";
            r.path = "/store";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        // Add records after flat index is built (go to delta)
        for (int i = 0; i < 3; i++) {
            FileRecord r;
            r.name = "zqxconfig_backup_" + std::to_string(i) + ".yml";
            r.path = "/store";
            r.type = 1;
            r.size = 50;
            r.modTime = 3000;
            engine.addRecord(std::move(r));
        }

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("regex:zqxconfig.*backup", 100, true, timing);
        check(results.size() == 3, "regex query finds 3 delta-added records");
    }

    // 81.5 completePhase2: records added during Phase 2 are findable via trigram
    // Exercises the fix: nameTrigramFlat_ must be cleared before replay so that
    // addTrigramsForRecord routes to nameTrigramIndex_ instead of delta.
    // We pre-populate flat via loadRecords, then loadRecordsV6 on the SAME engine
    // (which does NOT clear flat), then completePhase2 with new records.
    {
        std::cout << "  [81.5] Phase 2 replay preserves trigram index for new records\n";
        namespace fs = std::filesystem;
        std::string tmpDir = (fs::temp_directory_path() / "test_81_5").string()
                             + "_" + std::to_string(getpid());
        fs::create_directories(tmpDir);
        std::string v6Path = tmpDir + "/index.v6";

        auto engine = std::make_shared<SearchEngine>();

        // Step 1: loadRecords builds flat index → nameTrigramFlat_ is non-empty
        {
            std::vector<FileRecord> seed;
            for (int i = 0; i < 200; i++) {
                FileRecord r;
                r.name = "seedfile_" + std::to_string(i) + ".dat";
                r.path = "/seed";
                r.type = 1;
                r.size = 100;
                r.modTime = 1000;
                r.inode = static_cast<uint64_t>(i + 1);
                r.devId = 1;
                seed.push_back(std::move(r));
            }
            engine->loadRecords(std::move(seed));
        }

        // Step 2: write a v6 file from a separate engine
        {
            auto writer_engine = std::make_shared<SearchEngine>();
            for (int i = 0; i < 200; i++) {
                FileRecord r;
                r.name = "bulkitem_" + std::to_string(i) + ".dat";
                r.path = "/ph2dir";
                r.type = 1;
                r.size = 100;
                r.modTime = 1000;
                r.inode = static_cast<uint64_t>(i + 1);
                r.devId = 1;
                writer_engine->addRecord(std::move(r));
            }
            FlatIndexWriter writer(v6Path);
            IndexMetadata meta;
            meta.lastEventId = 1;
            check(writer.fullRewrite(*writer_engine, meta), "81.5: v6 write");
        }

        // Step 3: loadRecordsV6 on the same engine (does NOT clear nameTrigramFlat_)
        {
            FlatIndexWriter reader(v6Path);
            IndexMetadata loadedMeta;
            check(reader.load(*engine, &loadedMeta), "81.5: v6 load on engine with existing flat");
            check(engine->isPhase2Pending(), "81.5: phase2 pending after load");
        }

        // Step 4: add records during Phase 2 window
        for (int i = 0; i < 5; i++) {
            FileRecord r;
            r.name = "wqxunique_" + std::to_string(i) + ".txt";
            r.path = "/ph2dir";
            r.type = 1;
            r.size = 50;
            r.modTime = 2000;
            r.inode = static_cast<uint64_t>(300 + i);
            r.devId = 1;
            engine->addRecord(std::move(r));
        }

        // Step 5: completePhase2 — without the fix, replay would route to delta
        // which then gets cleared, losing these records from trigram index
        engine->completePhase2();
        check(!engine->isPhase2Pending(), "81.5: phase2 done");

        auto res = engine->query("wqxunique");
        check(res.size() == 5, "81.5: trigram query finds 5 Phase-2 added records");

        QueryTimingInfo timing;
        auto res2 = engine->queryAdvanced("regex:wqxunique.*\\.txt", 100, true, timing);
        check(res2.size() == 5, "81.5: regex trigram finds Phase-2 added records");

        fs::remove_all(tmpDir);
    }

    // 81.6 completePhase2: tombstoned records excluded after Phase 2
    {
        std::cout << "  [81.6] tombstoned records absent after completePhase2\n";
        namespace fs = std::filesystem;
        std::string tmpDir = (fs::temp_directory_path() / "test_81_6").string()
                             + "_" + std::to_string(getpid());
        fs::create_directories(tmpDir);
        std::string v6Path = tmpDir + "/index.v6";

        auto engine1 = std::make_shared<SearchEngine>();
        for (int i = 0; i < 100; i++) {
            FileRecord r;
            r.name = "phase2file_" + std::to_string(i) + ".dat";
            r.path = "/ph2test";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            r.inode = static_cast<uint64_t>(i + 1);
            r.devId = 1;
            engine1->addRecord(std::move(r));
        }

        FlatIndexWriter writer(v6Path);
        IndexMetadata meta;
        meta.lastEventId = 1;
        check(writer.fullRewrite(*engine1, meta), "81.6: v6 write");

        auto engine2 = std::make_shared<SearchEngine>();
        FlatIndexWriter reader(v6Path);
        IndexMetadata loadedMeta;
        check(reader.load(*engine2, &loadedMeta), "81.6: v6 load");

        engine2->removeByPath("/ph2test/phase2file_0.dat");
        engine2->removeByPath("/ph2test/phase2file_1.dat");
        engine2->removeByPath("/ph2test/phase2file_2.dat");

        engine2->completePhase2();

        auto results = engine2->query("phase2file");
        check(results.size() == 97, "81.6: query finds 97 records after tombstone + phase2");

        bool foundDeleted = false;
        for (uint32_t idx : results) {
            auto rec = engine2->getRecord(idx);
            if (rec.name == "phase2file_0.dat" || rec.name == "phase2file_1.dat" ||
                rec.name == "phase2file_2.dat") {
                foundDeleted = true;
            }
        }
        check(!foundDeleted, "81.6: tombstoned records not in query results");

        fs::remove_all(tmpDir);
    }

    std::cout << "\n";
}

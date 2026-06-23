#pragma once

static void runReviewFixTests() {
    std::cout << "\n=== Part 82: Code Review Fix Regression Tests ===\n\n";

    // 82.1 loadRecordsV6 on engine with existing indexes clears stale data
    {
        std::cout << "  [82.1] loadRecordsV6 clears stale secondary indexes\n";
        namespace fs = std::filesystem;
        std::string tmpDir = (fs::temp_directory_path() / "test_82_1").string()
                             + "_" + std::to_string(getpid());
        fs::create_directories(tmpDir);
        std::string v6Path = tmpDir + "/index.v6";

        auto engine = std::make_shared<SearchEngine>();

        // Step 1: loadRecords builds all indexes (flat, extension, bigram, etc.)
        {
            std::vector<FileRecord> seed;
            for (int i = 0; i < 300; i++) {
                FileRecord r;
                r.name = "oldfile_" + std::to_string(i) + ".dat";
                r.path = "/olddir";
                r.type = 1;
                r.size = 100;
                r.modTime = 1000;
                r.inode = static_cast<uint64_t>(i + 1);
                r.devId = 1;
                seed.push_back(std::move(r));
            }
            engine->loadRecords(std::move(seed));
        }

        // Verify old data is queryable
        auto oldRes = engine->query("oldfile");
        check(oldRes.size() == 300, "82.1: old data queryable before V6 load");

        // Step 2: write a V6 file with completely different data
        {
            auto writer_engine = std::make_shared<SearchEngine>();
            for (int i = 0; i < 200; i++) {
                FileRecord r;
                r.name = "newdata_" + std::to_string(i) + ".csv";
                r.path = "/newdir";
                r.type = 1;
                r.size = 200;
                r.modTime = 2000;
                r.inode = static_cast<uint64_t>(i + 1);
                r.devId = 2;
                writer_engine->addRecord(std::move(r));
            }
            FlatIndexWriter writer(v6Path);
            IndexMetadata meta;
            meta.lastEventId = 1;
            check(writer.fullRewrite(*writer_engine, meta), "82.1: v6 write");
        }

        // Step 3: loadRecordsV6 replaces main data
        {
            FlatIndexWriter reader(v6Path);
            IndexMetadata loadedMeta;
            check(reader.load(*engine, &loadedMeta), "82.1: v6 load");
            check(engine->isPhase2Pending(), "82.1: phase2 pending");
        }

        // Step 4: query during Phase 2 pending — must NOT return old data
        auto staleRes = engine->query("oldfile");
        check(staleRes.empty(), "82.1: old data not returned after V6 reload");

        // New data should be findable via linear scan (no trigram yet)
        auto newRes = engine->query("newdata");
        check(newRes.size() == 200, "82.1: new data found via linear scan during Phase 2");

        // Step 5: complete Phase 2 and verify
        engine->completePhase2();
        auto postRes = engine->query("newdata");
        check(postRes.size() == 200, "82.1: new data found after Phase 2 completes");

        auto staleRes2 = engine->query("oldfile");
        check(staleRes2.empty(), "82.1: old data still absent after Phase 2");

        fs::remove_all(tmpDir);
    }

    // 82.2 name trigram works with enablePathTrigramIndex=false
    {
        std::cout << "  [82.2] name trigram active without path trigram index\n";
        SearchEngineOptions opts;
        opts.enablePathTrigramIndex = false;
        SearchEngine engine(opts);

        std::vector<FileRecord> records;
        for (int i = 0; i < 500; i++) {
            FileRecord r;
            r.name = "generic_pad_" + std::to_string(i) + ".dat";
            r.path = "/nopath";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        for (int i = 0; i < 8; i++) {
            FileRecord r;
            r.name = "xqztarget_" + std::to_string(i) + ".txt";
            r.path = "/nopath";
            r.type = 1;
            r.size = 200;
            r.modTime = 2000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("xqztarget", 0, true, timing);
        check(results.size() == 8, "82.2: 3+ char query finds 8 matches without path trigram");
        check(timing.usedTrigram, "82.2: trigram path was used");
    }

    // 82.3 delta add then remove cancels cleanly
    {
        std::cout << "  [82.3] delta add+remove cancellation\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 100; i++) {
            FileRecord r;
            r.name = "baseline_item_" + std::to_string(i) + ".cpp";
            r.path = "/delta_cancel";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto r1 = engine.queryAdvanced("baseline_item", 0, true, timing);
        check(r1.size() == 100, "82.3: initial 100 records");

        // Add a record (goes to delta adds)
        FileRecord newRec;
        newRec.name = "baseline_item_new.cpp";
        newRec.path = "/delta_cancel";
        newRec.type = 1;
        newRec.size = 200;
        newRec.modTime = 2000;
        engine.addRecord(std::move(newRec));

        auto r2 = engine.queryAdvanced("baseline_item", 0, true, timing);
        check(r2.size() == 101, "82.3: 101 after add");

        // Remove the just-added record — should cancel the delta add
        engine.removeByPath("/delta_cancel/baseline_item_new.cpp");

        auto r3 = engine.queryAdvanced("baseline_item", 0, true, timing);
        check(r3.size() == 100, "82.3: back to 100 after add+remove");

        // Remove a record that's in the flat index
        engine.removeByPath("/delta_cancel/baseline_item_0.cpp");
        auto r4 = engine.queryAdvanced("baseline_item", 0, true, timing);
        check(r4.size() == 99, "82.3: 99 after removing flat record");

        // Re-add a record with same trigrams — should cancel the delta remove
        FileRecord readdRec;
        readdRec.name = "baseline_item_0.cpp";
        readdRec.path = "/delta_cancel";
        readdRec.type = 1;
        readdRec.size = 100;
        readdRec.modTime = 3000;
        engine.addRecord(std::move(readdRec));

        auto r5 = engine.queryAdvanced("baseline_item", 0, true, timing);
        check(r5.size() == 100, "82.3: 100 after re-add cancels remove");
    }

    // 82.4 bigram query works with enablePathTrigramIndex=false
    {
        std::cout << "  [82.4] bigram query active without path trigram index\n";
        SearchEngineOptions opts;
        opts.enablePathTrigramIndex = false;
        SearchEngine engine(opts);

        std::vector<FileRecord> records;
        for (int i = 0; i < 500; i++) {
            FileRecord r;
            r.name = "document_" + std::to_string(i) + ".txt";
            r.path = "/nopath";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        for (int i = 0; i < 5; i++) {
            FileRecord r;
            r.name = "qwerty_" + std::to_string(i) + ".log";
            r.path = "/nopath";
            r.type = 1;
            r.size = 200;
            r.modTime = 2000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("qw", 0, true, timing);
        check(results.size() == 5, "82.4: 2-char query 'qw' finds 5 without path trigram");
    }

    // 82.5 ext: filter returns correct results after V6 reload
    {
        std::cout << "  [82.5] ext: filter correct after V6 reload\n";
        namespace fs = std::filesystem;
        std::string tmpDir = (fs::temp_directory_path() / "test_82_5").string()
                             + "_" + std::to_string(getpid());
        fs::create_directories(tmpDir);
        std::string v6Path = tmpDir + "/index.v6";

        auto engine = std::make_shared<SearchEngine>();

        // Seed with .dat files
        {
            std::vector<FileRecord> seed;
            for (int i = 0; i < 100; i++) {
                FileRecord r;
                r.name = "old_" + std::to_string(i) + ".dat";
                r.path = "/old";
                r.type = 1;
                r.size = 100;
                r.modTime = 1000;
                r.inode = static_cast<uint64_t>(i + 1);
                r.devId = 1;
                seed.push_back(std::move(r));
            }
            engine->loadRecords(std::move(seed));
        }

        // Write V6 with .csv files
        {
            auto wr = std::make_shared<SearchEngine>();
            for (int i = 0; i < 50; i++) {
                FileRecord r;
                r.name = "report_" + std::to_string(i) + ".csv";
                r.path = "/new";
                r.type = 1;
                r.size = 200;
                r.modTime = 2000;
                r.inode = static_cast<uint64_t>(i + 1);
                r.devId = 2;
                wr->addRecord(std::move(r));
            }
            FlatIndexWriter writer(v6Path);
            IndexMetadata meta;
            meta.lastEventId = 1;
            check(writer.fullRewrite(*wr, meta), "82.5: v6 write");
        }

        // Load V6
        {
            FlatIndexWriter reader(v6Path);
            IndexMetadata loadedMeta;
            check(reader.load(*engine, &loadedMeta), "82.5: v6 load");
        }

        // During Phase 2 pending: ext: queries must fall through to linear scan
        QueryTimingInfo timing;
        auto datRes = engine->queryAdvanced("ext:dat", 0, true, timing);
        check(datRes.empty(), "82.5: ext:dat returns 0 after V6 reload");

        auto csvPending = engine->queryAdvanced("ext:csv", 0, true, timing);
        check(csvPending.size() == 50, "82.5: ext:csv finds 50 via linear scan during Phase 2");

        // Complete Phase 2 — ext index rebuilt
        engine->completePhase2();

        auto csvRes = engine->queryAdvanced("ext:csv", 0, true, timing);
        check(csvRes.size() == 50, "82.5: ext:csv finds 50 after Phase 2");

        fs::remove_all(tmpDir);
    }

    std::cout << "\n";
}

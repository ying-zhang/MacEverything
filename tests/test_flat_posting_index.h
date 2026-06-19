#pragma once

static void runFlatPostingIndexTests() {
    std::cout << "\n=== Part 80: FlatPostingIndex & Delta Buffer Tests ===\n\n";

    // 3a. FlatPostingIndex build and lookup
    {
        std::cout << "  [80.1] FlatPostingIndex build from map\n";
        std::unordered_map<uint32_t, std::vector<uint32_t>> map;
        map[100] = {1, 3, 5, 7};
        map[200] = {2, 4, 6};
        map[300] = {10, 20, 30, 40, 50};

        FlatPostingIndex<uint32_t> idx;
        idx.build(map);
        check(idx.size() == 3, "3 keys in flat index");
        check(idx.totalPostings() == 12, "12 total postings");
        check(!idx.empty(), "flat index not empty");

        auto r1 = idx.lookup(100);
        check(r1.count == 4, "key 100 has 4 postings");
        check(r1.data[0] == 1 && r1.data[3] == 7, "key 100 data correct");

        auto r2 = idx.lookup(200);
        check(r2.count == 3, "key 200 has 3 postings");

        auto r3 = idx.lookup(300);
        check(r3.count == 5, "key 300 has 5 postings");

        auto r4 = idx.lookup(999);
        check(r4.data == nullptr && r4.count == 0, "missing key returns null");
    }

    // 3a. buildMove
    {
        std::cout << "  [80.2] FlatPostingIndex buildMove\n";
        std::unordered_map<uint32_t, std::vector<uint32_t>> map;
        map[10] = {1, 2, 3};
        map[20] = {4, 5};

        FlatPostingIndex<uint32_t> idx;
        idx.buildMove(std::move(map));
        check(idx.size() == 2, "2 keys after buildMove");

        auto r = idx.lookup(10);
        check(r.count == 3, "key 10 has 3 postings");
        check(r.data[0] == 1, "first posting correct");
    }

    // 3a. Memory reporting
    {
        std::cout << "  [80.3] FlatPostingIndex memory\n";
        std::unordered_map<Trigram, std::vector<uint32_t>> map;
        for (uint32_t i = 0; i < 1000; i++) {
            map[i] = {i * 2, i * 2 + 1};
        }
        FlatPostingIndex<Trigram> idx;
        idx.build(map);
        check(idx.memoryBytes() > 0, "memoryBytes > 0");
        check(idx.size() == 1000, "1000 keys");
        check(idx.totalPostings() == 2000, "2000 total postings");
    }

    // 3b. End-to-end: SearchEngine uses FlatPostingIndex after loadRecords
    {
        std::cout << "  [80.4] SearchEngine uses flat index after loadRecords\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Create 500 records to have enough headroom for trigram threshold
        for (int i = 0; i < 500; i++) {
            FileRecord r;
            r.name = "generic_file_" + std::to_string(i) + ".dat";
            r.path = "/flat_test";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        // Add 10 with a unique name that will be selective via trigram
        for (int i = 0; i < 10; i++) {
            FileRecord r;
            r.name = "xyztarget_" + std::to_string(i) + ".txt";
            r.path = "/flat_test";
            r.type = 1;
            r.size = 200;
            r.modTime = 2000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("xyztarget", 0, true, timing);
        check(results.size() == 10, "flat index query 'xyztarget' finds 10");
        check(timing.usedTrigram, "used trigram path with flat index");
    }

    // 3b. Delta buffer: add/remove after loadRecords
    {
        std::cout << "  [80.5] Delta buffer: mutations after flat index built\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 50; i++) {
            FileRecord r;
            r.name = "gamma_delta_" + std::to_string(i) + ".cpp";
            r.path = "/delta_test";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto r1 = engine.queryAdvanced("gamma_delta", 0, true, timing);
        check(r1.size() == 50, "initial query finds 50");

        // Add record (goes to delta)
        FileRecord newRec;
        newRec.name = "gamma_delta_new.cpp";
        newRec.path = "/delta_test";
        newRec.type = 1;
        newRec.size = 200;
        newRec.modTime = 2000;
        engine.addRecord(std::move(newRec));

        auto r2 = engine.queryAdvanced("gamma_delta", 0, true, timing);
        check(r2.size() == 51, "after add, query finds 51");

        // Remove record (goes to delta removes)
        engine.removeByPath("/delta_test/gamma_delta_0.cpp");

        auto r3 = engine.queryAdvanced("gamma_delta", 0, true, timing);
        check(r3.size() == 50, "after remove, query finds 50");
    }

    // 3b. Delta survives through compaction (flat rebuilt)
    {
        std::cout << "  [80.6] Flat index rebuilt after compaction\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 30; i++) {
            FileRecord r;
            r.name = "epsilon_zeta_" + std::to_string(i) + ".h";
            r.path = "/compact_flat";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        // Remove some
        for (int i = 0; i < 10; i++) {
            engine.removeByPath("/compact_flat/epsilon_zeta_" + std::to_string(i) + ".h");
        }

        // Compact rebuilds flat index
        engine.compactRecords();

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("epsilon_zeta", 0, true, timing);
        check(results.size() == 20, "after compaction, query finds 20 remaining");

        // Add after compaction (delta on new flat)
        FileRecord nr;
        nr.name = "epsilon_zeta_extra.h";
        nr.path = "/compact_flat";
        nr.type = 1;
        nr.size = 300;
        nr.modTime = 3000;
        engine.addRecord(std::move(nr));

        auto r2 = engine.queryAdvanced("epsilon_zeta", 0, true, timing);
        check(r2.size() == 21, "after compaction + add, query finds 21");
    }

    std::cout << "\n";
}

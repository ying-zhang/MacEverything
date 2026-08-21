#pragma once
// Part 16: Recent Cache Correctness Tests
// Validates that the recentCache_ is maintained correctly through all mutation operations.

static void runRecentCacheTests() {
    std::cout << "═══ Part 16: Recent Cache Correctness ═══\n\n";

    // Test 1: addRecord updates cache
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 5; i++) {
            FileRecord r;
            r.name = "base" + std::to_string(i) + ".txt";
            r.path = "/tmp";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000 + i * 100;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        // Add a record newer than all existing ones
        FileRecord newer;
        newer.name = "newest.txt";
        newer.path = "/tmp";
        newer.type = 1;
        newer.size = 50;
        newer.modTime = 9999;
        engine.addRecord(std::move(newer));

        auto top = engine.recentIndices(1);
        check(top.size() == 1, "C16: addRecord — cache updated");
        check(engine.getRecord(top[0]).name == "newest.txt", "C16: addRecord — newest record at top");
    }

    // Test 2: removeByPath updates cache
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 5; i++) {
            FileRecord r;
            r.name = "file" + std::to_string(i) + ".txt";
            r.path = "/tmp";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000 + i * 100;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        // file4 is newest (modTime=1400). Remove it.
        engine.removeByPath("/tmp/file4.txt");
        auto top = engine.recentIndices(1);
        check(top.size() == 1, "C16: removeByPath — cache updated");
        check(engine.getRecord(top[0]).name == "file3.txt", "C16: removeByPath — next newest at top");
    }

    // Test 3: updateByPath updates cache
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 5; i++) {
            FileRecord r;
            r.name = "file" + std::to_string(i) + ".txt";
            r.path = "/tmp";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000 + i * 100;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        // Update file0 (oldest) to have the newest modTime
        FileRecord updated;
        updated.name = "file0.txt";
        updated.path = "/tmp";
        updated.type = 1;
        updated.size = 200;
        updated.modTime = 99999;
        engine.updateByPath("/tmp/file0.txt", std::move(updated));

        auto top = engine.recentIndices(1);
        check(top.size() == 1, "C16: updateByPath — cache updated");
        check(engine.getRecord(top[0]).name == "file0.txt", "C16: updateByPath — updated record at top");
        check(engine.getRecord(top[0]).modTime == 99999, "C16: updateByPath — modTime correct");
    }

    // Test 4: removeByPathPrefix updates cache
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 5; i++) {
            FileRecord r;
            r.name = "file" + std::to_string(i) + ".txt";
            r.path = "/tmp/subdir";
            r.type = 1;
            r.size = 100;
            r.modTime = 2000 + i * 100;
            records.push_back(std::move(r));
        }
        // Add one outside the prefix
        FileRecord outside;
        outside.name = "outside.txt";
        outside.path = "/other";
        outside.type = 1;
        outside.size = 50;
        outside.modTime = 500;
        records.push_back(std::move(outside));
        engine.loadRecords(std::move(records));

        engine.removeByPathPrefix("/tmp/subdir");
        auto top = engine.recentIndices(10);
        check(top.size() == 1, "C16: removeByPathPrefix — only outside record remains");
        check(engine.getRecord(top[0]).name == "outside.txt", "C16: removeByPathPrefix — correct record");
    }

    // Test 5: compactRecords rebuilds cache correctly
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 10; i++) {
            FileRecord r;
            r.name = "file" + std::to_string(i) + ".txt";
            r.path = "/tmp";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000 + i * 100;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        // Remove some records to create tombstones
        engine.removeByPath("/tmp/file9.txt"); // newest
        engine.removeByPath("/tmp/file7.txt");

        engine.compactRecords();

        auto top = engine.recentIndices(3);
        check(top.size() == 3, "C16: compactRecords — cache has 3 results");
        check(engine.getRecord(top[0]).name == "file8.txt", "C16: compactRecords — newest after compact");
        check(engine.getRecord(top[1]).name == "file6.txt", "C16: compactRecords — second newest");
        check(engine.getRecord(top[2]).name == "file5.txt", "C16: compactRecords — third newest");
    }

    // Test 6: cache size limit (kRecentCacheSize = 200)
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 500; i++) {
            FileRecord r;
            r.name = "file" + std::to_string(i) + ".txt";
            r.path = "/data";
            r.type = 1;
            r.size = 100;
            r.modTime = i; // file499 is newest
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        auto top200 = engine.recentIndices(200);
        check(top200.size() == 200, "C16: cache limit — returns 200");
        check(engine.getRecord(top200[0]).name == "file499.txt", "C16: cache limit — newest correct");
        check(engine.getRecord(top200[199]).name == "file300.txt", "C16: cache limit — 200th correct");
    }

    // Test 7: performance — recentIndices should be O(count), not O(N)
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.reserve(100000);
        for (int i = 0; i < 100000; i++) {
            FileRecord r;
            r.name = "perf_" + std::to_string(i) + ".txt";
            r.path = "/data/files";
            r.type = 1;
            r.size = 100;
            r.modTime = i;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        auto t0 = std::chrono::high_resolution_clock::now();
        for (int rep = 0; rep < 1000; rep++) {
            auto result = engine.recentIndices(100);
            (void)result;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double perCall = ms / 1000.0;
        std::cout << "    1000x recentIndices(100) on 100k records: "
                  << std::fixed << std::setprecision(2) << ms << "ms ("
                  << perCall << "ms/call)\n";
        check(perCall < 0.1, "C16: performance — recentIndices < 0.1ms/call (cached)");
    }

    std::cout << "\n";
}

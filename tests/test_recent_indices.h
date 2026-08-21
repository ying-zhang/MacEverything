#pragma once
// Part 11: recentIndices Tests

static void runRecentIndicesTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 11: recentIndices Tests\n";
    std::cout << "========================================\n\n";

    SearchEngine engine;

    // Create records with different modTimes
    std::vector<FileRecord> records;
    for (int i = 0; i < 10; i++) {
        FileRecord r;
        r.name = "file" + std::to_string(i) + ".txt";
        r.path = "/tmp";
        r.type = 1;
        r.size = 100;
        r.modTime = 1000 + i * 100; // file9 is newest (1900), file0 is oldest (1000)
        records.push_back(std::move(r));
    }
    engine.loadRecords(std::move(records));

    // Basic: get top 3 recent
    auto top3 = engine.recentIndices(3);
    check(top3.size() == 3, "recentIndices: returns 3 results");
    check(engine.getRecord(top3[0]).name == "file9.txt", "recentIndices: newest file first");
    check(engine.getRecord(top3[1]).name == "file8.txt", "recentIndices: second newest");
    check(engine.getRecord(top3[2]).name == "file7.txt", "recentIndices: third newest");

    // Request more than available
    auto all = engine.recentIndices(100);
    check(all.size() == 10, "recentIndices: clamps to record count");

    // After removing the newest, next one becomes first
    engine.removeByPath("/tmp/file9.txt");
    auto afterRemove = engine.recentIndices(1);
    check(afterRemove.size() == 1, "recentIndices: 1 result after remove");
    check(engine.getRecord(afterRemove[0]).name == "file8.txt", "recentIndices: newest after remove");

    // After adding a newer record, it appears first
    FileRecord newest;
    newest.name = "brand_new.txt";
    newest.path = "/tmp";
    newest.type = 1;
    newest.size = 50;
    newest.modTime = 9999;
    engine.addRecord(std::move(newest));
    auto afterAdd = engine.recentIndices(1);
    check(afterAdd.size() == 1, "recentIndices: 1 result after add");
    check(engine.getRecord(afterAdd[0]).name == "brand_new.txt", "recentIndices: added record is newest");

    // Empty engine
    SearchEngine empty;
    auto emptyResult = empty.recentIndices(10);
    check(emptyResult.empty(), "recentIndices: empty engine returns empty");

    // Performance: 100k records
    {
        SearchEngine big;
        std::vector<FileRecord> bigRecords;
        bigRecords.reserve(100000);
        for (int i = 0; i < 100000; i++) {
            FileRecord r;
            r.name = "perf_" + std::to_string(i) + ".txt";
            r.path = "/data/files";
            r.type = 1;
            r.size = 100;
            r.modTime = i;
            bigRecords.push_back(std::move(r));
        }
        big.loadRecords(std::move(bigRecords));

        auto t0 = std::chrono::high_resolution_clock::now();
        for (int rep = 0; rep < 100; rep++) {
            auto result = big.recentIndices(100);
            (void)result;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "    100x recentIndices(100) on 100k records: "
                  << std::fixed << std::setprecision(2) << ms << "ms ("
                  << ms / 100.0 << "ms/call)\n";
        check(ms < 5000.0, "recentIndices: 100x calls on 100k records completes within 5s");
    }

    std::cout << "\n";
}

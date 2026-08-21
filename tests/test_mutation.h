#pragma once
// ═══════════════════════════════════════════════════════
//  Part 3: SearchEngine Mutation Tests
// ═══════════════════════════════════════════════════════

static void runMutationTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 3: SearchEngine Mutation Tests\n";
    std::cout << "========================================\n\n";

    SearchEngine engine;

    // Load some initial records
    std::vector<FileRecord> initial;
    initial.push_back({"hello.txt", "/tmp", 1, 100, 1000});
    initial.push_back({"world.cpp", "/tmp", 1, 200, 2000});
    initial.push_back({"readme.md", "/home", 1, 300, 3000});
    initial.push_back({"testdir", "/var", 2, 0, 4000});
    engine.loadRecords(std::move(initial));

    check(engine.recordCount() == 4, "loadRecords: recordCount == 4");
    check(engine.liveRecordCount() == 4, "loadRecords: liveRecordCount == 4");

    // Query test
    auto res = engine.query("hello");
    check(res.size() == 1, "query 'hello': 1 match");
    check(engine.getRecord(res[0]).name == "hello.txt", "query 'hello': correct name");

    res = engine.query(".txt");
    check(res.size() == 1, "query '.txt': 1 match");

    // Case insensitive
    res = engine.query("README");
    check(res.size() == 1, "query 'README' (case-insensitive): 1 match");

    // ── addRecord ──
    std::cout << "\n  --- addRecord ---\n";
    uint32_t newIdx = engine.addRecord({"newfile.txt", "/tmp", 1, 500, 5000});
    check(newIdx == 4, "addRecord: returned index 4");
    check(engine.recordCount() == 5, "addRecord: recordCount == 5");
    check(engine.liveRecordCount() == 5, "addRecord: liveRecordCount == 5");
    res = engine.query("newfile");
    check(res.size() == 1, "addRecord: queryable by name");
    check(engine.getRecord(res[0]).size == 500, "addRecord: correct size");

    // ── removeByPath ──
    std::cout << "\n  --- removeByPath ---\n";
    bool removed = engine.removeByPath("/tmp/hello.txt");
    check(removed, "removeByPath '/tmp/hello.txt': returned true");
    check(engine.liveRecordCount() == 4, "removeByPath: liveRecordCount == 4");
    res = engine.query("hello");
    check(res.empty(), "removeByPath: 'hello' no longer queryable");

    // Tombstoned record
    const auto& tombstoned = engine.getRecord(0);
    check(tombstoned.type == 0, "removeByPath: record type == 0 (tombstone)");

    // Remove non-existent
    bool removedAgain = engine.removeByPath("/tmp/hello.txt");
    check(!removedAgain, "removeByPath non-existent: returned false");
    check(engine.liveRecordCount() == 4, "removeByPath non-existent: liveCount unchanged");

    bool removedBogus = engine.removeByPath("/no/such/path");
    check(!removedBogus, "removeByPath bogus path: returned false");

    // ── updateByPath ──
    std::cout << "\n  --- updateByPath ---\n";
    engine.updateByPath("/tmp/world.cpp", {"world_v2.cpp", "/tmp", 1, 999, 6000});
    check(engine.liveRecordCount() == 4, "updateByPath: liveRecordCount unchanged (4)");
    res = engine.query("world_v2");
    check(res.size() == 1, "updateByPath: new name queryable");
    check(engine.getRecord(res[0]).size == 999, "updateByPath: new record has updated size");
    res = engine.query("world.cpp");
    // world.cpp is tombstoned but world_v2.cpp contains "world" substring
    // The old exact "world.cpp" record is gone, let's check the tombstone
    const auto& oldRecord = engine.getRecord(1);
    check(oldRecord.type == 0, "updateByPath: old record tombstoned");

    // Update non-existent path (should just add)
    engine.updateByPath("/new/path/fresh.go", {"fresh.go", "/new/path", 1, 42, 7000});
    check(engine.liveRecordCount() == 5, "updateByPath new path: liveRecordCount == 5");
    res = engine.query("fresh.go");
    check(res.size() == 1, "updateByPath new path: queryable");

    // ── Mutation performance ──
    std::cout << "\n  --- Mutation Performance ---\n";
    if (gSkipPerformanceTests) {
        std::cout << "    [SKIP] Mutation performance benchmark skipped in --fast mode\n\n";
        return;
    }

    SearchEngine perfEngine;
    {
        std::vector<FileRecord> bulk;
        for (uint32_t i = 0; i < 100000; i++) {
            bulk.push_back({"file_" + std::to_string(i) + ".txt", "/perf/dir", 1, i * 10, (time_t)i});
        }
        perfEngine.loadRecords(std::move(bulk));
    }

    // Benchmark addRecord
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; i++) {
        perfEngine.addRecord({"added_" + std::to_string(i) + ".txt", "/perf/add", 1, 100, 1000});
    }
    auto t1 = std::chrono::steady_clock::now();
    double addTime = std::chrono::duration<double>(t1 - t0).count() * 1000;
    std::cout << "    10,000 addRecord:    " << std::fixed << std::setprecision(2) << addTime << "ms"
              << " (" << std::setprecision(1) << (addTime / 10000 * 1000) << "us/op)\n";

    // Benchmark removeByPath
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; i++) {
        perfEngine.removeByPath("/perf/dir/file_" + std::to_string(i) + ".txt");
    }
    t1 = std::chrono::steady_clock::now();
    double removeTime = std::chrono::duration<double>(t1 - t0).count() * 1000;
    std::cout << "    10,000 removeByPath: " << std::fixed << std::setprecision(2) << removeTime << "ms"
              << " (" << std::setprecision(1) << (removeTime / 10000 * 1000) << "us/op)\n";

    check(perfEngine.liveRecordCount() == 100000, "Mutation perf: liveCount == 100000 (100k - 10k removed + 10k added)");

    // Benchmark updateByPath
    t0 = std::chrono::steady_clock::now();
    for (int i = 10000; i < 20000; i++) {
        perfEngine.updateByPath("/perf/dir/file_" + std::to_string(i) + ".txt",
                                {"updated_" + std::to_string(i) + ".txt", "/perf/dir", 1, 999, 9999});
    }
    t1 = std::chrono::steady_clock::now();
    double updateTime = std::chrono::duration<double>(t1 - t0).count() * 1000;
    std::cout << "    10,000 updateByPath: " << std::fixed << std::setprecision(2) << updateTime << "ms"
              << " (" << std::setprecision(1) << (updateTime / 10000 * 1000) << "us/op)\n";

    std::cout << "\n";
}

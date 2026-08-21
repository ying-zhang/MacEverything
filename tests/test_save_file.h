#pragma once
// Part 7d: C4 — saveToFile checks writeRecord return value

static void runSaveToFileTest() {
    std::cout << "═══ Part 7d: saveToFile writeRecord Error Handling ═══\n\n";

    std::string tmpDir = "/tmp/maceverything_c4_test_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    SearchEngine engine;

    // Add some records
    for (int i = 0; i < 10; i++) {
        FileRecord rec;
        rec.name = "file" + std::to_string(i) + ".txt";
        rec.path = "/test/path";
        rec.type = 1;
        rec.size = 100 * i;
        rec.modTime = time(nullptr);
        engine.addRecord(std::move(rec));
    }

    // Save to a valid path should succeed
    std::string validPath = tmpDir + "/test_save.bin";
    bool ok = engine.saveToFile(validPath, 0);
    check(ok, "C4: saveToFile succeeds on valid path");

    // Save to an invalid path (non-existent directory) should fail
    std::string invalidPath = "/tmp/nonexistent_dir_" + std::to_string(getpid()) + "/sub/test.bin";
    bool fail = engine.saveToFile(invalidPath, 0);
    check(!fail, "C4: saveToFile fails on invalid path");

    // Verify the valid save can be loaded back
    SearchEngine engine2;
    ok = engine2.loadFromFile(validPath, static_cast<IndexMetadata*>(nullptr));
    check(ok, "C4: Saved file loads back correctly");
    check(engine2.liveRecordCount() == 10, "C4: All 10 records survive save/load");

    fs::remove_all(tmpDir);
    std::cout << "\n";
}

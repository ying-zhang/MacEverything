// Tests for compaction threshold: skip compaction when WAL entry count is below threshold
#pragma once

static void testCompactSkipsBelowThreshold() {
    std::cout << "\n--- Compact skips below threshold ---\n";

    std::string tmpDir = "/tmp/test_compact_threshold_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    std::string basePath = tmpDir + "/index.bin";
    std::string walPath  = tmpDir + "/index.wal";

    auto engine = std::make_shared<SearchEngine>();
    IndexPersistence persistence(engine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
    persistence.attachWAL();

    // Add a few records (below kCompactThreshold = 100)
    for (int i = 0; i < 5; i++) {
        FileRecord rec;
        rec.name = "file" + std::to_string(i) + ".txt";
        rec.path = tmpDir;
        rec.type = 1;
        rec.size = 100;
        rec.modTime = time(nullptr);
        engine->addRecord(std::move(rec));
    }

    // Compact without force — should skip because only 5 entries (< 100)
    persistence.compact(1);
    check(!fs::exists(ptablePathFor(basePath)), "ptable should NOT be written when below threshold");

    fs::remove_all(tmpDir);
}

static void testCompactProceedsAtThreshold() {
    std::cout << "\n--- Compact proceeds at threshold ---\n";

    std::string tmpDir = "/tmp/test_compact_threshold2_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    std::string basePath = tmpDir + "/index.bin";
    std::string walPath  = tmpDir + "/index.wal";

    auto engine = std::make_shared<SearchEngine>();
    IndexPersistence persistence(engine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
    persistence.attachWAL();

    // Add exactly kCompactThreshold records
    for (uint64_t i = 0; i < IndexPersistence::kCompactThreshold; i++) {
        FileRecord rec;
        rec.name = "file" + std::to_string(i) + ".txt";
        rec.path = tmpDir;
        rec.type = 1;
        rec.size = 100;
        rec.modTime = time(nullptr);
        engine->addRecord(std::move(rec));
    }

    // Compact without force — should proceed because entryCount >= threshold
    persistence.compact(1);
    check(fs::exists(basePath + ".v6"), "v6 index SHOULD be written when at threshold");

    fs::remove_all(tmpDir);
}

static void testCompactForceIgnoresThreshold() {
    std::cout << "\n--- Compact force=true ignores threshold ---\n";

    std::string tmpDir = "/tmp/test_compact_threshold3_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    std::string basePath = tmpDir + "/index.bin";
    std::string walPath  = tmpDir + "/index.wal";

    auto engine = std::make_shared<SearchEngine>();
    IndexPersistence persistence(engine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
    persistence.attachWAL();

    // Add just 1 record (well below threshold)
    FileRecord rec;
    rec.name = "single.txt";
    rec.path = tmpDir;
    rec.type = 1;
    rec.size = 42;
    rec.modTime = time(nullptr);
    engine->addRecord(std::move(rec));

    // Compact with force=true — should proceed regardless of entry count
    persistence.compact(1, /*force=*/true);
    check(fs::exists(basePath + ".v6"), "v6 index SHOULD be written when force=true");

    fs::remove_all(tmpDir);
}

static void testInitialSnapshotWritesWithHeaderOnlyWal() {
    std::cout << "\n--- Initial snapshot writes with header-only WAL ---\n";

    std::string tmpDir = "/tmp/test_initial_snapshot_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string basePath = tmpDir + "/index.bin";
    std::string walPath = tmpDir + "/index.wal";
    std::string v6Path = basePath + ".v6";

    auto engine = std::make_shared<SearchEngine>();
    FileRecord rec;
    rec.name = "scanned-before-wal.txt";
    rec.path = tmpDir;
    rec.type = 1;
    rec.size = 42;
    rec.modTime = time(nullptr);
    engine->addRecord(std::move(rec));

    // Full scans populate the engine first and attach the WAL afterwards.
    IndexPersistence persistence(
        engine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), v6Path);
    persistence.attachWAL();
    persistence.compact(1, /*force=*/true);

    check(fs::exists(v6Path), "initial force flush SHOULD write v6 with a header-only WAL");

    auto loaded = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata metadata;
    check(reader.load(*loaded, &metadata), "initial v6 snapshot should load");
    check(loaded->liveRecordCount() == 1, "initial v6 snapshot should contain scanned records");

    fs::remove_all(tmpDir);
}

static void runCompactThresholdTests() {
    testCompactSkipsBelowThreshold();
    testCompactProceedsAtThreshold();
    testCompactForceIgnoresThreshold();
    testInitialSnapshotWritesWithHeaderOnlyWal();
}

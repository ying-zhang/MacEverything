// Tests for dirty flag: compaction skipped when no mutations occurred
#pragma once

#include <filesystem>

// ─────────── IndexWAL dirty flag ───────────

static void testIndexWalDirtyFlag() {
    std::cout << "\n─── IndexWAL dirty flag ───\n";

    std::string walPath = "/tmp/test_dirty_wal.wal";

    // Clean up
    std::remove(walPath.c_str());

    // 1. Fresh WAL should not be dirty
    {
        IndexWAL wal;
        check(wal.open(walPath), "WAL open");
        check(!wal.isDirty(), "fresh WAL should not be dirty");

        // 2. After append, should be dirty
        FileRecord rec;
        rec.name = "test.txt";
        rec.path = "/tmp";
        rec.type = 1;
        check(wal.append(WALOp::Add, "/tmp/test.txt", rec), "WAL append");
        check(wal.isDirty(), "WAL should be dirty after append");

        // 3. clearDirty resets the flag
        wal.clearDirty();
        check(!wal.isDirty(), "WAL should not be dirty after clearDirty");

        // 4. Append again sets it
        check(wal.append(WALOp::Remove, "/tmp/test.txt"), "WAL append remove");
        check(wal.isDirty(), "WAL should be dirty after second append");

        wal.close();
    }

    std::remove(walPath.c_str());
}

// ─────────── IndexPersistence skips compaction when clean ───────────

static void testIndexPersistenceSkipsCleanCompaction() {
    std::cout << "\n─── IndexPersistence skips clean compaction ───\n";

    std::string tmpDir = "/tmp/test_dirty_index_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string basePath = tmpDir + "/index.bin";
    std::string walPath  = tmpDir + "/index.wal";
    std::string ptPath   = ptablePathFor(basePath);
    std::string v6Path   = basePath + ".v6";

    auto engine = std::make_shared<SearchEngine>();
    IndexPersistence persistence(engine, basePath, walPath, pagesPathFor(basePath), ptPath, v6Path);
    persistence.attachWAL();

    // Add a record so there's something to compact
    FileRecord rec;
    rec.name = "hello.txt";
    rec.path = "/tmp";
    rec.type = 1;
    rec.size = 42;
    rec.modTime = 1000;
    engine->addRecord(std::move(rec));

    // First compaction should succeed (WAL is dirty from addOrUpdate)
    persistence.compact(1, /*force=*/true);
    check(fs::exists(v6Path), "v6 index should be written after dirty compaction");

    // Get file modification time after first compaction
    auto modTime1 = fs::last_write_time(v6Path);

    // Wait briefly to ensure filesystem timestamp granularity
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Second compaction should be skipped (no mutations)
    persistence.compact(2);

    // v6 file should not be rewritten
    auto modTime2 = fs::last_write_time(v6Path);
    check(modTime1 == modTime2, "v6 index should NOT be rewritten when no mutations");

    // Clean up
    fs::remove_all(tmpDir);
}

// ─────────── ContentIndexPersistence skips compaction when clean ───────────

static void testContentIndexPersistenceSkipsCleanCompaction() {
    std::cout << "\n─── ContentIndexPersistence skips clean compaction ───\n";

    std::string basePath = "/tmp/test_dirty_content.bin";
    std::string walPath  = "/tmp/test_dirty_content.wal";

    // Clean up
    std::remove(basePath.c_str());
    std::remove(walPath.c_str());
    std::remove((walPath + ".new").c_str());

    auto contentIndex = std::make_shared<ContentIndex>();
    ContentIndexPersistence persistence(contentIndex, basePath, walPath);
    persistence.attachWAL();

    // Add content so there's something to compact
    std::vector<Trigram> trigrams = {100, 200, 300};
    persistence.walAppendAdd(0, 12345, trigrams);

    // Also update the in-memory content index
    contentIndex->insertFileInfo(0, 12345, std::move(trigrams));

    // First compaction should succeed (dirty from walAppendAdd)
    // Use force=true since we're testing dirty detection, not threshold
    persistence.compact(true);
    check(fs::exists(basePath), "content base should be written after dirty compaction");

    auto modTime1 = fs::last_write_time(basePath);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Second compaction should be skipped (no mutations)
    persistence.compact(true);

    auto modTime2 = fs::last_write_time(basePath);
    check(modTime1 == modTime2, "content base should NOT be rewritten when no mutations");

    // Now add another mutation — compaction should proceed
    persistence.walAppendRemove(0);
    contentIndex->removeFile(0);

    persistence.compact(true);

    auto modTime3 = fs::last_write_time(basePath);
    check(modTime3 != modTime2, "content base SHOULD be rewritten after new mutation");

    // Clean up
    std::remove(basePath.c_str());
    std::remove(walPath.c_str());
}

static void runDirtyCompactionTests() {
    testIndexWalDirtyFlag();
    testIndexPersistenceSkipsCleanCompaction();
    testContentIndexPersistenceSkipsCleanCompaction();
}

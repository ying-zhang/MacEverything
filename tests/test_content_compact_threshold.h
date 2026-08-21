#pragma once
// Part 35: ContentIndexPersistence compact threshold tests

static void runContentCompactThresholdTests() {
    std::cout << "═══ Part 35: ContentIndexPersistence Compact Threshold Tests ═══\n\n";

    std::string tmpDir = fs::temp_directory_path() / "me_test_ccthreshold";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    std::string basePath = tmpDir + "/content.idx";
    std::string walPath = tmpDir + "/content.wal";

    // Test 1: compact skips when WAL has no mutations (dirty=false)
    {
        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.attachWAL();

        // No mutations — compact should skip (no crash, no base file written)
        cip.compact();
        check(!fs::exists(basePath), "ContentCompactThreshold: no base file when WAL not dirty");
    }

    // Test 2: compact skips when below threshold (non-forced)
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.attachWAL();

        // Add entries below threshold (kCompactThreshold = 50)
        for (uint32_t i = 0; i < 10; i++) {
            cip.walAppendAdd(i, 0xAA + i, {100, 200});
        }

        // Non-forced compact should skip
        cip.compact(false);
        check(!fs::exists(basePath), "ContentCompactThreshold: skip below threshold (10 < 50)");
    }

    // Test 3: force=true bypasses threshold
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.attachWAL();

        // Add entries below threshold
        for (uint32_t i = 0; i < 5; i++) {
            cip.walAppendAdd(i, 0xBB + i, {300});
        }

        // Forced compact should proceed even below threshold
        cip.compact(true);
        check(fs::exists(basePath), "ContentCompactThreshold: force=true writes base file");
    }

    // Test 4: compact proceeds when at/above threshold
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.attachWAL();

        // Add exactly kCompactThreshold entries
        for (uint32_t i = 0; i < ContentIndexPersistence::kCompactThreshold; i++) {
            cip.walAppendAdd(i, 0xCC + i, {});
        }

        // Non-forced compact should proceed (at threshold)
        cip.compact(false);
        check(fs::exists(basePath), "ContentCompactThreshold: compact at threshold writes base");
    }

    // Test 5: after compact, second compact skips (WAL is fresh/not dirty)
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.attachWAL();

        for (uint32_t i = 0; i < ContentIndexPersistence::kCompactThreshold; i++) {
            cip.walAppendAdd(i, 0xDD + i, {});
        }

        cip.compact(false);
        check(fs::exists(basePath), "ContentCompactThreshold: first compact writes base");

        // Remove base to detect if second compact writes
        fs::remove(basePath);

        cip.compact(false);
        check(!fs::exists(basePath), "ContentCompactThreshold: second compact skips (WAL fresh)");
    }

    // Test 6: retained segments force a retry even when the active segment is below threshold.
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        {
            ContentIndexWAL baseWal;
            check(baseWal.open(walPath), "ContentCompactThreshold: pending base WAL opens");
            check(baseWal.appendAdd(1, "/tmp/pending.txt", 0xEE, {100}),
                  "ContentCompactThreshold: pending base WAL entry appended");
            baseWal.close();
        }
        {
            ContentIndexWAL activeWal;
            check(activeWal.open(walPath + ".seg.1"),
                  "ContentCompactThreshold: pending active segment opens");
            activeWal.close();
        }

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        check(cip.load(), "ContentCompactThreshold: pending segment state loads");
        cip.attachWAL();
        cip.compact(false);

        size_t segmentCount = 0;
        for (const auto& entry : fs::directory_iterator(tmpDir)) {
            const auto name = entry.path().filename().string();
            if (name == "content.wal" || name.rfind("content.wal.seg.", 0) == 0) segmentCount++;
        }
        check(fs::exists(basePath), "ContentCompactThreshold: pending segments trigger base rewrite");
        check(segmentCount == 1, "ContentCompactThreshold: pending segments collapse to one active WAL");
    }

    // Cleanup
    fs::remove_all(tmpDir);

    std::cout << "\n";
}

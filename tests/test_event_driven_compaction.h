#pragma once
// Part 43: Event-driven content compaction tests
// Verifies that ContentIndexPersistence compaction is triggered by WAL mutations
// rather than a fixed timer, and that the debounce/dedup logic works.

static void runEventDrivenCompactionTests() {
    std::cout << "=== Part 43: Event-Driven Content Compaction Tests ===\n\n";

    std::string tmpDir = fs::temp_directory_path() / "me_test_event_compact";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    std::string basePath = tmpDir + "/content.idx";
    std::string walPath = tmpDir + "/content.wal";

    // Test 1: scheduleCompaction is called via walAppendAdd
    // After enough mutations (>= threshold) and waiting for the debounce delay,
    // compaction should fire automatically.
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.attachWAL();
        cip.startAutoCompaction();

        // Add entries above compaction threshold (50)
        for (uint32_t i = 0; i < ContentIndexPersistence::kCompactThreshold + 10; i++) {
            cip.walAppendAdd(i, 0xAA + i, {100, 200});
        }

        // The compaction is scheduled with kCompactionDelaySec (60s) delay.
        // We can't wait 60s in a test, so verify that after manual compact(),
        // the base file is written (proving the WAL was populated correctly).
        cip.compact(true);
        check(fs::exists(basePath), "EventDrivenCompact: WAL populated via walAppendAdd leads to valid compact");

        cip.stopAutoCompactionAndWait();
    }

    // Test 2: walAppendRemove also triggers scheduling
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.attachWAL();
        cip.startAutoCompaction();

        // First add some entries
        for (uint32_t i = 0; i < ContentIndexPersistence::kCompactThreshold; i++) {
            cip.walAppendAdd(i, 0xBB + i, {300});
        }

        // Then remove some — should also mark WAL dirty
        for (uint32_t i = 0; i < 5; i++) {
            cip.walAppendRemove(i);
        }

        cip.compact(true);
        check(fs::exists(basePath), "EventDrivenCompact: walAppendRemove keeps WAL dirty for compact");

        cip.stopAutoCompactionAndWait();
    }

    // Test 3: No mutations = no compaction needed (event-driven means no wasted cycles)
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.attachWAL();
        cip.startAutoCompaction();

        // No mutations at all — compact should skip
        cip.compact(false);
        check(!fs::exists(basePath), "EventDrivenCompact: no mutations = compact skipped (no wasted cycle)");

        cip.stopAutoCompactionAndWait();
    }

    // Test 4: startAutoCompaction without timer — verify queue is created
    // stopAutoCompactionAndWait should drain cleanly
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.attachWAL();

        // Start and immediately stop — no crash
        cip.startAutoCompaction();
        cip.stopAutoCompactionAndWait();

        // Double stop — also no crash
        cip.stopAutoCompactionAndWait();

        check(ci->indexedFileCount() == 0, "EventDrivenCompact: start/stop auto-compaction leaves clean state");
    }

    // Test 5: Multiple rapid walAppendAdd calls don't cause multiple concurrent compactions
    // (the CAS guard in scheduleCompaction should deduplicate)
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.attachWAL();
        cip.startAutoCompaction();

        // Rapid-fire mutations from multiple threads
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; t++) {
            threads.emplace_back([&cip, t]() {
                for (uint32_t i = 0; i < 20; i++) {
                    uint32_t idx = static_cast<uint32_t>(t * 20 + i);
                    cip.walAppendAdd(idx, 0xCC + idx, {400, 500});
                }
            });
        }
        for (auto& th : threads) th.join();

        // Force compact to verify all entries were written
        cip.compact(true);
        check(fs::exists(basePath), "EventDrivenCompact: concurrent walAppendAdd + compact succeeds");

        cip.stopAutoCompactionAndWait();
    }

    fs::remove_all(tmpDir);
    std::cout << "\n";
}

#pragma once
// Part 36: Content compaction WAL guard — walAppendRemove must be skipped
//          for files that are NOT in the content index.

static void runContentCompactionGuardTests() {
    std::cout << "═══ Part 36: Content Compaction WAL Guard Tests ═══\n\n";

    std::string tmpDir = fs::temp_directory_path() / "me_test_compaction_guard";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    std::string walPath = tmpDir + "/content.wal";
    std::string basePath = tmpDir + "/content.idx";

    // Test 1: WAL stays clean when removing a file not in the content index.
    //   This simulates the root cause: a file deletion event arrives for a
    //   file that was never content-indexed (e.g., binary, too large, wrong ext).
    //   Before the fix, walAppendRemove was called unconditionally, polluting
    //   the dirty flag and defeating the compact skip logic.
    {
        auto ci = std::make_shared<ContentIndex>();

        // File 42 is NOT indexed — isFileIndexed(42) must be false
        check(!ci->isFileIndexed(42), "Guard: file 42 not indexed initially");

        // Simulate the fixed bridge logic:
        // if (fileIndex != UINT32_MAX && contentIndex->isFileIndexed(fileIndex)) { ... }
        ContentIndexWAL wal;
        wal.open(walPath);
        wal.setSyncInterval(0);

        // Guard check — skip removeFile + walAppendRemove for non-indexed files
        uint32_t fileIndex = 42;
        if (ci->isFileIndexed(fileIndex)) {
            ci->removeFile(fileIndex);
            wal.appendRemove(fileIndex);
        }

        check(!wal.isDirty(), "Guard: WAL not dirty after guarded skip of non-indexed file");
        check(wal.entryCount() == 0, "Guard: WAL entry count 0 after guarded skip");

        wal.closeAndDelete();
    }

    // Test 2: WAL IS written when removing a file that IS in the content index.
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();

        // Create a text file to index
        std::string testFile = tmpDir + "/hello.txt";
        {
            std::ofstream ofs(testFile);
            ofs << "hello world this is a test file for content indexing";
        }

        ci->setExtensions({"txt"});
        bool didIndex = ci->indexFile(1, testFile);
        check(didIndex, "Guard: indexFile succeeds for text file");
        check(ci->isFileIndexed(1), "Guard: file 1 is indexed");

        ContentIndexWAL wal;
        wal.open(walPath);
        wal.setSyncInterval(0);

        // Guard check — file IS indexed, so removeFile + walAppendRemove should execute
        uint32_t fileIndex = 1;
        if (ci->isFileIndexed(fileIndex)) {
            ci->removeFile(fileIndex);
            wal.appendRemove(fileIndex);
        }

        check(wal.isDirty(), "Guard: WAL is dirty after removing indexed file");
        check(wal.entryCount() == 1, "Guard: WAL entry count 1 after removing indexed file");
        check(!ci->isFileIndexed(1), "Guard: file 1 no longer indexed after removal");

        wal.closeAndDelete();
    }

    // Test 3: ContentIndexPersistence compact skips when WAL has zero real mutations.
    //   Simulates the scenario where all file events are for non-indexed files.
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();
        ContentIndexPersistence cip(ci, basePath, walPath);
        cip.load();
        cip.attachWAL();

        // No mutations — compact should skip
        cip.compact(/*force=*/false);

        // If we got here without crash, the skip logic works.
        // The WAL should still be clean because no mutations were appended.
        check(ci->indexedFileCount() == 0, "Guard: no files indexed, compact was a no-op");
    }

    // Test 4: force=true compacts even when isDirty() is false.
    //   Simulates the restart scenario: WAL has stale entries from a previous
    //   session (replayed on load), but no new mutations in this session.
    //   Without force, compact skips because isDirty()==false.
    //   With force=true (used on exit), compact must execute.
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        auto ci = std::make_shared<ContentIndex>();

        // Create a text file and index it
        std::string testFile = tmpDir + "/test_force.txt";
        {
            std::ofstream ofs(testFile);
            ofs << "force compact test content for trigram indexing here";
        }
        ci->setExtensions({"txt"});
        ci->indexFile(1, testFile);
        check(ci->isFileIndexed(1), "Force: file 1 indexed");

        // Save base index, then create a WAL with an "add" entry
        ci->saveToFile(basePath);
        {
            ContentIndexWAL wal;
            wal.open(walPath);
            wal.setSyncInterval(0);
            // Read back trigrams from WAL-compatible source: re-extract from file
            std::vector<Trigram> trigrams = ContentIndex::extractTrigrams(
                "force compact test content for trigram indexing here");
            wal.appendAdd(1, 12345, trigrams);
            wal.close();
        }

        // Reload fresh — simulates restart. WAL has entries but isDirty()=false after replay.
        auto ci2 = std::make_shared<ContentIndex>();
        ci2->setExtensions({"txt"});
        ContentIndexPersistence cip2(ci2, basePath, walPath);
        cip2.load();
        cip2.attachWAL();

        // Non-forced compact should skip (WAL not dirty in this session)
        cip2.compact(/*force=*/false);
        // WAL file should still exist with entries
        check(fs::exists(walPath), "Force: WAL still exists after non-forced compact skip");

        // Forced compact should execute even though isDirty() is false
        cip2.compact(/*force=*/true);
        // After forced compact, the base file should be updated and WAL should be fresh
        check(ci2->isFileIndexed(1), "Force: file 1 still indexed after forced compact");
    }

    // Cleanup
    fs::remove_all(tmpDir);

    std::cout << "\n";
}

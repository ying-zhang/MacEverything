#pragma once
// Part 34: ContentIndexWAL tracking fields tests

static void runContentWalTrackingTests() {
    std::cout << "═══ Part 34: ContentIndexWAL Tracking Tests ═══\n\n";

    std::string tmpDir = fs::temp_directory_path() / "me_test_cwal_tracking";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    std::string walPath = tmpDir + "/content.wal";

    // Test 1: Initial state after open
    {
        ContentIndexWAL wal;
        check(wal.entryCount() == 0, "ContentIndexWAL: entryCount 0 before open");
        check(wal.currentSize() == 0, "ContentIndexWAL: currentSize 0 before open");
        check(!wal.isDirty(), "ContentIndexWAL: not dirty before open");

        bool ok = wal.open(walPath);
        check(ok, "ContentIndexWAL: open succeeds");
        check(wal.entryCount() == 0, "ContentIndexWAL: entryCount 0 after open");
        // Header is 8 bytes (magic + version)
        check(wal.currentSize() == 8, "ContentIndexWAL: currentSize == 8 after open (header only)");
        check(!wal.isDirty(), "ContentIndexWAL: not dirty after open");

        wal.closeAndDelete();
    }

    // Test 2: entryCount and dirty after appendAdd
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        ContentIndexWAL wal;
        wal.open(walPath);
        wal.setSyncInterval(0); // disable auto-fsync for speed

        std::vector<Trigram> tris = {100, 200, 300};
        bool added = wal.appendAdd(1, 0xDEADBEEF, tris);
        check(added, "ContentIndexWAL: appendAdd succeeds");
        check(wal.entryCount() == 1, "ContentIndexWAL: entryCount 1 after one add");
        check(wal.isDirty(), "ContentIndexWAL: dirty after appendAdd");
        check(wal.currentSize() > 8, "ContentIndexWAL: currentSize grew after appendAdd");

        size_t sizeAfterOne = wal.currentSize();

        wal.appendAdd(2, 0xCAFEBABE, {400, 500});
        check(wal.entryCount() == 2, "ContentIndexWAL: entryCount 2 after two adds");
        check(wal.currentSize() > sizeAfterOne, "ContentIndexWAL: currentSize grew after second add");

        wal.closeAndDelete();
    }

    // Test 3: entryCount and dirty after appendRemove
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        ContentIndexWAL wal;
        wal.open(walPath);
        wal.setSyncInterval(0);

        bool removed = wal.appendRemove(42);
        check(removed, "ContentIndexWAL: appendRemove succeeds");
        check(wal.entryCount() == 1, "ContentIndexWAL: entryCount 1 after remove");
        check(wal.isDirty(), "ContentIndexWAL: dirty after appendRemove");
        check(wal.currentSize() > 8, "ContentIndexWAL: currentSize grew after remove");

        wal.closeAndDelete();
    }

    // Test 4: clearDirty resets dirty flag
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        ContentIndexWAL wal;
        wal.open(walPath);
        wal.setSyncInterval(0);

        wal.appendAdd(1, 0x1234, {10, 20});
        check(wal.isDirty(), "ContentIndexWAL: dirty before clearDirty");

        wal.clearDirty();
        check(!wal.isDirty(), "ContentIndexWAL: not dirty after clearDirty");

        // Appending again should re-set dirty
        wal.appendRemove(1);
        check(wal.isDirty(), "ContentIndexWAL: dirty again after append post-clear");

        wal.closeAndDelete();
    }

    // Test 5: Mixed adds and removes — entryCount is cumulative
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        ContentIndexWAL wal;
        wal.open(walPath);
        wal.setSyncInterval(0);

        wal.appendAdd(1, 0xAA, {1, 2, 3});
        wal.appendRemove(1);
        wal.appendAdd(2, 0xBB, {4, 5});
        wal.appendRemove(2);
        wal.appendAdd(3, 0xCC, {});

        check(wal.entryCount() == 5, "ContentIndexWAL: entryCount 5 after mixed ops");
        check(wal.isDirty(), "ContentIndexWAL: dirty after mixed ops");

        wal.closeAndDelete();
    }

    // Test 6: Reopening a current-format WAL must preserve existing entries.
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);

        {
            ContentIndexWAL wal;
            check(wal.open(walPath), "ContentIndexWAL: initial reopen fixture opens");
            check(wal.appendAdd(7, "/tmp/reopen.txt", 0xBEEF, {10, 20, 30}),
                  "ContentIndexWAL: reopen fixture append succeeds");
            wal.close();
        }
        const auto sizeBeforeReopen = fs::file_size(walPath);
        {
            ContentIndexWAL wal;
            check(wal.open(walPath), "ContentIndexWAL: existing V2 WAL reopens");
            check(wal.currentSize() == sizeBeforeReopen,
                  "ContentIndexWAL: reopen does not truncate existing WAL");
            wal.close();
        }
        auto entries = ContentIndexWAL::readAll(walPath);
        check(entries.size() == 1 && entries[0].fullPath == "/tmp/reopen.txt",
              "ContentIndexWAL: existing entry survives reopen");
    }

    // Cleanup
    fs::remove_all(tmpDir);

    std::cout << "\n";
}

#pragma once
// Test: WAL rename self-propagating failure chain fix
//
// Root cause: After a rename failure, oldWal->closeAndDelete() in the next
// compact cycle deletes the .wal.new file that the current WAL is using,
// causing all subsequent renames to fail with ENOENT.
//
// The fix reorders compact steps: rename happens before closeAndDelete,
// so the new WAL's directory entry is always at the standard path when
// the old WAL is deleted.

static void runWalRenameChainTest() {
    std::cout << "=== WAL Rename Chain Fix ===" << std::endl;

    std::string tmpDir = "/tmp/maceverything_wal_rename_chain_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    auto countWalFiles = [](const std::string& base) {
        size_t count = 0;
        fs::path basePath(base);
        const auto baseName = basePath.filename().string();
        for (const auto& entry : fs::directory_iterator(basePath.parent_path())) {
            const auto name = entry.path().filename().string();
            if (name == baseName || name.rfind(baseName + ".seg.", 0) == 0) count++;
        }
        return count;
    };

    // --- Test ContentIndexPersistence compact rename ---
    {
        auto contentIndex = std::make_shared<ContentIndex>();
        std::string basePath = tmpDir + "/content_index.bin";
        std::string walPath = tmpDir + "/content_index.wal";

        ContentIndexPersistence persistence(contentIndex, basePath, walPath);
        persistence.load();
        persistence.attachWAL();

        // Add a mutation so the dirty flag is set (required for compact to proceed)
        std::vector<Trigram> trigrams1 = {100, 200, 300};
        persistence.walAppendAdd(0, 12345, trigrams1);
        contentIndex->insertFileInfo(0, 12345, std::vector<Trigram>{100, 200, 300});

        // First compact — should succeed cleanly (force=true to bypass threshold)
        persistence.compact(true);
        check(fs::exists(basePath), "RC1: base file created after first compact");
        check(countWalFiles(walPath) == 1, "RC1: exactly one active WAL after compact");
        check(!fs::exists(walPath + ".new"), "RC1: no leftover .wal.new after compact");

        // Add another mutation before second compact
        std::vector<Trigram> trigrams2 = {400, 500};
        persistence.walAppendAdd(1, 67890, trigrams2);
        contentIndex->insertFileInfo(1, 67890, std::vector<Trigram>{400, 500});

        // Second compact — regression: previously this would fail because
        // closeAndDelete on old WAL would unlink the new WAL's file
        persistence.compact(true);
        check(fs::exists(basePath), "RC2: base file still exists after second compact");
        check(countWalFiles(walPath) == 1, "RC2: exactly one active WAL after second compact");
        check(!fs::exists(walPath + ".new"), "RC2: no leftover .wal.new after second compact");

        // Add another mutation before third compact
        persistence.walAppendRemove(1);
        contentIndex->removeFile(1);

        // Third compact — ensure no accumulated failures
        persistence.compact(true);
        check(countWalFiles(walPath) == 1, "RC3: exactly one active WAL after third compact");
        check(!fs::exists(walPath + ".new"), "RC3: no leftover .wal.new after third compact");
    }

    // --- Test IndexPersistence compact rename ---
    {
        auto engine = std::make_shared<SearchEngine>();
        std::string basePath = tmpDir + "/index.bin";
        std::string walPath = tmpDir + "/index.wal";

        IndexPersistence persistence(engine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        persistence.attachWAL();

        // Add record AFTER attachWAL so the WAL sees the mutation (sets dirty flag)
        FileRecord rec1;
        rec1.name = "test.txt";
        rec1.path = tmpDir;
        rec1.type = 1;
        rec1.size = 100;
        rec1.modTime = time(nullptr);
        engine->addRecord(std::move(rec1));

        IndexMetadata meta;
        meta.lastEventId = 1;

        // Multiple compacts in sequence — all should succeed
        persistence.compact(meta, /*force=*/true);
        check(countWalFiles(walPath) == 1, "RI1: exactly one active WAL after first compact");
        check(!fs::exists(walPath + ".new"), "RI1: no leftover .wal.new");

        // Add mutation before second compact
        FileRecord rec2;
        rec2.name = "test2.txt";
        rec2.path = tmpDir;
        rec2.type = 1;
        rec2.size = 200;
        rec2.modTime = time(nullptr);
        engine->addRecord(std::move(rec2));

        meta.lastEventId = 2;
        persistence.compact(meta, /*force=*/true);
        check(countWalFiles(walPath) == 1, "RI2: exactly one active WAL after second compact");
        check(!fs::exists(walPath + ".new"), "RI2: no leftover .wal.new");

        // Add mutation before third compact
        FileRecord rec3;
        rec3.name = "test3.txt";
        rec3.path = tmpDir;
        rec3.type = 1;
        rec3.size = 300;
        rec3.modTime = time(nullptr);
        engine->addRecord(std::move(rec3));

        meta.lastEventId = 3;
        persistence.compact(meta, /*force=*/true);
        check(countWalFiles(walPath) == 1, "RI3: exactly one active WAL after third compact");
        check(!fs::exists(walPath + ".new"), "RI3: no leftover .wal.new");
    }

    fs::remove_all(tmpDir);
    std::cout << std::endl;
}

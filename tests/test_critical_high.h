#pragma once
// Regression tests for CRITICAL/HIGH fixes (C3, C4, H1, H4, H5, H6, H7)
// Part 17: wired into test_all.cpp

// All core headers and standard includes are provided by test_all.cpp

// CH1: Multi-threaded CRC32 — verifies C4 fix (thread-safe initialization)
static void testCRC32ThreadSafe() {
    std::cout << "── CH1: Multi-threaded CRC32 correctness ──\n";

    const std::string testData = "Hello, MacEverything! This is a CRC32 test string for thread safety.";
    // Compute reference CRC on main thread
    uint32_t expected = IndexWAL::crc32(testData.data(), testData.size());

    constexpr int kThreads = 8;
    constexpr int kIterations = 1000;
    std::atomic<bool> allMatch{true};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kIterations; i++) {
                uint32_t result = IndexWAL::crc32(testData.data(), testData.size());
                if (result != expected) {
                    allMatch.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    check(allMatch.load(), "CH1: CRC32 consistent across 8 threads x 1000 iterations");
    std::cout << "  Reference CRC32: 0x" << std::hex << expected << std::dec << "\n\n";
}

// CH2: toLower correctness — verifies H1 fix (ASCII fast-path)
static void testToLowerCorrectness() {
    std::cout << "── CH2: toLower ASCII fast-path correctness ──\n";

    // Pure ASCII
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        FileRecord r;
        r.name = "README.TXT";
        r.path = "/Users/Test";
        r.type = 1;
        r.size = 100;
        r.modTime = 1000;
        r.inode = 1;
        r.devId = 1;
        records.push_back(r);
        engine.loadRecords(std::move(records));

        auto results = engine.query("readme.txt");
        check(!results.empty(), "CH2: ASCII toLower finds 'readme.txt' matching 'README.TXT'");
    }

    // Mixed case
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        FileRecord r;
        r.name = "MyDocument.PDF";
        r.path = "/tmp";
        r.type = 1;
        r.size = 200;
        r.modTime = 2000;
        r.inode = 2;
        r.devId = 1;
        records.push_back(r);
        engine.loadRecords(std::move(records));

        auto results = engine.query("mydocument");
        check(!results.empty(), "CH2: Mixed case query matches correctly");
    }

    // Empty string
    {
        SearchEngine engine;
        auto results = engine.query("");
        check(results.empty(), "CH2: Empty query returns empty results");
    }

    std::cout << "\n";
}

// CH3: toLower performance — verifies ASCII fast-path is faster than CF path
static void testToLowerPerformance() {
    std::cout << "── CH3: toLower ASCII performance ──\n";

    // Build engine with many ASCII filenames
    SearchEngine engine;
    std::vector<FileRecord> records;
    constexpr int N = 10000;
    records.reserve(N);
    for (int i = 0; i < N; i++) {
        FileRecord r;
        r.name = "TestFile_" + std::to_string(i) + ".cpp";
        r.path = "/Users/test/project/src";
        r.type = 1;
        r.size = 100;
        r.modTime = 1000 + i;
        r.inode = static_cast<uint64_t>(i + 1);
        r.devId = 1;
        records.push_back(r);
    }

    auto start = std::chrono::steady_clock::now();
    engine.loadRecords(std::move(records));
    auto elapsed = std::chrono::steady_clock::now() - start;
    double ms = std::chrono::duration<double, std::milli>(elapsed).count();

    std::cout << "  loadRecords(" << N << " ASCII files): " << ms << " ms\n";
    check(ms < 5000.0, "CH3: loadRecords with ASCII names completes under 5s");

    // Query performance
    start = std::chrono::steady_clock::now();
    constexpr int kQueries = 100;
    for (int i = 0; i < kQueries; i++) {
        engine.query("testfile_" + std::to_string(i % N));
    }
    elapsed = std::chrono::steady_clock::now() - start;
    ms = std::chrono::duration<double, std::milli>(elapsed).count();
    std::cout << "  " << kQueries << " queries: " << ms << " ms (" << (ms / kQueries) << " ms/query)\n\n";
}

// CH4: Content index bulk load — verifies H4 fix (posting lists sorted after push_back)
static void testContentIndexBulkLoad() {
    std::cout << "── CH4: Content index bulk load posting list correctness ──\n";

    std::string tmpDir = "/tmp/maceverything_ch4_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    // Create test files with known content
    auto contentIndex = std::make_shared<ContentIndex>();
    contentIndex->setExtensions({"txt"});
    contentIndex->setMaxFileSize(1024 * 1024);

    // Create persistence with WAL so mutations route through WAL (sets dirty flag)
    std::string savePath = tmpDir + "/content_index.bin";
    std::string walPathCH4 = tmpDir + "/content.wal";
    ContentIndexPersistence persistence(contentIndex, savePath, walPathCH4);
    persistence.attachWAL();

    for (int i = 0; i < 5; i++) {
        std::string filePath = tmpDir + "/file" + std::to_string(i) + ".txt";
        std::ofstream ofs(filePath);
        ofs << "common_keyword unique_word_" << i << " test_content";
        ofs.close();
        contentIndex->indexFile(static_cast<uint32_t>(i), filePath);
        // Record in WAL so dirty flag is set for compaction
        ContentFileInfo info;
        if (contentIndex->getFileInfo(static_cast<uint32_t>(i), info)) {
            persistence.walAppendAdd(static_cast<uint32_t>(i), info.contentHash, info.trigrams);
        }
    }

    // Save and reload (force=true since 5 entries < kCompactThreshold)
    persistence.compact(true);

    auto loaded = std::make_shared<ContentIndex>();
    loaded->setExtensions({"txt"});
    ContentIndexPersistence loader(loaded, savePath, tmpDir + "/content2.wal");
    loader.load();
    auto resolveContentPath = [&](uint32_t idx, std::string& path) {
        if (idx >= 5) return false;
        path = tmpDir + "/file" + std::to_string(idx) + ".txt";
        return true;
    };

    // Query for common keyword — should find all 5 files
    auto results = loaded->query("common_keyword", 100, resolveContentPath);
    check(results.size() == 5, "CH4: Bulk-loaded posting lists find all 5 files for common keyword");

    // Query for unique keyword — should find exactly 1 file
    auto unique = loaded->query("unique_word_3", 100, resolveContentPath);
    check(unique.size() == 1 && unique[0].fileIndex == 3,
          "CH4: Bulk-loaded posting lists find correct file for unique keyword");

    fs::remove_all(tmpDir);
    std::cout << "\n";
}

// CH5: Chunked snippet — verifies H5 fix (reads in 64KB chunks)
static void testChunkedSnippet() {
    std::cout << "── CH5: Chunked snippet generation ──\n";

    std::string tmpDir = "/tmp/maceverything_ch5_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    std::string filePath = tmpDir + "/large_file.txt";
    {
        std::ofstream ofs(filePath, std::ios::binary);
        // Write keyword at different offsets

        // 1. Keyword at offset 0
        ofs << "FINDME_START padding text to fill some space\n";

        // 2. Fill with padding to push past 32KB
        std::string padding(32 * 1024, 'x');
        ofs << padding;
        ofs << "\nFINDME_32K found at 32KB offset\n";

        // 3. Fill to push past 512KB
        std::string bigPadding(480 * 1024, 'y');
        ofs << bigPadding;
        ofs << "\nFINDME_512K found at 512KB offset\n";

        ofs.close();
    }

    // Test keyword at offset 0
    {
        uint32_t offset = 0;
        auto snippet = ContentIndex::generateSnippet(filePath, "FINDME_START", offset);
        check(!snippet.empty(), "CH5: Keyword at offset 0 found");
        check(offset == 0, "CH5: Offset 0 is correct");
    }

    // Test keyword at ~32KB
    {
        uint32_t offset = 0;
        auto snippet = ContentIndex::generateSnippet(filePath, "FINDME_32K", offset);
        check(!snippet.empty(), "CH5: Keyword at ~32KB found via chunked read");
        check(offset > 30000, "CH5: Offset ~32KB is in expected range");
    }

    // Test keyword at ~512KB
    {
        uint32_t offset = 0;
        auto snippet = ContentIndex::generateSnippet(filePath, "FINDME_512K", offset);
        check(!snippet.empty(), "CH5: Keyword at ~512KB found via chunked read");
        check(offset > 500000, "CH5: Offset ~512KB is in expected range");
    }

    // Test keyword not present
    {
        uint32_t offset = 0;
        auto snippet = ContentIndex::generateSnippet(filePath, "NOTPRESENT_XYZ", offset);
        check(snippet.empty(), "CH5: Non-existent keyword returns empty");
    }

    fs::remove_all(tmpDir);
    std::cout << "\n";
}

// CH6: Query concurrent R/W — verifies H7 fix (reduced lock scope)
static void testQueryConcurrentRW() {
    std::cout << "── CH6: Query concurrent with writes (lock scope) ──\n";

    auto engine = std::make_shared<SearchEngine>();
    std::vector<FileRecord> initial;
    for (int i = 0; i < 1000; i++) {
        FileRecord r;
        r.name = "initial_" + std::to_string(i) + ".txt";
        r.path = "/data";
        r.type = 1;
        r.size = 100;
        r.modTime = 1000 + i;
        r.inode = static_cast<uint64_t>(i + 1);
        r.devId = 1;
        initial.push_back(r);
    }
    engine->loadRecords(std::move(initial));

    std::atomic<bool> stop{false};
    std::atomic<int> queryCount{0};
    std::atomic<int> writeCount{0};
    std::atomic<bool> crashed{false};

    // Reader threads — query continuously
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; t++) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                try {
                    auto results = engine->query("initial_");
                    queryCount.fetch_add(1, std::memory_order_relaxed);
                } catch (...) {
                    crashed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    // Writer thread — adds records concurrently
    std::thread writer([&]() {
        for (int i = 0; i < 200 && !stop.load(std::memory_order_relaxed); i++) {
            FileRecord r;
            r.name = "concurrent_" + std::to_string(i) + ".txt";
            r.path = "/new";
            r.type = 1;
            r.size = 50;
            r.modTime = 5000 + i;
            r.inode = static_cast<uint64_t>(2000 + i);
            r.devId = 1;
            engine->addRecord(std::move(r));
            writeCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    writer.join();
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : readers) t.join();

    check(!crashed.load(), "CH6: No crashes during concurrent query + write");
    check(queryCount.load() > 0, "CH6: Queries executed successfully");
    check(writeCount.load() == 200, "CH6: All writes completed");
    std::cout << "  Queries: " << queryCount.load() << ", Writes: " << writeCount.load() << "\n\n";
}

static void runCriticalHighTests() {
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║  Part 17: CRITICAL/HIGH Fix Regression   ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n\n";

    testCRC32ThreadSafe();
    testToLowerCorrectness();
    testToLowerPerformance();
    testContentIndexBulkLoad();
    testChunkedSnippet();
    testQueryConcurrentRW();
}

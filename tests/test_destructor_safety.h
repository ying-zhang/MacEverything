#pragma once
// Part 7b: C2 — Destructor use-after-free (stopAutoCompactionAndWait)

static void runDestructorSafetyTest() {
    std::cout << "═══ Part 7b: Destructor Safety (stopAutoCompactionAndWait) ═══\n\n";

    std::string tmpDir = "/tmp/maceverything_c2_test_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    // Test IndexPersistence: create, start auto-compaction, destroy quickly
    {
        auto engine = std::make_shared<SearchEngine>();
        std::string basePath = tmpDir + "/idx.bin";
        std::string walPath = tmpDir + "/idx.wal";
        auto persistence = std::make_unique<IndexPersistence>(engine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
        persistence->attachWAL();
        persistence->startAutoCompaction(0.1, std::shared_ptr<FileSystemWatcher>(nullptr));  // 100ms interval
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        persistence.reset();  // Should not crash — destructor waits for in-flight compaction
        check(persistence == nullptr, "C2: IndexPersistence destroyed safely with active compaction timer");
    }

    // Test ContentIndexPersistence: same pattern
    {
        auto contentIndex = std::make_shared<ContentIndex>();
        std::string basePath = tmpDir + "/cidx.bin";
        std::string walPath = tmpDir + "/cidx.wal";
        auto persistence = std::make_unique<ContentIndexPersistence>(contentIndex, basePath, walPath);
        persistence->attachWAL();
        persistence->startAutoCompaction(0.1);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        persistence.reset();  // Should not crash
        check(persistence == nullptr, "C2: ContentIndexPersistence destroyed safely with active compaction timer");
    }

    fs::remove_all(tmpDir);
    std::cout << "\n";
}

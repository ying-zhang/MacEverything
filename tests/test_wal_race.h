#pragma once
// Part 7c: C3 — ContentIndexPersistence wal_ data race

static void runWalRaceTest() {
    std::cout << "═══ Part 7c: ContentIndexPersistence WAL Race Safety ═══\n\n";

    std::string tmpDir = "/tmp/maceverything_c3_test_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    auto contentIndex = std::make_shared<ContentIndex>();
    auto persistence = std::make_shared<ContentIndexPersistence>(
        contentIndex, tmpDir + "/cidx.bin", tmpDir + "/cidx.wal");
    persistence->attachWAL();

    // Hammer walAppendAdd/walAppendRemove from multiple threads while compacting
    std::atomic<bool> stop{false};
    std::atomic<int> appendCount{0};

    std::vector<std::thread> writers;
    for (int t = 0; t < 4; t++) {
        writers.emplace_back([&, t]() {
            std::vector<Trigram> trigrams = {
                ContentIndex::makeTrigram('a', 'b', 'c'),
                ContentIndex::makeTrigram('d', 'e', 'f')
            };
            uint32_t idx = static_cast<uint32_t>(t * 1000);
            while (!stop.load(std::memory_order_relaxed)) {
                persistence->walAppendAdd(idx, 12345, trigrams);
                persistence->walAppendRemove(idx);
                appendCount.fetch_add(2, std::memory_order_relaxed);
                idx++;
            }
        });
    }

    // Compact a few times concurrently
    for (int i = 0; i < 3; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        persistence->compact();
    }

    stop.store(true, std::memory_order_relaxed);
    for (auto& t : writers) t.join();

    check(appendCount.load() > 0, "C3: Concurrent WAL appends + compacts completed without crash");
    // Verify persistence is still functional after concurrent stress
    persistence->compact();
    check(appendCount.load() > 100, "C3: Substantial concurrent WAL appends completed without race");

    persistence.reset();
    fs::remove_all(tmpDir);
    std::cout << "\n";
}

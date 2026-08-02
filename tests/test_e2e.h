#pragma once
// ═══════════════════════════════════════════════════════
//  Part 6: End-to-End Integration Test
//  (FSEvents -> SearchEngine pipeline)
// ═══════════════════════════════════════════════════════

static void runEndToEndTest() {
    std::cout << "========================================\n";
    std::cout << "  Part 6: End-to-End Integration\n";
    std::cout << "  (FSEvents -> SearchEngine pipeline)\n";
    std::cout << "========================================\n\n";

    std::string tmpDir = "/tmp/maceverything_e2e_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    SearchEngine engine;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<int> changeCount{0};

    FileSystemWatcher watcher;
    watcher.start(tmpDir, [&](std::vector<FileSystemWatcher::Event> events) {
        for (const auto& event : events) {
            const std::string& path = event.path;
            FSEventStreamEventFlags flags = event.flags;

            bool itemRemoved = (flags & kFSEventStreamEventFlagItemRemoved) != 0;
            bool itemRenamed = (flags & kFSEventStreamEventFlagItemRenamed) != 0;

            struct stat st;
            bool exists = (lstat(path.c_str(), &st) == 0);

            if (itemRemoved || (itemRenamed && !exists)) {
                engine.removeByPath(path);
            } else if (exists) {
                std::string dirPath, fileName;
                size_t lastSlash = path.rfind('/');
                if (lastSlash != std::string::npos) {
                    dirPath = path.substr(0, lastSlash);
                    fileName = path.substr(lastSlash + 1);
                } else {
                    dirPath = ".";
                    fileName = path;
                }
                if (fileName.empty()) continue;

                uint8_t type = 4;
                if (S_ISREG(st.st_mode))     type = 1;
                else if (S_ISDIR(st.st_mode)) type = 2;
                else if (S_ISLNK(st.st_mode)) type = 3;

                FileRecord record;
                record.name = fileName;
                record.path = dirPath;
                record.type = type;
                record.size = S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0;
                record.modTime = st.st_mtime;

                engine.updateByPath(path, std::move(record));
            }
        }
        changeCount.fetch_add(1);
        cv.notify_all();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto waitForChange = [&](int prevCount, int timeoutMs) -> bool {
        std::unique_lock<std::mutex> lock(mtx);
        return cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
            return changeCount.load() > prevCount;
        });
    };

    // Create a file
    int prevCount = changeCount.load();
    {
        std::ofstream ofs(tmpDir + "/e2e_test.txt");
        ofs << "e2e test content\n";
    }
    waitForChange(prevCount, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto res = engine.query("e2e_test");
    check(res.size() == 1, "E2E: Created file appears in search");
    if (!res.empty()) {
        const auto& r = engine.getRecord(res[0]);
        check(r.type == 1, "E2E: Record type is file (1)");
        check(r.size > 0, "E2E: Record size > 0");
    }
    check(engine.liveRecordCount() >= 1, "E2E: liveRecordCount >= 1");

    // Rename the file
    prevCount = changeCount.load();
    fs::rename(tmpDir + "/e2e_test.txt", tmpDir + "/e2e_renamed.txt");
    waitForChange(prevCount, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    res = engine.query("e2e_renamed");
    check(res.size() == 1, "E2E: Renamed file appears in search");
    // The old name should be gone (tombstoned)
    res = engine.query("e2e_test");
    // Note: "e2e_test" is a substring of nothing after rename, but "e2e_renamed" does not contain "e2e_test"
    // Actually "e2e_test" won't match "e2e_renamed" since query matches on filename only
    // Let's verify the exact old path is removed
    bool oldRemoved = engine.indexForPath(tmpDir + "/e2e_test.txt") == UINT32_MAX;
    check(oldRemoved, "E2E: Old path no longer in index");

    // Delete the file
    prevCount = changeCount.load();
    fs::remove(tmpDir + "/e2e_renamed.txt");
    waitForChange(prevCount, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    res = engine.query("e2e_renamed");
    check(res.empty(), "E2E: Deleted file removed from search");

    watcher.stop();
    fs::remove_all(tmpDir);
    std::cout << "\n";
}

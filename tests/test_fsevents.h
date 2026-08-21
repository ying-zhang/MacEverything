#pragma once
// ═══════════════════════════════════════════════════════
//  Part 4: FSEvents Integration Test
// ═══════════════════════════════════════════════════════

static void runFSEventsTest() {
    std::cout << "========================================\n";
    std::cout << "  Part 4: FSEvents Integration Test\n";
    std::cout << "========================================\n\n";

    // Create temp directory
    std::string tmpDir = "/tmp/maceverything_test_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<FileSystemWatcher::Event> receivedEvents;

    FileSystemWatcher watcher;
    watcher.start(tmpDir, [&](std::vector<FileSystemWatcher::Event> events) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& e : events) {
            receivedEvents.push_back(std::move(e));
        }
        cv.notify_all();
    });

    check(watcher.isRunning(), "FileSystemWatcher started");

    // Wait a bit for FSEvents to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto waitForEvents = [&](int expectedMin, int timeoutMs) -> bool {
        std::unique_lock<std::mutex> lock(mtx);
        return cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
            return (int)receivedEvents.size() >= expectedMin;
        });
    };

    // ── Test 1: Create file ──
    std::cout << "  --- File Create ---\n";
    receivedEvents.clear();
    auto createStart = std::chrono::steady_clock::now();
    runShellCommand("echo 'hello world' > " + tmpDir + "/testfile.txt");
    bool gotCreate = waitForEvents(1, 3000);
    auto createEnd = std::chrono::steady_clock::now();
    double createLatency = std::chrono::duration<double>(createEnd - createStart).count() * 1000;

    check(gotCreate, "Create event received");
    if (gotCreate) {
        bool foundPath = false;
        for (const auto& e : receivedEvents) {
            if (e.path.find("testfile.txt") != std::string::npos) {
                foundPath = true;
                break;
            }
        }
        check(foundPath, "Create event has correct path");
        std::cout << "    Event latency: " << std::fixed << std::setprecision(0) << createLatency << "ms\n";
        std::cout << "    Events received: " << receivedEvents.size() << "\n";
    }

    // ── Test 2: Modify file ──
    std::cout << "\n  --- File Modify ---\n";
    receivedEvents.clear();
    auto modStart = std::chrono::steady_clock::now();
    runShellCommand("echo 'more content' >> " + tmpDir + "/testfile.txt");
    bool gotModify = waitForEvents(1, 3000);
    auto modEnd = std::chrono::steady_clock::now();
    double modLatency = std::chrono::duration<double>(modEnd - modStart).count() * 1000;

    check(gotModify, "Modify event received");
    if (gotModify) {
        std::cout << "    Event latency: " << std::fixed << std::setprecision(0) << modLatency << "ms\n";
        std::cout << "    Events received: " << receivedEvents.size() << "\n";
    }

    // ── Test 3: Rename file ──
    std::cout << "\n  --- File Rename ---\n";
    receivedEvents.clear();
    auto renStart = std::chrono::steady_clock::now();
    runShellCommand("mv " + tmpDir + "/testfile.txt " + tmpDir + "/renamed.txt");
    bool gotRename = waitForEvents(1, 3000);
    auto renEnd = std::chrono::steady_clock::now();
    double renLatency = std::chrono::duration<double>(renEnd - renStart).count() * 1000;

    check(gotRename, "Rename event received");
    if (gotRename) {
        std::cout << "    Event latency: " << std::fixed << std::setprecision(0) << renLatency << "ms\n";
        std::cout << "    Events received: " << receivedEvents.size() << "\n";
    }

    // ── Test 4: Delete file ──
    std::cout << "\n  --- File Delete ---\n";
    receivedEvents.clear();
    auto delStart = std::chrono::steady_clock::now();
    runShellCommand("rm " + tmpDir + "/renamed.txt");
    bool gotDelete = waitForEvents(1, 3000);
    auto delEnd = std::chrono::steady_clock::now();
    double delLatency = std::chrono::duration<double>(delEnd - delStart).count() * 1000;

    check(gotDelete, "Delete event received");
    if (gotDelete) {
        std::cout << "    Event latency: " << std::fixed << std::setprecision(0) << delLatency << "ms\n";
        std::cout << "    Events received: " << receivedEvents.size() << "\n";
    }

    // ── Test 5: Batch create ──
    std::cout << "\n  --- Batch Create (100 files) ---\n";
    receivedEvents.clear();
    auto batchStart = std::chrono::steady_clock::now();
    runShellCommand("for i in $(seq 0 99); do echo 'batch' > " + tmpDir + "/batch_$i.txt; done");
    // Wait for at least some events (FSEvents coalesces)
    bool gotBatch = waitForEvents(10, 5000);
    auto batchEnd = std::chrono::steady_clock::now();
    double batchLatency = std::chrono::duration<double>(batchEnd - batchStart).count() * 1000;

    check(gotBatch, "Batch create: received events");
    std::cout << "    Events received: " << receivedEvents.size() << "\n";
    std::cout << "    Batch latency: " << std::fixed << std::setprecision(0) << batchLatency << "ms\n";

    // Stop watcher
    watcher.stop();
    check(!watcher.isRunning(), "FileSystemWatcher stopped");

    // Cleanup
    fs::remove_all(tmpDir);
    std::cout << "\n";
}

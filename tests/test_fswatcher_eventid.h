#pragma once

inline void runFSWatcherEventIdTests() {
    std::cout << "\n═══════════════════════════════════════════\n";
    std::cout << "  Part 37: FSWatcher getCurrentSystemEventId\n";
    std::cout << "═══════════════════════════════════════════\n\n";

    // Test 1: getCurrentSystemEventId() returns a non-zero value
    {
        FSEventStreamEventId eid = FileSystemWatcher::getCurrentSystemEventId();
        check(eid > 0, "getCurrentSystemEventId() should return non-zero");
        std::cout << "  [info] getCurrentSystemEventId() = " << eid << "\n";
    }

    // Test 2: Consecutive calls are monotonically non-decreasing
    {
        FSEventStreamEventId eid1 = FileSystemWatcher::getCurrentSystemEventId();
        FSEventStreamEventId eid2 = FileSystemWatcher::getCurrentSystemEventId();
        check(eid2 >= eid1, "getCurrentSystemEventId() should be monotonically non-decreasing");
    }

    // Test 3: A new watcher has lastEventId = 0 before any callback
    {
        FileSystemWatcher w("test-eventid");
        check(w.getLastEventId() == 0,
               "New watcher should have lastEventId=0 before start");
    }

    // Test 4: totalEventsReceived starts at 0
    {
        FileSystemWatcher w("test-events-count");
        check(w.totalEventsReceived() == 0,
               "New watcher should have totalEventsReceived=0");
    }

    std::cout << "\n";
}

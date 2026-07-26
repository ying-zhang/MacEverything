#pragma once
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

class FileSystemWatcher {
public:
    struct Event {
        std::string path;
        FSEventStreamEventFlags flags;
    };

    using Callback = std::function<void(std::vector<Event>)>;
    using ReplayDoneCallback = std::function<void()>;

    explicit FileSystemWatcher(std::string label = "default") : label_(std::move(label)) {}
    ~FileSystemWatcher();

    FileSystemWatcher(const FileSystemWatcher&) = delete;
    FileSystemWatcher& operator=(const FileSystemWatcher&) = delete;

    /// Start monitoring from now (kFSEventStreamEventIdSinceNow).
    void start(const std::string& rootPath, Callback callback);
    void start(const std::vector<std::string>& rootPaths, Callback callback);

    /// Start monitoring from a specific event ID (for replaying missed events).
    /// When sinceEventId != 0, FSEvents will replay all events since that ID.
    /// Set onReplayDone to be notified when HistoryDone fires (replay complete).
    void start(const std::string& rootPath, FSEventStreamEventId sinceEventId,
               Callback callback, ReplayDoneCallback onReplayDone = nullptr);
    void start(const std::vector<std::string>& rootPaths, FSEventStreamEventId sinceEventId,
               Callback callback, ReplayDoneCallback onReplayDone = nullptr);

    /// Stop monitoring.
    void stop();

    bool isRunning() const {
        return running_.load(std::memory_order_acquire);
    }

    /// Get the latest event ID seen by the watcher (for persistence).
    FSEventStreamEventId getLastEventId() const {
        return lastEventId_.load(std::memory_order_relaxed);
    }

    /// Whether the FSEvents journal was truncated (kFSEventStreamEventFlagMustScanSubDirs seen).
    bool isJournalTruncated() const {
        return journalTruncated_.load(std::memory_order_relaxed);
    }

    /// Total raw events received across all callback invocations (for diagnostics).
    uint64_t totalEventsReceived() const {
        return totalEventsReceived_.load(std::memory_order_relaxed);
    }

    /// Get the current system-wide FSEvents event ID without requiring a stream.
    /// Uses FSEventsGetLastEventIdForDeviceBeforeTime() on the root device.
    static FSEventStreamEventId getCurrentSystemEventId();

    /// Set a semaphore to signal on journal truncation (for early abort of replay waits).
    /// Accepts void* to avoid ABI mismatch between C++ and Objective-C++ (dispatch_semaphore_t
    /// has different mangling under ARC). Caller must pass a dispatch_semaphore_t cast to void*.
    void setEarlyAbortSemaphore(void* sem);

    /// Set paths to exclude from FSEvents monitoring.
    /// Must be called before start(). Paths are matched as prefixes.
    void setExclusionPaths(std::vector<std::string> paths);

private:
    std::string label_;
    FSEventStreamRef stream_ = nullptr;
    dispatch_queue_t queue_ = nullptr;
    void* queueKey_ = nullptr;
    Callback callback_;
    ReplayDoneCallback onReplayDone_;
    std::atomic<FSEventStreamEventId> lastEventId_{0};
    std::atomic<bool> journalTruncated_{false};
    std::atomic<uint64_t> totalEventsReceived_{0};
    std::atomic<bool> running_{false};
    std::atomic<void*> earlyAbortSem_{nullptr}; // dispatch_semaphore_t; void* for ABI compat
    std::vector<std::string> exclusionPaths_;
    std::mutex stateMutex_;
    mutable std::mutex lifecycleMutex_;

    void startInternal(const std::vector<std::string>& rootPaths, FSEventStreamEventId sinceEventId);

    static void fseventsCallback(
        ConstFSEventStreamRef streamRef,
        void* clientCallBackInfo,
        size_t numEvents,
        void* eventPaths,
        const FSEventStreamEventFlags eventFlags[],
        const FSEventStreamEventId eventIds[]);
};

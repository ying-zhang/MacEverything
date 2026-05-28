#include "FileSystemWatcher.h"
#include "Logger.h"
#include <cstring>

FileSystemWatcher::~FileSystemWatcher() {
    stop();
}

void FileSystemWatcher::start(const std::string& rootPath, Callback callback) {
    start(std::vector<std::string>{rootPath}, std::move(callback));
}

void FileSystemWatcher::start(const std::vector<std::string>& rootPaths, Callback callback) {
    if (stream_) return;
    callback_ = std::move(callback);
    onReplayDone_ = nullptr;
    startInternal(rootPaths, kFSEventStreamEventIdSinceNow);
}

void FileSystemWatcher::start(const std::string& rootPath,
                               FSEventStreamEventId sinceEventId,
                               Callback callback,
                               ReplayDoneCallback onReplayDone) {
    start(std::vector<std::string>{rootPath}, sinceEventId, std::move(callback), std::move(onReplayDone));
}

void FileSystemWatcher::start(const std::vector<std::string>& rootPaths,
                               FSEventStreamEventId sinceEventId,
                               Callback callback,
                               ReplayDoneCallback onReplayDone) {
    if (stream_) return;
    callback_ = std::move(callback);
    onReplayDone_ = std::move(onReplayDone);
    startInternal(rootPaths, sinceEventId);
}

void FileSystemWatcher::setExclusionPaths(std::vector<std::string> paths) {
    exclusionPaths_ = std::move(paths);
}

FSEventStreamEventId FileSystemWatcher::getCurrentSystemEventId() {
    return FSEventsGetCurrentEventId();
}

void FileSystemWatcher::setEarlyAbortSemaphore(void* sem) {
    earlyAbortSem_ = sem;
}

void FileSystemWatcher::startInternal(const std::vector<std::string>& rootPaths,
                                       FSEventStreamEventId sinceEventId) {
    journalTruncated_.store(false, std::memory_order_relaxed);
    totalEventsReceived_.store(0, std::memory_order_relaxed);

    CFMutableArrayRef pathsToWatch = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    for (const auto& rootPath : rootPaths) {
        CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault,
                                                      rootPath.c_str(),
                                                      kCFStringEncodingUTF8);
        CFArrayAppendValue(pathsToWatch, path);
        CFRelease(path);
    }
    if (CFArrayGetCount(pathsToWatch) == 0) {
        CFRelease(pathsToWatch);
        return;
    }

    FSEventStreamContext context = {};
    context.info = this;

    stream_ = FSEventStreamCreate(
        kCFAllocatorDefault,
        &FileSystemWatcher::fseventsCallback,
        &context,
        pathsToWatch,
        sinceEventId,
        0.3, // 300ms coalesce latency
        kFSEventStreamCreateFlagFileEvents |
        kFSEventStreamCreateFlagNoDefer |
        kFSEventStreamCreateFlagUseCFTypes |
        kFSEventStreamCreateFlagIgnoreSelf
    );

    CFRelease(pathsToWatch);
    if (!stream_) return;

    if (!exclusionPaths_.empty()) {
        CFMutableArrayRef exclusions = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
        for (const auto& p : exclusionPaths_) {
            CFStringRef cfp = CFStringCreateWithCString(kCFAllocatorDefault, p.c_str(), kCFStringEncodingUTF8);
            CFArrayAppendValue(exclusions, cfp);
            CFRelease(cfp);
        }
        FSEventStreamSetExclusionPaths(stream_, exclusions);
        CFRelease(exclusions);
    }

    queue_ = dispatch_queue_create("com.maceverything.fswatcher", DISPATCH_QUEUE_SERIAL);
    FSEventStreamSetDispatchQueue(stream_, queue_);
    FSEventStreamStart(stream_);
    LOG_INFO("FSWatcher", "[" << label_ << "] Started watching "
             << rootPaths.size() << " root(s)");
}

void FileSystemWatcher::stop() {
    if (!stream_ && !queue_) return;
    LOG_INFO("FSWatcher", "[" << label_ << "] Stopped");
    if (stream_) {
        FSEventStreamStop(stream_);
        FSEventStreamInvalidate(stream_);
        FSEventStreamRelease(stream_);
        stream_ = nullptr;
    }
    if (queue_) {
        // C3 fix: Drain any in-flight callback on the serial queue, then null
        // the std::function captures on the queue thread to avoid a data race
        // (fseventsCallback reads these fields on the same queue).
        dispatch_sync(queue_, ^{
            callback_ = nullptr;
            onReplayDone_ = nullptr;
        });
        dispatch_release(queue_);
        queue_ = nullptr;
    } else {
        callback_ = nullptr;
        onReplayDone_ = nullptr;
    }
}

void FileSystemWatcher::fseventsCallback(
    ConstFSEventStreamRef /*streamRef*/,
    void* clientCallBackInfo,
    size_t numEvents,
    void* eventPaths,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[])
{
    auto* watcher = static_cast<FileSystemWatcher*>(clientCallBackInfo);

    // Track the latest event ID for persistence
    if (numEvents > 0) {
        watcher->lastEventId_.store(eventIds[numEvents - 1], std::memory_order_relaxed);
    }

    // Accumulate total raw events received (for replay diagnostics)
    watcher->totalEventsReceived_.fetch_add(numEvents, std::memory_order_relaxed);

    CFArrayRef paths = static_cast<CFArrayRef>(eventPaths);
    std::vector<Event> events;
    events.reserve(numEvents);

    bool historyDone = false;

    for (size_t i = 0; i < numEvents; i++) {
        FSEventStreamEventFlags flags = eventFlags[i];

        // Skip root-changed meta events
        if (flags & kFSEventStreamEventFlagRootChanged) continue;

        // H-7: Skip unmount events (e.g. external disk ejected)
        if (flags & kFSEventStreamEventFlagUnmount) continue;

        // Detect history-done — replay is complete
        if (flags & kFSEventStreamEventFlagHistoryDone) {
            historyDone = true;
            continue;
        }

        // Parse path first — needed for exclusion checks below
        CFStringRef cfPath = static_cast<CFStringRef>(CFArrayGetValueAtIndex(paths, static_cast<CFIndex>(i)));
        char pathBuf[PATH_MAX];
        if (!CFStringGetCString(cfPath, pathBuf, sizeof(pathBuf), kCFStringEncodingUTF8)) continue;

        std::string pathStr(pathBuf);

        // Filter out noisy system paths
        if (pathStr.find("/.Spotlight-V100/") != std::string::npos) continue;
        if (pathStr.find("/.fseventsd/") != std::string::npos) continue;
        if (pathStr.find("/.Trashes/") != std::string::npos) continue;
        if (pathStr.find("/private/var/folders/") != std::string::npos &&
            pathStr.find("/com.apple.") != std::string::npos) continue;

        // Skip events from app's own excluded directories
        // (path is inside an exclusion directory)
        bool excluded = false;
        for (const auto& ep : watcher->exclusionPaths_) {
            if (pathStr.size() >= ep.size() && pathStr.compare(0, ep.size(), ep) == 0
                && (pathStr.size() == ep.size() || pathStr[ep.size()] == '/')) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;

        // Detect journal truncation — need full rescan.
        // Skip MustScanSubDirs events whose path is an ancestor of an excluded
        // directory: these are typically triggered by the app's own cache writes
        // (e.g., path="/Users/.../Library/Caches/" is ancestor of the excluded
        // ".../Library/Caches/com.maceverything.app").
        if (flags & kFSEventStreamEventFlagMustScanSubDirs) {
            bool isExcludedAncestor = false;
            for (const auto& ep : watcher->exclusionPaths_) {
                if (ep.size() > pathStr.size() &&
                    ep.compare(0, pathStr.size(), pathStr) == 0) {
                    isExcludedAncestor = true;
                    break;
                }
            }
            if (isExcludedAncestor) continue;

            watcher->journalTruncated_.store(true, std::memory_order_relaxed);
            // Signal early abort so replay waiter doesn't block for the full timeout
            if (watcher->earlyAbortSem_) {
                dispatch_semaphore_signal(static_cast<dispatch_semaphore_t>(watcher->earlyAbortSem_));
            }
        }

        events.push_back({std::move(pathStr), flags});
    }

    if (!events.empty() && watcher->callback_) {
        LOG_DEBUG("FSWatcher", "Received " << events.size() << " events (from " << numEvents << " raw)");
        watcher->callback_(std::move(events));
    }

    // Fire replay-done callback after delivering any final events
    if (historyDone && watcher->onReplayDone_) {
        auto cb = std::move(watcher->onReplayDone_);
        watcher->onReplayDone_ = nullptr;
        cb();
    }
}

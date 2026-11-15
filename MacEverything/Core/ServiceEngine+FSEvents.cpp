#include "ServiceEngine.h"
#include "DirectoryScanner.h"
#include "RescanDebounce.h"
#include "Logger.h"
#include <algorithm>
#include "PathUtils.h"
#include <filesystem>
#include <sys/stat.h>
#include <fnmatch.h>
#include <cerrno>
#include <cctype>

namespace fs = std::filesystem;

namespace {
bool mapEventPathToConfiguredRoot(const std::string& root, const std::string& path,
                                 std::string& mappedPath);

bool hasAppBundleSuffix(const std::string& path) {
    if (path.size() < 4) return false;
    const size_t offset = path.size() - 4;
    return path[offset] == '.' && std::tolower(static_cast<unsigned char>(path[offset + 1])) == 'a' &&
           std::tolower(static_cast<unsigned char>(path[offset + 2])) == 'p' &&
           std::tolower(static_cast<unsigned char>(path[offset + 3])) == 'p';
}
}

int ServiceEngine::lstatEventPath(const std::string& path, struct stat& result) const {
    const int error = eventLstatErrorForTests_.load(std::memory_order_acquire);
    if (error != 0) {
        errno = error;
        return -1;
    }
    return lstat(path.c_str(), &result);
}

void ServiceEngine::applyFSEvents(
    const std::vector<FileSystemWatcher::Event>& events,
    std::shared_ptr<SearchEngine> engine) {
    if (!engine || events.empty()) return;
    directEventApplyCount_.fetch_add(events.size(), std::memory_order_acq_rel);

    const auto cfg = safeConfig();
    const auto roots = effectiveScanRoots();
    std::vector<SearchEngine::MutationOp> ops;
    std::vector<std::string> contentRemovals;
    std::vector<std::string> contentUpserts;
    ops.reserve(events.size());
    contentRemovals.reserve(events.size());
    contentUpserts.reserve(events.size());

    for (const auto& event : events) {
        const auto& eventPath = event.path;
        std::string path = eventPath;
        for (const auto& root : roots) {
            if (mapEventPathToConfiguredRoot(root, eventPath, path)) break;
        }
        if (!isPathAllowedByConfig(path, false) || rescanDirectoryForEvent(path).empty()) continue;
        if (!cfg.includeAppBundleContents && isInsideAppBundle(path)) continue;

        struct stat st {};
        const bool exists = lstatEventPath(path, st) == 0;
        if (!exists) {
            // An inaccessible file is not evidence of deletion. A later event
            // or reconciliation can retry without destroying the last snapshot.
            if (errno != ENOENT && errno != ENOTDIR) continue;
            if (cfg.contentIndexingEnabled) contentRemovals.push_back(path);
            ops.push_back({SearchEngine::MutationOp::REMOVE, path, {}});
            continue;
        }

        const size_t slash = path.rfind('/');
        const std::string dirPath = slash == std::string::npos ? "." : path.substr(0, slash);
        const std::string fileName = slash == std::string::npos ? path : path.substr(slash + 1);
        if (fileName.empty()) continue;

        uint8_t type = 4;
        if (S_ISREG(st.st_mode)) type = 1;
        else if (S_ISDIR(st.st_mode)) type = pathEndsWithApp(path) ? 5 : 2;
        else if (S_ISLNK(st.st_mode)) type = 3;

        FileRecord record;
        record.name = fileName;
        record.path = dirPath;
        record.type = type;
        record.size = S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0;
        record.modTime = st.st_mtime;
        record.inode = st.st_ino;
        record.devId = static_cast<int32_t>(st.st_dev);
        ops.push_back({SearchEngine::MutationOp::UPDATE, path, std::move(record)});
        if (type == 1 && cfg.contentIndexingEnabled && isPathAllowedByConfig(path, true)) {
            contentUpserts.push_back(path);
        }
    }

    for (const auto& path : contentRemovals) updateContentForPath(path, true, engine);
    engine->batchMutate(std::move(ops));
    for (const auto& path : contentUpserts) updateContentForPath(path, false, engine);
}

// ═══════════════════════════════════════════════════════
//  Live monitoring
// ═══════════════════════════════════════════════════════

namespace {

bool mapEventPathToConfiguredRoot(const std::string& root, const std::string& path,
                                  std::string& mappedPath) {
    if (PathUtils::pathContainsOrEquals(root, path)) {
        mappedPath = path;
        return true;
    }
    std::error_code ec;
    const fs::path realRoot = fs::canonical(root, ec);
    if (ec || !PathUtils::pathContainsOrEquals(realRoot.string(), path)) return false;
    const fs::path relative = fs::path(path).lexically_relative(realRoot);
    if (relative.empty() && path != realRoot.string()) return false;
    mappedPath = relative.empty() ? root : (fs::path(root) / relative).lexically_normal().string();
    return true;
}

bool replayStartsEarlier(FSEventStreamEventId requested, FSEventStreamEventId anchor) {
    if (requested == kFSEventStreamEventIdSinceNow) return false;
    if (anchor == 0 || anchor == kFSEventStreamEventIdSinceNow) return true;
    return requested < anchor;
}

} // namespace

std::string ServiceEngine::rescanDirectoryForEvent(const std::string& path,
                                                   bool useEventDirectory) const {
    if (path.empty()) return {};
    const auto roots = effectiveScanRoots();
    const auto cfg = safeConfig();
    for (const auto& root : roots) {
        std::string mappedPath;
        if (!mapEventPathToConfiguredRoot(root, path, mappedPath)) continue;

        fs::path scanRoot(PathUtils::trimTrailingSlash(fs::path(root).lexically_normal().string()));
        fs::path event(PathUtils::trimTrailingSlash(fs::path(mappedPath).lexically_normal().string()));
        std::error_code ec;
        const bool eventIsDirectory = fs::is_directory(event, ec);
        fs::path candidate = (useEventDirectory && eventIsDirectory)
            ? event : event.parent_path();
        if (useEventDirectory && eventIsDirectory && !cfg.includeAppBundleContents &&
            hasAppBundleSuffix(mappedPath)) {
            candidate = event.parent_path();
        }
        if (candidate.empty()) candidate = scanRoot;
        if (event == scanRoot) candidate = scanRoot;
        if (!PathUtils::pathContainsOrEquals(scanRoot.string(), candidate.string())) continue;

        // A path below an excluded component must not be promoted to a scan
        // root: doing so would bypass the scanner's ancestor pruning.
        fs::path cursor = scanRoot;
        auto relative = candidate.lexically_relative(scanRoot);
        if (relative.empty() && candidate != scanRoot) continue;
        bool allowed = true;
        for (const auto& component : relative) {
            if (component == "." || component.empty()) continue;
            cursor /= component;
            const std::string ancestor = cursor.string();
            const std::string name = cursor.filename().string();
            const bool isAppBundleDescendant = !cfg.includeAppBundleContents &&
                isInsideAppBundle(ancestor + "/child");
            if ((!cfg.includeHidden && !name.empty() && name.front() == '.') ||
                isAppBundleDescendant) {
                allowed = false;
                break;
            }
            for (const auto& pattern : cfg.excludedPatterns) {
                if (!pattern.empty() && fnmatch(pattern.c_str(), name.c_str(), FNM_CASEFOLD) == 0) {
                    allowed = false;
                    break;
                }
            }
            if (!allowed || !isPathAllowedByConfig(ancestor, false)) {
                allowed = false;
                break;
            }
        }
        // A broader root may prune this ancestor while an explicit nested
        // root still scans it. Match any root, as the full scanner does.
        if (allowed) return candidate.string();
    }
    return {};
}

void ServiceEngine::startOrRestartVolumeMonitoring() {
    if (!safeConfig().realtimeMonitoring) return;
    if (!startupCompleted_.load(std::memory_order_acquire)) return;
    if (lifecycleBusy()) {
        noteDeferredMonitoringRefresh();
        return;
    }
    if (isMonitoring_.load(std::memory_order_acquire)) restartMonitoring();
    else startMonitoring();
}

void ServiceEngine::attemptVolumeMonitoringForTests() {
    startOrRestartVolumeMonitoring();
}

void ServiceEngine::armVolumeMonitoringBeforeReplayForTests() {
    armVolumeMonitoringBeforeReplay_.store(true, std::memory_order_release);
}

void ServiceEngine::noteDeferredMonitoringRefresh() {
    std::lock_guard<std::mutex> lock(deferredVolumeMutex_);
    deferredMonitoringRefresh_ = true;
}

void ServiceEngine::applyDeferredVolumeWork(bool requireStartup) {
    if (shuttingDown_.load(std::memory_order_acquire)) return;
    if (lifecycleBusy()) return;
    if (requireStartup && !startupCompleted_.load(std::memory_order_acquire)) return;

    std::vector<DeferredVolumeWork> work;
    bool refresh = false;
    {
        std::lock_guard<std::mutex> lock(deferredVolumeMutex_);
        if (lifecycleBusy()) return;
        if (requireStartup && !startupCompleted_.load(std::memory_order_acquire)) return;
        work.swap(deferredVolumeWork_);
        // A failed first scan has not finished startup. Keep the watcher
        // request until a later successful start can honor it.
        if (startupCompleted_.load(std::memory_order_acquire)) {
            refresh = deferredMonitoringRefresh_;
            deferredMonitoringRefresh_ = false;
        }
    }
    if (refresh) startOrRestartVolumeMonitoring();
    // The batch in `work` is already off the shared queue. A background
    // replacement has to invalidate it by snapshot id; discardDeferredEvents()
    // cannot see it. An empty startup drain must not consume the test hook.
    if (!work.empty() &&
        publishFreshScanOnNextDeferredDrain_.exchange(false, std::memory_order_acq_rel)) {
        auto engine = safeEngine();
        auto roots = effectiveScanRoots();
        DirectoryScanner scanner;
        scanner.scan(roots, scanConfigForRoots(roots));
        if (scanner.isComplete() && engine) {
            publishScannedRecords(engine, scanner.takeResults(), nullptr, nullptr,
                                  lifecycleGeneration_.load(std::memory_order_acquire));
        }
    }
    for (auto& item : work) {
        if (item.kind == DeferredVolumeWork::Kind::Remove) {
            removeSubtreeNow(std::move(item.path), std::move(item.removeCompletion),
                             item.cancelIfRemounted);
        } else if (item.kind == DeferredVolumeWork::Kind::Rename) {
            refreshRenamedPathNow(item.path, item.newPath);
        } else if (item.kind == DeferredVolumeWork::Kind::Events) {
            const uint64_t generation = item.generation;
            const uint64_t indexRevision = item.indexRevision;
            auto events = std::move(item.events);
            enqueueMutation(^{
                this->applyOrDeferEvents(generation, indexRevision, events, nullptr, false);
            });
        } else {
            rescanSubtreeNow(std::move(item.path), std::move(item.rescanCompletion));
        }
    }
    deferredDrainCount_.fetch_add(1, std::memory_order_acq_rel);
}

void ServiceEngine::refreshMonitoringForVolumeChange() {
    if (shuttingDown_.load(std::memory_order_acquire)) return;
    if (!safeConfig().realtimeMonitoring) return;
    // A scan or sync still owns the index. Remember the mount change and
    // start the live watcher only after that work publishes its result.
    if (lifecycleBusy() || !startupCompleted_.load(std::memory_order_acquire)) {
        if (lifecycleBusy()) {
            std::lock_guard<std::mutex> lock(deferredVolumeMutex_);
            deferredMonitoringRefresh_ = true;
        }
        if (!lifecycleBusy() && startupCompleted_.load(std::memory_order_acquire)) {
            applyDeferredVolumeWork();
        }
        return;
    }
    startOrRestartVolumeMonitoring();
}

void ServiceEngine::startMonitoring(FSEventStreamEventId sinceEventId) {
    bool defer = false;
    {
        std::lock_guard<std::mutex> lock(monitoringMutex_);
        if (shuttingDown_.load(std::memory_order_acquire)) return;
        // The idle check in refreshMonitoringForVolumeChange() can pass
        // before a scan publishes isScanning_. Refuse the start here, while
        // this lock is held, so that stream cannot outrun stopMonitoring().
        if (lifecycleBusy()) {
            defer = true;
        } else if (isMonitoring_.load(std::memory_order_relaxed) &&
                   !replayStartsEarlier(sinceEventId, monitoringAnchor_.load(std::memory_order_relaxed))) {
            return;
        } else {
            if (isMonitoring_.load(std::memory_order_relaxed)) {
                quiesceMonitoringUnlocked();
            }
            startMonitoringUnlocked(sinceEventId);
        }
    }
    if (defer) noteDeferredMonitoringRefresh();
}

void ServiceEngine::startMonitoringUnlocked(FSEventStreamEventId sinceEventId) {
    auto roots = effectiveScanRoots();
    roots.erase(std::remove_if(roots.begin(), roots.end(), [](const std::string& root) {
        if (PathUtils::isReachableDirectory(root)) return false;
        LOG_INFO("FSWatcher", "Not watching unavailable scan root: " << root);
        return true;
    }), roots.end());
    if (roots.empty()) {
        LOG_INFO("FSWatcher", "No mounted scan roots to watch");
        return;
    }

    // Exclude app's own cache directory from FSEvents
    auto cfg = safeConfig();
    std::vector<std::string> exclusions = scanConfigForRoots(roots).excludedPaths;
    std::string cacheExclusion = cfg.cachePath;
    if (cacheExclusion.empty()) {
        const char* home = std::getenv("HOME");
        if (home) cacheExclusion = std::string(home) + "/Library/Caches/com.maceverything.app";
    }
    if (!cacheExclusion.empty()) {
        exclusions.push_back(cacheExclusion);
    }
    if (!exclusions.empty()) {
        watcher_->setExclusionPaths(exclusions);
    }
    // Seed the watermark before the stream starts so a callback cannot
    // publish a newer id that this assignment would then rewind.
    if (watcher_) watcher_->setLastEventId(sinceEventId);

    const bool started = watcher_->start(roots, sinceEventId, [this](std::vector<FileSystemWatcher::Event> events) {
        if (shuttingDown_.load(std::memory_order_relaxed)) return;
        if (!mutationQueue_ || events.empty()) return;
        const uint64_t generation = lifecycleGeneration_.load(std::memory_order_acquire);
        const uint64_t indexRevision = indexRevision_.load(std::memory_order_acquire);
        dispatch_async(mutationQueue_, ^{
            this->applyOrDeferEvents(generation, indexRevision, events, nullptr,
                                     /*allowWhileSyncing=*/false);
        });
    });

    isMonitoring_.store(started, std::memory_order_release);
    if (started) {
        monitoringAnchor_.store(sinceEventId, std::memory_order_release);
    }
    // SMB streams can start successfully while delivering no remote file
    // events. Keep a bounded polling safety net for every network root.
    startNetworkPolling(roots);
    if (!started) {
        LOG_WARN("FSWatcher", "Unable to start FSEvents stream; live monitoring is disabled");
    }
}

void ServiceEngine::startNetworkPolling(const std::vector<std::string>& roots) {
    if (networkPollingTimer_ || !mutationQueue_ || shuttingDown_.load(std::memory_order_acquire)) {
        return;
    }
    bool hasNetworkRoot = false;
    for (const auto& root : roots) {
        if (PathUtils::isNetworkFilesystem(root)) {
            hasNetworkRoot = true;
            break;
        }
    }
    if (!hasNetworkRoot) return;

    networkPollingTimer_ = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER, 0, 0, mutationQueue_);
    constexpr uint64_t kNetworkPollIntervalSec = 60;
    dispatch_source_set_timer(networkPollingTimer_,
                              dispatch_time(DISPATCH_TIME_NOW, kNetworkPollIntervalSec * NSEC_PER_SEC),
                              kNetworkPollIntervalSec * NSEC_PER_SEC,
                              NSEC_PER_SEC);
    dispatch_source_t timer = networkPollingTimer_;
    dispatch_source_set_event_handler(timer, ^{
        auto pollingState = this->networkPollingState_;
        if (this->shuttingDown_.load(std::memory_order_acquire) ||
            pollingState->inFlight.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        std::vector<std::string> networkRoots;
        for (const auto& root : this->effectiveScanRoots()) {
            if (PathUtils::isNetworkFilesystem(root)) networkRoots.push_back(root);
        }
        if (networkRoots.empty()) {
            pollingState->inFlight.store(false, std::memory_order_release);
            return;
        }

        const uint64_t pollSerial = pollingState->serial.load(std::memory_order_acquire);
        auto remaining = std::make_shared<std::atomic<size_t>>(networkRoots.size());
        auto finish = [pollingState, remaining, pollSerial] {
            if (remaining->fetch_sub(1, std::memory_order_acq_rel) == 1 &&
                pollingState->serial.load(std::memory_order_acquire) == pollSerial) {
                pollingState->inFlight.store(false, std::memory_order_release);
            }
        };
        for (const auto& root : networkRoots) {
            this->rescanSubtree(root, finish);
        }
    });
    dispatch_resume(networkPollingTimer_);
    LOG_INFO("FSWatcher", "Network volume polling safety net enabled (60s interval)");
}

void ServiceEngine::stopNetworkPolling() {
    networkPollingState_->serial.fetch_add(1, std::memory_order_acq_rel);
    networkPollingState_->inFlight.store(false, std::memory_order_release);
    if (!networkPollingTimer_) return;
    dispatch_source_cancel(networkPollingTimer_);
    dispatch_release(networkPollingTimer_);
    networkPollingTimer_ = nullptr;
}

void ServiceEngine::quiesceMonitoringUnlocked() {
    isMonitoring_.store(false, std::memory_order_release);
    monitoringAnchor_.store(0, std::memory_order_release);
    if (watcher_) watcher_->stop();
    stopNetworkPolling();

    std::lock_guard<std::mutex> lock(pendingRescanMutex_);
    if (rescanDebounceTimer_) {
        dispatch_source_cancel(rescanDebounceTimer_);
        dispatch_release(rescanDebounceTimer_);
        rescanDebounceTimer_ = nullptr;
    }
    pendingRescanPaths_.clear();
    lastRescanTime_.clear();
}

void ServiceEngine::stopMonitoringObservers() {
    auto persistence = safePersistence();
    if (persistence) persistence->stopAutoCompactionAndWait();
    auto cp = safeContentPersistence();
    if (cp) cp->stopAutoCompactionAndWait();
}

void ServiceEngine::restartMonitoring() {
    bool defer = false;
    bool restarted = false;
    {
        std::lock_guard<std::mutex> lock(monitoringMutex_);
        if (shuttingDown_.load(std::memory_order_acquire)) return;
        if (lifecycleBusy()) {
            defer = true;
        } else if (isMonitoring_.load(std::memory_order_relaxed)) {
            FSEventStreamEventId lastEventId = kFSEventStreamEventIdSinceNow;
            if (watcher_) lastEventId = watcher_->getLastEventId();
            if (lastEventId == 0) {
                lastEventId = monitoringAnchor_.load(std::memory_order_relaxed);
            }
            if (lastEventId == 0) lastEventId = kFSEventStreamEventIdSinceNow;
            LOG_INFO("ServiceEngine", "Restarting monitoring (scan roots changed)");
            quiesceMonitoringUnlocked();
            startMonitoringUnlocked(lastEventId);
            restarted = true;
        }
    }
    if (defer) noteDeferredMonitoringRefresh();
    if (!restarted) return;
    stopMonitoringObservers();
    if (safeConfig().automaticMaintenanceEnabled) {
        auto persistence = safePersistence();
        if (persistence) persistence->startAutoCompaction(300.0, watcher_);
    }
}

void ServiceEngine::stopMonitoring() {
    {
        std::lock_guard<std::mutex> lock(monitoringMutex_);
        quiesceMonitoringUnlocked();
    }
    stopMonitoringObservers();
}

// ═══════════════════════════════════════════════════════
//  Rescan debounce
// ═══════════════════════════════════════════════════════

void ServiceEngine::scheduleRescanForPaths(const std::vector<std::string>& paths) {
    if (!mutationQueue_ || shuttingDown_.load(std::memory_order_acquire) ||
        !isMonitoring_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(pendingRescanMutex_);

    pendingRescanPaths_ = mergeRescanPaths(pendingRescanPaths_, minimizeRescanPaths(paths));

    LOG_INFO("FSWatcher", "Debounce: " << pendingRescanPaths_.size()
             << " pending rescan path(s), scheduling " << kRescanDebounceDelaySec << "s delay");

    if (rescanDebounceTimer_) {
        dispatch_source_cancel(rescanDebounceTimer_);
        dispatch_release(rescanDebounceTimer_);
        rescanDebounceTimer_ = nullptr;
    }

    rescanDebounceTimer_ = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER, 0, 0, mutationQueue_);

    uint64_t delaySec = static_cast<uint64_t>(kRescanDebounceDelaySec * NSEC_PER_SEC);
    dispatch_source_set_timer(rescanDebounceTimer_, dispatch_time(DISPATCH_TIME_NOW, delaySec),
                              DISPATCH_TIME_FOREVER, NSEC_PER_SEC / 10);

    dispatch_source_t scheduledTimer = rescanDebounceTimer_;
    dispatch_source_set_event_handler(scheduledTimer, ^{
        this->flushPendingRescans(scheduledTimer);
    });
    dispatch_resume(rescanDebounceTimer_);
}

void ServiceEngine::flushPendingRescans(dispatch_source_t firingTimer) {
    std::set<std::string> pathsToRescan;
    std::set<std::string> throttledPaths;
    {
        std::lock_guard<std::mutex> lock(pendingRescanMutex_);
        if (firingTimer && rescanDebounceTimer_ != firingTimer) return;
        for (const auto& path : pendingRescanPaths_) {
            if (shouldThrottleRescan(path, lastRescanTime_, kRescanThrottleIntervalSec)) {
                throttledPaths.insert(path);
                LOG_INFO("FSWatcher", "Throttled rescan for: " << path
                         << " (within " << kRescanThrottleIntervalSec << "s window)");
            } else {
                pathsToRescan.insert(path);
            }
        }
        pendingRescanPaths_ = throttledPaths;

        if (rescanDebounceTimer_) {
            dispatch_source_cancel(rescanDebounceTimer_);
            dispatch_release(rescanDebounceTimer_);
            rescanDebounceTimer_ = nullptr;
        }
    }

    if (pathsToRescan.empty() && throttledPaths.empty()) return;

    LOG_INFO("FSWatcher", "Flushing debounced rescan: " << pathsToRescan.size()
             << " path(s) to rescan, " << throttledPaths.size() << " throttled");

    for (const auto& path : pathsToRescan) {
        rescanSubtree(path);

        std::lock_guard<std::mutex> lock(pendingRescanMutex_);
        lastRescanTime_[path] = std::chrono::steady_clock::now();
    }

    // Clean up old entries in lastRescanTime_ (> 2x throttle interval)
    {
        std::lock_guard<std::mutex> lock(pendingRescanMutex_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = lastRescanTime_.begin(); it != lastRescanTime_.end(); ) {
            auto elapsed = std::chrono::duration<double>(now - it->second).count();
            if (elapsed > kRescanThrottleIntervalSec * 2.0) {
                it = lastRescanTime_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // If there are throttled paths, schedule a retry when throttle expires
    if (!throttledPaths.empty() && !shuttingDown_.load(std::memory_order_acquire) &&
        isMonitoring_.load(std::memory_order_acquire) && mutationQueue_) {
        std::lock_guard<std::mutex> lock(pendingRescanMutex_);
        if (!rescanDebounceTimer_) {
            rescanDebounceTimer_ = dispatch_source_create(
                DISPATCH_SOURCE_TYPE_TIMER, 0, 0, mutationQueue_);
            uint64_t delay = static_cast<uint64_t>(kRescanThrottleIntervalSec * NSEC_PER_SEC);
            dispatch_source_set_timer(rescanDebounceTimer_,
                                      dispatch_time(DISPATCH_TIME_NOW, delay),
                                      DISPATCH_TIME_FOREVER, NSEC_PER_SEC);
            dispatch_source_t retryTimer = rescanDebounceTimer_;
            dispatch_source_set_event_handler(retryTimer, ^{
                this->flushPendingRescans(retryTimer);
            });
            dispatch_resume(rescanDebounceTimer_);
        }
    }
}

void ServiceEngine::deferVolumeRescan(std::string dir, std::function<void()> completion) {
    {
        std::lock_guard<std::mutex> lock(deferredVolumeMutex_);
        DeferredVolumeWork work;
        work.kind = DeferredVolumeWork::Kind::Rescan;
        work.path = std::move(dir);
        work.rescanCompletion = std::move(completion);
        deferredVolumeWork_.push_back(std::move(work));
    }
    if (!lifecycleBusy() && startupCompleted_.load(std::memory_order_acquire)) {
        applyDeferredVolumeWork();
    }
}

void ServiceEngine::deferVolumeRemoval(std::string path,
                                       std::function<void(uint32_t)> completion,
                                       bool cancelIfRemounted) {
    {
        std::lock_guard<std::mutex> lock(deferredVolumeMutex_);
        DeferredVolumeWork work;
        work.kind = DeferredVolumeWork::Kind::Remove;
        work.path = std::move(path);
        work.removeCompletion = std::move(completion);
        work.cancelIfRemounted = cancelIfRemounted;
        deferredVolumeWork_.push_back(std::move(work));
    }
    if (!lifecycleBusy() && startupCompleted_.load(std::memory_order_acquire)) {
        applyDeferredVolumeWork();
    }
}

void ServiceEngine::queueWatcherEventsForTests(std::vector<FileSystemWatcher::Event> events) {
    const uint64_t generation = lifecycleGeneration_.load(std::memory_order_acquire);
    const uint64_t indexRevision = indexRevision_.load(std::memory_order_acquire);
    deferVolumeEvents(generation, indexRevision, std::move(events));
}

void ServiceEngine::publishFreshScanOnNextDeferredDrainForTests() {
    publishFreshScanOnNextDeferredDrain_.store(true, std::memory_order_release);
}

void ServiceEngine::enqueueMutation(dispatch_block_t work) {
    if (!work) return;
    if (!mutationQueue_ || dispatch_get_specific(this) == this) {
        work();
        return;
    }
    dispatch_async(mutationQueue_, work);
}

void ServiceEngine::deferVolumeEvents(uint64_t generation,
                                     uint64_t indexRevision,
                                     std::vector<FileSystemWatcher::Event> events) {
    if (events.empty()) return;
    {
        std::lock_guard<std::mutex> lock(deferredVolumeMutex_);
        DeferredVolumeWork work;
        work.kind = DeferredVolumeWork::Kind::Events;
        work.generation = generation;
        work.indexRevision = indexRevision;
        work.events = std::move(events);
        deferredVolumeWork_.push_back(std::move(work));
    }
    if (!lifecycleBusy() && startupCompleted_.load(std::memory_order_acquire)) {
        applyDeferredVolumeWork();
    }
}

void ServiceEngine::discardDeferredEvents() {
    std::lock_guard<std::mutex> lock(deferredVolumeMutex_);
    deferredVolumeWork_.erase(
        std::remove_if(deferredVolumeWork_.begin(), deferredVolumeWork_.end(),
                       [](const DeferredVolumeWork& work) {
                           return work.kind == DeferredVolumeWork::Kind::Events;
                       }),
        deferredVolumeWork_.end());
}

void ServiceEngine::applyOrDeferEvents(uint64_t generation,
                                      uint64_t indexRevision,
                                      std::vector<FileSystemWatcher::Event> events,
                                      std::shared_ptr<SearchEngine> requiredEngine,
                                      bool allowWhileSyncing) {
    std::vector<FileSystemWatcher::Event> directEvents;
    std::vector<std::string> rescanDirs;
    directEvents.reserve(events.size());
    for (auto& event : events) {
        if (event.flags & kFSEventStreamEventFlagMustScanSubDirs) {
            auto dir = rescanDirectoryForEvent(event.path, true);
            if (!dir.empty()) rescanDirs.push_back(std::move(dir));
        } else {
            directEvents.push_back(std::move(event));
        }
    }

    auto observedEngine = safeEngine();
    std::vector<std::vector<std::string>> priorRenamePaths(directEvents.size());
    std::vector<struct stat> renameStats(directEvents.size());
    std::vector<bool> renameExists(directEvents.size(), false);
    std::vector<bool> removedDirectories(directEvents.size(), false);
    const bool canInspectSnapshot = observedEngine &&
        isGenerationCurrent(generation) &&
        indexRevision_.load(std::memory_order_acquire) == indexRevision &&
        !isScanning_.load(std::memory_order_acquire) &&
        (allowWhileSyncing || !isSyncing_.load(std::memory_order_acquire));
    for (size_t i = 0; i < directEvents.size(); ++i) {
        if (!canInspectSnapshot) break;
        const bool renamed = (directEvents[i].flags & kFSEventStreamEventFlagItemRenamed) != 0;
        const bool removed = (directEvents[i].flags & kFSEventStreamEventFlagItemRemoved) != 0;
        renameExists[i] = lstatEventPath(directEvents[i].path, renameStats[i]) == 0;
        const int statError = renameExists[i] ? 0 : errno;
        if (!observedEngine) continue;
        if (renamed && renameExists[i]) {
            priorRenamePaths[i] = observedEngine->pathsForFileIdentity(
                static_cast<uint64_t>(renameStats[i].st_ino),
                static_cast<int32_t>(renameStats[i].st_dev));
        } else if ((removed || renamed) && (statError == ENOENT || statError == ENOTDIR)) {
            // Moving a directory outside the watched scope can report only
            // the missing old path with ItemRenamed, without ItemRemoved.
            std::string indexedPath = directEvents[i].path;
            for (const auto& root : effectiveScanRoots()) {
                if (mapEventPathToConfiguredRoot(root, directEvents[i].path, indexedPath)) break;
            }
            const auto index = observedEngine->indexForPath(indexedPath);
            if (index != UINT32_MAX) {
                const auto type = observedEngine->getRecord(index).type;
                removedDirectories[i] = type == 2 || type == 5;
            }
        }
    }

    std::vector<std::string> reconcileDirs;
    std::vector<FileSystemWatcher::Event> eventsToApply;
    eventsToApply.reserve(directEvents.size());
    bool applied = false;
    bool defer = false;
    {
        std::lock_guard<std::mutex> stateLock(lifecycleStateMutex_);
        if (shuttingDown_.load(std::memory_order_acquire)) return;
        // The batch is stamped with the generation and snapshot that observed
        // the events. A rebuild reads the disk itself; applying the old batch
        // to the new index can delete files the scan just recorded. Background
        // full replacement keeps the generation and bumps the snapshot instead.
        // Do not queue the batch again: the shared queue was already drained,
        // and re-deferring a stale snapshot would apply it after publish.
        if (!isGenerationCurrent(generation)) return;
        if (indexRevision_.load(std::memory_order_acquire) != indexRevision) return;
        const bool scanning = isScanning_.load(std::memory_order_acquire);
        const bool syncing = isSyncing_.load(std::memory_order_acquire);
        auto engine = safeEngine();
        const bool engineMismatch = requiredEngine && engine != requiredEngine;
        if (scanning || (!allowWhileSyncing && syncing) || !engine || engineMismatch ||
            engine != observedEngine) {
            defer = true;
        } else if (!directEvents.empty()) {
            for (size_t i = 0; i < directEvents.size(); ++i) {
                const auto& event = directEvents[i];
                if (removedDirectories[i]) {
                    const auto parent = rescanDirectoryForEvent(event.path);
                    if (!parent.empty()) reconcileDirs.push_back(parent);
                    continue;
                }
                if ((event.flags & kFSEventStreamEventFlagItemRenamed) == 0 || !renameExists[i]) {
                    eventsToApply.push_back(event);
                    continue;
                }

                if (S_ISDIR(renameStats[i].st_mode)) {
                    const auto newParent = rescanDirectoryForEvent(event.path);
                    if (!newParent.empty()) reconcileDirs.push_back(newParent);
                    for (const auto& oldPath : priorRenamePaths[i]) {
                        const auto oldParent = rescanDirectoryForEvent(oldPath);
                        if (!oldParent.empty()) reconcileDirs.push_back(oldParent);
                    }
                } else {
                    for (const auto& oldPath : priorRenamePaths[i]) {
                        struct stat oldStat {};
                        const bool oldPathMissing = lstat(oldPath.c_str(), &oldStat) != 0 &&
                            (errno == ENOENT || errno == ENOTDIR);
                        if (oldPath != event.path && oldPathMissing) {
                            eventsToApply.push_back({oldPath, kFSEventStreamEventFlagItemRemoved});
                        }
                    }
                    eventsToApply.push_back(event);
                }
            }
            if (!eventsToApply.empty()) {
                applyFSEvents(eventsToApply, engine);
                applied = true;
            }
        }
    }
    if (defer) {
        if (!directEvents.empty()) {
            deferVolumeEvents(generation, indexRevision, std::move(directEvents));
        }
        for (auto& dir : rescanDirs) deferVolumeRescan(std::move(dir), {});
        return;
    }
    if (!reconcileDirs.empty()) {
        for (const auto& dir : minimizeRescanPaths(std::move(reconcileDirs))) {
            rescanSubtree(dir);
        }
    }
    if (applied && !allowWhileSyncing && onIndexChanged) onIndexChanged();
    if (!rescanDirs.empty()) {
        // Replay runs while monitoring is stopped. Queue the subtree work
        // instead of dropping it in scheduleRescanForPaths().
        if (allowWhileSyncing || !isMonitoring_.load(std::memory_order_acquire)) {
            for (auto& dir : rescanDirs) deferVolumeRescan(std::move(dir), {});
        } else {
            scheduleRescanForPaths(rescanDirs);
        }
    }
}

void ServiceEngine::deferVolumeRename(std::string oldPath, std::string newPath) {
    {
        std::lock_guard<std::mutex> lock(deferredVolumeMutex_);
        DeferredVolumeWork work;
        work.kind = DeferredVolumeWork::Kind::Rename;
        work.path = std::move(oldPath);
        work.newPath = std::move(newPath);
        deferredVolumeWork_.push_back(std::move(work));
    }
    if (!lifecycleBusy() && startupCompleted_.load(std::memory_order_acquire)) {
        applyDeferredVolumeWork();
    }
}

void ServiceEngine::rescanSubtree(const std::string& dir,
                                   std::function<void()> completion) {
    if (lifecycleBusy() || shuttingDown_.load(std::memory_order_acquire)) {
        if (shuttingDown_.load(std::memory_order_acquire)) {
            if (completion) {
                dispatch_async(dispatch_get_main_queue(), ^{ completion(); });
            }
            return;
        }
        deferVolumeRescan(dir, std::move(completion));
        return;
    }
    rescanSubtreeNow(dir, std::move(completion));
}

void ServiceEngine::rescanSubtreeNow(const std::string& dir,
                                     std::function<void()> completion) {
    auto completionCopy = completion ? std::make_shared<std::function<void()>>(std::move(completion)) : nullptr;
    auto notifyCompletion = [completionCopy] {
        if (!completionCopy) return;
        dispatch_async(dispatch_get_main_queue(), ^{ (*completionCopy)(); });
    };

    // Copy dir to avoid dangling reference — the caller's temporary may be
    // destroyed before the async block executes.
    std::string dirCopy = dir;
    uint64_t generation = lifecycleGeneration_.load(std::memory_order_acquire);

    if (!mutationQueue_ || shuttingDown_.load(std::memory_order_acquire)) {
        notifyCompletion();
        return;
    }
    enqueueMutation(^{
        // A scan or rebuild can start after the caller checked lifecycleBusy().
        // Defer instead of mutating an engine that loadRecords() is about to
        // replace, or an engine that is no longer published.
        if (this->shuttingDown_.load(std::memory_order_acquire)) {
            notifyCompletion();
            return;
        }
        if (!this->isGenerationCurrent(generation) || this->lifecycleBusy()) {
            std::function<void()> again = completionCopy ? *completionCopy : std::function<void()>{};
            this->deferVolumeRescan(dirCopy, std::move(again));
            return;
        }

        auto scanner = std::make_shared<DirectoryScanner>();
        if (!this->registerScanner(scanner, generation)) {
            notifyCompletion();
            return;
        }
        std::vector<std::string> roots{dirCopy};
        // Preserve explicit roots that bypass ancestor pruning in a full scan.
        for (const auto& root : this->effectiveScanRoots()) {
            if (root != dirCopy && PathUtils::pathContainsOrEquals(dirCopy, root)) {
                roots.push_back(root);
            }
        }
        scanner->scan(roots, this->scanConfigForRoots(roots));
        this->unregisterScanner(scanner);
        if (scanner->deferredVolumeCount() > 0 && scanner->reachableRootCount() == 0) {
            LOG_INFO("ServiceEngine", "Subtree scan skipped; volume is not connected: " << dirCopy);
            notifyCompletion();
            return;
        }
        if (!scanner->isComplete()) {
            const auto& stats = scanner->getStats();
            LOG_ERROR("ServiceEngine", "Subtree scan incomplete; preserving indexed subtree "
                      << dirCopy << " (" << stats.errorCount.load(std::memory_order_relaxed)
                      << " errors)");
            notifyCompletion();
            return;
        }
        auto freshRecords = scanner->takeResults();
        LOG_INFO("ServiceEngine", "rescanSubtree(" << dirCopy << "): scanned "
                 << freshRecords.size() << " records");

        enum class RescanAction { Apply, Defer, Drop };
        RescanAction action = RescanAction::Apply;
        bool applied = false;
        {
            // setEngine() / loadRecords() publish under this lock. Hold it from
            // the generation check through the index write so this rescan cannot
            // modify an engine a rebuild has just swapped in.
            std::lock_guard<std::mutex> stateLock(this->lifecycleStateMutex_);
            if (this->shuttingDown_.load(std::memory_order_acquire)) {
                action = RescanAction::Drop;
            } else if (!this->isGenerationCurrent(generation) || this->lifecycleBusy()) {
                action = RescanAction::Defer;
            } else if (auto engine = this->safeEngine()) {
                // DirectoryScanner emits children, not its root. Prefix
                // replacement also removes the exact root record, so restore
                // that record when it was part of the published index. Keep
                // batchRescanPrefix's replacement contract unchanged.
                const auto rootIndex = engine->indexForPath(dirCopy);
                if (rootIndex != UINT32_MAX) {
                    struct stat st {};
                    if (lstat(dirCopy.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                        notifyCompletion();
                        return;
                    }
                    auto record = engine->getRecord(rootIndex);
                    record.size = 0;
                    record.modTime = st.st_mtime;
                    record.inode = st.st_ino;
                    record.devId = static_cast<int32_t>(st.st_dev);
                    record.type = pathEndsWithApp(dirCopy) ? 5 : 2;
                    freshRecords.push_back(std::move(record));
                }
                std::vector<std::string> contentPaths;
                contentPaths.reserve(freshRecords.size());
                for (const auto& record : freshRecords) {
                    if (record.type != 1) continue;
                    auto fullPath = SearchEngine::makeFullPath(record.path, record.name);
                    if (this->isPathAllowedByConfig(fullPath, true)) {
                        contentPaths.push_back(std::move(fullPath));
                    }
                }

                auto ci = this->safeContentIndex();
                auto cp = this->safeContentPersistence();
                if (ci) {
                    auto mappingLease = ci->acquireFileIndexMappingLease();
                    auto oldEntries = ci->removeByPathPrefix(dirCopy);
                    if (cp) {
                        for (const auto& [oldIndex, info] : oldEntries) {
                            cp->walAppendRemove(oldIndex, info.fullPath);
                        }
                    }
                }

                uint32_t removed = engine->batchRescanPrefix(dirCopy, std::move(freshRecords));

                uint32_t total = engine->recordCount();
                uint32_t live  = engine->liveRecordCount();
                LOG_INFO("ServiceEngine", "rescanSubtree(" << dirCopy << "): removed="
                         << removed << " total=" << total << " live=" << live);
                if (total > live && (total - live) > total * 3 / 10) {
                    if (ci) ci->beginFileIndexRemap();
                    auto remap = engine->compactRecords();
                    if (!remap.empty() && ci) ci->remapFileIndices(remap);
                    if (ci) ci->endFileIndexRemap();
                }

                for (const auto& path : contentPaths) {
                    this->updateContentForPath(path, false, engine);
                }
                this->noteIndexedIdentityForCompletedRescan(dirCopy);
                applied = true;
            } else {
                action = RescanAction::Drop;
            }
        }
        if (action == RescanAction::Defer) {
            std::function<void()> again = completionCopy ? *completionCopy : std::function<void()>{};
            this->deferVolumeRescan(dirCopy, std::move(again));
            return;
        }
        if (applied && onIndexChanged) onIndexChanged();
        notifyCompletion();
    });
}

void ServiceEngine::refreshRenamedPath(const std::string& oldPath,
                                       const std::string& newPath) {
    if (oldPath.empty() || newPath.empty() || oldPath == newPath) return;
    if (shuttingDown_.load(std::memory_order_acquire)) return;
    if (lifecycleBusy()) {
        deferVolumeRename(oldPath, newPath);
        return;
    }
    refreshRenamedPathNow(oldPath, newPath);
}

void ServiceEngine::refreshRenamedPathNow(const std::string& oldPath,
                                          const std::string& newPath) {
    if (oldPath.empty() || newPath.empty() || oldPath == newPath) return;
    const uint64_t generation = lifecycleGeneration_.load(std::memory_order_acquire);
    const std::string oldPathCopy = oldPath;
    const std::string newPathCopy = newPath;
    if (!mutationQueue_ || shuttingDown_.load(std::memory_order_acquire)) return;
    enqueueMutation(^{
        if (this->shuttingDown_.load(std::memory_order_acquire)) return;
        if (!this->isGenerationCurrent(generation) || this->lifecycleBusy()) {
            this->deferVolumeRename(oldPathCopy, newPathCopy);
            return;
        }
        std::vector<FileSystemWatcher::Event> events{
            {oldPathCopy, kFSEventStreamEventFlagItemRemoved},
            {newPathCopy, kFSEventStreamEventFlagItemRenamed}
        };
        this->applyOrDeferEvents(
            generation, this->indexRevision_.load(std::memory_order_acquire),
            std::move(events), this->safeEngine(), false);
    });
}

void ServiceEngine::removeSubtree(const std::string& pathPrefix,
                                   std::function<void(uint32_t)> completion) {
    if (shuttingDown_.load(std::memory_order_acquire)) {
        if (completion) {
            dispatch_async(dispatch_get_main_queue(), ^{ completion(0); });
        }
        return;
    }
    if (lifecycleBusy()) {
        deferVolumeRemoval(pathPrefix, std::move(completion), /*cancelIfRemounted=*/true);
        return;
    }
    removeSubtreeNow(pathPrefix, std::move(completion), /*cancelIfRemounted=*/false);
}

void ServiceEngine::removeSubtreeNow(const std::string& pathPrefix,
                                     std::function<void(uint32_t)> completion,
                                     bool cancelIfRemounted) {
    auto completionCopy = completion
        ? std::make_shared<std::function<void(uint32_t)>>(std::move(completion))
        : nullptr;
    auto notify = [completionCopy](uint32_t removed) {
        if (!completionCopy) return;
        dispatch_async(dispatch_get_main_queue(), ^{ (*completionCopy)(removed); });
    };

    std::string prefixCopy = pathPrefix;
    const uint64_t generation = lifecycleGeneration_.load(std::memory_order_acquire);
    if (!mutationQueue_ || shuttingDown_.load(std::memory_order_acquire)) {
        notify(0);
        return;
    }
    enqueueMutation(^{
        if (this->shuttingDown_.load(std::memory_order_acquire)) {
            notify(0);
            return;
        }
        if (!this->isGenerationCurrent(generation) || this->lifecycleBusy()) {
            std::function<void(uint32_t)> again =
                completionCopy ? *completionCopy : std::function<void(uint32_t)>{};
            this->deferVolumeRemoval(prefixCopy, std::move(again), /*cancelIfRemounted=*/true);
            return;
        }
        if (cancelIfRemounted && PathUtils::isReachableDirectory(prefixCopy)) {
            LOG_INFO("ServiceEngine", "Skipping deferred removal; path is mounted again: "
                     << prefixCopy);
            notify(0);
            return;
        }

        enum class RemovalAction { Apply, Defer, Drop };
        RemovalAction action = RemovalAction::Apply;
        uint32_t removed = 0;
        {
            // Rebuild publishes a replacement engine under this lock. Hold it
            // across the delete so the removal cannot land on the engine that
            // was just swapped out.
            std::lock_guard<std::mutex> stateLock(this->lifecycleStateMutex_);
            if (this->shuttingDown_.load(std::memory_order_acquire)) {
                action = RemovalAction::Drop;
            } else if (!this->isGenerationCurrent(generation) || this->lifecycleBusy()) {
                action = RemovalAction::Defer;
            } else if (cancelIfRemounted && PathUtils::isReachableDirectory(prefixCopy)) {
                LOG_INFO("ServiceEngine", "Skipping deferred removal; path is mounted again: "
                         << prefixCopy);
                action = RemovalAction::Drop;
            } else if (auto engine = this->safeEngine()) {
                auto ci = this->safeContentIndex();
                auto mappingLease = ci ? ci->acquireFileIndexMappingLease()
                                       : std::shared_lock<std::shared_mutex>();
                std::vector<uint32_t> removedIndices;
                removed = engine->removeByPathPrefixCollectingIndices(prefixCopy, &removedIndices);
                if (this->safeConfig().contentIndexingEnabled && ci) {
                    auto cp = this->safeContentPersistence();
                    for (uint32_t idx : removedIndices) {
                        ContentFileInfo info;
                        bool hadContent = ci->getFileInfo(idx, info);
                        ci->removeFile(idx);
                        if (cp && hadContent) cp->walAppendRemove(idx, info.fullPath);
                    }
                }
            } else {
                action = RemovalAction::Drop;
            }
        }
        if (action == RemovalAction::Drop) {
            notify(0);
            return;
        }
        if (action == RemovalAction::Defer) {
            std::function<void(uint32_t)> again =
                completionCopy ? *completionCopy : std::function<void(uint32_t)>{};
            this->deferVolumeRemoval(prefixCopy, std::move(again), /*cancelIfRemounted=*/true);
            return;
        }

        LOG_INFO("ServiceEngine", "removeSubtree(" << prefixCopy << "): removed " << removed << " records");
        if (this->onIndexChanged) this->onIndexChanged();
        notify(removed);
    });
}

#pragma once
#include "SearchEngine.h"
#include "ContentIndex.h"
#include "ContentIndexPersistence.h"
#include "IndexPersistence.h"
#include "FileSystemWatcher.h"
#include "DirectoryScanner.h"
#include "HttpServer.h"
#include "InstanceLock.h"
#include "RescanDebounce.h"
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <set>
#include <chrono>
#include <functional>
#include <string>
#include <mutex>
#include <vector>
#include <dispatch/dispatch.h>

namespace PathUtils {
struct VolumeKindStore;
}

/// Configuration for ServiceEngine.
struct ServiceConfig {
    std::string scanRoot = "/";
    std::vector<std::string> scanRoots;
    std::vector<std::string> excludedPaths;
    std::vector<std::string> excludedPatterns;
    std::vector<std::string> contentRoots;
    std::vector<std::string> contentExcludedPaths;
    std::string cachePath;   // e.g. ~/Library/Caches/com.maceverything.app
    std::string logPath;     // e.g. ~/Library/Logs/MacEverything
    std::string appVersion = "unknown"; // injected by the production app bundle
    uint16_t httpPort = 0;   // 0 = no HTTP server; >0 = auto-start after index is ready
    bool httpAuthenticationEnabled = false;
    std::string processType = "gui";  // exposed in /api/health for diagnostics
    bool includeHidden = false;
    bool includeSystem = false;
    bool includeAppBundleContents = false;
    bool realtimeMonitoring = true;
    bool contentIndexingEnabled = true;
    bool automaticMaintenanceEnabled = true;
    bool enablePinyinInitials = true;
    bool enablePathTrigramIndex = true;
};

/// Pure C++ orchestration engine — owns all core objects and lifecycle.
/// The GUI bridge owns the production instance of this class.
class ServiceEngine {
public:
    // ── Callbacks set by caller ──
    using IndexChangedCallback = std::function<void()>;
    using ScanProgressCallback = std::function<void(uint64_t scanned, uint64_t dirs)>;
    using ContentProgressCallback = std::function<void(uint64_t indexed, uint64_t total)>;
    using ContentCompleteCallback = std::function<void(uint32_t totalIndexed)>;
    using StartupCallback = std::function<void(uint32_t totalRecords, bool didFullScan)>;
    using StartupFailedCallback = std::function<void(const std::string& reason)>;

    explicit ServiceEngine(const ServiceConfig& config);
    ~ServiceEngine();

    ServiceEngine(const ServiceEngine&) = delete;
    ServiceEngine& operator=(const ServiceEngine&) = delete;

    // ── Lifecycle ──
    void startFullScan(StartupCallback completion);
    void startIncremental(StartupCallback completion);
    void rebuildIndex(StartupCallback completion);
    void shutdown();

    // ── HTTP ──
    void startHttpServer(uint16_t port);
    void stopHttpServer();
    void refreshHttpAuthentication();
    void updateConfig(const ServiceConfig& config);
    void setAdminCallbacks(HttpServer::AdminCallbacks callbacks);

    // ── Public operations ──
    void rescanSubtree(const std::string& dir,
                       std::function<void()> completion = nullptr);
    void refreshRenamedPath(const std::string& oldPath, const std::string& newPath);
    void removeSubtree(const std::string& pathPrefix,
                       std::function<void(uint32_t)> completion = nullptr);
    /// Queue events tagged with the current lifecycle generation and index
    /// snapshot. Tests use this while a scan owns the index; production events
    /// come from FSEvents.
    void queueWatcherEventsForTests(std::vector<FileSystemWatcher::Event> events);
    /// Test only. The next deferred-volume drain rescans the configured roots
    /// and publishes that snapshot through the background replacement path
    /// before applying the batch it has already taken off the shared queue.
    void publishFreshScanOnNextDeferredDrainForTests();
    bool startupCompletedForTests() const {
        return startupCompleted_.load(std::memory_order_acquire);
    }
    uint64_t deferredDrainCountForTests() const {
        return deferredDrainCount_.load(std::memory_order_acquire);
    }
    /// Test only. Runs the volume-notification watcher start while the caller
    /// chooses the lifecycle state.
    void attemptVolumeMonitoringForTests();
    /// Test only. The next full-scan publish starts a watcher from "now"
    /// before the scan replay, so the replay must replace that stream.
    void armVolumeMonitoringBeforeReplayForTests();
    uint64_t monitoringAnchorForTests() const {
        return monitoringAnchor_.load(std::memory_order_acquire);
    }
    void rebuildContentIndex();
    void rebuildContentIndex(const std::vector<std::string>& extensions,
                             uint64_t maxFileSize);
    void requestContentIndexRebuild(const std::vector<std::string>& extensions,
                                    uint64_t maxFileSize);
    void clearContentIndex();
    void compactIndex();

    // ── Volume unmount protection ──
    void markVolumeUnmounting(const std::string& volumePath);
    void clearVolumeUnmounting(const std::string& volumePath);
    bool isVolumeUnmounting(const std::string& path) const;
    /// Restart FSEvents after a volume mount or unmount so configured roots are
    /// watched only while those directories exist. Requests that arrive during
    /// a scan or background sync are applied after that work finishes.
    void refreshMonitoringForVolumeChange();

    // ── Thread-safe accessors ──
    std::shared_ptr<SearchEngine> safeEngine();
    std::shared_ptr<ContentIndex> safeContentIndex();
    std::shared_ptr<IndexPersistence> safePersistence();
    std::shared_ptr<ContentIndexPersistence> safeContentPersistence();

    // ── State queries ──
    bool isScanning() const  { return isScanning_.load(std::memory_order_relaxed); }
    bool isMonitoring() const { return isMonitoring_.load(std::memory_order_relaxed); }
    bool isSyncing() const   { return isSyncing_.load(std::memory_order_relaxed); }
    /// True once startup was aborted (e.g. another instance holds the index
    /// lock). Callers must treat the engine as unusable; the success completion
    /// is not a reliable signal of a usable index when this is set.
    bool isStartupFatal() const { return startupFatal_.load(std::memory_order_acquire); }

    uint32_t recordCount();
    uint32_t liveRecordCount();
    bool isContentPathAllowed(const std::string& path) const {
        return isPathAllowedByConfig(path, true);
    }

    // ── Callbacks (set before starting) ──
    IndexChangedCallback onIndexChanged;
    ScanProgressCallback onScanProgress;
    ContentProgressCallback onContentIndexProgress;
    ContentCompleteCallback onContentIndexComplete;
    /// Fired when startup cannot produce a usable index, including an instance
    /// lock failure or an incomplete initial scan. Invoked before the startup
    /// completion callback and before an incomplete scan can be persisted.
    StartupFailedCallback onStartupFailed;

private:
    struct NetworkPollingState {
        std::atomic<bool> inFlight{false};
        std::atomic<uint64_t> serial{0};
    };

    // ── Internal methods (ServiceEngine.cpp) ──
    void setEngine(std::shared_ptr<SearchEngine> engine);
    void setPersistence(std::shared_ptr<IndexPersistence> persistence);
    void setContentPersistence(std::shared_ptr<ContentIndexPersistence> persistence);
    void rebuildContentIndexOnMutationQueue(const std::vector<std::string>& extensions,
                                            uint64_t maxFileSize);
    void clearContentIndexOnMutationQueue();
    void backgroundSyncEngine(std::shared_ptr<SearchEngine> engine,
                              std::shared_ptr<IndexPersistence> persistence,
                              uint64_t lastEventId,
                              std::chrono::steady_clock::time_point incrementalStart,
                              std::chrono::steady_clock::time_point indexLoadDone,
                              uint64_t generation);
    // Build Phase 2 secondary indices, retrying with backoff when
    // SearchEngine::completePhase2() defers due to transient memory pressure.
    // Runs as a single background block tracked by backgroundGroup_.
    void runPhase2Completion(uint64_t generation);
    IndexMetadata buildMetadata();

    // ── FSEvents methods (ServiceEngine+FSEvents.cpp) ──
    void applyFSEvents(const std::vector<FileSystemWatcher::Event>& events,
                       std::shared_ptr<SearchEngine> engine);
    void startMonitoring(FSEventStreamEventId sinceEventId = kFSEventStreamEventIdSinceNow);
    /// Caller holds `monitoringMutex_` and has already stopped any stream
    /// that this start replaces.
    void startMonitoringUnlocked(FSEventStreamEventId sinceEventId);
    void noteDeferredMonitoringRefresh();
    void stopMonitoring();
    void restartMonitoring();
    void startNetworkPolling(const std::vector<std::string>& roots);
    void stopNetworkPolling();
    void scheduleRescanForPaths(const std::vector<std::string>& paths);
    void flushPendingRescans(dispatch_source_t firingTimer = nullptr);

    // ── Content methods (ServiceEngine+Content.cpp) ──
    void startContentIndexing();
    void setupContentPersistence();
    void updateContentForPath(const std::string& fullPath, bool removed,
                              std::shared_ptr<SearchEngine> engine);

    // ── Config snapshot (thread-safe) ──
    ServiceConfig safeConfig() const;

    // ── Static helpers ──
    static bool isInsideAppBundle(const std::string& path);
    static bool pathEndsWithApp(const std::string& path);
    std::vector<std::string> effectiveScanRoots() const;
    void useVolumeKindStore(const ServiceConfig& config);
    PathUtils::VolumeKindStore& ownVolumeKinds();
    void pruneUnmountedVolumes(const std::shared_ptr<SearchEngine>& engine);
    void noteIndexedScanRoots(const std::vector<std::string>& roots);
    void noteIndexedIdentityForCompletedRescan(const std::string& scannedPath);
    bool lifecycleBusy() const;
    void deferVolumeRescan(std::string dir, std::function<void()> completion);
    void deferVolumeRemoval(std::string path, std::function<void(uint32_t)> completion,
                            bool cancelIfRemounted);
    void deferVolumeRename(std::string oldPath, std::string newPath);
    void deferVolumeEvents(uint64_t generation,
                           uint64_t indexRevision,
                           std::vector<FileSystemWatcher::Event> events);
    /// Run `work` on `mutationQueue_`. A caller that is already on that queue
    /// runs it inline so one deferred batch keeps its arrival order.
    void enqueueMutation(dispatch_block_t work);
    /// Drop replay batches still sitting on the shared deferred queue.
    /// A batch already swapped into a local drain is not visible here; it
    /// carries `indexRevision` and is dropped when that snapshot is replaced.
    void discardDeferredEvents();
    /// Apply events to the engine published for `generation` and
    /// `indexRevision`, or queue them when a scan owns the index.
    /// `requiredEngine` refuses a replay that would otherwise keep writing an
    /// engine that is no longer published. A stale snapshot is dropped, not
    /// queued again. Caller must already be on `mutationQueue_`.
    void applyOrDeferEvents(uint64_t generation,
                            uint64_t indexRevision,
                            std::vector<FileSystemWatcher::Event> events,
                            std::shared_ptr<SearchEngine> requiredEngine,
                            bool allowWhileSyncing);
    enum class SnapshotPublishResult { Replaced, Superseded, Unchanged };
    /// Caller holds `lifecycleStateMutex_`. Drops deferred FSEvents batches
    /// for the snapshot that `setEngine` or `loadRecords` is about to replace.
    void retireDeferredEventSnapshotUnlocked();
    /// Publish `records` into `engine` under the lifecycle lock. Bumps the
    /// index snapshot first so event batches already taken off the deferred
    /// queue cannot modify the replacement. Does not change
    /// `lifecycleGeneration_`.
    SnapshotPublishResult publishScannedRecords(
        const std::shared_ptr<SearchEngine>& engine,
        std::vector<FileRecord> records,
        const std::shared_ptr<ContentIndex>& replacementContent,
        const std::shared_ptr<IndexPersistence>& persistence,
        uint64_t generation);
    void rescanSubtreeNow(const std::string& dir, std::function<void()> completion);
    void removeSubtreeNow(const std::string& pathPrefix,
                          std::function<void(uint32_t)> completion,
                          bool cancelIfRemounted);
    void refreshRenamedPathNow(const std::string& oldPath, const std::string& newPath);
    void startOrRestartVolumeMonitoring();
    /// `requireStartup` is false when the scan failed and no later success
    /// will drain the queue. Removals, rescans, and renames still run.
    void applyDeferredVolumeWork(bool requireStartup = true);
    void quiesceMonitoringUnlocked();
    void stopMonitoringObservers();
    ScanConfig scanConfigForRoots(const std::vector<std::string>& roots) const;
    ScanConfig scanConfig() const;
    bool isPathAllowedByConfig(const std::string& path, bool forContent) const;
    std::string configSignature() const;
    bool isGenerationCurrent(uint64_t generation) const;
    bool registerScanner(const std::shared_ptr<DirectoryScanner>& scanner,
                         uint64_t generation);
    void unregisterScanner(const std::shared_ptr<DirectoryScanner>& scanner);
    void cancelActiveScanners();
    void removeIndexFiles(const ServiceConfig& config);
    /// Acquire the single-instance lock if not already held. On failure sets
    /// startupFatal_, fires onStartupFailed and returns false; the caller must
    /// then abort startup without loading, replaying or writing any index file.
    bool ensureInstanceLock(const ServiceConfig& config);

    // ── Config (protected by configMutex_) ──
    ServiceConfig config_;
    mutable std::shared_mutex configMutex_;

    // ── Core objects ──
    std::unique_ptr<PathUtils::VolumeKindStore> volumeKinds_;
    std::shared_ptr<SearchEngine> engine_;
    std::shared_ptr<FileSystemWatcher> watcher_;
    std::shared_ptr<ContentIndex> contentIndex_;
    std::shared_ptr<IndexPersistence> persistence_;
    std::shared_ptr<ContentIndexPersistence> contentPersistence_;
    std::shared_ptr<HttpServer> httpServer_;
    HttpServer::AdminCallbacks adminCallbacks_;
    InstanceLock instanceLock_;

    // ── Thread safety ──
    std::shared_mutex engineMutex_;
    std::shared_mutex contentMutex_;
    std::shared_mutex persistenceMutex_;
    std::shared_mutex contentPersistenceMutex_;
    std::mutex httpMutex_;
    /// Publishes a generation, scan/sync flags, and the current engine together.
    /// Index mutations must hold this lock from the generation check through
    /// the write, so a rebuild cannot swap the engine in between.
    std::mutex lifecycleStateMutex_;
    std::mutex activeScannersMutex_;
    std::vector<std::weak_ptr<DirectoryScanner>> activeScanners_;

    // ── Dispatch queues & groups ──
    dispatch_queue_t mutationQueue_;
    dispatch_group_t backgroundGroup_;
    dispatch_group_t contentIndexingGroup_;
    dispatch_queue_t lifecycleQueue_;
    dispatch_source_t networkPollingTimer_ = nullptr;

    // ── Atomic flags ──
    std::atomic<bool> isScanning_{false};
    std::atomic<bool> isMonitoring_{false};
    /// Event id the running watcher was started from. `SinceNow` means the
    /// stream is not replaying a scan.
    std::atomic<uint64_t> monitoringAnchor_{0};
    std::atomic<bool> armVolumeMonitoringBeforeReplay_{false};
    std::atomic<bool> isContentIndexing_{false};
    std::atomic<uint64_t> contentIndexingActiveGeneration_{0};
    std::atomic<bool> shuttingDown_{false};
    std::atomic<bool> startupCompleted_{false};
    std::atomic<bool> startupFatal_{false};
    std::atomic<bool> isSyncing_{false};
    std::atomic<bool> cancelContentIndexing_{false};
    std::atomic<uint64_t> contentIndexGeneration_{0};
    std::atomic<uint64_t> contentRebuildRevision_{0};
    std::shared_ptr<NetworkPollingState> networkPollingState_ =
        std::make_shared<NetworkPollingState>();
    std::mutex contentRebuildRequestMutex_;
    std::vector<std::string> pendingContentExtensions_;
    uint64_t pendingContentMaxFileSize_ = 0;
    bool hasPendingContentConfig_ = false;
    std::atomic<uint64_t> lifecycleGeneration_{1};
    /// Records currently published in the engine. Background full replacement
    /// bumps this without retiring the sync's lifecycle generation.
    std::atomic<uint64_t> indexRevision_{1};
    std::atomic<bool> publishFreshScanOnNextDeferredDrain_{false};
    std::atomic<uint64_t> deferredDrainCount_{0};
    std::atomic<bool> rebuilding_{false};

    // ── Volume unmount tracking ──
    mutable std::mutex unmountingVolumesMutex_;
    std::set<std::string> unmountingVolumes_;
    // Serializes watcher start/stop and the network polling timer.
    std::mutex monitoringMutex_;
    // Mount, rescan, and removal requests that arrived while a scan or sync
    // owned the index. One queue keeps unmount and remount in arrival order.
    std::mutex deferredVolumeMutex_;
    struct DeferredVolumeWork {
        enum class Kind { Rescan, Remove, Rename, Events };
        Kind kind = Kind::Rescan;
        std::string path;
        std::string newPath;
        std::vector<FileSystemWatcher::Event> events;
        /// Generation that produced `events`. A later rebuild must not apply
        /// them to the replacement index.
        uint64_t generation = 0;
        /// Index snapshot these events belong to. Background full replacement
        /// keeps the lifecycle generation and bumps this instead.
        uint64_t indexRevision = 0;
        std::function<void()> rescanCompletion;
        std::function<void(uint32_t)> removeCompletion;
        /// A removal queued while a scan owns the index must not run after the
        /// same path has been mounted again.
        bool cancelIfRemounted = false;
    };
    std::vector<DeferredVolumeWork> deferredVolumeWork_;
    bool deferredMonitoringRefresh_ = false;

    // ── Rescan debounce state ──
    std::mutex pendingRescanMutex_;
    std::set<std::string> pendingRescanPaths_;
    dispatch_source_t rescanDebounceTimer_ = nullptr;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastRescanTime_;

    // ── Constants ──
    static constexpr double kRescanDebounceDelaySec = 5.0;
    static constexpr double kRescanThrottleIntervalSec = 300.0;
};

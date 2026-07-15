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
#include <dispatch/dispatch.h>

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
    uint16_t httpPort = 0;   // 0 = no HTTP server; >0 = auto-start after index is ready
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
/// Bridge (GUI) and CLI daemon both instantiate this class.
class ServiceEngine {
public:
    // ── Callbacks set by caller ──
    using IndexChangedCallback = std::function<void()>;
    using ScanProgressCallback = std::function<void(uint64_t scanned, uint64_t dirs)>;
    using ContentProgressCallback = std::function<void(uint64_t indexed, uint64_t total)>;
    using ContentCompleteCallback = std::function<void(uint32_t totalIndexed)>;
    using StartupCallback = std::function<void(uint32_t totalRecords, bool didFullScan)>;

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
    void updateConfig(const ServiceConfig& config);
    void setAdminCallbacks(HttpServer::AdminCallbacks callbacks);

    // ── Public operations ──
    void rescanSubtree(const std::string& dir,
                       std::function<void()> completion = nullptr);
    void removeSubtree(const std::string& pathPrefix,
                       std::function<void(uint32_t)> completion = nullptr);
    void rebuildContentIndex();
    void clearContentIndex();
    void compactIndex();

    // ── Volume unmount protection ──
    void markVolumeUnmounting(const std::string& volumePath);
    void clearVolumeUnmounting(const std::string& volumePath);
    bool isVolumeUnmounting(const std::string& path) const;

    // ── Thread-safe accessors ──
    std::shared_ptr<SearchEngine> safeEngine();
    std::shared_ptr<ContentIndex> safeContentIndex();
    std::shared_ptr<IndexPersistence> safePersistence();
    std::shared_ptr<ContentIndexPersistence> safeContentPersistence();

    // ── State queries ──
    bool isScanning() const  { return isScanning_.load(std::memory_order_relaxed); }
    bool isMonitoring() const { return isMonitoring_.load(std::memory_order_relaxed); }
    bool isSyncing() const   { return isSyncing_.load(std::memory_order_relaxed); }

    uint32_t recordCount();
    uint32_t liveRecordCount();

    // ── Callbacks (set before starting) ──
    IndexChangedCallback onIndexChanged;
    ScanProgressCallback onScanProgress;
    ContentProgressCallback onContentIndexProgress;
    ContentCompleteCallback onContentIndexComplete;

private:
    // ── Internal methods (ServiceEngine.cpp) ──
    void setEngine(std::shared_ptr<SearchEngine> engine);
    void setPersistence(std::shared_ptr<IndexPersistence> persistence);
    void setContentPersistence(std::shared_ptr<ContentIndexPersistence> persistence);
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
    void startMonitoring();
    void stopMonitoring();
    void restartMonitoring();
    void scheduleRescanForPaths(const std::vector<std::string>& paths);
    void flushPendingRescans();

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

    // ── Config (protected by configMutex_) ──
    ServiceConfig config_;
    mutable std::shared_mutex configMutex_;

    // ── Core objects ──
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
    std::mutex lifecycleStateMutex_;
    std::mutex activeScannersMutex_;
    std::vector<std::weak_ptr<DirectoryScanner>> activeScanners_;

    // ── Dispatch queues & groups ──
    dispatch_queue_t mutationQueue_;
    dispatch_group_t backgroundGroup_;
    dispatch_group_t contentIndexingGroup_;
    dispatch_queue_t lifecycleQueue_;

    // ── Atomic flags ──
    std::atomic<bool> isScanning_{false};
    std::atomic<bool> isMonitoring_{false};
    std::atomic<bool> isContentIndexing_{false};
    std::atomic<bool> shuttingDown_{false};
    std::atomic<bool> startupCompleted_{false};
    std::atomic<bool> isSyncing_{false};
    std::atomic<bool> cancelContentIndexing_{false};
    std::atomic<uint64_t> contentIndexGeneration_{0};
    std::atomic<uint64_t> lifecycleGeneration_{1};
    std::atomic<bool> rebuilding_{false};

    // ── Volume unmount tracking ──
    mutable std::mutex unmountingVolumesMutex_;
    std::set<std::string> unmountingVolumes_;

    // ── Rescan debounce state ──
    std::mutex pendingRescanMutex_;
    std::set<std::string> pendingRescanPaths_;
    dispatch_source_t rescanDebounceTimer_ = nullptr;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastRescanTime_;

    // ── Constants ──
    static constexpr double kRescanDebounceDelaySec = 5.0;
    static constexpr double kRescanThrottleIntervalSec = 300.0;
    static constexpr const char* kAppVersion = "1.1.0";
};

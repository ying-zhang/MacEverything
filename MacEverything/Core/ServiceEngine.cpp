#include "ServiceEngine.h"
#include "PathUtils.h"
#include "Logger.h"
#include "HttpToken.h"
#include <sys/stat.h>
#include <sys/mount.h>
#include <filesystem>
#include <fnmatch.h>
#include <sstream>

namespace fs = std::filesystem;

namespace {

SearchEngineOptions searchOptionsFromConfig(const ServiceConfig& config) {
    SearchEngineOptions options;
    options.enablePinyinInitials = config.enablePinyinInitials;
    options.enablePathTrigramIndex = config.enablePathTrigramIndex;
    options.enablePathIndex = true;
    return options;
}

/// Like pathContainsOrEquals, but also matches when `parent` uses a symlinked
/// spelling while `child` uses the real path. FSEvents reports real paths
/// (e.g. /private/tmp for /tmp, /private/var for /var), so a configured root
/// spelled "/tmp/foo" would otherwise fail to match an event path
/// "/private/tmp/foo/bar". Resolves the parent (which must exist) and retries.
static bool pathContainsOrEqualsReal(const std::string& parent, const std::string& child) {
    if (PathUtils::pathContainsOrEquals(parent, child)) return true;
    std::error_code ec;
    fs::path real = fs::canonical(parent, ec);
    if (!ec) return PathUtils::pathContainsOrEquals(real.string(), child);
    return false;
}

bool pathContainsComponentPath(const std::string& path, const std::string& componentPath) {
    if (componentPath.empty()) return false;
    size_t pos = path.find(componentPath);
    while (pos != std::string::npos) {
        const bool startsAtComponent = pos == 0 || path[pos - 1] == '/';
        const size_t end = pos + componentPath.size();
        const bool endsAtComponent = end == path.size() || path[end] == '/';
        if (startsAtComponent && endsAtComponent) {
            return true;
        }
        pos = path.find(componentPath, pos + 1);
    }
    return false;
}

bool isSystemFilteredPath(const std::string& path) {
    return path == "/System" ||
           path.rfind("/System/", 0) == 0 ||
           path == "/private/var" ||
           path.rfind("/private/var/", 0) == 0 ||
           pathContainsComponentPath(path, "Library/Caches") ||
           pathContainsComponentPath(path, ".Spotlight-V100") ||
           pathContainsComponentPath(path, ".fseventsd") ||
           pathContainsComponentPath(path, ".Trashes");
}

std::vector<std::string> systemAllowedPathsForRoots(const std::vector<std::string>& roots) {
    std::vector<std::string> allowed;
    allowed.reserve(roots.size());
    for (const auto& root : roots) {
        if (!root.empty() && root != "/" && isSystemFilteredPath(root)) {
            allowed.push_back(root);
        }
    }
    return allowed;
}

bool hasSystemAllowedPath(const std::vector<std::string>& roots, const std::string& path) {
    for (const auto& allowed : systemAllowedPathsForRoots(roots)) {
        if (PathUtils::pathContainsOrEquals(allowed, path)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> exclusionsForRoots(const std::vector<std::string>& roots,
                                            const std::vector<std::string>& excludedPaths) {
    std::vector<std::string> filtered;
    filtered.reserve(excludedPaths.size());

    for (const auto& excluded : excludedPaths) {
        if (excluded.empty()) continue;

        bool overriddenByExplicitRoot = false;
        for (const auto& root : roots) {
            if (PathUtils::pathContainsOrEquals(excluded, root)) {
                overriddenByExplicitRoot = true;
                break;
            }
        }

        if (!overriddenByExplicitRoot) {
            filtered.push_back(excluded);
        }
    }

    return filtered;
}

} // namespace

// ═══════════════════════════════════════════════════════
//  Construction / Destruction
// ═══════════════════════════════════════════════════════

ServiceEngine::ServiceEngine(const ServiceConfig& config)
    : config_(config),
      volumeKinds_(std::make_unique<PathUtils::VolumeKindStore>())
{
    useVolumeKindStore(config_);
    engine_ = std::make_shared<SearchEngine>(searchOptionsFromConfig(config_));
    watcher_ = std::make_shared<FileSystemWatcher>("live");
    contentIndex_ = std::make_shared<ContentIndex>();
    mutationQueue_ = dispatch_queue_create("com.maceverything.mutation", DISPATCH_QUEUE_SERIAL);
    dispatch_queue_set_specific(mutationQueue_, this, this, nullptr);
    backgroundGroup_ = dispatch_group_create();
    contentIndexingGroup_ = dispatch_group_create();
    lifecycleQueue_ = dispatch_queue_create("com.maceverything.lifecycle", DISPATCH_QUEUE_SERIAL);
}

ServiceEngine::~ServiceEngine() {
    shutdown();
}

ServiceConfig ServiceEngine::safeConfig() const {
    std::shared_lock lock(configMutex_);
    return config_;
}

void ServiceEngine::updateConfig(const ServiceConfig& config) {
    bool restartHttp;
    bool rootsChanged;
    {
        std::unique_lock lock(configMutex_);
        restartHttp = config_.httpPort != config.httpPort ||
                      config_.httpAuthenticationEnabled != config.httpAuthenticationEnabled;
        rootsChanged = config_.scanRoots != config.scanRoots ||
                       config_.scanRoot != config.scanRoot;
        config_ = config;
    }
    useVolumeKindStore(config);

    if (restartHttp) {
        stopHttpServer();
        if (config.httpPort > 0) {
            startHttpServer(config.httpPort);
        }
    }

    LOG_INFO("ServiceEngine", "updateConfig: rootsChanged=" << rootsChanged
             << " httpChanged=" << restartHttp
             << " monitoring=" << isMonitoring_.load(std::memory_order_relaxed)
             << " liveRecords=" << liveRecordCount());

    if (rootsChanged && isMonitoring_.load(std::memory_order_relaxed)) {
        restartMonitoring();
    }
}

void ServiceEngine::setAdminCallbacks(HttpServer::AdminCallbacks callbacks) {
    std::shared_ptr<HttpServer> server;
    {
        std::lock_guard<std::mutex> lock(httpMutex_);
        adminCallbacks_ = callbacks;
        server = httpServer_;
    }
    if (server) server->setAdminCallbacks(std::move(callbacks));
}

bool ServiceEngine::isGenerationCurrent(uint64_t generation) const {
    return !shuttingDown_.load(std::memory_order_acquire) &&
           lifecycleGeneration_.load(std::memory_order_acquire) == generation;
}

bool ServiceEngine::registerScanner(const std::shared_ptr<DirectoryScanner>& scanner,
                                    uint64_t generation) {
    std::lock_guard<std::mutex> lock(activeScannersMutex_);
    if (!isGenerationCurrent(generation)) {
        scanner->cancel();
        return false;
    }
    activeScanners_.erase(
        std::remove_if(activeScanners_.begin(), activeScanners_.end(),
                       [](const auto& weak) { return weak.expired(); }),
        activeScanners_.end());
    activeScanners_.push_back(scanner);
    return true;
}

void ServiceEngine::unregisterScanner(const std::shared_ptr<DirectoryScanner>& scanner) {
    std::lock_guard<std::mutex> lock(activeScannersMutex_);
    activeScanners_.erase(
        std::remove_if(activeScanners_.begin(), activeScanners_.end(),
                       [&](const auto& weak) {
                           auto current = weak.lock();
                           return !current || current == scanner;
                       }),
        activeScanners_.end());
}

void ServiceEngine::cancelActiveScanners() {
    std::lock_guard<std::mutex> lock(activeScannersMutex_);
    for (auto& weak : activeScanners_) {
        if (auto scanner = weak.lock()) scanner->cancel();
    }
}

void ServiceEngine::removeIndexFiles(const ServiceConfig& config) {
    const std::vector<std::string> names = {
        "index.bin", "index.wal", "index.wal.new", "index.pages",
        "index.ptable", "index.v6", "content_index.bin",
        "content_index.wal", "content_index.wal.new"
    };
    for (const auto& name : names) {
        std::error_code ec;
        fs::remove(config.cachePath + "/" + name, ec);
        if (ec) LOG_WARN("ServiceEngine", "Failed to remove " << name << ": " << ec.message());
    }
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(config.cachePath, ec)) {
        if (ec) break;
        auto name = entry.path().filename().string();
        if (name.rfind("index.wal.seg.", 0) == 0 ||
            name.rfind("content_index.wal.seg.", 0) == 0) {
            fs::remove(entry.path(), ec);
            ec.clear();
        }
    }
}

bool ServiceEngine::ensureInstanceLock(const ServiceConfig& config) {
    const std::string cacheDir = config.cachePath;
    const std::string lockPath = cacheDir + "/.instance.lock";
    if (instanceLock_.isLocked()) {
        if (instanceLock_.path() == lockPath) return true;
        startupFatal_.store(true, std::memory_order_release);
        const std::string reason =
            "cache path changed while the index lock is held; refusing to access " + lockPath;
        LOG_ERROR("ServiceEngine", reason);
        if (onStartupFailed) onStartupFailed(reason);
        return false;
    }

    std::error_code dirEc;
    fs::create_directories(cacheDir, dirEc);

    InstanceLockError lockErr = InstanceLockError::None;
    if (instanceLock_.tryLock(lockPath, config.appVersion, &lockErr)) {
        startupFatal_.store(false, std::memory_order_release);
        return true;
    }

    startupFatal_.store(true, std::memory_order_release);
    std::string reason;
    if (lockErr == InstanceLockError::AlreadyLocked) {
        reason = "another instance holds the index lock at " + lockPath +
                 "; refusing to load or modify shared index state";
    } else {
        reason = "failed to open or lock " + lockPath +
                 "; check directory permissions and disk space";
    }
    LOG_ERROR("ServiceEngine", reason);
    if (onStartupFailed) onStartupFailed(reason);
    return false;
}

// ═══════════════════════════════════════════════════════
//  Thread-safe accessors
// ═══════════════════════════════════════════════════════

std::shared_ptr<SearchEngine> ServiceEngine::safeEngine() {
    std::shared_lock lock(engineMutex_);
    return engine_;
}

void ServiceEngine::setEngine(std::shared_ptr<SearchEngine> engine) {
    std::unique_lock lock(engineMutex_);
    engine_ = engine;
}

std::shared_ptr<ContentIndex> ServiceEngine::safeContentIndex() {
    std::shared_lock lock(contentMutex_);
    return contentIndex_;
}

std::shared_ptr<IndexPersistence> ServiceEngine::safePersistence() {
    std::shared_lock lock(persistenceMutex_);
    return persistence_;
}

void ServiceEngine::setPersistence(std::shared_ptr<IndexPersistence> persistence) {
    std::unique_lock lock(persistenceMutex_);
    persistence_ = persistence;
}

std::shared_ptr<ContentIndexPersistence> ServiceEngine::safeContentPersistence() {
    std::shared_lock lock(contentPersistenceMutex_);
    return contentPersistence_;
}

void ServiceEngine::setContentPersistence(std::shared_ptr<ContentIndexPersistence> persistence) {
    std::unique_lock lock(contentPersistenceMutex_);
    contentPersistence_ = persistence;
}

// ═══════════════════════════════════════════════════════
//  State queries
// ═══════════════════════════════════════════════════════

uint32_t ServiceEngine::recordCount() {
    auto engine = safeEngine();
    return engine ? engine->recordCount() : 0;
}

uint32_t ServiceEngine::liveRecordCount() {
    auto engine = safeEngine();
    return engine ? engine->liveRecordCount() : 0;
}

// ═══════════════════════════════════════════════════════
//  Metadata builder (pure C++ — no NSProcessInfo)
// ═══════════════════════════════════════════════════════

IndexMetadata ServiceEngine::buildMetadata() {
    IndexMetadata meta;
    meta.lastEventId = watcher_ ? watcher_->getLastEventId() : 0;
    auto roots = effectiveScanRoots();
    auto cfg = safeConfig();
    std::string joinedRoots;
    for (size_t i = 0; i < roots.size(); i++) {
        if (i > 0) joinedRoots += ";";
        joinedRoots += roots[i];
    }
    meta.extra[IndexMetadata::kScanRoot] = joinedRoots.empty() ? cfg.scanRoot : joinedRoots;
    meta.extra[IndexMetadata::kAppVersion] = cfg.appVersion;
    meta.extra[IndexMetadata::kRecordFormat] = "v6_flat";
    meta.extra[IndexMetadata::kOSVersion] = PathUtils::getOSVersionString();
    meta.extra["config_signature"] = configSignature();
    return meta;
}

// ═══════════════════════════════════════════════════════
//  Volume unmount protection
// ═══════════════════════════════════════════════════════

void ServiceEngine::markVolumeUnmounting(const std::string& volumePath) {
    std::lock_guard<std::mutex> lock(unmountingVolumesMutex_);
    unmountingVolumes_.insert(volumePath);
    LOG_INFO("ServiceEngine", "Marked volume as unmounting: " << volumePath);
}

void ServiceEngine::clearVolumeUnmounting(const std::string& volumePath) {
    std::lock_guard<std::mutex> lock(unmountingVolumesMutex_);
    unmountingVolumes_.erase(volumePath);
}

bool ServiceEngine::isVolumeUnmounting(const std::string& path) const {
    std::lock_guard<std::mutex> lock(unmountingVolumesMutex_);
    for (const auto& vol : unmountingVolumes_) {
        if (path.size() >= vol.size() &&
            path.compare(0, vol.size(), vol) == 0 &&
            (path.size() == vol.size() || path[vol.size()] == '/')) {
            return true;
        }
    }
    return false;
}

PathUtils::VolumeKindStore& ServiceEngine::ownVolumeKinds() {
    return *volumeKinds_;
}

void ServiceEngine::useVolumeKindStore(const ServiceConfig& config) {
    auto& store = ownVolumeKinds();
    if (config.cachePath.empty()) {
        PathUtils::setVolumeKindStorePath(store, {});
        return;
    }
    PathUtils::setVolumeKindStorePath(store, config.cachePath + "/volume-kinds.tsv");
    PathUtils::loadVolumeKinds(store);
}

bool ServiceEngine::lifecycleBusy() const {
    return isSyncing_.load(std::memory_order_acquire) ||
           isScanning_.load(std::memory_order_acquire);
}

void ServiceEngine::pruneUnmountedVolumes(const std::shared_ptr<SearchEngine>& engine) {
    if (!engine) return;
    for (const auto& root : effectiveScanRoots()) {
        // Offline NAS shares and unknown /Volumes paths stay in the cache.
        // Only a local external disk we previously saw mounted is removed.
        if (!PathUtils::isUnpluggedRemovableVolume(ownVolumeKinds(), root)) continue;
        const uint32_t removed = engine->removeByPathPrefix(root);
        if (removed > 0) {
            LOG_INFO("ServiceEngine", "Dropped " << removed
                     << " cached records for unplugged external volume: " << root);
        }
    }
}

void ServiceEngine::noteIndexedScanRoots(const std::vector<std::string>& roots) {
    for (const auto& root : roots) {
        if (!PathUtils::isReachableDirectory(root)) continue;
        const auto traits = PathUtils::inspectMountedVolume(ownVolumeKinds(), root);
        if (traits.uuid.empty()) continue;
        PathUtils::noteIndexedVolumeIdentity(ownVolumeKinds(), root, traits.uuid);
    }
}

void ServiceEngine::noteIndexedIdentityForCompletedRescan(const std::string& scannedPath) {
    if (!PathUtils::scannedPathCoversConfiguredVolumeRoots(scannedPath, effectiveScanRoots())) {
        return;
    }
    if (!PathUtils::isReachableDirectory(scannedPath)) return;
    const auto traits = PathUtils::inspectMountedVolume(ownVolumeKinds(), scannedPath);
    if (traits.uuid.empty()) return;
    PathUtils::noteIndexedVolumeIdentity(ownVolumeKinds(), scannedPath, traits.uuid);
}

// ═══════════════════════════════════════════════════════
//  Full scan
// ═══════════════════════════════════════════════════════

void ServiceEngine::startFullScan(StartupCallback completion) {
    const ServiceConfig config = safeConfig();
    uint64_t generation = 0;
    {
        // Publish "scan owns the index" before any mutation can pass its
        // lifecycle check. The store and that check share lifecycleStateMutex_.
        std::lock_guard<std::mutex> stateLock(lifecycleStateMutex_);
        generation = lifecycleGeneration_.load(std::memory_order_acquire);
        if (shuttingDown_.load(std::memory_order_acquire)) {
            if (completion) completion(0, false);
            return;
        }
        isScanning_.store(true, std::memory_order_release);
    }
    stopMonitoring();

    if (!ensureInstanceLock(config)) {
        {
            std::lock_guard<std::mutex> stateLock(lifecycleStateMutex_);
            if (isGenerationCurrent(generation)) {
                isScanning_.store(false, std::memory_order_release);
            }
        }
        if (completion) completion(0, false);
        applyDeferredVolumeWork(/*requireStartup=*/false);
        return;
    }

    auto roots = effectiveScanRoots();
    auto scannerConfig = scanConfigForRoots(roots);
    LOG_INFO("ServiceEngine", "startFullScan from " << roots.size() << " root(s)");

    dispatch_group_async(backgroundGroup_, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        auto scanStart = std::chrono::steady_clock::now();
        const FSEventStreamEventId scanStartEventId =
            FileSystemWatcher::getCurrentSystemEventId();
        auto scanner = std::make_shared<DirectoryScanner>();
        if (!this->registerScanner(scanner, generation)) return;

        // Progress polling timer
        dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
            dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0));
        dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 0),
                                  200 * NSEC_PER_MSEC, 50 * NSEC_PER_MSEC);
        std::weak_ptr<DirectoryScanner> scannerWeak = scanner;
        auto progressCb = this->onScanProgress;
        dispatch_source_set_event_handler(timer, ^{
            if (!progressCb) return;
            auto s = scannerWeak.lock();
            if (!s) return;
            const auto& stats = s->getStats();
            progressCb(stats.fileCount.load(std::memory_order_relaxed),
                       stats.dirCount.load(std::memory_order_relaxed));
        });
        dispatch_resume(timer);

        scanner->scan(roots, scannerConfig);
        dispatch_source_cancel(timer);
        dispatch_release(timer);
        this->unregisterScanner(scanner);

        if (!this->isGenerationCurrent(generation)) return;

        if (!scanner->isComplete()) {
            const auto& stats = scanner->getStats();
            const auto reason = "Full scan incomplete: " +
                std::to_string(stats.errorCount.load(std::memory_order_relaxed)) + " root or traversal error(s)";
            LOG_ERROR("ServiceEngine", reason);
            {
                std::lock_guard<std::mutex> stateLock(this->lifecycleStateMutex_);
                if (!this->isGenerationCurrent(generation)) return;
                this->isScanning_.store(false, std::memory_order_release);
            }
            if (onStartupFailed) onStartupFailed(reason);
            if (completion) completion(0, false);
            // Startup will not complete, so the normal drain never runs.
            this->applyDeferredVolumeWork(/*requireStartup=*/false);
            return;
        }

        auto results = scanner->takeResults();
        auto engine = std::make_shared<SearchEngine>(searchOptionsFromConfig(config));
        engine->loadRecords(std::move(results));
        uint32_t count = engine->liveRecordCount();

        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - scanStart).count();
        LOG_INFO("ServiceEngine", "Scan completed: " << count << " records in " << elapsed << "s");

        {
            std::lock_guard<std::mutex> stateLock(this->lifecycleStateMutex_);
            if (!this->isGenerationCurrent(generation)) return;
            // The scan result replaces every event observed against the
            // previous snapshot. Real changes since scanStartEventId are
            // replayed by the watcher started below.
            this->retireDeferredEventSnapshotUnlocked();
            this->setEngine(engine);
            this->isScanning_.store(false, std::memory_order_relaxed);
        }
        // The classification file must describe the index that was published.
        // A superseded scan returns above, before setEngine().
        if (this->isGenerationCurrent(generation)) {
            this->noteIndexedScanRoots(roots);
        }

        if (completion) completion(count, true);

        // Deferred mount work waits until startup has completed. The no-cache
        // callback sets the flag too; a direct startFullScan caller does not.
        // A superseded scan must not start its own watcher, but it may drain
        // work that the current lifecycle is already idle enough to accept.
        if (this->isGenerationCurrent(generation)) {
            this->startupCompleted_.store(true, std::memory_order_release);
            if (this->armVolumeMonitoringBeforeReplay_.exchange(false, std::memory_order_acq_rel)) {
                this->startOrRestartVolumeMonitoring();
            }
            if (config.realtimeMonitoring) {
                this->startMonitoring(scanStartEventId);
            }
        }
        this->applyDeferredVolumeWork();

        // Content indexing in background
        dispatch_group_async(this->backgroundGroup_, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            if (!this->isGenerationCurrent(generation)) return;
            if (config.contentIndexingEnabled) {
                this->setupContentPersistence();
                this->startContentIndexing();
            }
        });
    });
}

// ═══════════════════════════════════════════════════════
//  Incremental load (cached index + background sync)
// ═══════════════════════════════════════════════════════

void ServiceEngine::startIncremental(StartupCallback completion) {
    const ServiceConfig config = safeConfig();
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> stateLock(lifecycleStateMutex_);
        generation = lifecycleGeneration_.load(std::memory_order_acquire);
        if (shuttingDown_.load(std::memory_order_acquire)) {
            if (completion) completion(0, false);
            return;
        }
        isScanning_.store(true, std::memory_order_release);
        startupCompleted_.store(false, std::memory_order_release);
        isSyncing_.store(false, std::memory_order_release);
    }
    stopMonitoring();

    if (!ensureInstanceLock(config)) {
        {
            std::lock_guard<std::mutex> stateLock(lifecycleStateMutex_);
            if (isGenerationCurrent(generation)) {
                isScanning_.store(false, std::memory_order_release);
            }
        }
        if (completion) completion(0, false);
        applyDeferredVolumeWork(/*requireStartup=*/false);
        return;
    }

    LOG_INFO("ServiceEngine", "startIncremental from " << effectiveScanRoots().size() << " root(s)");

    dispatch_group_async(backgroundGroup_, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (!this->isGenerationCurrent(generation)) return;
        auto incrementalStart = std::chrono::steady_clock::now();
        auto engine = std::make_shared<SearchEngine>(searchOptionsFromConfig(config));

        std::string cacheStr = config.cachePath + "/index.bin";
        std::string walStr   = config.cachePath + "/index.wal";
        std::string pagesStr = config.cachePath + "/index.pages";
        std::string ptableStr = config.cachePath + "/index.ptable";
        std::string v6Str    = config.cachePath + "/index.v6";

        auto persistence = std::make_unique<IndexPersistence>(
            engine, cacheStr, walStr, pagesStr, ptableStr, v6Str);

        uint64_t lastEventId = persistence->load(this->configSignature());
        if (!this->isGenerationCurrent(generation)) return;
        auto indexLoadDone = std::chrono::steady_clock::now();
        // Drop records for local external disks that were unplugged while the
        // app was not running. Network volumes and unknown roots stay cached.
        this->pruneUnmountedVolumes(engine);
        uint32_t loadedCount = engine->liveRecordCount();

        if (loadedCount > 0) {
            // Have cached index: deliver immediately, then sync in background
            auto sharedPersistence = std::shared_ptr<IndexPersistence>(std::move(persistence));

            bool expected = false;
            if (!this->startupCompleted_.compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel)) {
                return;
            }

            {
                std::lock_guard<std::mutex> stateLock(this->lifecycleStateMutex_);
                if (!this->isGenerationCurrent(generation)) return;
                this->setEngine(engine);
                this->setPersistence(sharedPersistence);
                this->isScanning_.store(false, std::memory_order_relaxed);
                this->isSyncing_.store(true, std::memory_order_relaxed);
            }

            // Auto-start HTTP server once engine is available
            if (config.httpPort > 0) {
                this->startHttpServer(config.httpPort);
            }

            sharedPersistence->attachWAL();
            sharedPersistence->setContentIndex(this->safeContentIndex());
            if (config.contentIndexingEnabled) this->setupContentPersistence();

            uint32_t count = engine->liveRecordCount();
            if (completion) completion(count, false);

            // Dispatch Phase 2: build trigram indices in background
            if (engine->isPhase2Pending()) {
                dispatch_group_async(this->backgroundGroup_, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
                    if (!this->isGenerationCurrent(generation)) return;
                    this->runPhase2Completion(generation);
                });
            }

            this->backgroundSyncEngine(engine, sharedPersistence, lastEventId,
                                       incrementalStart, indexLoadDone, generation);
            return;
        }

        // No cache: full scan
        if (!this->isGenerationCurrent(generation)) return;
        this->startFullScan([this, completion, generation](uint32_t count, bool didFullScan) {
            if (!this->isGenerationCurrent(generation)) return;
            if (!didFullScan) {
                // startFullScan already reported the scan failure. Do not
                // create a persistence object around a missing engine or
                // overwrite a healthy index with an empty snapshot.
                if (completion) completion(0, false);
                return;
            }
            bool expected = false;
            if (!this->startupCompleted_.compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel)) {
                return;
            }

            auto currentConfig = safeConfig();
            std::string cacheStr = currentConfig.cachePath + "/index.bin";
            std::string walStr   = currentConfig.cachePath + "/index.wal";
            std::string pagesStr = currentConfig.cachePath + "/index.pages";
            std::string ptableStr = currentConfig.cachePath + "/index.ptable";
            std::string v6Str    = currentConfig.cachePath + "/index.v6";

            auto newPersistence = std::make_shared<IndexPersistence>(
                this->safeEngine(), cacheStr, walStr, pagesStr, ptableStr, v6Str);
            if (!this->isGenerationCurrent(generation)) return;
            this->setPersistence(newPersistence);
            newPersistence->attachWAL();
            newPersistence->setContentIndex(this->safeContentIndex());
            if (currentConfig.automaticMaintenanceEnabled) {
                newPersistence->startAutoCompaction(300.0, this->watcher_);
            }

            auto meta = this->buildMetadata();
            newPersistence->flush(meta, /*force=*/true);

            // Auto-start HTTP server once engine is available
            if (currentConfig.httpPort > 0) {
                this->startHttpServer(currentConfig.httpPort);
            }

            if (completion) completion(count, true);
        });
    });
}

void ServiceEngine::rebuildIndex(StartupCallback completion) {
    bool expected = false;
    if (!rebuilding_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    if (shuttingDown_.load(std::memory_order_acquire)) {
        rebuilding_.store(false, std::memory_order_release);
        return;
    }

    auto config = safeConfig();

    // Acquire lock before touching index files (removeIndexFiles runs later).
    if (!ensureInstanceLock(config)) {
        rebuilding_.store(false, std::memory_order_release);
        if (completion) completion(0, false);
        return;
    }

    // Rebuild replaces the in-memory engine and removes persisted files before
    // scanning. A previously seen local external disk may be unplugged and is
    // indexed again when it mounts. An offline NAS, an unknown /Volumes path,
    // or a missing local folder blocks the rebuild so the last index survives.
    for (const auto& root : effectiveScanRoots()) {
        struct stat rootStat{};
        if (stat(root.c_str(), &rootStat) == 0 && S_ISDIR(rootStat.st_mode)) continue;
        if (PathUtils::isUnpluggedRemovableVolume(ownVolumeKinds(), root)) {
            LOG_INFO("ServiceEngine", "Rebuild skipping unplugged external volume: " << root);
            continue;
        }
        const std::string reason = "Rebuild blocked: scan root unavailable: " + root;
        LOG_ERROR("ServiceEngine", reason);
        rebuilding_.store(false, std::memory_order_release);
        if (onStartupFailed) onStartupFailed(reason);
        if (completion) completion(0, false);
        return;
    }

    auto oldContent = safeContentIndex();
    auto newContent = std::make_shared<ContentIndex>();
    if (oldContent) {
        newContent->setExtensions(oldContent->getExtensions());
        newContent->setMaxFileSize(oldContent->getMaxFileSize());
    }
    uint64_t rebuildGeneration = 0;
    {
        std::lock_guard<std::mutex> stateLock(lifecycleStateMutex_);
        rebuildGeneration = lifecycleGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
        isScanning_.store(true, std::memory_order_relaxed);
        isSyncing_.store(false, std::memory_order_relaxed);
        startupCompleted_.store(false, std::memory_order_relaxed);
        setEngine(std::make_shared<SearchEngine>(searchOptionsFromConfig(config)));
        {
            std::unique_lock contentLock(contentMutex_);
            contentIndex_ = newContent;
        }
    }
    // Events queued for the previous generation must not run against the
    // engine this rebuild is about to publish.
    discardDeferredEvents();
    if (onIndexChanged) onIndexChanged();

    dispatch_async(lifecycleQueue_, ^{
        LOG_INFO("ServiceEngine", "rebuildIndex: draining previous lifecycle");
        this->stopMonitoring();
        this->contentIndexGeneration_.fetch_add(1, std::memory_order_acq_rel);
        this->cancelContentIndexing_.store(true, std::memory_order_relaxed);
        this->cancelActiveScanners();

        dispatch_sync(this->mutationQueue_, ^{});
        dispatch_group_wait(this->contentIndexingGroup_, DISPATCH_TIME_FOREVER);
        dispatch_group_wait(this->backgroundGroup_, DISPATCH_TIME_FOREVER);
        if (this->shuttingDown_.load(std::memory_order_acquire)) {
            this->rebuilding_.store(false, std::memory_order_release);
            return;
        }

        auto oldPersistence = this->safePersistence();
        if (oldPersistence) oldPersistence->stopAutoCompactionAndWait();
        this->setPersistence(nullptr);
        oldPersistence.reset();

        auto oldContentPersistence = this->safeContentPersistence();
        if (oldContentPersistence) oldContentPersistence->stopAutoCompactionAndWait();
        this->setContentPersistence(nullptr);
        oldContentPersistence.reset();

        this->removeIndexFiles(config);
        this->cancelContentIndexing_.store(false, std::memory_order_relaxed);
        this->isContentIndexing_.store(false, std::memory_order_relaxed);

        // The callback can be discarded without invocation when its lifecycle
        // generation is cancelled. Tying this guard to every callback copy
        // releases rebuilding_ on both success and cancellation.
        auto rebuildReset = std::shared_ptr<void>(nullptr, [this, rebuildGeneration](void*) {
            if (this->lifecycleGeneration_.load(std::memory_order_acquire) == rebuildGeneration) {
                this->rebuilding_.store(false, std::memory_order_release);
            }
        });
        this->startIncremental([this, completion, rebuildReset](uint32_t count, bool didFullScan) {
            (void)rebuildReset;
            this->rebuilding_.store(false, std::memory_order_release);
            if (completion) completion(count, didFullScan);
        });
    });
}

// ═══════════════════════════════════════════════════════
//  Background sync (FSEvents replay or full scan)
// ═══════════════════════════════════════════════════════

void ServiceEngine::runPhase2Completion(uint64_t generation) {
    // completePhase2() may defer (return with phase2Pending_ still true) when it
    // estimates the secondary indices won't fit in available memory. Without a
    // retry, a single transient memory spike at startup would permanently leave
    // search on the slow linear-scan fallback. Retry with exponential backoff so
    // Phase 2 completes once memory frees up.
    constexpr int kMaxAttempts = 8;
    dispatch_semaphore_t sleeper = dispatch_semaphore_create(0);

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (!this->isGenerationCurrent(generation)) break;

        auto eng = this->safeEngine();
        if (!eng || !eng->isPhase2Pending()) break;  // already built or no engine

        eng->completePhase2();
        if (this->onIndexChanged) this->onIndexChanged();

        if (!eng->isPhase2Pending()) break;  // succeeded — indices active

        if (attempt + 1 >= kMaxAttempts) {
            LOG_WARN("ServiceEngine", "Phase 2 still pending after " << kMaxAttempts
                     << " attempts; secondary indices unbuilt (search uses linear-scan fallback).");
            break;
        }

        // Backoff: 15s, 30s, 60s, 120s, 240s, then capped at 300s.
        int64_t delaySec = int64_t(15) << attempt;
        if (delaySec > 300) delaySec = 300;
        LOG_INFO("ServiceEngine", "Phase 2 deferred (low memory); retrying in " << delaySec << "s");

        // Shutdown-responsive sleep: wake every second to re-check shuttingDown_
        // so teardown isn't blocked waiting out a long backoff interval.
        for (int64_t elapsed = 0; elapsed < delaySec; ++elapsed) {
            if (!this->isGenerationCurrent(generation)) break;
            dispatch_semaphore_wait(sleeper, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC));
        }
    }

    dispatch_release(sleeper);
}

void ServiceEngine::retireDeferredEventSnapshotUnlocked() {
    indexRevision_.fetch_add(1, std::memory_order_acq_rel);
    discardDeferredEvents();
}

ServiceEngine::SnapshotPublishResult ServiceEngine::publishScannedRecords(
    const std::shared_ptr<SearchEngine>& engine,
    std::vector<FileRecord> records,
    const std::shared_ptr<ContentIndex>& replacementContent,
    const std::shared_ptr<IndexPersistence>& persistence,
    uint64_t generation)
{
    // lifecycleStateMutex_ then deferredVolumeMutex_ (discardDeferredEvents).
    // Event applies take the lifecycle lock first, then the deferred lock if
    // they queue again, so this order cannot invert.
    std::lock_guard<std::mutex> stateLock(lifecycleStateMutex_);
    if (!isGenerationCurrent(generation)) return SnapshotPublishResult::Superseded;
    SnapshotPublishResult result = SnapshotPublishResult::Unchanged;
    if (engine && safeEngine() == engine) {
        // Local drains already hold batches stamped with the previous id.
        // discardDeferredEvents() only sees the shared queue.
        retireDeferredEventSnapshotUnlocked();
        engine->loadRecords(std::move(records));
        if (replacementContent) {
            std::unique_lock contentLock(contentMutex_);
            contentIndex_ = replacementContent;
        }
        if (persistence) persistence->setContentIndex(replacementContent);
        result = SnapshotPublishResult::Replaced;
    }
    if (isGenerationCurrent(generation)) {
        isSyncing_.store(false, std::memory_order_release);
    }
    return result;
}

void ServiceEngine::backgroundSyncEngine(
    std::shared_ptr<SearchEngine> engine,
    std::shared_ptr<IndexPersistence> sharedPersistence,
    uint64_t lastEventId,
    std::chrono::steady_clock::time_point incrementalStart,
    std::chrono::steady_clock::time_point indexLoadDone,
    uint64_t generation)
{
    const ServiceConfig config = safeConfig();
    dispatch_group_async(backgroundGroup_, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (!this->isGenerationCurrent(generation)) return;
        auto roots = effectiveScanRoots();
        bool hasNetworkRoot = false;
        std::vector<std::string> networkRoots;
        std::vector<std::string> localRoots;
        for (const auto& root : roots) {
            if (!PathUtils::isReachableDirectory(root)) {
                if (PathUtils::isUnpluggedRemovableVolume(this->ownVolumeKinds(), root)) {
                    LOG_INFO("ServiceEngine", "Deferring unplugged external volume during sync: " << root);
                } else {
                    LOG_INFO("ServiceEngine", "Keeping cached records for unavailable scan root: " << root);
                }
                continue;
            }
            PathUtils::observeVolumeKind(this->ownVolumeKinds(), root);
            if (PathUtils::isNetworkFilesystem(root)) {
                hasNetworkRoot = true;
                networkRoots.push_back(root);
            } else {
                localRoots.push_back(root);
            }
        }

        // SMB can expose a running FSEvents stream without delivering remote
        // changes. Keep the persisted index available and reconcile each root
        // asynchronously instead of waiting for replay to time out.
        if (hasNetworkRoot) {
            LOG_INFO("ServiceEngine", "Network root detected; skipping FSEvents replay and using polling reconciliation");
            for (const auto& root : networkRoots) {
                this->rescanSubtree(root);
            }
            if (localRoots.empty()) {
                {
                    std::lock_guard<std::mutex> stateLock(this->lifecycleStateMutex_);
                    if (!this->isGenerationCurrent(generation)) return;
                    this->isSyncing_.store(false, std::memory_order_release);
                }
                if (config.realtimeMonitoring) this->startMonitoring();
                if (config.automaticMaintenanceEnabled) {
                    sharedPersistence->startAutoCompaction(300.0, this->watcher_);
                }
                if (config.contentIndexingEnabled) {
                    dispatch_group_async(this->backgroundGroup_, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
                        if (!this->isGenerationCurrent(generation)) return;
                        this->startContentIndexing();
                    });
                }
                if (this->onIndexChanged) this->onIndexChanged();
                this->applyDeferredVolumeWork();
                return;
            }
            // Reconcile NAS roots by polling, but still replay local roots
            // from the persisted event watermark below.
            roots = std::move(localRoots);
        } else {
            roots = std::move(localRoots);
        }

        if (roots.empty()) {
            LOG_INFO("ServiceEngine", "No mounted local roots to replay");
            {
                std::lock_guard<std::mutex> stateLock(this->lifecycleStateMutex_);
                if (!this->isGenerationCurrent(generation)) return;
                this->isSyncing_.store(false, std::memory_order_release);
            }
            if (config.realtimeMonitoring) this->startMonitoring();
            if (config.automaticMaintenanceEnabled) {
                sharedPersistence->startAutoCompaction(300.0, this->watcher_);
            }
            if (config.contentIndexingEnabled) {
                dispatch_group_async(this->backgroundGroup_, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
                    if (!this->isGenerationCurrent(generation)) return;
                    this->startContentIndexing();
                });
            }
            if (this->onIndexChanged) this->onIndexChanged();
            this->applyDeferredVolumeWork();
            return;
        }

        // Try FSEvents replay
        auto replayDone = std::make_shared<std::atomic<bool>>(false);
        auto journalTruncated = std::make_shared<std::atomic<bool>>(false);

        auto watcherForReplay = std::make_unique<FileSystemWatcher>("replay");
        auto* watcherPtr = watcherForReplay.get();

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);

        watcherPtr->start(
            roots,
            lastEventId,
            [this, engine, generation](std::vector<FileSystemWatcher::Event> events) {
                if (!this->mutationQueue_ || events.empty()) return;
                // The watcher queue must not block on the mutation queue: stop()
                // drains the watcher queue, and a mutation can stop a watcher.
                const uint64_t indexRevision =
                    this->indexRevision_.load(std::memory_order_acquire);
                dispatch_async(this->mutationQueue_, ^{
                    this->applyOrDeferEvents(generation, indexRevision, events, engine,
                                             /*allowWhileSyncing=*/true);
                });
            },
            [replayDone, journalTruncated, watcherPtr, sem] {
                replayDone->store(true);
                journalTruncated->store(watcherPtr->isJournalTruncated());
                dispatch_semaphore_signal(sem);
            }
        );

        long result = dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
        watcherForReplay->stop();
        // Continue the live stream after the last event this replay actually
        // delivered. A zero id means the callback stored nothing, so keep the
        // watermark replay started from. Event id 0 would replay the journal
        // from the beginning.
        const FSEventStreamEventId deliveredReplayId = watcherPtr->getLastEventId();
        const FSEventStreamEventId replayResumeId =
            deliveredReplayId != 0 ? deliveredReplayId :
            (lastEventId != 0 ? lastEventId : kFSEventStreamEventIdSinceNow);
        // Finish replay blocks that stop() only queued. loadRecords below takes
        // the same lifecycle lock those blocks use.
        if (this->mutationQueue_) {
            dispatch_sync(this->mutationQueue_, ^{});
        }
        if (!this->isGenerationCurrent(generation)) return;

        if (result == 0 && replayDone->load() && !journalTruncated->load()) {
            // Replay succeeded
            auto now = std::chrono::steady_clock::now();
            auto loadTime = std::chrono::duration<double>(indexLoadDone - incrementalStart).count();
            auto replayTime = std::chrono::duration<double>(now - indexLoadDone).count();
            auto totalTime = std::chrono::duration<double>(now - incrementalStart).count();
            LOG_INFO("ServiceEngine", "Incremental startup completed: "
                     << engine->liveRecordCount() << " records — "
                     << "index load " << loadTime << "s, "
                     << "FSEvents replay " << replayTime << "s, "
                     << "total " << totalTime << "s");

            {
                std::lock_guard<std::mutex> stateLock(this->lifecycleStateMutex_);
                if (!this->isGenerationCurrent(generation)) return;
                this->isSyncing_.store(false, std::memory_order_release);
            }
            if (config.realtimeMonitoring) {
                this->startMonitoring(replayResumeId);
            }
            if (config.automaticMaintenanceEnabled) {
                sharedPersistence->startAutoCompaction(300.0, this->watcher_);
            }

            if (config.contentIndexingEnabled) {
                dispatch_group_async(this->backgroundGroup_, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
                    if (!this->isGenerationCurrent(generation)) return;
                    this->startContentIndexing();
                });
            }
            if (this->onIndexChanged) this->onIndexChanged();
            this->applyDeferredVolumeWork();
            return;
        }

        // Replay failed: background full scan
        LOG_WARN("ServiceEngine", "FSEvents replay failed — background full scan");

        auto scanner = std::make_shared<DirectoryScanner>();
        if (!this->registerScanner(scanner, generation)) return;

        // Progress reporting
        std::weak_ptr<DirectoryScanner> scannerWeak = scanner;
        dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
            dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0));
        dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 0),
                                  200 * NSEC_PER_MSEC, 50 * NSEC_PER_MSEC);
        auto progressCb = this->onScanProgress;
        dispatch_source_set_event_handler(timer, ^{
            if (!progressCb) return;
            auto s = scannerWeak.lock();
            if (!s) return;
            const auto& stats = s->getStats();
            progressCb(stats.fileCount.load(std::memory_order_relaxed),
                       stats.dirCount.load(std::memory_order_relaxed));
        });
        dispatch_resume(timer);

        // Changes the walker has already passed are not in the snapshot.
        // Replay them from this id once the replacement is published.
        const FSEventStreamEventId scanStartEventId =
            FileSystemWatcher::getCurrentSystemEventId();
        scanner->scan(this->effectiveScanRoots(), this->scanConfig());
        dispatch_source_cancel(timer);
        dispatch_release(timer);
        this->unregisterScanner(scanner);
        if (!this->isGenerationCurrent(generation)) return;

        if (!scanner->isComplete()) {
            const auto& stats = scanner->getStats();
            LOG_ERROR("ServiceEngine", "Background scan incomplete; preserving cached index ("
                      << stats.errorCount.load(std::memory_order_relaxed) << " errors)");
            {
                std::lock_guard<std::mutex> stateLock(this->lifecycleStateMutex_);
                if (!this->isGenerationCurrent(generation)) return;
                this->isSyncing_.store(false, std::memory_order_release);
            }
            this->applyDeferredVolumeWork();
            return;
        }

        auto freshRecords = scanner->takeResults();
        // isSyncing_ is still true, so live mutations stay queued. Stop the
        // previous content store before publishing the replacement index.
        std::shared_ptr<ContentIndex> replacementContent;
        if (config.contentIndexingEnabled && this->isGenerationCurrent(generation)) {
            auto previousContent = this->safeContentIndex();
            replacementContent = std::make_shared<ContentIndex>();
            if (previousContent) {
                replacementContent->setExtensions(previousContent->getExtensions());
                replacementContent->setMaxFileSize(previousContent->getMaxFileSize());
            }
            sharedPersistence->setContentIndexPersistence(nullptr);
            sharedPersistence->setContentIndex(nullptr);
            auto previousCP = this->safeContentPersistence();
            if (previousCP) previousCP->stopAutoCompactionAndWait();
            this->setContentPersistence(nullptr);
            previousCP.reset();
            std::error_code ec;
            fs::remove(config.cachePath + "/content_index.bin", ec);
            ec.clear();
            fs::remove(config.cachePath + "/content_index.wal", ec);
            ec.clear();
            for (const auto& entry : fs::directory_iterator(config.cachePath, ec)) {
                if (ec) break;
                if (entry.path().filename().string().rfind("content_index.wal.seg.", 0) == 0) {
                    fs::remove(entry.path(), ec);
                    ec.clear();
                }
            }
        }

        // Hold the publish lock across the replacement so a rescan or replay
        // cannot write this engine between the check and loadRecords. The
        // snapshot bump also retires event batches that applyDeferredVolumeWork
        // already swapped out of the shared queue.
        const SnapshotPublishResult published = this->publishScannedRecords(
            engine, std::move(freshRecords), replacementContent, sharedPersistence,
            generation);
        if (published == SnapshotPublishResult::Superseded) return;
        if (published != SnapshotPublishResult::Replaced) {
            this->applyDeferredVolumeWork();
            return;
        }
        if (this->isGenerationCurrent(generation)) {
            this->noteIndexedScanRoots(this->effectiveScanRoots());
        }
        uint32_t finalCount = engine->liveRecordCount();

        auto scanNow = std::chrono::steady_clock::now();
        auto loadTime = std::chrono::duration<double>(indexLoadDone - incrementalStart).count();
        auto scanTime = std::chrono::duration<double>(scanNow - indexLoadDone).count();
        auto totalTime = std::chrono::duration<double>(scanNow - incrementalStart).count();
        LOG_INFO("ServiceEngine", "Background scan completed: "
                 << finalCount << " records — "
                 << "index load " << loadTime << "s, "
                 << "rescan " << scanTime << "s, "
                 << "total " << totalTime << "s");

        sharedPersistence->stopAutoCompactionAndWait();

        std::string cacheStr = config.cachePath + "/index.bin";
        std::string walStr   = config.cachePath + "/index.wal";
        std::string pagesStr = config.cachePath + "/index.pages";
        std::string ptableStr = config.cachePath + "/index.ptable";
        std::string v6Str    = config.cachePath + "/index.v6";

        auto newPersistence = std::make_shared<IndexPersistence>(
            engine, cacheStr, walStr, pagesStr, ptableStr, v6Str);
        this->setPersistence(newPersistence);
        newPersistence->attachWAL();
        newPersistence->setContentIndex(this->safeContentIndex());

        if (config.realtimeMonitoring) {
            this->startMonitoring(scanStartEventId == 0
                                  ? kFSEventStreamEventIdSinceNow
                                  : scanStartEventId);
        }
        if (config.automaticMaintenanceEnabled) {
            newPersistence->startAutoCompaction(300.0, this->watcher_);
        }

        auto meta = this->buildMetadata();
        newPersistence->flush(meta, /*force=*/true);

        if (config.contentIndexingEnabled) {
            dispatch_group_async(this->backgroundGroup_, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
                if (!this->isGenerationCurrent(generation)) return;
                this->setupContentPersistence();
                this->startContentIndexing();
            });
        }
        if (this->onIndexChanged) this->onIndexChanged();
        this->applyDeferredVolumeWork();
    });
}

// ═══════════════════════════════════════════════════════
//  Compaction
// ═══════════════════════════════════════════════════════

void ServiceEngine::compactIndex() {
    LOG_INFO("ServiceEngine", "compactIndex started");
    auto persistence = safePersistence();
    if (persistence && watcher_) {
        LOG_TIMER("ServiceEngine", "compactIndex");
        uint64_t eventId = watcher_->getLastEventId();
        persistence->setContentIndex(safeContentIndex());
        persistence->setContentIndexPersistence(safeContentPersistence());
        persistence->compact(eventId);
    }
}

std::vector<std::string> ServiceEngine::effectiveScanRoots() const {
    std::shared_lock lock(configMutex_);
    if (!config_.scanRoots.empty()) return config_.scanRoots;
    return {config_.scanRoot};
}

ScanConfig ServiceEngine::scanConfigForRoots(const std::vector<std::string>& roots) const {
    auto cfg = safeConfig();
    ScanConfig config;
    config.excludedPaths = exclusionsForRoots(roots, cfg.excludedPaths);
    config.excludedPatterns = cfg.excludedPatterns;
    config.systemAllowedPaths = systemAllowedPathsForRoots(roots);
    config.includeHidden = cfg.includeHidden;
    config.includeSystem = cfg.includeSystem;
    config.includeAppBundleContents = cfg.includeAppBundleContents;
    config.volumeKinds = volumeKinds_.get();
    return config;
}

ScanConfig ServiceEngine::scanConfig() const {
    return scanConfigForRoots(effectiveScanRoots());
}

bool ServiceEngine::isPathAllowedByConfig(const std::string& path, bool forContent) const {
    auto cfg = safeConfig();

    std::vector<std::string> roots;
    if (forContent && !cfg.contentRoots.empty()) {
        roots = cfg.contentRoots;
    } else if (!cfg.scanRoots.empty()) {
        roots = cfg.scanRoots;
    } else {
        roots = {cfg.scanRoot};
    }

    bool insideRoot = roots.empty();
    for (const auto& root : roots) {
        if (pathContainsOrEqualsReal(root, path)) {
            insideRoot = true;
            break;
        }
    }
    if (!insideRoot) return false;

    std::vector<std::string> excluded = exclusionsForRoots(roots, cfg.excludedPaths);
    if (forContent) {
        auto contentExcluded = exclusionsForRoots(roots, cfg.contentExcludedPaths);
        excluded.insert(excluded.end(), contentExcluded.begin(), contentExcluded.end());
    }
    for (const auto& ex : excluded) {
        if (ex.empty()) continue;
        if (pathContainsOrEqualsReal(ex, path)) {
            return false;
        }
    }

    size_t slash = path.rfind('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (!cfg.includeHidden && !name.empty() && name[0] == '.') return false;
    if (!cfg.includeSystem &&
        isSystemFilteredPath(path) &&
        !hasSystemAllowedPath(roots, path)) {
        return false;
    }

    for (const auto& pattern : cfg.excludedPatterns) {
        if (!pattern.empty() && fnmatch(pattern.c_str(), name.c_str(), FNM_CASEFOLD) == 0) {
            return false;
        }
    }
    return true;
}

std::string ServiceEngine::configSignature() const {
    auto cfg = safeConfig();
    auto roots = !cfg.scanRoots.empty() ? cfg.scanRoots : std::vector<std::string>{cfg.scanRoot};

    auto appendList = [](std::ostringstream& out, const std::vector<std::string>& values) {
        out << values.size() << ":";
        for (const auto& value : values) {
            out << value.size() << "=" << value << ";";
        }
    };

    std::ostringstream out;
    out << "v3|roots=";
    appendList(out, roots);
    out << "|excludedPaths=";
    appendList(out, cfg.excludedPaths);
    out << "|excludedPatterns=";
    appendList(out, cfg.excludedPatterns);
    out << "|hidden=" << (cfg.includeHidden ? 1 : 0);
    out << "|system=" << (cfg.includeSystem ? 1 : 0);
    out << "|bundles=" << (cfg.includeAppBundleContents ? 1 : 0);
    out << "|pinyin=" << (cfg.enablePinyinInitials ? 1 : 0);
    out << "|pathTrigram=" << (cfg.enablePathTrigramIndex ? 1 : 0);
    return out.str();
}

// ═══════════════════════════════════════════════════════
//  HTTP Server
// ═══════════════════════════════════════════════════════

void ServiceEngine::startHttpServer(uint16_t port) {
    std::lock_guard<std::mutex> lock(httpMutex_);
    if (!httpServer_) {
        httpServer_ = std::make_shared<HttpServer>();
    }
    httpServer_->setAdminCallbacks(adminCallbacks_);
    auto curConfig = safeConfig();
    httpServer_->setServerMetadata(
        {curConfig.processType, 1, curConfig.appVersion});

    std::string token = curConfig.httpAuthenticationEnabled
        ? HttpToken::ensureTokenFile() : std::string();
    httpServer_->setAuthenticationRequired(curConfig.httpAuthenticationEnabled);
    httpServer_->setAuthToken(token);
    if (curConfig.httpAuthenticationEnabled && token.empty())
        LOG_ERROR("ServiceEngine", "HTTP token unavailable — protected API endpoints will return 503");

    bool ok = httpServer_->start(port,
        [this]() -> std::shared_ptr<SearchEngine> { return this->safeEngine(); },
        [this]() -> std::shared_ptr<ContentIndex> { return this->safeContentIndex(); },
        [this](const std::string& path) { return this->isContentPathAllowed(path); });
    if (!ok) {
        LOG_ERROR("ServiceEngine", "HTTP server failed to start on port " << port);
        return;
    }
}

void ServiceEngine::refreshHttpAuthentication() {
    std::lock_guard<std::mutex> lock(httpMutex_);
    if (!httpServer_) return;
    auto config = safeConfig();
    std::string token = config.httpAuthenticationEnabled
        ? HttpToken::ensureTokenFile() : std::string();
    httpServer_->setAuthenticationRequired(config.httpAuthenticationEnabled);
    httpServer_->setAuthToken(token);
}

void ServiceEngine::stopHttpServer() {
    std::lock_guard<std::mutex> lock(httpMutex_);
    if (httpServer_) {
        httpServer_->stop();
    }
}

// ═══════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════

void ServiceEngine::shutdown() {
    bool expected = false;
    if (!shuttingDown_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return; // Already shutting down or shut down
    }

    {
        std::lock_guard<std::mutex> stateLock(lifecycleStateMutex_);
        lifecycleGeneration_.fetch_add(1, std::memory_order_acq_rel);
    }
    rebuilding_.store(false, std::memory_order_release);
    stopHttpServer();
    LOG_INFO("ServiceEngine", "shutdown started");

    cancelContentIndexing_.store(true, std::memory_order_relaxed);
    contentIndexGeneration_.fetch_add(1, std::memory_order_acq_rel);
    cancelActiveScanners();

    if (lifecycleQueue_) dispatch_sync(lifecycleQueue_, ^{});
    if (backgroundGroup_) {
        dispatch_group_wait(backgroundGroup_, DISPATCH_TIME_FOREVER);
    }
    stopMonitoring();
    if (mutationQueue_) dispatch_sync(mutationQueue_, ^{});
    if (contentIndexingGroup_) {
        dispatch_group_wait(contentIndexingGroup_, DISPATCH_TIME_FOREVER);
    }
    isScanning_.store(false, std::memory_order_relaxed);
    isSyncing_.store(false, std::memory_order_relaxed);
    isContentIndexing_.store(false, std::memory_order_relaxed);

    uint64_t lastEventId = watcher_ ? watcher_->getLastEventId() : 0;

    // Final compaction
    auto persistence = safePersistence();
    if (persistence) {
        persistence->setContentIndex(safeContentIndex());
        persistence->setContentIndexPersistence(safeContentPersistence());
        persistence->compact(lastEventId, /*force=*/true);
    }
    auto cp = safeContentPersistence();
    if (cp) {
        cp->compact(/*force=*/true);
    }

    // Release GCD objects
    if (mutationQueue_) {
        dispatch_release(mutationQueue_);
        mutationQueue_ = nullptr;
    }
    if (backgroundGroup_) {
        dispatch_release(backgroundGroup_);
        backgroundGroup_ = nullptr;
    }
    if (contentIndexingGroup_) {
        dispatch_release(contentIndexingGroup_);
        contentIndexingGroup_ = nullptr;
    }
    if (lifecycleQueue_) {
        dispatch_release(lifecycleQueue_);
        lifecycleQueue_ = nullptr;
    }

    LOG_INFO("ServiceEngine", "shutdown completed");
}

// ═══════════════════════════════════════════════════════
//  Static helpers
// ═══════════════════════════════════════════════════════

bool ServiceEngine::isInsideAppBundle(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find(".app/", pos)) != std::string::npos) {
        if (pos >= 1 && path[pos - 1] != '/') {
            return true;
        }
        pos += 5;
    }
    return false;
}

bool ServiceEngine::pathEndsWithApp(const std::string& path) {
    return path.size() > 4 &&
           path[path.size()-4] == '.' && path[path.size()-3] == 'a' &&
           path[path.size()-2] == 'p' && path[path.size()-1] == 'p';
}

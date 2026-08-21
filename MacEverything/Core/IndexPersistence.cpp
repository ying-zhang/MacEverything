#include "IndexPersistence.h"
#include "Logger.h"
#include "PathUtils.h"
#include <cerrno>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <algorithm>

namespace {
std::vector<std::string> walSegments(const std::string& base) {
    namespace fs = std::filesystem;
    std::vector<std::pair<uint64_t, std::string>> found;
    std::error_code ec;
    if (fs::exists(base, ec)) found.push_back({0, base});
    const std::string prefix = base + ".seg.";
    fs::path p(base);
    auto dir = p.parent_path().empty() ? fs::path(".") : p.parent_path();
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        auto name = e.path().string();
        if (name.rfind(prefix, 0) != 0) continue;
        try { found.push_back({std::stoull(name.substr(prefix.size())), name}); }
        catch (...) {}
    }
    std::sort(found.begin(), found.end());
    std::vector<std::string> paths;
    for (auto& pth : found) paths.push_back(std::move(pth.second));
    return paths;
}

void removeWalSegmentsExcept(const std::string& base, const std::string& keep = {}) {
    bool removed = false;
    for (const auto& segment : walSegments(base)) {
        if (segment != keep && std::remove(segment.c_str()) == 0) removed = true;
    }
    if (removed) PathUtils::syncParentDirectory(base);
}

std::string nextWalSegment(const std::string& base) {
    uint64_t n = 1;
    const std::string prefix = base + ".seg.";
    for (const auto& path : walSegments(base)) {
        if (path.rfind(prefix, 0) != 0) continue;
        try { n = std::max(n, std::stoull(path.substr(prefix.size())) + 1); }
        catch (...) {}
    }
    return prefix + std::to_string(n);
}
}

IndexPersistence::IndexPersistence(std::shared_ptr<SearchEngine> engine,
                                   const std::string& basePath,
                                   const std::string& walPath,
                                   const std::string& pagesPath,
                                   const std::string& ptablePath,
                                   const std::string& v6Path)
    : engine_(std::move(engine))
    , pagedWriter_(std::make_unique<PagedIndexWriter>(pagesPath, ptablePath))
    , flatWriter_(std::make_unique<FlatIndexWriter>(v6Path))
    , basePath_(basePath)
    , v6Path_(v6Path)
    , walPath_(walPath)
{}

IndexPersistence::~IndexPersistence() {
    alive_->store(false, std::memory_order_release);
    stopTimer();
    if (engine_) engine_->detachWAL();
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        if (wal_) wal_->close();
    }
}

void IndexPersistence::setContentIndex(std::shared_ptr<ContentIndex> ci) {
    std::lock_guard<std::mutex> lock(compactionMutex_);
    contentIndex_ = std::move(ci);
}

void IndexPersistence::setContentIndexPersistence(
        std::shared_ptr<ContentIndexPersistence> cp) {
    std::lock_guard<std::mutex> lock(compactionMutex_);
    contentPersistence_ = std::move(cp);
}

uint64_t IndexPersistence::load() {
    return load("");
}

uint64_t IndexPersistence::load(const std::string& expectedConfigSignature) {
    uint64_t lastEventId = 0;

    // 1. Try v6 flat SoA format first (fast path)
    bool loaded = false;
    if (flatWriter_->exists()) {
        IndexMetadata meta;
        loaded = flatWriter_->load(*engine_, &meta);
        if (loaded) {
            auto it = meta.extra.find("config_signature");
            std::string actualSignature = it == meta.extra.end() ? "" : it->second;
            if (!expectedConfigSignature.empty() && actualSignature != expectedConfigSignature) {
                LOG_WARN("IndexPersistence", "Ignoring v6 index because config changed");
                engine_->loadRecords({});
                std::remove(v6Path_.c_str());
                removeWalSegmentsExcept(walPath_);
                loaded = false;
            } else {
                lastEventId = meta.lastEventId;
                LOG_INFO("IndexPersistence", "Loaded v6 flat index, lastEventId=" << lastEventId
                          << ", liveRecords=" << engine_->liveRecordCount());
            }
        } else {
            LOG_ERROR("IndexPersistence", "v6 flat index corrupt, trying paged format");
        }
    }

    // 2. Fallback to paged format (v5)
    if (!loaded && pagedWriter_->exists()) {
        IndexMetadata meta;
        loaded = pagedWriter_->load(*engine_, &meta);
        if (loaded) {
            if (!expectedConfigSignature.empty()) {
                LOG_WARN("IndexPersistence", "Ignoring paged index because config signature is unavailable");
                engine_->loadRecords({});
                removeWalSegmentsExcept(walPath_);
                loaded = false;
            } else {
                lastEventId = meta.lastEventId;
                LOG_INFO("IndexPersistence", "Loaded paged index, lastEventId=" << lastEventId
                          << ", liveRecords=" << engine_->liveRecordCount());
                // Auto-migrate to v6 flat format
                IndexMetadata migrateMeta;
                migrateMeta.lastEventId = lastEventId;
                if (flatWriter_->fullRewrite(*engine_, migrateMeta)) {
                    LOG_INFO("IndexPersistence", "Migrated paged index to v6 flat format");
                }
            }
        } else {
            LOG_ERROR("IndexPersistence", "Paged index corrupt, trying legacy format");
        }
    }

    // 3. Fallback to legacy v3 format
    if (!loaded) {
        loaded = engine_->loadFromFile(basePath_, &lastEventId);
        if (loaded) {
            if (!expectedConfigSignature.empty()) {
                LOG_WARN("IndexPersistence", "Ignoring legacy index because config signature is unavailable");
                engine_->loadRecords({});
                removeWalSegmentsExcept(walPath_);
                loaded = false;
            } else {
                LOG_INFO("IndexPersistence", "Loaded legacy index, lastEventId=" << lastEventId
                          << ", liveRecords=" << engine_->liveRecordCount());
                // Auto-migrate to v6 flat format
                IndexMetadata migrateMeta;
                migrateMeta.lastEventId = lastEventId;
                if (flatWriter_->fullRewrite(*engine_, migrateMeta)) {
                    LOG_INFO("IndexPersistence", "Migrated legacy index to v6 flat format");
                }
            }
        } else {
            LOG_INFO("IndexPersistence", "No base index found at " << basePath_);
        }
    }

    // 3. In-place WAL replay using pathIndex_ for O(1) lookups
    for (const auto& segment : walSegments(walPath_)) {
        auto entries = IndexWAL::readAll(segment);
        if (!entries.empty()) {
            LOG_INFO("IndexPersistence", "Replaying " << entries.size() << " WAL entries from " << segment);
            engine_->replayWALEntries(std::move(entries));
        }
    }

    return lastEventId;
}

void IndexPersistence::attachWAL() {
    auto newWal = std::make_shared<IndexWAL>();
    auto segments = walSegments(walPath_);
    std::string activePath = segments.empty() ? walPath_ : segments.back();
    bool opened = newWal->open(activePath);
    if (!opened) {
        // Preserve an incompatible/corrupt segment for diagnosis and continue
        // durability on a fresh segment. A successful base rewrite later removes it.
        activePath = nextWalSegment(walPath_);
        newWal = std::make_shared<IndexWAL>();
        opened = newWal->open(activePath);
    }
    if (opened) {
        {
            std::lock_guard<std::mutex> lock(walMutex_);
            wal_ = newWal;
        }
        engine_->attachWAL(newWal);
        LOG_INFO("IndexPersistence", "WAL attached at " << activePath);
    } else {
        LOG_ERROR("IndexPersistence", "Failed to open WAL at " << walPath_);
    }
}

void IndexPersistence::flush(uint64_t lastEventId, bool force) {
    IndexMetadata meta;
    meta.lastEventId = lastEventId;
    flush(meta, force);
}

void IndexPersistence::flush(const IndexMetadata& metadata, bool force) {
    std::lock_guard<std::mutex> compactionLock(compactionMutex_);

    // Skip logic:
    //   - No WAL → skip.
    //   - force=true: an empty WAL can skip only when a v6 base already exists.
    //     A first full scan populates the engine before the WAL is attached, so
    //     it still needs an initial base rewrite.
    //   - Otherwise: skip if not dirty or below threshold.
    static constexpr size_t kWALHeaderSize = 2 * sizeof(uint32_t); // magic + version
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        const bool pendingSegments = walSegments(walPath_).size() > 1;
        const bool forceRewrite = engine_->needsFullRewrite();
        if (!wal_) {
            LOG_INFO("IndexPersistence", "Skipping flush — no WAL");
            return;
        }
        if (force) {
            if (wal_->currentSize() <= kWALHeaderSize && !pendingSegments &&
                flatWriter_->exists() && !forceRewrite) {
                LOG_INFO("IndexPersistence", "Skipping flush — WAL is empty");
                return;
            }
        } else {
            if (!wal_->isDirty() && !pendingSegments && !forceRewrite) {
                LOG_INFO("IndexPersistence", "Skipping flush — no mutations since last flush");
                return;
            }
            if (wal_->entryCount() < kCompactThreshold && !pendingSegments && !forceRewrite) {
                LOG_INFO("IndexPersistence", "Skipping flush — only "
                          << wal_->entryCount() << " entries (threshold=" << kCompactThreshold << ")");
                return;
            }
        }
    }

    // After a base write failure, retry against the existing active WAL instead
    // of opening another segment on every timer tick. The retained WAL may
    // replay operations already present in the new base, which is safe because
    // WAL mutations are path-idempotent.
    if (previousBaseWriteFailed_) {
        std::shared_ptr<IndexWAL> activeWal;
        {
            std::lock_guard<std::mutex> lock(walMutex_);
            activeWal = wal_;
        }
        const uint64_t rewriteGeneration = engine_->fullRewriteGeneration();
        if (!activeWal || !flatWriter_->fullRewrite(*engine_, metadata)) {
            LOG_ERROR("IndexPersistence", "Base rewrite retry failed — retaining existing WAL segments");
            return;
        }
        engine_->acknowledgeFullRewrite(rewriteGeneration);
        removeWalSegmentsExcept(walPath_, activeWal->path());
        previousBaseWriteFailed_ = false;
        LOG_INFO("IndexPersistence", "Base rewrite retry succeeded without rotating WAL");
        return;
    }

    // Check if full compaction is needed (tombstone ratio)
    uint32_t totalCount = engine_->recordCount();
    uint32_t liveCount = engine_->liveRecordCount();
    double tombstoneRatio = totalCount > 0
        ? static_cast<double>(totalCount - liveCount) / totalCount
        : 0.0;

    if (tombstoneRatio > kTombstoneCompactRatio) {
        LOG_INFO("IndexPersistence", "Tombstone ratio " << (tombstoneRatio * 100)
                  << "% > " << (kTombstoneCompactRatio * 100) << "% — triggering full compaction");
        fullCompactLocked(metadata);
        return;
    }

    // 1. Open a fresh WAL before detaching the old one
    auto newWal = std::make_shared<IndexWAL>();
    std::string newWalPath = nextWalSegment(walPath_);
    if (!newWal->open(newWalPath)) {
        LOG_ERROR("IndexPersistence", "Failed to open new WAL for flush");
        return;
    }

    // 2. Atomically swap WAL
    std::shared_ptr<IndexWAL> oldWal;
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        oldWal = wal_;
        wal_ = newWal;
    }
    engine_->attachWAL(newWal);

    // 3. Full rewrite v6 flat format
    const uint64_t rewriteGeneration = engine_->fullRewriteGeneration();
    bool writeOk = flatWriter_->fullRewrite(*engine_, metadata);

    if (writeOk) {
        previousBaseWriteFailed_ = false;
        engine_->acknowledgeFullRewrite(rewriteGeneration);
        LOG_INFO("IndexPersistence", "Flushed v6 flat index, lastEventId=" << metadata.lastEventId
                  << ", liveRecords=" << engine_->liveRecordCount());
    } else {
        LOG_ERROR("IndexPersistence", "Failed to flush index — retaining WAL segments");
        previousBaseWriteFailed_ = true;
        return;
    }

    if (oldWal) oldWal->close();
    removeWalSegmentsExcept(walPath_, newWalPath);
}

void IndexPersistence::fullCompact(const IndexMetadata& metadata) {
    std::lock_guard<std::mutex> compactionLock(compactionMutex_);
    fullCompactLocked(metadata);
}

void IndexPersistence::fullCompactLocked(const IndexMetadata& metadata) {
    // 1. Open a fresh WAL
    auto newWal = std::make_shared<IndexWAL>();
    std::string newWalPath = nextWalSegment(walPath_);
    if (!newWal->open(newWalPath)) {
        LOG_ERROR("IndexPersistence", "Failed to open new WAL for full compaction");
        return;
    }

    // 2. Swap WAL
    std::shared_ptr<IndexWAL> oldWal;
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        oldWal = wal_;
        wal_ = newWal;
    }
    engine_->attachWAL(newWal);

    // 3. Compact in-memory records (remove tombstones). Mark the entire
    // cross-index transition so readers cannot observe new engine indices with
    // the old content-index mapping.
    if (contentIndex_) contentIndex_->beginFileIndexRemap();
    uint32_t beforeTotal = engine_->recordCount();
    uint32_t beforeLive = engine_->liveRecordCount();
    auto remap = engine_->compactRecords();
    uint32_t reclaimed = beforeTotal - beforeLive;
    if (reclaimed > 0) {
        LOG_INFO("IndexPersistence", "Reclaimed " << reclaimed << " tombstones ("
                  << beforeTotal << " -> " << beforeLive << " records)");
    }
    if (!remap.empty() && contentIndex_) {
        contentIndex_->remapFileIndices(remap);
        // Persist the remapped in-memory cache opportunistically.  Its on-disk
        // records are path-keyed, so a failure here cannot create numeric-index
        // aliasing after restart.
        if (contentPersistence_) {
            contentPersistence_->compact(true);
        }
    }
    if (contentIndex_) contentIndex_->endFileIndexRemap();

    // 4. Full rewrite v6 flat format
    const uint64_t rewriteGeneration = engine_->fullRewriteGeneration();
    if (flatWriter_->fullRewrite(*engine_, metadata)) {
        previousBaseWriteFailed_ = false;
        engine_->acknowledgeFullRewrite(rewriteGeneration);
        LOG_INFO("IndexPersistence", "Full compaction done, lastEventId=" << metadata.lastEventId
                  << ", liveRecords=" << engine_->liveRecordCount());
    } else {
        LOG_ERROR("IndexPersistence", "Failed to write full compaction — retaining WAL segments");
        previousBaseWriteFailed_ = true;
        return;
    }

    if (oldWal) oldWal->close();
    removeWalSegmentsExcept(walPath_, newWalPath);
}

double IndexPersistence::computeAdaptiveInterval() const {
    auto dirtyPages = engine_->getDirtyPageNumbers();
    uint32_t dirtyCount = static_cast<uint32_t>(dirtyPages.size());
    uint32_t liveCount = engine_->liveRecordCount();
    uint32_t totalPages = (liveCount + SearchEngine::kRecordsPerPage - 1) / SearchEngine::kRecordsPerPage;
    if (totalPages == 0) totalPages = 1;

    double dirtyRatio = static_cast<double>(dirtyCount) / totalPages;

    double interval;
    if (dirtyRatio > 0.3) {
        interval = kMinIntervalSec;
    } else if (dirtyRatio > 0.1) {
        interval = kBaseIntervalSec * 0.5;
    } else if (dirtyRatio > 0.01) {
        interval = kBaseIntervalSec;
    } else {
        interval = kMaxIntervalSec;
    }

    // WAL size override
    size_t walSize = 0;
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        if (wal_) walSize = wal_->currentSize();
    }
    if (walSize > kWALSizeFlushThreshold) {
        interval = std::min(interval, kMinIntervalSec);
    }

    return interval;
}

void IndexPersistence::stopTimer() {
    if (timer_) {
        dispatch_source_cancel(timer_);
        dispatch_release(timer_);
        timer_ = nullptr;
    }
    if (timerQueue_) {
        dispatch_sync(timerQueue_, ^{});
        dispatch_release(timerQueue_);
        timerQueue_ = nullptr;
    }
    currentIntervalSec_ = 0;
}

void IndexPersistence::rescheduleTimer(double intervalSec) {
    if (!timer_) return;
    if (std::abs(intervalSec - currentIntervalSec_) < 1.0) return;

    uint64_t intervalNs = static_cast<uint64_t>(intervalSec * NSEC_PER_SEC);
    dispatch_source_set_timer(timer_,
                              dispatch_time(DISPATCH_TIME_NOW, intervalNs),
                              intervalNs,
                              30 * NSEC_PER_SEC);
    currentIntervalSec_ = intervalSec;
}

void IndexPersistence::startAutoCompaction(double intervalSec, std::shared_ptr<FileSystemWatcher> watcher) {
    stopTimer();
    alive_ = std::make_shared<std::atomic<bool>>(true);

    currentIntervalSec_ = intervalSec;
    timerQueue_ = dispatch_queue_create("com.maceverything.index.compaction", DISPATCH_QUEUE_SERIAL);
    timer_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, timerQueue_);

    uint64_t intervalNs = static_cast<uint64_t>(intervalSec * NSEC_PER_SEC);
    dispatch_source_set_timer(timer_,
                              dispatch_time(DISPATCH_TIME_NOW, intervalNs),
                              intervalNs,
                              30 * NSEC_PER_SEC);

    auto* self = this;
    auto alive = alive_;
    dispatch_source_set_event_handler(timer_, ^{
        if (!alive->load(std::memory_order_acquire)) return;
        uint64_t eventId = watcher ? watcher->getLastEventId() : 0;
        self->flush(eventId);
        double newInterval = self->computeAdaptiveInterval();
        self->rescheduleTimer(newInterval);
    });

    dispatch_resume(timer_);
    LOG_INFO("IndexPersistence", "Auto-compaction started (initial interval " << intervalSec << "s)");
}

void IndexPersistence::stopAutoCompactionAndWait() {
    stopTimer();
}

#include "SearchEngine.h"
#include "StringUtils.h"
#include "Logger.h"
#include <algorithm>
#include <thread>

// ---------------------------------------------------------------------------
// v6 Flat SoA: loadRecordsV6, completePhase2, snapshotForV6
// ---------------------------------------------------------------------------

void SearchEngine::loadRecordsV6(StringPool&& origNamePool,
                                  StringPool&& namePool,
                                  std::vector<uint32_t>&& pathIndices,
                                  StringPool&& pathPool,
                                  std::vector<uint8_t>&& types,
                                  std::vector<uint64_t>&& sizes,
                                  std::vector<int64_t>&& modTimes,
                                  std::vector<uint64_t>&& inodes,
                                  std::vector<int32_t>&& devIds) {
    std::unique_lock lock(mutex_);

    uint32_t n = origNamePool.entryCount();

    // Install SoA columns directly (zero-copy from v6 file)
    origNamePool_ = std::move(origNamePool);
    namePool_ = std::move(namePool);
    pinyinInitialsPool_.clear();
    pinyinInitialsTrigramIndex_.clear();
    pathIndices_ = std::move(pathIndices);
    pathPool_ = std::move(pathPool);
    types_ = std::move(types);
    sizes_ = std::move(sizes);
    modTimes_ = std::move(modTimes);
    inodes_ = std::move(inodes);
    devIds_ = std::move(devIds);

    // Rebuild pathLookup_ and lowerPathLookup_ from pathPool_ entries
    pathLookup_.clear();
    lowerPathLookup_.clear();
    pathLookup_.reserve(pathPool_.entryCount());
    lowerPathLookup_.reserve(pathPool_.entryCount());
    for (uint32_t i = 0; i < pathPool_.entryCount(); i++) {
        if (pathPool_.isLive(i)) {
            pathLookup_[pathPool_.str(i)] = i;
            lowerPathLookup_[lowerPathStr(pathPool_, i)] = i;
        }
    }

    // Build pathIndex_ (lowercase full path -> record index)
    // Parallelize full-path construction
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads < 1) numThreads = 1;
    if (numThreads > 32) numThreads = 32;
    size_t chunkSize = (n + numThreads - 1) / numThreads;
    std::vector<std::string> loweredPaths(n);
    {
        std::vector<std::thread> pathThreads;
        pathThreads.reserve(numThreads);
        for (unsigned t = 0; t < numThreads; t++) {
            size_t start = t * chunkSize;
            size_t end = std::min(start + chunkSize, static_cast<size_t>(n));
            if (start >= end) break;
            pathThreads.emplace_back([this, &loweredPaths, start, end] {
                for (size_t i = start; i < end; i++) {
                    uint32_t idx = static_cast<uint32_t>(i);
                    loweredPaths[i] = makeFullPath(lowerPathStr(pathPool_, pathIndices_[idx]),
                                                   namePool_.view(idx));
                }
            });
        }
        for (auto& th : pathThreads) th.join();
    }

    // Insert into pathIndex_ and merge tombstone dedup (last-wins overwrites)
    pathIndex_.clear();
    pathIndexCollisions_.clear();
    uint32_t actualLive = 0;
    if (options_.enablePathIndex) {
        pathIndex_.reserve(n);
        for (uint32_t i = 0; i < n; i++) {
            if (types_[i] == 0) continue;
            setPathIndexUnlocked(loweredPaths[i], i);
        }

        // Tombstone orphaned duplicates: records not in pathIndex_ as winners
        std::vector<bool> isWinner(n, false);
        for (const auto& [_, idx] : pathIndex_) {
            isWinner[idx] = true;
        }
        for (const auto& [_, list] : pathIndexCollisions_) {
            for (uint32_t idx : list) isWinner[idx] = true;
        }
        for (uint32_t i = 0; i < n; i++) {
            if (types_[i] == 0) continue;
            if (isWinner[i]) {
                actualLive++;
            } else {
                tombstoneAt(i);
            }
        }
    } else {
        for (uint32_t i = 0; i < n; i++) {
            if (types_[i] != 0) actualLive++;
        }
    }

    liveCount_.store(actualLive, std::memory_order_relaxed);

    // Phase 2: defer trigram index building to background
    phase2Pending_.store(true, std::memory_order_release);
    phase2StartRecordCount_ = static_cast<uint32_t>(types_.size());

    // Initialize dirty page bitmap
    uint32_t pageCount = (n + kRecordsPerPage - 1) / kRecordsPerPage;
    dirtyPages_.assign(pageCount, false);
    fullRewriteNeeded_.store(false, std::memory_order_relaxed);

    LOG_INFO("SearchEngine", "loadRecordsV6: loaded " << n << " records (" << actualLive
             << " live), Phase 2 pending");
}

void SearchEngine::completePhase2() {
    if (!phase2Pending_.load(std::memory_order_acquire)) return;

    LOG_INFO("SearchEngine", "Phase 2: building trigram indices in background...");

    // Snapshot data needed for building indices (under shared lock)
    // Only snapshot types_ (lightweight ~5MB for 5.5M records) instead of
    // the full SoA data (~440MB if using FileRecord structs).
    std::vector<uint8_t> snapTypes;
    std::vector<int64_t> snapModTimes;
    StringPool snapNamePool;
    StringPool snapOrigNamePool;
    StringPool snapPathPool;
    std::vector<uint32_t> snapPathIndices;
    uint32_t snapPathPoolSize;
    uint32_t snapSize;
    {
        std::shared_lock lock(mutex_);
        snapTypes = types_;
        snapModTimes = modTimes_;
        snapNamePool = namePool_;
        if (options_.enablePinyinInitials) snapOrigNamePool = origNamePool_;
        snapPathPool = pathPool_;
        snapPathIndices = pathIndices_;
        snapPathPoolSize = pathPool_.entryCount();
        snapSize = static_cast<uint32_t>(types_.size());
    }

    // Build all indices without holding any lock (~3s)
    auto trigramIndex = buildTrigramIndexFromData(snapTypes, snapNamePool);
    StringPool pinyinInitialsPool;
    std::unordered_map<Trigram, std::vector<uint32_t>> pinyinInitialsTrigramIndex;
    if (options_.enablePinyinInitials) {
        pinyinInitialsPool = buildPinyinInitialsPoolFromData(snapOrigNamePool);
        pinyinInitialsTrigramIndex = buildTrigramIndexFromData(snapTypes, pinyinInitialsPool);
    }
    std::unordered_map<Trigram, std::vector<uint32_t>> pathTrigramIndex;
    std::vector<std::vector<uint32_t>> pathIdxToRecords;
    if (options_.enablePathTrigramIndex) {
        pathTrigramIndex = buildPathTrigramIndexFromData(snapPathPool);
        pathIdxToRecords = buildPathIdxToRecordsFromData(snapTypes, snapPathIndices, snapPathPoolSize);
    }
    auto recentCache = buildRecentCacheFromData(snapTypes, snapModTimes, kRecentCacheSize);
    auto extensionIndex = buildExtensionIndexFromData(snapTypes, snapNamePool);
    auto cjkBigramIndex = buildCJKBigramIndexFromData(snapTypes, snapNamePool);

    LOG_INFO("SearchEngine", "Phase 2: indices built, swapping under lock...");

    // Swap under unique lock and replay mutations that occurred during build
    {
        std::unique_lock lock(mutex_);

        nameTrigramIndex_ = std::move(trigramIndex);
        pinyinInitialsPool_ = std::move(pinyinInitialsPool);
        pinyinInitialsTrigramIndex_ = std::move(pinyinInitialsTrigramIndex);
        pathTrigramIndex_ = std::move(pathTrigramIndex);
        pathIdxToRecords_ = std::move(pathIdxToRecords);
        recentCache_ = std::move(recentCache);
        extensionIndex_ = std::move(extensionIndex);
        cjkBigramIndex_ = std::move(cjkBigramIndex);

        // Replay records added during Phase 2 build
        uint32_t currentSize = static_cast<uint32_t>(types_.size());
        uint32_t replayCount = 0;
        for (uint32_t i = snapSize; i < currentSize; i++) {
            if (types_[i] == 0) continue;
            if (options_.enablePinyinInitials) {
                while (pinyinInitialsPool_.entryCount() < i) {
                    pinyinInitialsPool_.append("");
                }
                pinyinInitialsPool_.append(me::mandarinInitialsKey(origNamePool_.str(i)));
            }
            // Add trigrams for this record
            addTrigramsForRecord(i, namePool_.data(i), namePool_.length(i));
            addPinyinInitialsForRecord(i);
            addPathTrigramsForRecord(i);
            addExtensionForRecord(i);
            addCJKBigramsForRecord(i, namePool_.data(i), namePool_.length(i));
            addToRecentCache(i, static_cast<time_t>(modTimes_[i]));
            replayCount++;
        }

        // Replay tombstones: records that were live in snapshot but deleted during build
        for (uint32_t i = 0; i < snapSize; i++) {
            if (i >= types_.size()) break;
            if (snapTypes[i] != 0 && types_[i] == 0) {
                // Was live in snapshot, now tombstoned — trigram was built, need to remove
                // The trigram entry points at index i which is now tombstoned.
                // Query-time type==0 check will filter it, but we can clean up explicitly.
                removeTrigramsForRecord(i);  // Safe: namePool_[i] is tombstoned, this is a no-op
                removePinyinInitialsForRecord(i);
            }
        }

        phase2Pending_.store(false, std::memory_order_release);

        LOG_INFO("SearchEngine", "Phase 2 complete: replayed " << replayCount
                 << " mutations, trigram indices active");
    }
}

SearchEngine::V6Snapshot SearchEngine::snapshotForV6() const {
    std::shared_lock lock(mutex_);
    V6Snapshot snap;
    snap.origNamePool = origNamePool_;
    snap.namePool = namePool_;
    snap.pathIndices = pathIndices_;
    snap.pathPool = pathPool_;
    snap.types = types_;
    snap.sizes = sizes_;
    snap.modTimes = modTimes_;
    snap.inodes = inodes_;
    snap.devIds = devIds_;
    snap.liveCount = liveCount_.load(std::memory_order_relaxed);
    return snap;
}

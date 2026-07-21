#include "ContentIndexPersistence.h"
#include "Logger.h"
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <algorithm>

namespace {
std::vector<std::string> contentWalSegments(const std::string& base) {
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
    for (auto& item : found) paths.push_back(std::move(item.second));
    return paths;
}

std::string nextContentWalSegment(const std::string& base) {
    uint64_t n = 1;
    const std::string prefix = base + ".seg.";
    for (const auto& path : contentWalSegments(base)) {
        if (path.rfind(prefix, 0) != 0) continue;
        try { n = std::max(n, std::stoull(path.substr(prefix.size())) + 1); }
        catch (...) {}
    }
    return prefix + std::to_string(n);
}
}

// ============================================================
// ContentIndexWAL
// ============================================================

ContentIndexWAL::~ContentIndexWAL() {
    close();
}

bool ContentIndexWAL::open(const std::string& walPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) return false;

    path_ = walPath;
    file_ = fopen(walPath.c_str(), "a+b");
    if (!file_) return false;

    // H-3: Write magic+version header if this is a new (empty) file
    if (fseek(file_, 0, SEEK_END) != 0) {
        fclose(file_);
        file_ = nullptr;
        return false;
    }
    long pos = ftell(file_);
    if (pos < 0) {
        fclose(file_);
        file_ = nullptr;
        return false;
    }
    if (pos == 0) {
        uint32_t magic = kMagic;
        uint32_t version = kVersion;
        if (fwrite(&magic, sizeof(uint32_t), 1, file_) != 1 ||
            fwrite(&version, sizeof(uint32_t), 1, file_) != 1) {
            fclose(file_);
            file_ = nullptr;
            return false;
        }
        fflush(file_);
        currentSize_ = 2 * sizeof(uint32_t);
    } else {
        // Numeric-index WALs from the previous format cannot be replayed
        // safely.  Start a fresh derived-cache WAL instead of appending to it.
        uint32_t magic = 0, version = 0;
        fseek(file_, 0, SEEK_SET);
        bool compatible = fread(&magic, sizeof(uint32_t), 1, file_) == 1 &&
                          fread(&version, sizeof(uint32_t), 1, file_) == 1 &&
                          magic == kMagic && version == kVersion;
        if (!compatible) {
            fclose(file_);
            file_ = fopen(walPath.c_str(), "wb");
            if (!file_) return false;
            magic = kMagic;
            version = kVersion;
            if (fwrite(&magic, sizeof(uint32_t), 1, file_) != 1 ||
                fwrite(&version, sizeof(uint32_t), 1, file_) != 1) {
                fclose(file_);
                file_ = nullptr;
                return false;
            }
            fflush(file_);
            currentSize_ = 2 * sizeof(uint32_t);
        } else {
            currentSize_ = static_cast<size_t>(pos);
            fseek(file_, 0, SEEK_END);
        }
    }

    entryCount_ = 0;
    dirty_.store(false, std::memory_order_relaxed);
    return true;
}

bool ContentIndexWAL::appendAdd(uint32_t fileIndex, const std::string& fullPath,
                                 uint64_t contentHash, const std::vector<Trigram>& trigrams,
                                 time_t lastModTime) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) return false;

    // H-6: Check WAL file size limit (in-memory tracking)
    if (currentSize_ >= kMaxWALSize) return false;

    // Build entry into buffer for CRC32
    std::string buf;
    uint8_t op = Entry::Add;
    buf.append(reinterpret_cast<const char*>(&op), 1);
    buf.append(reinterpret_cast<const char*>(&fileIndex), sizeof(uint32_t));
    uint32_t pathLen = static_cast<uint32_t>(fullPath.size());
    buf.append(reinterpret_cast<const char*>(&pathLen), sizeof(uint32_t));
    buf.append(fullPath.data(), fullPath.size());
    buf.append(reinterpret_cast<const char*>(&contentHash), sizeof(uint64_t));

    uint32_t triCount = static_cast<uint32_t>(trigrams.size());
    buf.append(reinterpret_cast<const char*>(&triCount), sizeof(uint32_t));
    if (triCount > 0) {
        buf.append(reinterpret_cast<const char*>(trigrams.data()), sizeof(Trigram) * triCount);
    }

    // V2: append lastModTime as int64_t
    int64_t modTime64 = static_cast<int64_t>(lastModTime);
    buf.append(reinterpret_cast<const char*>(&modTime64), sizeof(int64_t));

    // Write entry + CRC32
    if (fwrite(buf.data(), 1, buf.size(), file_) != buf.size()) return false;
    uint32_t checksum = IndexWAL::crc32(buf.data(), buf.size());
    if (fwrite(&checksum, sizeof(uint32_t), 1, file_) != 1) return false;

    size_t written = buf.size() + sizeof(uint32_t);
    currentSize_ += written;
    entryCount_++;
    dirty_.store(true, std::memory_order_relaxed);

    fflush(file_);
    unflushedCount_++;

    if (syncInterval_ > 0 && unflushedCount_ >= syncInterval_) {
        fsync(fileno(file_));
        unflushedCount_ = 0;
    }

    return true;
}

bool ContentIndexWAL::appendRemove(uint32_t fileIndex, const std::string& fullPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) return false;

    // H-6: Check WAL file size limit (in-memory tracking)
    if (currentSize_ >= kMaxWALSize) return false;

    // Build entry into buffer for CRC32
    std::string buf;
    uint8_t op = Entry::Remove;
    buf.append(reinterpret_cast<const char*>(&op), 1);
    buf.append(reinterpret_cast<const char*>(&fileIndex), sizeof(uint32_t));
    uint32_t pathLen = static_cast<uint32_t>(fullPath.size());
    buf.append(reinterpret_cast<const char*>(&pathLen), sizeof(uint32_t));
    buf.append(fullPath.data(), fullPath.size());

    // Write entry + CRC32
    if (fwrite(buf.data(), 1, buf.size(), file_) != buf.size()) return false;
    uint32_t checksum = IndexWAL::crc32(buf.data(), buf.size());
    if (fwrite(&checksum, sizeof(uint32_t), 1, file_) != 1) return false;

    size_t written = buf.size() + sizeof(uint32_t);
    currentSize_ += written;
    entryCount_++;
    dirty_.store(true, std::memory_order_relaxed);

    fflush(file_);
    unflushedCount_++;

    if (syncInterval_ > 0 && unflushedCount_ >= syncInterval_) {
        fsync(fileno(file_));
        unflushedCount_ = 0;
    }

    return true;
}

std::vector<ContentIndexWAL::Entry> ContentIndexWAL::readAll(const std::string& walPath) {
    std::vector<Entry> entries;

    FILE* f = fopen(walPath.c_str(), "rb");
    if (!f) return entries;

    // Verify the current path-based WAL header. Legacy numeric WALs are
    // intentionally ignored so the derived content cache is rebuilt safely.
    uint32_t magic = 0, version = 0;
    if (fread(&magic, sizeof(uint32_t), 1, f) != 1 ||
        fread(&version, sizeof(uint32_t), 1, f) != 1 ||
        magic != kMagic || version != kVersion) {
        fclose(f);
        return entries;
    }

    while (true) {
        long startPos = ftell(f);
        if (startPos < 0) break;

        Entry entry;

        uint8_t opByte;
        if (fread(&opByte, 1, 1, f) != 1) break;
        if (opByte < 1 || opByte > 2) break;
        entry.op = static_cast<Entry::Op>(opByte);

        if (fread(&entry.fileIndex, sizeof(uint32_t), 1, f) != 1) break;
        uint32_t pathLen = 0;
        if (fread(&pathLen, sizeof(uint32_t), 1, f) != 1 || pathLen > 1024 * 1024) break;
        entry.fullPath.resize(pathLen);
        if (pathLen > 0 && fread(entry.fullPath.data(), 1, pathLen, f) != pathLen) break;

        if (entry.op == Entry::Add) {
            if (fread(&entry.contentHash, sizeof(uint64_t), 1, f) != 1) break;

            uint32_t triCount;
            if (fread(&triCount, sizeof(uint32_t), 1, f) != 1) break;
            if (triCount > 1000000) break; // sanity limit

            entry.trigrams.resize(triCount);
            if (triCount > 0 && fread(entry.trigrams.data(), sizeof(Trigram), triCount, f) != triCount) break;

            // V2: read lastModTime
            int64_t modTime64;
            if (fread(&modTime64, sizeof(int64_t), 1, f) != 1) break;
            entry.lastModTime = static_cast<time_t>(modTime64);
        }

        // Read and verify CRC32
        long endPos = ftell(f);
        if (endPos < 0) break;
        size_t entryLen = static_cast<size_t>(endPos - startPos);

        uint32_t storedCRC;
        if (fread(&storedCRC, sizeof(uint32_t), 1, f) != 1) break;

        // Re-read entry bytes for CRC computation
        std::vector<uint8_t> rawBuf(entryLen);
        long afterCRC = ftell(f);
        fseek(f, startPos, SEEK_SET);
        if (fread(rawBuf.data(), 1, entryLen, f) != entryLen) break;
        fseek(f, afterCRC, SEEK_SET);

        uint32_t computedCRC = IndexWAL::crc32(rawBuf.data(), rawBuf.size());
        if (computedCRC != storedCRC) {
            // H-4: Log CRC mismatch location for diagnostics
            LOG_ERROR("ContentIndexWAL", "CRC mismatch at offset " << startPos
                      << ", recovered " << entries.size() << " entries");
            break;
        }

        entries.push_back(std::move(entry));
    }

    fclose(f);
    return entries;
}

void ContentIndexWAL::sync() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ && unflushedCount_ > 0) {
        fsync(fileno(file_));
        unflushedCount_ = 0;
    }
}

void ContentIndexWAL::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        if (unflushedCount_ > 0) {
            fsync(fileno(file_));
            unflushedCount_ = 0;
        }
        fclose(file_);
        file_ = nullptr;
    }
}

void ContentIndexWAL::closeAndDelete() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
    if (!path_.empty()) {
        remove(path_.c_str());
    }
}

void ContentIndexWAL::updatePath(const std::string& newPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = newPath;
}

// ============================================================
// ContentIndexPersistence
// ============================================================

ContentIndexPersistence::ContentIndexPersistence(std::shared_ptr<ContentIndex> index,
                                                 const std::string& basePath,
                                                 const std::string& walPath,
                                                 ContentIndex::IndexResolver resolveIndex)
    : index_(std::move(index))
    , basePath_(basePath)
    , walPath_(walPath)
    , resolveIndex_(std::move(resolveIndex))
{}

ContentIndexPersistence::~ContentIndexPersistence() {
    alive_->store(false, std::memory_order_release);
    stopAutoCompactionAndWait();
    if (wal_) wal_->close();
}

bool ContentIndexPersistence::load() {
    // 1. Load base index
    bool loaded = index_->loadFromFile(basePath_, resolveIndex_);
    if (loaded) {
        LOG_INFO("ContentIndexPersistence", "Loaded base content index, files="
                  << index_->indexedFileCount());
    } else {
        LOG_INFO("ContentIndexPersistence", "No base content index found at " << basePath_);
    }

    bool replayed = false;
    // 2. Replay WAL entries
    for (const auto& segment : contentWalSegments(walPath_)) {
        auto entries = ContentIndexWAL::readAll(segment);
        if (!entries.empty()) {
            replayed = true;
            LOG_INFO("ContentIndexPersistence", "Replaying " << entries.size() << " content WAL entries");
            for (auto& entry : entries) {
                switch (entry.op) {
                    case ContentIndexWAL::Entry::Add:
                        {
                            uint32_t idx = resolveIndex_ ? resolveIndex_(entry.fullPath) : entry.fileIndex;
                            if (idx != UINT32_MAX) {
                                index_->insertFileInfo(idx, entry.contentHash, std::move(entry.trigrams),
                                                       entry.lastModTime, entry.fullPath);
                            }
                        }
                        break;
                    case ContentIndexWAL::Entry::Remove:
                        {
                            uint32_t idx = resolveIndex_ ? resolveIndex_(entry.fullPath) : entry.fileIndex;
                            if (idx != UINT32_MAX) index_->removeFile(idx);
                        }
                        break;
                }
            }
            LOG_INFO("ContentIndexPersistence", "Content WAL replay done, indexed files="
                      << index_->indexedFileCount());
        }
    }

    return loaded || replayed;
}

void ContentIndexPersistence::attachWAL() {
    wal_ = std::make_shared<ContentIndexWAL>();
    auto segments = contentWalSegments(walPath_);
    std::string activePath = segments.empty() ? walPath_ : segments.back();
    if (wal_->open(activePath)) {
        LOG_INFO("ContentIndexPersistence", "Content WAL attached at " << walPath_);
    } else {
        LOG_ERROR("ContentIndexPersistence", "Failed to open content WAL at " << walPath_);
        wal_.reset();
    }
}

void ContentIndexPersistence::compact(bool force) {
    std::lock_guard<std::mutex> compactionLock(compactionMutex_);

    // Multi-tier skip logic:
    //   - No WAL → skip.
    //   - force=true: skip only if WAL file is header-only (no entries at all,
    //     including stale entries from a previous session that weren't compacted).
    //     This ensures exit compaction flushes replayed-but-uncompacted WAL data.
    //   - Otherwise: skip if not dirty or below threshold.
    static constexpr size_t kWALHeaderSize = 2 * sizeof(uint32_t); // magic + version
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        const bool pendingSegments = contentWalSegments(walPath_).size() > 1;
        if (!wal_) {
            LOG_INFO("ContentIndexPersistence", "Skipping compaction — no WAL");
            return;
        }
        if (force) {
            // Only skip if WAL file is truly empty (header-only, no stale entries)
            if (wal_->currentSize() <= kWALHeaderSize && !pendingSegments) {
                LOG_INFO("ContentIndexPersistence", "Skipping compaction — WAL is empty");
                return;
            }
        } else {
            if (!wal_->isDirty() && !pendingSegments) {
                LOG_INFO("ContentIndexPersistence", "Skipping compaction — no mutations since last compact");
                return;
            }
            if (wal_->entryCount() < kCompactThreshold && !pendingSegments) {
                LOG_INFO("ContentIndexPersistence", "Skipping compaction — below threshold ("
                          << wal_->entryCount() << " < " << kCompactThreshold << ")");
                return;
            }
        }
    }

    // 1. Open a fresh WAL before detaching old one (gap-free swap)
    auto newWal = std::make_shared<ContentIndexWAL>();
    std::string newWalPath = nextContentWalSegment(walPath_);
    if (!newWal->open(newWalPath)) {
        LOG_ERROR("ContentIndexPersistence", "Failed to open new content WAL for compaction");
        return;
    }

    // 2. Swap WAL (under walMutex_ so walAppendAdd/Remove see the new WAL)
    std::shared_ptr<ContentIndexWAL> oldWal;
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        oldWal = wal_;
        wal_ = newWal;
    }

    // 3. Write new base file (crash-safety: C-1 fix — write before removing old WAL)
    if (index_->saveToFile(basePath_)) {
        LOG_INFO("ContentIndexPersistence", "Compacted content index, files="
                  << index_->indexedFileCount()
                  << ", walEntries=" << (oldWal ? oldWal->entryCount() : 0)
                  << ", walBytes=" << (oldWal ? oldWal->currentSize() : 0));
    } else {
        LOG_ERROR("ContentIndexPersistence", "Failed to write content base index"
                  << " — retaining WAL segments");
        return;
    }

    if (oldWal) {
        oldWal->close();
    }
    for (const auto& segment : contentWalSegments(walPath_)) {
        if (segment != newWalPath) std::remove(segment.c_str());
    }
}

void ContentIndexPersistence::startAutoCompaction(double /*intervalSec*/) {
    stopAutoCompactionAndWait();
    compactionQueue_ = dispatch_queue_create(
        "com.maceverything.content.compaction", DISPATCH_QUEUE_SERIAL);
    LOG_INFO("ContentIndexPersistence", "Auto-compaction started (event-driven)");
}

void ContentIndexPersistence::stopAutoCompactionAndWait() {
    if (compactionQueue_) {
        // Drain any pending work
        dispatch_sync(compactionQueue_, ^{});
        dispatch_release(compactionQueue_);
        compactionQueue_ = nullptr;
    }
    compactionScheduled_.store(false, std::memory_order_relaxed);
}

void ContentIndexPersistence::scheduleCompaction() {
    if (!compactionQueue_) return;

    // Only schedule if not already pending
    bool expected = false;
    if (!compactionScheduled_.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel)) {
        return;
    }

    auto delaySec = kCompactionDelaySec;
    auto alive = alive_;  // prevent use-after-free: block outlives object
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(delaySec * NSEC_PER_SEC)),
        compactionQueue_,
        ^{
            if (!alive->load(std::memory_order_acquire)) return;
            this->compactionScheduled_.store(false, std::memory_order_relaxed);
            this->compact();
        });
}

void ContentIndexPersistence::walAppendAdd(uint32_t fileIndex, const std::string& fullPath,
                                            uint64_t contentHash, const std::vector<Trigram>& trigrams,
                                            time_t lastModTime) {
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        if (wal_) {
            wal_->appendAdd(fileIndex, fullPath, contentHash, trigrams, lastModTime);
        }
    }
    scheduleCompaction();
}

void ContentIndexPersistence::walAppendRemove(uint32_t fileIndex, const std::string& fullPath) {
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        if (wal_) {
            wal_->appendRemove(fileIndex, fullPath);
        }
    }
    scheduleCompaction();
}

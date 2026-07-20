#include "ContentIndexPersistence.h"
#include "Logger.h"
#include <unistd.h>
#include <cerrno>
#include <cstring>

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
    file_ = fopen(walPath.c_str(), "ab");
    if (!file_) return false;

    // H-3: Write magic+version header if this is a new (empty) file
    long pos = ftell(file_);
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
        currentSize_ = static_cast<size_t>(pos);
    }

    entryCount_ = 0;
    dirty_.store(false, std::memory_order_relaxed);
    return true;
}

bool ContentIndexWAL::appendAdd(uint32_t fileIndex, uint64_t contentHash,
                                 const std::vector<Trigram>& trigrams, time_t lastModTime) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) return false;

    // H-6: Check WAL file size limit (in-memory tracking)
    if (currentSize_ >= kMaxWALSize) return false;

    // Build entry into buffer for CRC32
    std::string buf;
    uint8_t op = Entry::Add;
    buf.append(reinterpret_cast<const char*>(&op), 1);
    buf.append(reinterpret_cast<const char*>(&fileIndex), sizeof(uint32_t));
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

bool ContentIndexWAL::appendRemove(uint32_t fileIndex) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) return false;

    // H-6: Check WAL file size limit (in-memory tracking)
    if (currentSize_ >= kMaxWALSize) return false;

    // Build entry into buffer for CRC32
    std::string buf;
    uint8_t op = Entry::Remove;
    buf.append(reinterpret_cast<const char*>(&op), 1);
    buf.append(reinterpret_cast<const char*>(&fileIndex), sizeof(uint32_t));

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

    // H-3: Verify magic+version header
    uint32_t magic = 0, version = 0;
    if (fread(&magic, sizeof(uint32_t), 1, f) != 1 ||
        fread(&version, sizeof(uint32_t), 1, f) != 1 ||
        magic != kMagic || version != kVersion) {
        // Legacy WAL without header — try reading from the beginning
        fseek(f, 0, SEEK_SET);
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
                                                 const std::string& walPath)
    : index_(std::move(index))
    , basePath_(basePath)
    , walPath_(walPath)
{}

ContentIndexPersistence::~ContentIndexPersistence() {
    alive_->store(false, std::memory_order_release);
    stopAutoCompactionAndWait();
    if (wal_) wal_->close();
}

bool ContentIndexPersistence::load() {
    // 1. Load base index
    bool loaded = index_->loadFromFile(basePath_);
    if (loaded) {
        LOG_INFO("ContentIndexPersistence", "Loaded base content index, files="
                  << index_->indexedFileCount());
    } else {
        LOG_INFO("ContentIndexPersistence", "No base content index found at " << basePath_);
    }

    // 2. Replay WAL entries
    auto entries = ContentIndexWAL::readAll(walPath_);
    if (!entries.empty()) {
        LOG_INFO("ContentIndexPersistence", "Replaying " << entries.size() << " content WAL entries");
        for (auto& entry : entries) {
            switch (entry.op) {
                case ContentIndexWAL::Entry::Add:
                    index_->insertFileInfo(entry.fileIndex, entry.contentHash, std::move(entry.trigrams), entry.lastModTime);
                    break;
                case ContentIndexWAL::Entry::Remove:
                    index_->removeFile(entry.fileIndex);
                    break;
            }
        }
        LOG_INFO("ContentIndexPersistence", "Content WAL replay done, indexed files="
                  << index_->indexedFileCount());
    }

    return loaded || !entries.empty();
}

void ContentIndexPersistence::attachWAL() {
    wal_ = std::make_shared<ContentIndexWAL>();
    if (wal_->open(walPath_)) {
        LOG_INFO("ContentIndexPersistence", "Content WAL attached at " << walPath_);
    } else {
        LOG_ERROR("ContentIndexPersistence", "Failed to open content WAL at " << walPath_);
        wal_.reset();
    }
}

void ContentIndexPersistence::compact(bool force) {
    // Multi-tier skip logic:
    //   - No WAL → skip.
    //   - force=true: skip only if WAL file is header-only (no entries at all,
    //     including stale entries from a previous session that weren't compacted).
    //     This ensures exit compaction flushes replayed-but-uncompacted WAL data.
    //   - Otherwise: skip if not dirty or below threshold.
    static constexpr size_t kWALHeaderSize = 2 * sizeof(uint32_t); // magic + version
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        if (!wal_) {
            LOG_INFO("ContentIndexPersistence", "Skipping compaction — no WAL");
            return;
        }
        if (force) {
            // Only skip if WAL file is truly empty (header-only, no stale entries)
            if (wal_->currentSize() <= kWALHeaderSize) {
                LOG_INFO("ContentIndexPersistence", "Skipping compaction — WAL is empty");
                return;
            }
        } else {
            if (!wal_->isDirty()) {
                LOG_INFO("ContentIndexPersistence", "Skipping compaction — no mutations since last compact");
                return;
            }
            if (wal_->entryCount() < kCompactThreshold) {
                LOG_INFO("ContentIndexPersistence", "Skipping compaction — below threshold ("
                          << wal_->entryCount() << " < " << kCompactThreshold << ")");
                return;
            }
        }
    }

    // 1. Open a fresh WAL before detaching old one (gap-free swap)
    auto newWal = std::make_shared<ContentIndexWAL>();
    std::string newWalPath = walPath_ + ".new";
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
                  << " — rolling back WAL");
        {
            std::lock_guard<std::mutex> lock(walMutex_);
            wal_ = oldWal;
        }
        newWal->closeAndDelete();
        return;
    }

    // 4. Rename new WAL from .wal.new to .wal.
    //    POSIX rename atomically replaces the old .wal directory entry,
    //    so old WAL's inode is unlinked from the directory — its fd remains
    //    valid but the file is gone once the fd is closed.
    //    This avoids the self-propagating failure chain: we never call
    //    closeAndDelete() on oldWal, so there's no risk of unlinking the
    //    new WAL's file.
    if (rename(newWalPath.c_str(), walPath_.c_str()) != 0) {
        LOG_ERROR("ContentIndexPersistence", "Failed to rename WAL: " << newWalPath
                  << " -> " << walPath_ << " (errno=" << errno << ": " << strerror(errno) << ")");
    } else {
        std::lock_guard<std::mutex> lock(walMutex_);
        if (wal_) wal_->updatePath(walPath_);
    }

    // 5. Close old WAL (just close fd — rename already replaced the directory entry)
    if (oldWal) {
        oldWal->close();
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

void ContentIndexPersistence::walAppendAdd(uint32_t fileIndex, uint64_t contentHash,
                                            const std::vector<Trigram>& trigrams, time_t lastModTime) {
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        if (wal_) {
            wal_->appendAdd(fileIndex, contentHash, trigrams, lastModTime);
        }
    }
    scheduleCompaction();
}

void ContentIndexPersistence::walAppendRemove(uint32_t fileIndex) {
    {
        std::lock_guard<std::mutex> lock(walMutex_);
        if (wal_) {
            wal_->appendRemove(fileIndex);
        }
    }
    scheduleCompaction();
}

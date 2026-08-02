#pragma once
#include "ContentIndex.h"
#include "IndexWAL.h" // for IndexWAL::crc32()
#include <dispatch/dispatch.h>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdio>

/// WAL for content index mutations.
class ContentIndexWAL {
public:
    ContentIndexWAL() = default;
    ~ContentIndexWAL();

    ContentIndexWAL(const ContentIndexWAL&) = delete;
    ContentIndexWAL& operator=(const ContentIndexWAL&) = delete;

    /// Open WAL file for appending.
    bool open(const std::string& walPath);

    /// Append an add entry with a stable path identity.
    bool appendAdd(uint32_t fileIndex, const std::string& fullPath, uint64_t contentHash,
                   const std::vector<Trigram>& trigrams, time_t lastModTime = 0);
    bool appendAdd(uint32_t fileIndex, uint64_t contentHash,
                   const std::vector<Trigram>& trigrams, time_t lastModTime = 0) {
        return appendAdd(fileIndex, {}, contentHash, trigrams, lastModTime);
    }

    /// Append a remove entry: fileIndex.
    bool appendRemove(uint32_t fileIndex, const std::string& fullPath);
    bool appendRemove(uint32_t fileIndex) { return appendRemove(fileIndex, {}); }

    /// Read all valid entries from a WAL file.
    struct Entry {
        enum Op : uint8_t { Add = 1, Remove = 2 };
        Op op;
        uint32_t fileIndex;
        std::string fullPath;
        uint64_t contentHash; // only for Add
        std::vector<Trigram> trigrams; // only for Add
        time_t lastModTime = 0; // only for Add
    };
    static std::vector<Entry> readAll(const std::string& walPath,
                                      size_t* validBytes = nullptr);

    /// Force fsync to disk. Call when you need durability guarantees.
    void sync();

    /// Set the number of entries between automatic fsyncs (0 = never auto-fsync).
    void setSyncInterval(uint64_t entries) { syncInterval_ = entries; }

    void close();
    void closeAndDelete();
    bool isOpen() const { return file_ != nullptr; }

    /// Update the internal path after an external rename (e.g., compact rename).
    void updatePath(const std::string& newPath);

    /// Maximum WAL file size (20 MB).
    static constexpr size_t kMaxWALSize = 20 * 1024 * 1024;

    /// H-3: WAL file header constants
    static constexpr uint32_t kMagic   = 0x43574C31; // "CWL1"
    static constexpr uint32_t kVersion = 2;

    /// Tracking accessors (aligned with IndexWAL)
    uint64_t entryCount() const { return entryCount_; }
    size_t currentSize() const { return currentSize_; }
    bool isDirty() const { return dirty_.load(std::memory_order_relaxed); }
    void clearDirty() { dirty_.store(false, std::memory_order_relaxed); }

private:
    FILE* file_ = nullptr;
    std::string path_;
    uint64_t unflushedCount_ = 0;
    uint64_t syncInterval_ = 64;  // fsync every 64 entries by default
    uint64_t entryCount_ = 0;     // total entries appended
    size_t currentSize_ = 0;      // in-memory file size tracking
    std::atomic<bool> dirty_{false}; // set on append, cleared on compact
    bool failed_ = false;
    std::mutex mutex_;
};

/// Orchestrates content index persistence: base file + WAL + auto-compaction.
class ContentIndexPersistence {
public:
    ContentIndexPersistence(std::shared_ptr<ContentIndex> index,
                            const std::string& basePath,
                            const std::string& walPath,
                            ContentIndex::IndexResolver resolveIndex = {});
    ~ContentIndexPersistence();

    ContentIndexPersistence(const ContentIndexPersistence&) = delete;
    ContentIndexPersistence& operator=(const ContentIndexPersistence&) = delete;

    /// Load base index + replay WAL entries.
    bool load();

    /// Start logging mutations to WAL.
    void attachWAL();

    /// Compact: write new base, clear WAL.
    /// If force is false, compaction is skipped when WAL is below threshold.
    void compact(bool force = false);

    /// Minimum WAL entry count before non-forced compaction triggers.
    static constexpr uint64_t kCompactThreshold = 50;

    /// Prepare the compaction queue. Compaction is event-driven: it fires
    /// after WAL mutations, not on a fixed timer.
    void startAutoCompaction(double intervalSec = 0);

    /// Stop auto-compaction and wait for in-flight compaction to finish.
    void stopAutoCompactionAndWait();

    /// WAL accessors for the bridge layer to log mutations.
    void walAppendAdd(uint32_t fileIndex, const std::string& fullPath, uint64_t contentHash,
                      const std::vector<Trigram>& trigrams, time_t lastModTime = 0);
    void walAppendAdd(uint32_t fileIndex, uint64_t contentHash,
                      const std::vector<Trigram>& trigrams, time_t lastModTime = 0) {
        walAppendAdd(fileIndex, {}, contentHash, trigrams, lastModTime);
    }
    void walAppendRemove(uint32_t fileIndex, const std::string& fullPath);
    void walAppendRemove(uint32_t fileIndex) { walAppendRemove(fileIndex, {}); }

    const std::string& basePath() const { return basePath_; }
    const std::string& walPath() const { return walPath_; }

    /// Delay before event-driven compaction fires after the last mutation.
    static constexpr double kCompactionDelaySec = 60.0;

private:
    std::shared_ptr<ContentIndex> index_;
    std::shared_ptr<ContentIndexWAL> wal_;
    std::string basePath_;
    std::string walPath_;
    ContentIndex::IndexResolver resolveIndex_;
    std::mutex compactionMutex_;
    std::mutex walMutex_;
    std::mutex queueMutex_;
    dispatch_queue_t compactionQueue_ = nullptr;
    dispatch_source_t compactionTimer_ = nullptr;
    std::atomic<bool> compactionScheduled_{false};
    std::atomic<bool> forceCompactionNeeded_{false};
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    /// Schedule a compaction after kCompactionDelaySec. Called from walAppend*.
    void scheduleCompaction();
};

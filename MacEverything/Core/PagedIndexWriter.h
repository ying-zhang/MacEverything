#pragma once
#include "SearchEngine.h"
#include "IndexWAL.h"  // for IndexWAL::crc32()
#include <string>
#include <vector>
#include <cstdint>

/// Page-based incremental persistence for SearchEngine records.
///
/// Two-file format:
///   index.pages  — append-only page blobs (magic "MEPG")
///   index.ptable — atomic page table with offsets/lengths/CRC (magic "MEPT")
///
/// Only dirty pages are appended on flush; old page data becomes dead space.
/// fullRewrite() reclaims dead space by rewriting both files.
class PagedIndexWriter {
public:
    PagedIndexWriter(const std::string& pagesPath, const std::string& ptablePath);

    /// Load records from .ptable + .pages into a SearchEngine.
    /// Returns false if files don't exist or are corrupt.
    bool load(SearchEngine& engine, IndexMetadata* outMeta);

    /// Incrementally write only dirty pages. Appends to .pages, atomically rewrites .ptable.
    /// Clears engine's dirty page bitmap on success.
    bool flushDirtyPages(SearchEngine& engine, const IndexMetadata& meta);

    /// Full rewrite of both files (after compactRecords or dead space reclaim).
    /// Clears engine's dirty pages and fullRewriteNeeded flags on success.
    bool fullRewrite(SearchEngine& engine, const IndexMetadata& meta);

    /// Fraction of .pages file that is dead (superseded) data. Range [0, 1].
    double deadSpaceRatio() const;

    /// Whether the paged files exist and are loadable.
    bool exists() const;

    static constexpr uint32_t kPagesMagic  = 0x4D455047; // "MEPG"
    static constexpr uint32_t kPtableMagic = 0x4D455054; // "MEPT"
    static constexpr uint32_t kVersionV4   = 1;  // v4: per-record full path strings
    static constexpr uint32_t kVersionV5   = 2;  // v5: path dictionary + namePool direct write
    static constexpr uint32_t kVersion     = kVersionV5; // current write version

private:
    struct PageEntry {
        uint64_t offset;       // byte offset in .pages file
        uint32_t byteLength;   // page blob size in bytes
        uint16_t recordCount;  // actual records in this page (last page may be < kRecordsPerPage)
        uint32_t crc32;        // CRC32 of page blob data
    };

    std::string pagesPath_;
    std::string ptablePath_;
    std::vector<PageEntry> pageEntries_;
    uint64_t pagesFileSize_ = 0;

    /// Write the .ptable file atomically (tmp + fsync + rename).
    bool writePtable(const IndexMetadata& meta, uint32_t totalRecords) const;

    /// Parse a v5 page blob. Returns records (original name, path cleared),
    /// lowerNames, and pathIndices for direct installation into SearchEngine.
    struct V5PageResult {
        std::vector<FileRecord> records;
        std::vector<std::string> lowerNames;
        std::vector<uint32_t> pathIndices;
    };
    static bool deserializePageV5(const uint8_t* data, size_t len,
                                  uint16_t expectedCount,
                                  V5PageResult& out);

    /// Loaded ptable version (1=v4, 2=v5) for migration detection.
    uint32_t loadedVersion_ = 0;

    /// Cached path dictionaries for v5 serialization.
    StringPool pathDict_;
    StringPool lowerPathDict_;

    /// Write a StringPool (buffer + entries) to a FILE with CRC32. Returns false on I/O error.
    static bool writeStringPool(FILE* f, const StringPool& pool);

    /// Read a StringPool (buffer + entries) from a FILE, verifying CRC32. Returns false on error.
    static bool readStringPool(FILE* f, StringPool& pool);
};

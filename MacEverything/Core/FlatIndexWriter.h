#pragma once
#include "SearchEngine.h"
#include "IndexWAL.h"  // for IndexWAL::crc32()
#include <string>
#include <cstdint>

/// Flat SoA persistence for SearchEngine — v6 format.
///
/// Single-file format (index.v6):
///   Header (64 bytes): magic + version + counts + lastEventId + CRC
///   Section Table: sectionCount * 24 bytes
///   Sections: contiguous SoA column data
///
/// Designed for sub-second Phase 1 load: bulk fread() of each section,
/// direct installation via SearchEngine::loadRecordsV6().
class FlatIndexWriter {
public:
    explicit FlatIndexWriter(const std::string& path);

    /// Load records from v6 file into engine.
    /// Phase 1 only — trigram indices are deferred to Phase 2.
    /// Returns false if file doesn't exist or is corrupt.
    bool load(SearchEngine& engine, IndexMetadata* outMeta);

    /// Full rewrite of v6 file from engine snapshot.
    /// Uses tmp + fsync + rename for atomicity.
    bool fullRewrite(SearchEngine& engine, const IndexMetadata& meta);

    /// Whether the v6 file exists.
    bool exists() const;

    const std::string& path() const { return path_; }

    // --- Format constants ---
    static constexpr char kMagic[4] = {'M', 'E', 'V', '6'};
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kHeaderSize = 64;

    // Section IDs
    static constexpr uint32_t kSectionNamesOrig     = 1;
    static constexpr uint32_t kSectionNamesLower     = 2;
    static constexpr uint32_t kSectionPathPool       = 3;
    static constexpr uint32_t kSectionLowerPathPool  = 4;  // legacy: read for backward compat, no longer written
    static constexpr uint32_t kSectionPathIndices    = 5;
    static constexpr uint32_t kSectionTypes          = 6;
    static constexpr uint32_t kSectionSizes          = 7;
    static constexpr uint32_t kSectionModTimes       = 8;
    static constexpr uint32_t kSectionInodes         = 9;
    static constexpr uint32_t kSectionDevIds         = 10;
    static constexpr uint32_t kSectionMetadataKV     = 11;
    static constexpr uint32_t kSectionCount          = 10;  // excludes legacy kSectionLowerPathPool
    static constexpr uint32_t kSectionCountLegacy    = 11;  // v6 files written before lowerPathPool removal

private:
    std::string path_;

    // Section table entry (on-disk layout)
    struct SectionEntry {
        uint32_t sectionId;
        uint32_t reserved;     // padding for alignment
        uint64_t offset;       // byte offset from file start
        uint32_t byteLength;   // section data size in bytes
        uint32_t crc32;        // CRC32 of section data
    };
    static_assert(sizeof(SectionEntry) == 24, "SectionEntry must be 24 bytes");

    // Header (on-disk layout)
    struct Header {
        char magic[4];
        uint32_t version;
        uint32_t recordCount;
        uint32_t liveCount;
        int64_t  timestamp;
        uint64_t lastEventId;
        uint32_t sectionCount;
        uint32_t headerCRC;
        char     reserved[20];
    };
    static_assert(sizeof(Header) == 64, "Header must be 64 bytes");

    /// Write a StringPool section: bufferSize(4) + buffer + entryCount(4) + entries
    static bool writeStringPoolSection(FILE* f, const StringPool& pool,
                                       uint32_t& outSize, uint32_t& outCRC);

    /// Read a StringPool section from buffer
    static bool readStringPoolSection(const uint8_t* data, size_t len, StringPool& pool);

    /// Write raw array section
    template<typename T>
    static bool writeArraySection(FILE* f, const std::vector<T>& vec,
                                  uint32_t& outSize, uint32_t& outCRC);

    /// Write metadata key-value pairs section
    static bool writeMetadataSection(FILE* f, const IndexMetadata& meta,
                                     uint32_t& outSize, uint32_t& outCRC);

    /// Read metadata key-value pairs from buffer
    static bool readMetadataSection(const uint8_t* data, size_t len, IndexMetadata& meta);
};

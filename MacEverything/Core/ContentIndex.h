#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstdio>

/// A trigram is 3 consecutive lowercase ASCII bytes packed into the low 24 bits of a uint32_t.
using Trigram = uint32_t;

/// Fast ASCII lowercase table: A-Z -> a-z, all other bytes identity.
/// Used by extractTrigrams and bigram extraction instead of locale-aware std::tolower.
namespace me_ascii {
inline constexpr uint8_t kLowerTable[256] = {
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
     32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
     48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
     64, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111,
    112,113,114,115,116,117,118,119,120,121,122, 91, 92, 93, 94, 95,
     96, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111,
    112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,
    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
    144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
    160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
    224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
    240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
};
} // namespace me_ascii

/// Result of a content search query.
struct ContentMatch {
    uint32_t fileIndex;       // index into SearchEngine's SoA columns
    std::string snippet;      // ~200 chars context around match
    uint32_t matchOffset;     // byte offset of match in file
};

/// Per-file content indexing metadata.
struct ContentFileInfo {
    uint64_t contentHash;     // simple hash of file content for change detection
    std::vector<Trigram> trigrams; // all trigrams extracted from this file
    time_t lastModTime = 0;   // file modification time for incremental indexing
};

/// Trigram-based inverted index for full-text content search.
///
/// Indexes file contents by extracting all 3-byte trigrams and building
/// an inverted index mapping each trigram to the set of fileIndices that contain it.
/// Query: extract trigrams from keyword → intersect posting lists → verify matches by re-reading files.
class ContentIndex {
public:
    ContentIndex();
    ~ContentIndex() = default;

    // --- Indexing ---

    /// Index a single file's content. Reads the file, extracts trigrams, updates inverted index.
    /// Returns true if the file was newly indexed or updated (false if binary, too large, unreadable, or unchanged).
    /// If modTime > 0 and matches the stored lastModTime, skips I/O entirely (incremental optimization).
    bool indexFile(uint32_t fileIndex, const std::string& fullPath, time_t modTime = 0);

    /// Remove a file from the content index.
    void removeFile(uint32_t fileIndex);

    /// Check if a file is already indexed.
    bool isFileIndexed(uint32_t fileIndex) const;

    // --- Querying ---

    /// Search for files containing the keyword. Returns matches with snippets.
    /// Uses trigram intersection for keywords >= 3 chars, brute-force for shorter.
    std::vector<ContentMatch> query(const std::string& keyword, uint32_t maxResults = 100) const;

    // --- Configuration ---

    /// Set allowed file extensions (lowercase, without dot). Empty = index all text files.
    void setExtensions(const std::vector<std::string>& exts);

    /// Set maximum file size to index (bytes). Default 1MB.
    void setMaxFileSize(uint64_t bytes);

    /// Get current extensions list.
    std::vector<std::string> getExtensions() const;

    /// Get current max file size.
    uint64_t getMaxFileSize() const;

    // --- Persistence ---

    /// Save the entire content index to a binary file.
    bool saveToFile(const std::string& path) const;

    /// Load the content index from a binary file.
    bool loadFromFile(const std::string& path);

    // --- Stats ---

    /// Number of indexed files.
    uint32_t indexedFileCount() const;

    // --- Low-level access for persistence layer ---

    /// Get the ContentFileInfo for a specific file (for WAL serialization).
    bool getFileInfo(uint32_t fileIndex, ContentFileInfo& info) const;

    /// Directly insert file info (for WAL replay / load).
    void insertFileInfo(uint32_t fileIndex, uint64_t contentHash, std::vector<Trigram>&& trigrams, time_t lastModTime = 0);

    /// Directly remove file info and update inverted index.
    void removeFileInternal(uint32_t fileIndex);

    /// Remap fileIndices after SearchEngine compaction. Thread-safe.
    void remapFileIndices(const std::unordered_map<uint32_t, uint32_t>& remap);

    /// Remove content entries whose fileIndex points to a non-regular-file record
    /// in the search engine (e.g., after search engine compaction shifted indices).
    /// Returns the number of pruned entries.
    uint32_t pruneStaleEntries(const std::unordered_set<uint32_t>& validFileIndices);

    /// Return the list of fileIndex keys currently in the content index. Thread-safe.
    std::vector<uint32_t> getIndexedFileIndices() const;

    // --- Helpers (public for testing) ---

    /// Check if a filename has an allowed extension.
    bool hasAllowedExtension(const std::string& filename) const;

    /// Extract all trigrams from text (lowercased).
    static std::vector<Trigram> extractTrigrams(const std::string& text);

    /// Pack 3 bytes into a Trigram.
    static inline Trigram makeTrigram(uint8_t a, uint8_t b, uint8_t c) {
        return (static_cast<uint32_t>(a) << 16) |
               (static_cast<uint32_t>(b) << 8)  |
               static_cast<uint32_t>(c);
    }

    /// Generate a snippet showing the keyword in context.
    /// Reads the file and finds the first occurrence of keyword, returning surrounding text.
    static std::string generateSnippet(const std::string& path,
                                       const std::string& keyword,
                                       uint32_t& outOffset,
                                       uint32_t contextChars = 80);

private:
    // Inverted index: trigram → sorted list of fileIndices
    std::unordered_map<Trigram, std::vector<uint32_t>> invertedIndex_;

    // Per-file info: fileIndex → content hash + trigrams
    std::unordered_map<uint32_t, ContentFileInfo> fileInfos_;

    // Configuration
    std::unordered_set<std::string> extensions_;
    uint64_t maxFileSize_ = 1 * 1024 * 1024; // 1MB

    mutable std::shared_mutex mutex_;

    // Lock-free version for internal use (caller must hold mutex_)
    bool hasAllowedExtensionLocked(const std::string& filename) const;

    // Internal: read file and check for binary in a single open/read pass.
    // Returns empty string if binary or unreadable.
    static std::string readFileIfText(const std::string& path, uint64_t maxSize);

    // Internal: compute a simple hash of content
    static uint64_t hashContent(const std::string& content);
};

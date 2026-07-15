#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstdio>
#include <functional>

/// A trigram is 3 consecutive lowercase ASCII bytes packed into the low 24 bits of a uint32_t.
using Trigram = uint32_t;

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

enum ContentIndexUpdate {
    Unchanged,
    Upserted,
    Removed,
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
    ContentIndexUpdate indexFile(uint32_t fileIndex, const std::string& fullPath,
                                 time_t modTime = 0);

    /// Remove a file from the content index.
    void removeFile(uint32_t fileIndex);

    /// Check if a file is already indexed.
    bool isFileIndexed(uint32_t fileIndex) const;

    // --- Querying ---

    /// Search for files containing the keyword. Returns matches with snippets.
    /// Uses trigram intersection for keywords >= 3 chars, brute-force for shorter.
    using PathResolver = std::function<bool(uint32_t fileIndex, std::string& fullPath)>;
    std::vector<ContentMatch> query(const std::string& keyword, uint32_t maxResults,
                                    const PathResolver& resolvePath) const;

    // --- Configuration ---

    /// Set allowed file extensions (lowercase, without dot). Empty disables indexing.
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

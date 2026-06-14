#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <algorithm>

/// Contiguous memory pool for strings.
/// All strings are stored end-to-end in a single char buffer.
/// Each string is referenced by an Entry {offset, length}.
/// Supports append, tombstone (length=0), and compaction.
class StringPool {
public:
    struct Entry {
        uint32_t offset;
        uint16_t length;
    };

    StringPool() = default;
    StringPool(StringPool&&) = default;
    StringPool& operator=(StringPool&&) = default;
    StringPool(const StringPool&) = default;
    StringPool& operator=(const StringPool&) = default;

    /// Append a string to the pool. Returns the entry index.
    uint32_t append(const char* data, uint16_t len) {
        uint32_t idx = static_cast<uint32_t>(entries_.size());
        uint32_t off = static_cast<uint32_t>(buffer_.size());
        buffer_.insert(buffer_.end(), data, data + len);
        entries_.push_back({off, len});
        return idx;
    }

    /// Append from C string literal.
    uint32_t append(const char* s) {
        size_t len = std::strlen(s);
        return append(s, static_cast<uint16_t>(std::min<size_t>(len, UINT16_MAX)));
    }

    /// Append from std::string.
    uint32_t append(const std::string& s) {
        return append(s.data(), static_cast<uint16_t>(std::min<size_t>(s.size(), UINT16_MAX)));
    }

    /// Append from string_view.
    uint32_t append(std::string_view sv) {
        return append(sv.data(), static_cast<uint16_t>(std::min<size_t>(sv.size(), UINT16_MAX)));
    }

    /// Mark entry as tombstoned. Buffer space is not reclaimed.
    void tombstone(uint32_t idx) {
        if (idx < entries_.size()) {
            entries_[idx].length = 0;
        }
    }

    /// Pointer to the string data for entry idx.
    const char* data(uint32_t idx) const {
        if (idx >= entries_.size()) return "";
        auto& e = entries_[idx];
        if (e.offset + e.length > buffer_.size()) return "";
        return buffer_.data() + e.offset;
    }

    /// Length of entry idx (0 if tombstoned or out-of-bounds).
    uint16_t length(uint32_t idx) const {
        if (idx >= entries_.size()) return 0;
        auto& e = entries_[idx];
        if (e.offset + e.length > buffer_.size()) return 0;
        return e.length;
    }

    /// Whether entry idx is live (not tombstoned).
    bool isLive(uint32_t idx) const {
        return idx < entries_.size() && entries_[idx].length > 0;
    }

    /// Get a string_view for entry idx.
    std::string_view view(uint32_t idx) const {
        if (idx >= entries_.size()) return {};
        auto& e = entries_[idx];
        if (e.offset + e.length > buffer_.size()) return {};
        return {buffer_.data() + e.offset, e.length};
    }

    /// Get a std::string copy for entry idx.
    std::string str(uint32_t idx) const {
        if (idx >= entries_.size()) return {};
        auto& e = entries_[idx];
        if (e.offset + e.length > buffer_.size()) return {};
        return std::string(buffer_.data() + e.offset, e.length);
    }

    /// Raw contiguous buffer pointer. All strings are packed here.
    const char* rawBuffer() const { return buffer_.data(); }

    /// Total size of the raw buffer in bytes.
    size_t rawSize() const { return buffer_.size(); }

    /// Allocated capacity of the raw buffer in bytes.
    size_t rawCapacity() const { return buffer_.capacity(); }

    /// Pointer to entries array.
    const Entry* entries() const { return entries_.data(); }

    /// Number of entries (including tombstoned).
    uint32_t entryCount() const { return static_cast<uint32_t>(entries_.size()); }

    /// Allocated capacity of the entry table.
    size_t entryCapacity() const { return entries_.capacity(); }

    /// Pre-allocate buffer and entry storage.
    void reserve(size_t totalBytes, size_t entryCount) {
        buffer_.reserve(totalBytes);
        entries_.reserve(entryCount);
    }

    /// Bulk-load from a vector of strings. Clears existing content.
    void loadBulk(const std::vector<std::string>& strings) {
        clear();
        size_t totalLen = 0;
        for (const auto& s : strings) totalLen += s.size();
        buffer_.reserve(totalLen);
        entries_.reserve(strings.size());
        for (const auto& s : strings) {
            append(s);
        }
    }

    /// Construct pool directly from raw binary data (copy-based bulk load).
    /// Used by v5 persistence to restore a StringPool from on-disk buffer+entries.
    void loadRaw(const char* buffer, size_t bufferSize,
                 const Entry* entries, uint32_t entryCount) {
        buffer_.assign(buffer, buffer + bufferSize);
        entries_.assign(entries, entries + entryCount);
    }

    /// Move-based bulk load (zero-copy from pre-read vectors).
    /// Used by v6 persistence for direct I/O without intermediate copies.
    void loadRaw(std::vector<char>&& buffer, std::vector<Entry>&& entries) {
        buffer_ = std::move(buffer);
        entries_ = std::move(entries);
    }

    /// Clear all entries and buffer.
    void clear() {
        buffer_.clear();
        entries_.clear();
    }

    /// Create a compacted copy containing only live entries matching liveMask.
    /// Defined after class body (CompactResult uses StringPool by value).
    struct CompactResult;
    CompactResult compact(const std::vector<bool>& liveMask) const;

private:
    std::vector<char> buffer_;
    std::vector<Entry> entries_;
};

/// Result of compaction — defined outside class so StringPool is complete.
struct StringPool::CompactResult {
    StringPool compacted;
    std::vector<uint32_t> remap; // remap[oldIdx] = newIdx, UINT32_MAX if not live
};

inline StringPool::CompactResult StringPool::compact(const std::vector<bool>& liveMask) const {
    CompactResult result;
    result.remap.assign(entries_.size(), UINT32_MAX);
    size_t liveBytes = 0;
    size_t liveCount = 0;
    for (uint32_t i = 0; i < entries_.size(); i++) {
        if (i < liveMask.size() && liveMask[i] && entries_[i].length > 0) {
            liveBytes += entries_[i].length;
            liveCount++;
        }
    }
    result.compacted.reserve(liveBytes, liveCount);
    for (uint32_t i = 0; i < entries_.size(); i++) {
        if (i < liveMask.size() && liveMask[i] && entries_[i].length > 0) {
            uint32_t newIdx = result.compacted.append(
                buffer_.data() + entries_[i].offset, entries_[i].length);
            result.remap[i] = newIdx;
        }
    }
    return result;
}

#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <unordered_map>

/// A read-only flat posting index: sorted keys with concatenated posting lists.
/// Replaces unordered_map<KeyType, vector<uint32_t>> for static index data,
/// providing better cache locality and lower memory overhead.
template<typename KeyType>
class FlatPostingIndex {
public:
    FlatPostingIndex() = default;

    /// Build from an unordered_map, consuming (moving) the posting lists.
    void buildMove(std::unordered_map<KeyType, std::vector<uint32_t>>&& map) {
        keys_.clear();
        offsets_.clear();
        postings_.clear();

        keys_.reserve(map.size());
        for (auto& [key, _] : map) {
            keys_.push_back(key);
        }
        std::sort(keys_.begin(), keys_.end());

        size_t totalPostings = 0;
        for (const auto& [_, list] : map) totalPostings += list.size();
        postings_.reserve(totalPostings);

        offsets_.reserve(keys_.size() + 1);
        uint32_t offset = 0;
        for (const auto& key : keys_) {
            offsets_.push_back(offset);
            auto& list = map[key];
            postings_.insert(postings_.end(), list.begin(), list.end());
            offset += static_cast<uint32_t>(list.size());
        }
        offsets_.push_back(offset);
    }

    /// Build from an unordered_map (copy).
    void build(const std::unordered_map<KeyType, std::vector<uint32_t>>& map) {
        keys_.clear();
        offsets_.clear();
        postings_.clear();

        keys_.reserve(map.size());
        for (const auto& [key, _] : map) {
            keys_.push_back(key);
        }
        std::sort(keys_.begin(), keys_.end());

        size_t totalPostings = 0;
        for (const auto& [_, list] : map) totalPostings += list.size();
        postings_.reserve(totalPostings);

        offsets_.reserve(keys_.size() + 1);
        uint32_t offset = 0;
        for (const auto& key : keys_) {
            offsets_.push_back(offset);
            const auto& list = map.at(key);
            postings_.insert(postings_.end(), list.begin(), list.end());
            offset += static_cast<uint32_t>(list.size());
        }
        offsets_.push_back(offset);
    }

    /// Look up a key. Returns pointer and count (zero-copy).
    /// Returns {nullptr, 0} if key not found.
    struct LookupResult {
        const uint32_t* data;
        uint32_t count;
    };

    LookupResult lookup(KeyType key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key);
        if (it == keys_.end() || *it != key) return {nullptr, 0};
        size_t idx = static_cast<size_t>(it - keys_.begin());
        uint32_t start = offsets_[idx];
        uint32_t end = offsets_[idx + 1];
        return {postings_.data() + start, end - start};
    }

    bool empty() const { return keys_.empty(); }
    size_t size() const { return keys_.size(); }
    size_t totalPostings() const { return postings_.size(); }

    size_t memoryBytes() const {
        return keys_.capacity() * sizeof(KeyType) +
               offsets_.capacity() * sizeof(uint32_t) +
               postings_.capacity() * sizeof(uint32_t);
    }

    void clear() {
        keys_.clear(); keys_.shrink_to_fit();
        offsets_.clear(); offsets_.shrink_to_fit();
        postings_.clear(); postings_.shrink_to_fit();
    }

private:
    std::vector<KeyType> keys_;
    std::vector<uint32_t> offsets_;
    std::vector<uint32_t> postings_;
};

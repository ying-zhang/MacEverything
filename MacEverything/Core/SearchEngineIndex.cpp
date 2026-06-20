#include "SearchEngine.h"
#include "StringUtils.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// Trigram posting list intersection utilities
// ---------------------------------------------------------------------------

std::vector<uint32_t> SearchEngine::intersectPostingLists(
    const std::unordered_map<Trigram, std::vector<uint32_t>>& index,
    const std::string& keyword,
    bool& allFound) {
    allFound = true;
    // extractTrigrams already returns deduplicated trigrams via thread_local bitmap
    auto keyTrigrams = ContentIndex::extractTrigrams(keyword);
    if (keyTrigrams.empty()) { allFound = false; return {}; }

    std::vector<const std::vector<uint32_t>*> postings;
    for (Trigram t : keyTrigrams) {
        auto it = index.find(t);
        if (it == index.end()) { allFound = false; return {}; }
        postings.push_back(&it->second);
    }

    std::sort(postings.begin(), postings.end(),
        [](const auto* a, const auto* b) { return a->size() < b->size(); });

    thread_local std::vector<uint32_t> tl_buf_a;
    thread_local std::vector<uint32_t> tl_buf_b;
    auto* current = &tl_buf_a;
    auto* next = &tl_buf_b;
    current->clear();
    current->assign(postings[0]->begin(), postings[0]->end());

    for (size_t i = 1; i < postings.size() && !current->empty(); i++) {
        const auto& other = *postings[i];
        next->clear();
        next->reserve(std::min(current->size(), other.size()));
        std::set_intersection(current->begin(), current->end(),
                              other.begin(), other.end(),
                              std::back_inserter(*next));
        std::swap(current, next);
    }
    return std::vector<uint32_t>(current->begin(), current->end());
}

std::vector<uint32_t> SearchEngine::intersectPostingListsFlat(
    const FlatPostingIndex<Trigram>& flat,
    const TrigramDelta& delta,
    const std::string& keyword,
    bool& allFound) {
    allFound = true;
    auto keyTrigrams = ContentIndex::extractTrigrams(keyword);
    if (keyTrigrams.empty()) { allFound = false; return {}; }

    // For each trigram, merge flat lookup with delta adds/removes
    std::vector<std::vector<uint32_t>> merged;
    merged.reserve(keyTrigrams.size());
    for (Trigram t : keyTrigrams) {
        auto lr = flat.lookup(t);
        auto addIt = delta.adds.find(t);
        auto remIt = delta.removes.find(t);

        bool hasFlat = lr.data != nullptr && lr.count > 0;
        bool hasAdds = addIt != delta.adds.end() && !addIt->second.empty();
        bool hasRemoves = remIt != delta.removes.end() && !remIt->second.empty();

        if (!hasFlat && !hasAdds) { allFound = false; return {}; }

        if (!hasAdds && !hasRemoves) {
            merged.emplace_back(lr.data, lr.data + lr.count);
            continue;
        }

        // Merge: (flat UNION adds) MINUS removes
        std::vector<uint32_t> combined;
        if (hasFlat && hasAdds) {
            combined.reserve(lr.count + addIt->second.size());
            std::set_union(lr.data, lr.data + lr.count,
                           addIt->second.begin(), addIt->second.end(),
                           std::back_inserter(combined));
        } else if (hasFlat) {
            combined.assign(lr.data, lr.data + lr.count);
        } else {
            combined = addIt->second;
        }

        if (hasRemoves) {
            std::vector<uint32_t> filtered;
            filtered.reserve(combined.size());
            std::set_difference(combined.begin(), combined.end(),
                                remIt->second.begin(), remIt->second.end(),
                                std::back_inserter(filtered));
            combined = std::move(filtered);
        }

        if (combined.empty()) { allFound = false; return {}; }
        merged.push_back(std::move(combined));
    }

    // Sort by size (smallest first) for optimal intersection
    std::sort(merged.begin(), merged.end(),
        [](const auto& a, const auto& b) { return a.size() < b.size(); });

    thread_local std::vector<uint32_t> tl_flat_a;
    thread_local std::vector<uint32_t> tl_flat_b;
    auto* current = &tl_flat_a;
    auto* next = &tl_flat_b;
    *current = std::move(merged[0]);

    for (size_t i = 1; i < merged.size() && !current->empty(); i++) {
        next->clear();
        next->reserve(std::min(current->size(), merged[i].size()));
        std::set_intersection(current->begin(), current->end(),
                              merged[i].begin(), merged[i].end(),
                              std::back_inserter(*next));
        std::swap(current, next);
    }
    return std::vector<uint32_t>(current->begin(), current->end());
}

std::vector<uint32_t> SearchEngine::intersectPostingListsMulti(
    const std::unordered_map<Trigram, std::vector<uint32_t>>& index,
    const std::vector<std::string>& segments,
    bool& allFound) {
    allFound = true;
    // Collect trigrams from all segments, dedup via thread_local bitmap
    static constexpr size_t kBitmapSize = 1 << 24;
    thread_local std::vector<bool> seen(kBitmapSize, false);
    thread_local std::vector<Trigram> dirty;
    for (auto t : dirty) seen[t] = false;
    dirty.clear();

    std::vector<Trigram> allTrigrams;
    for (const auto& seg : segments) {
        auto segTrigrams = ContentIndex::extractTrigrams(seg);
        for (Trigram t : segTrigrams) {
            if (!seen[t]) {
                seen[t] = true;
                dirty.push_back(t);
                allTrigrams.push_back(t);
            }
        }
    }
    if (allTrigrams.empty()) { allFound = false; return {}; }

    std::vector<const std::vector<uint32_t>*> postings;
    for (Trigram t : allTrigrams) {
        auto it = index.find(t);
        if (it == index.end()) { allFound = false; return {}; }
        postings.push_back(&it->second);
    }

    std::sort(postings.begin(), postings.end(),
        [](const auto* a, const auto* b) { return a->size() < b->size(); });

    thread_local std::vector<uint32_t> tl_multi_a;
    thread_local std::vector<uint32_t> tl_multi_b;
    auto* current = &tl_multi_a;
    auto* next = &tl_multi_b;
    current->clear();
    current->assign(postings[0]->begin(), postings[0]->end());

    for (size_t i = 1; i < postings.size() && !current->empty(); i++) {
        const auto& other = *postings[i];
        next->clear();
        next->reserve(std::min(current->size(), other.size()));
        std::set_intersection(current->begin(), current->end(),
                              other.begin(), other.end(),
                              std::back_inserter(*next));
        std::swap(current, next);
    }
    return std::vector<uint32_t>(current->begin(), current->end());
}

std::vector<uint32_t> SearchEngine::unionPostingListsMulti(
    const std::unordered_map<Trigram, std::vector<uint32_t>>& index,
    const std::vector<std::string>& segments) {
    // For each segment, intersect its trigrams to get that segment's candidates.
    // Then UNION across all segments. This is safe for both AND and OR semantics
    // from FilteredRE2 — wider than pure intersection but never drops results.
    std::vector<uint32_t> result;
    for (const auto& seg : segments) {
        bool allFound = false;
        auto segCands = intersectPostingLists(index, seg, allFound);
        if (!allFound) continue;  // skip atoms with no trigram coverage
        if (result.empty()) {
            result = std::move(segCands);
        } else {
            std::vector<uint32_t> merged;
            merged.reserve(result.size() + segCands.size());
            std::set_union(result.begin(), result.end(),
                           segCands.begin(), segCands.end(),
                           std::back_inserter(merged));
            result = std::move(merged);
        }
    }
    return result;
}

std::vector<uint32_t> SearchEngine::unionPostingListsMultiFlat(
    const FlatPostingIndex<Trigram>& flat,
    const TrigramDelta& delta,
    const std::vector<std::string>& segments) {
    std::vector<uint32_t> result;
    for (const auto& seg : segments) {
        bool allFound = false;
        auto segCands = intersectPostingListsFlat(flat, delta, seg, allFound);
        if (!allFound) continue;
        if (result.empty()) {
            result = std::move(segCands);
        } else {
            std::vector<uint32_t> merged;
            merged.reserve(result.size() + segCands.size());
            std::set_union(result.begin(), result.end(),
                           segCands.begin(), segCands.end(),
                           std::back_inserter(merged));
            result = std::move(merged);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Name trigram index build / add / remove
// ---------------------------------------------------------------------------

std::unordered_map<Trigram, std::vector<uint32_t>>
SearchEngine::buildTrigramIndexFromData(const std::vector<uint8_t>& types,
                                        const StringPool& namePool) {
    std::unordered_map<Trigram, std::vector<uint32_t>> index;
    for (uint32_t i = 0; i < namePool.entryCount(); i++) {
        if (types[i] == 0 || !namePool.isLive(i)) continue;
        std::string_view sv = namePool.view(i);
        auto trigrams = ContentIndex::extractTrigrams(std::string(sv));
        std::sort(trigrams.begin(), trigrams.end());
        trigrams.erase(std::unique(trigrams.begin(), trigrams.end()), trigrams.end());
        for (Trigram t : trigrams) {
            index[t].push_back(i);
        }
    }
    for (auto& [trigram, list] : index) {
        std::sort(list.begin(), list.end());
    }
    return index;
}

void SearchEngine::buildTrigramIndex() {
    nameTrigramIndex_ = buildTrigramIndexFromData(types_, namePool_);
}

StringPool SearchEngine::buildPinyinInitialsPoolFromData(const StringPool& origNamePool) {
    StringPool pool;
    pool.reserve(origNamePool.rawSize(), origNamePool.entryCount());
    for (uint32_t i = 0; i < origNamePool.entryCount(); i++) {
        std::string key;
        if (origNamePool.isLive(i)) {
            key = me::mandarinInitialsKey(origNamePool.str(i));
        }
        pool.append(key);
    }
    return pool;
}

void SearchEngine::buildPinyinInitialsPoolFromOrigNames() {
    pinyinInitialsPool_ = buildPinyinInitialsPoolFromData(origNamePool_);
}

void SearchEngine::buildPinyinInitialsIndex() {
    if (!options_.enablePinyinInitials) {
        pinyinInitialsTrigramIndex_.clear();
        return;
    }
    pinyinInitialsTrigramIndex_ = buildTrigramIndexFromData(types_, pinyinInitialsPool_);
}

void SearchEngine::addPinyinInitialsForRecord(uint32_t idx) {
    if (!options_.enablePinyinInitials) return;
    if (idx >= pinyinInitialsPool_.entryCount()) return;
    auto len = pinyinInitialsPool_.length(idx);
    if (len == 0) return;
    auto trigrams = ContentIndex::extractTrigrams(std::string(pinyinInitialsPool_.data(idx), len));
    std::sort(trigrams.begin(), trigrams.end());
    trigrams.erase(std::unique(trigrams.begin(), trigrams.end()), trigrams.end());

    for (Trigram t : trigrams) {
        auto& list = pinyinInitialsTrigramIndex_[t];
        auto pos = std::lower_bound(list.begin(), list.end(), idx);
        list.insert(pos, idx);
    }
}

void SearchEngine::removePinyinInitialsForRecord(uint32_t idx) {
    if (!options_.enablePinyinInitials) return;
    if (idx >= pinyinInitialsPool_.entryCount() || !pinyinInitialsPool_.isLive(idx)) return;
    auto trigrams = ContentIndex::extractTrigrams(
        std::string(pinyinInitialsPool_.data(idx), pinyinInitialsPool_.length(idx)));
    std::sort(trigrams.begin(), trigrams.end());
    trigrams.erase(std::unique(trigrams.begin(), trigrams.end()), trigrams.end());
    for (Trigram t : trigrams) {
        auto it = pinyinInitialsTrigramIndex_.find(t);
        if (it != pinyinInitialsTrigramIndex_.end()) {
            auto& list = it->second;
            auto pos = std::lower_bound(list.begin(), list.end(), idx);
            if (pos != list.end() && *pos == idx) {
                list.erase(pos);
            }
            if (list.empty()) {
                pinyinInitialsTrigramIndex_.erase(it);
            }
        }
    }
}

void SearchEngine::addTrigramsForRecord(uint32_t idx, const char* data, uint16_t len) {
    auto trigrams = ContentIndex::extractTrigrams(std::string(data, len));
    std::sort(trigrams.begin(), trigrams.end());
    trigrams.erase(std::unique(trigrams.begin(), trigrams.end()), trigrams.end());

    if (!nameTrigramFlat_.empty()) {
        for (Trigram t : trigrams) {
            auto& list = nameTrigramDelta_.adds[t];
            auto pos = std::lower_bound(list.begin(), list.end(), idx);
            if (pos == list.end() || *pos != idx) list.insert(pos, idx);
        }
    } else {
        for (Trigram t : trigrams) {
            auto& list = nameTrigramIndex_[t];
            auto pos = std::lower_bound(list.begin(), list.end(), idx);
            list.insert(pos, idx);
        }
    }
}

void SearchEngine::removeTrigramsForRecord(uint32_t idx) {
    if (idx >= namePool_.entryCount() || !namePool_.isLive(idx)) return;
    auto trigrams = ContentIndex::extractTrigrams(std::string(namePool_.data(idx), namePool_.length(idx)));
    std::sort(trigrams.begin(), trigrams.end());
    trigrams.erase(std::unique(trigrams.begin(), trigrams.end()), trigrams.end());

    if (!nameTrigramFlat_.empty()) {
        for (Trigram t : trigrams) {
            auto& list = nameTrigramDelta_.removes[t];
            auto pos = std::lower_bound(list.begin(), list.end(), idx);
            if (pos == list.end() || *pos != idx) list.insert(pos, idx);
        }
    } else {
        for (Trigram t : trigrams) {
            auto it = nameTrigramIndex_.find(t);
            if (it != nameTrigramIndex_.end()) {
                auto& list = it->second;
                auto pos = std::lower_bound(list.begin(), list.end(), idx);
                if (pos != list.end() && *pos == idx) {
                    list.erase(pos);
                }
                if (list.empty()) {
                    nameTrigramIndex_.erase(it);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Extension index build / add / remove
// ---------------------------------------------------------------------------

/// Extract lowercase extension from name data. Returns empty string if no dot.
static std::string extractExtFromName(const char* data, uint16_t len) {
    int dotPos = -1;
    for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
        if (data[i] == '.') { dotPos = i; break; }
    }
    if (dotPos < 0 || dotPos == static_cast<int>(len) - 1) return {};
    // namePool_ stores lowercase names already
    return std::string(data + dotPos + 1, len - dotPos - 1);
}

std::unordered_map<std::string, std::vector<uint32_t>>
SearchEngine::buildExtensionIndexFromData(const std::vector<uint8_t>& types,
                                           const StringPool& namePool) {
    std::unordered_map<std::string, std::vector<uint32_t>> index;
    for (uint32_t i = 0; i < namePool.entryCount(); i++) {
        if (types[i] == 0 || !namePool.isLive(i)) continue;
        std::string ext = extractExtFromName(namePool.data(i), namePool.length(i));
        if (!ext.empty()) {
            index[ext].push_back(i);
        }
    }
    // Posting lists are already in order since we iterate i = 0..N
    return index;
}

void SearchEngine::buildExtensionIndex() {
    extensionIndex_ = buildExtensionIndexFromData(types_, namePool_);
}

void SearchEngine::addExtensionForRecord(uint32_t idx) {
    if (idx >= namePool_.entryCount() || !namePool_.isLive(idx)) return;
    std::string ext = extractExtFromName(namePool_.data(idx), namePool_.length(idx));
    if (ext.empty()) return;
    auto& list = extensionIndex_[ext];
    auto pos = std::lower_bound(list.begin(), list.end(), idx);
    list.insert(pos, idx);
}

void SearchEngine::removeExtensionForRecord(uint32_t idx) {
    if (idx >= namePool_.entryCount() || !namePool_.isLive(idx)) return;
    std::string ext = extractExtFromName(namePool_.data(idx), namePool_.length(idx));
    if (ext.empty()) return;
    auto it = extensionIndex_.find(ext);
    if (it != extensionIndex_.end()) {
        auto& list = it->second;
        auto pos = std::lower_bound(list.begin(), list.end(), idx);
        if (pos != list.end() && *pos == idx) {
            list.erase(pos);
        }
        if (list.empty()) {
            extensionIndex_.erase(it);
        }
    }
}

// ---------------------------------------------------------------------------
// Path trigram index build / add / remove
// ---------------------------------------------------------------------------

std::unordered_map<Trigram, std::vector<uint32_t>>
SearchEngine::buildPathTrigramIndexFromData(const StringPool& pathPool) {
    std::unordered_map<Trigram, std::vector<uint32_t>> index;
    for (uint32_t pi = 0; pi < pathPool.entryCount(); pi++) {
        if (!pathPool.isLive(pi)) continue;
        std::string lp = lowerPathStr(pathPool, pi);
        auto trigrams = ContentIndex::extractTrigrams(lp);
        std::sort(trigrams.begin(), trigrams.end());
        trigrams.erase(std::unique(trigrams.begin(), trigrams.end()), trigrams.end());
        for (Trigram t : trigrams) {
            index[t].push_back(pi);
        }
    }
    for (auto& [_, list] : index) {
        std::sort(list.begin(), list.end());
    }
    return index;
}

std::vector<std::vector<uint32_t>>
SearchEngine::buildPathIdxToRecordsFromData(const std::vector<uint8_t>& types,
                                            const std::vector<uint32_t>& pathIndices,
                                            uint32_t pathPoolSize) {
    std::vector<std::vector<uint32_t>> mapping(pathPoolSize);
    for (size_t i = 0; i < types.size(); i++) {
        if (types[i] == 0) continue;
        uint32_t pi = pathIndices[i];
        if (pi < mapping.size()) {
            mapping[pi].push_back(static_cast<uint32_t>(i));
        }
    }
    for (auto& list : mapping) {
        std::sort(list.begin(), list.end());
    }
    return mapping;
}

void SearchEngine::buildPathTrigramIndex() {
    if (!options_.enablePathTrigramIndex) {
        pathTrigramIndex_.clear();
        return;
    }
    pathTrigramIndex_ = buildPathTrigramIndexFromData(pathPool_);
}

void SearchEngine::rebuildPathIdxToRecords() {
    if (!options_.enablePathTrigramIndex) {
        pathIdxToRecords_.clear();
        return;
    }
    pathIdxToRecords_ = buildPathIdxToRecordsFromData(types_, pathIndices_, pathPool_.entryCount());
}

void SearchEngine::addPathTrigramsForRecord(uint32_t idx) {
    if (!options_.enablePathTrigramIndex) return;
    if (idx >= pathIndices_.size()) return;
    uint32_t pi = pathIndices_[idx];
    // Ensure pathIdxToRecords_ is large enough
    if (pi >= pathIdxToRecords_.size()) {
        pathIdxToRecords_.resize(pi + 1);
    }
    auto& list = pathIdxToRecords_[pi];
    auto pos = std::lower_bound(list.begin(), list.end(), idx);
    list.insert(pos, idx);
}

void SearchEngine::removePathTrigramsForRecord(uint32_t idx) {
    if (!options_.enablePathTrigramIndex) return;
    if (idx >= pathIndices_.size()) return;
    uint32_t pi = pathIndices_[idx];
    if (pi >= pathIdxToRecords_.size()) return;
    auto& list = pathIdxToRecords_[pi];
    auto pos = std::lower_bound(list.begin(), list.end(), idx);
    if (pos != list.end() && *pos == idx) {
        list.erase(pos);
    }
}

void SearchEngine::ensurePathTrigramsForPathIdx(uint32_t pathIdx) {
    if (!options_.enablePathTrigramIndex) return;
    // Check if this pathIdx already has entries in pathTrigramIndex_
    std::string lowerPath = lowerPathStr(pathPool_, pathIdx);
    auto trigrams = ContentIndex::extractTrigrams(lowerPath);
    std::sort(trigrams.begin(), trigrams.end());
    trigrams.erase(std::unique(trigrams.begin(), trigrams.end()), trigrams.end());
    for (Trigram t : trigrams) {
        auto& list = pathTrigramIndex_[t];
        auto pos = std::lower_bound(list.begin(), list.end(), pathIdx);
        if (pos == list.end() || *pos != pathIdx) {
            list.insert(pos, pathIdx);
        }
    }
}

// ---------------------------------------------------------------------------
// CJK Bigram Index
// ---------------------------------------------------------------------------

bool SearchEngine::isCJKCodepoint(uint32_t cp) {
    return (cp >= 0x3400 && cp <= 0x4DBF) ||   // CJK Unified Ideographs Extension A
           (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK Unified Ideographs
           (cp >= 0xF900 && cp <= 0xFAFF);     // CJK Compatibility Ideographs
}

// Decode UTF-8 character, return codepoint and advance pointer.
// Returns 0 on error.
static uint32_t decodeUTF8(const char* data, size_t len, size_t& pos) {
    if (pos >= len) return 0;
    uint8_t b0 = static_cast<uint8_t>(data[pos]);
    if (b0 < 0x80) { pos++; return b0; }
    if ((b0 & 0xE0) == 0xC0 && pos + 1 < len) {
        uint32_t cp = (b0 & 0x1F) << 6 | (static_cast<uint8_t>(data[pos+1]) & 0x3F);
        pos += 2; return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && pos + 2 < len) {
        uint32_t cp = (b0 & 0x0F) << 12 | (static_cast<uint8_t>(data[pos+1]) & 0x3F) << 6
                      | (static_cast<uint8_t>(data[pos+2]) & 0x3F);
        pos += 3; return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && pos + 3 < len) {
        uint32_t cp = (b0 & 0x07) << 18 | (static_cast<uint8_t>(data[pos+1]) & 0x3F) << 12
                      | (static_cast<uint8_t>(data[pos+2]) & 0x3F) << 6
                      | (static_cast<uint8_t>(data[pos+3]) & 0x3F);
        pos += 4; return cp;
    }
    pos++; return 0;  // invalid
}

std::vector<uint64_t> SearchEngine::extractCJKBigrams(const char* data, uint16_t len) {
    std::vector<uint64_t> result;
    std::vector<uint32_t> cjkChars;
    size_t pos = 0;
    while (pos < len) {
        uint32_t cp = decodeUTF8(data, len, pos);
        if (isCJKCodepoint(cp)) {
            cjkChars.push_back(cp);
        } else {
            cjkChars.clear();  // break CJK sequence on non-CJK char
        }
        if (cjkChars.size() >= 2) {
            uint32_t prev = cjkChars[cjkChars.size() - 2];
            uint32_t curr = cjkChars[cjkChars.size() - 1];
            uint64_t bigram = (static_cast<uint64_t>(prev) << 32) | curr;
            result.push_back(bigram);
        }
    }
    // Deduplicate
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void SearchEngine::addCJKBigramsForRecord(uint32_t idx, const char* data, uint16_t len) {
    auto bigrams = extractCJKBigrams(data, len);
    for (uint64_t bg : bigrams) {
        auto& list = cjkBigramIndex_[bg];
        auto pos = std::lower_bound(list.begin(), list.end(), idx);
        if (pos == list.end() || *pos != idx) {
            list.insert(pos, idx);
        }
    }
}

void SearchEngine::removeCJKBigramsForRecord(uint32_t idx, const char* data, uint16_t len) {
    auto bigrams = extractCJKBigrams(data, len);
    for (uint64_t bg : bigrams) {
        auto it = cjkBigramIndex_.find(bg);
        if (it == cjkBigramIndex_.end()) continue;
        auto& list = it->second;
        auto pos = std::lower_bound(list.begin(), list.end(), idx);
        if (pos != list.end() && *pos == idx) {
            list.erase(pos);
            if (list.empty()) cjkBigramIndex_.erase(it);
        }
    }
}

std::unordered_map<uint64_t, std::vector<uint32_t>> SearchEngine::buildCJKBigramIndexFromData(
    const std::vector<uint8_t>& types, const StringPool& namePool) {
    std::unordered_map<uint64_t, std::vector<uint32_t>> index;
    for (uint32_t i = 0; i < namePool.entryCount(); i++) {
        if (i >= types.size() || types[i] == 0) continue;
        if (!namePool.isLive(i)) continue;
        auto bigrams = extractCJKBigrams(namePool.data(i), namePool.length(i));
        for (uint64_t bg : bigrams) {
            index[bg].push_back(i);
        }
    }
    // Sort all posting lists
    for (auto& [bg, list] : index) {
        std::sort(list.begin(), list.end());
    }
    return index;
}

// ---------------------------------------------------------------------------
// ASCII Bigram Index (for 2-character keyword queries)
// ---------------------------------------------------------------------------

std::vector<uint16_t> SearchEngine::extractAsciiBigrams(const char* data, uint16_t len) {
    if (len < 2) return {};
    std::vector<uint16_t> result;
    for (uint16_t i = 0; i + 1 < len; i++) {
        uint8_t a = me_ascii::kLowerTable[static_cast<uint8_t>(data[i])];
        uint8_t b = me_ascii::kLowerTable[static_cast<uint8_t>(data[i + 1])];
        result.push_back(packBigram(a, b));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void SearchEngine::addBigramsForRecord(uint32_t idx, const char* data, uint16_t len) {
    auto bigrams = extractAsciiBigrams(data, len);
    for (uint16_t bg : bigrams) {
        auto& list = nameBigramIndex_[bg];
        auto pos = std::lower_bound(list.begin(), list.end(), idx);
        if (pos == list.end() || *pos != idx) {
            list.insert(pos, idx);
        }
    }
}

void SearchEngine::removeBigramsForRecord(uint32_t idx, const char* data, uint16_t len) {
    auto bigrams = extractAsciiBigrams(data, len);
    for (uint16_t bg : bigrams) {
        auto it = nameBigramIndex_.find(bg);
        if (it == nameBigramIndex_.end()) continue;
        auto& list = it->second;
        auto pos = std::lower_bound(list.begin(), list.end(), idx);
        if (pos != list.end() && *pos == idx) {
            list.erase(pos);
            if (list.empty()) nameBigramIndex_.erase(it);
        }
    }
}

std::unordered_map<uint16_t, std::vector<uint32_t>> SearchEngine::buildBigramIndexFromData(
    const std::vector<uint8_t>& types, const StringPool& namePool) {
    std::unordered_map<uint16_t, std::vector<uint32_t>> index;
    for (uint32_t i = 0; i < namePool.entryCount(); i++) {
        if (i >= types.size() || types[i] == 0) continue;
        if (!namePool.isLive(i)) continue;
        auto bigrams = extractAsciiBigrams(namePool.data(i), namePool.length(i));
        for (uint16_t bg : bigrams) {
            index[bg].push_back(i);
        }
    }
    for (auto& [bg, list] : index) {
        std::sort(list.begin(), list.end());
    }
    return index;
}

// ---------------------------------------------------------------------------
// Dirty page tracking
// ---------------------------------------------------------------------------

void SearchEngine::markPageDirty(uint32_t recordIndex) {
    uint32_t page = recordIndex / kRecordsPerPage;
    if (page < dirtyPages_.size()) {
        dirtyPages_[page] = true;
    }
}

std::vector<uint32_t> SearchEngine::getDirtyPageNumbers() const {
    std::shared_lock lock(mutex_);
    std::vector<uint32_t> result;
    for (size_t i = 0; i < dirtyPages_.size(); i++) {
        if (dirtyPages_[i]) result.push_back(static_cast<uint32_t>(i));
    }
    return result;
}

void SearchEngine::clearDirtyPages() {
    std::unique_lock lock(mutex_);
    std::fill(dirtyPages_.begin(), dirtyPages_.end(), false);
}

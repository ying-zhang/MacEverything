#include "SearchEngine.h"
#include "SIMDSearch.h"
#include "StringUtils.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>
#include <unordered_set>
#include <dispatch/dispatch.h>

// ---------------------------------------------------------------------------
// pathSegmentsMatch: check if dirPath satisfies path segment constraints
// ---------------------------------------------------------------------------
bool SearchEngine::segmentTextMatches(std::string_view candidate,
                                      std::string_view needle,
                                      PathSegmentKind kind) {
    if (needle.empty()) return true;
    if (candidate.size() < needle.size()) return false;
    switch (kind) {
        case PathSegmentKind::PREFIX:
            return candidate.compare(0, needle.size(), needle) == 0;
        case PathSegmentKind::SUFFIX:
            return candidate.compare(candidate.size() - needle.size(), needle.size(), needle) == 0;
        case PathSegmentKind::EXACT:
            return candidate == needle;
        case PathSegmentKind::SUBSTRING:
        default:
            return candidate.find(needle) != std::string_view::npos;
    }
}

bool SearchEngine::namePatternMatches(const ParsedQuery& pq,
                                      const char* nameData,
                                      uint16_t nameLen,
                                      uint8_t recType) {
    const auto& namePattern = pq.namePattern;
    if (namePattern.empty()) return true;
    std::string_view nameView(nameData, nameLen);
    if (pq.mode == QueryMode::DIR_EXACT) {
        if (recType != 2) return false;
        return nameView == namePattern;
    }
    return segmentTextMatches(nameView, namePattern, pq.nameKind);
}

bool SearchEngine::pathSegmentsMatch(std::string_view dirPath,
                                     const std::vector<PathSegment>& segments) {
    if (segments.empty()) return true;

    // Split dirPath into components by '/'
    // dirPath is already lowercase (computed on-the-fly from pathPool_)
    std::vector<std::string_view> components;
    size_t start = 0;
    for (size_t i = 0; i <= dirPath.size(); i++) {
        if (i == dirPath.size() || dirPath[i] == '/') {
            if (i > start) {
                components.push_back(dirPath.substr(start, i - start));
            }
            start = i + 1;
        }
    }

    // Match segments right-to-left against path components
    int segIdx = static_cast<int>(segments.size()) - 1;
    int compIdx = static_cast<int>(components.size()) - 1;

    while (segIdx >= 0 && compIdx >= 0) {
        // Input is already lowercase — direct string_view find
        if (segmentTextMatches(components[compIdx],
                               segments[segIdx].text,
                               segments[segIdx].kind)) {
            segIdx--;
            compIdx--;
        } else {
            if (segIdx < static_cast<int>(segments.size()) - 1 &&
                segments[segIdx + 1].adjacentToNext == false) {
                compIdx--;
            } else if (segIdx == static_cast<int>(segments.size()) - 1) {
                compIdx--;
            } else {
                return false;
            }
        }
    }

    return segIdx < 0;
}

// ---------------------------------------------------------------------------
// estimateTrigramCost: cheap upper-bound for trigram candidate count
// ---------------------------------------------------------------------------
size_t SearchEngine::estimateTrigramCost(const std::string& keyword) const {
    bool hasIndex = !nameTrigramFlat_.empty() || !nameTrigramIndex_.empty();
    if (keyword.size() < 3 || !hasIndex) return SIZE_MAX;
    auto trigrams = ContentIndex::extractTrigrams(keyword);
    std::unordered_set<Trigram> unique(trigrams.begin(), trigrams.end());
    if (unique.empty()) return SIZE_MAX;

    size_t minSize = SIZE_MAX;
    if (!nameTrigramFlat_.empty()) {
        for (Trigram t : unique) {
            auto lr = nameTrigramFlat_.lookup(t);
            auto addIt = nameTrigramDelta_.adds.find(t);
            auto remIt = nameTrigramDelta_.removes.find(t);
            bool hasFlat = lr.data != nullptr && lr.count > 0;
            bool hasAdds = addIt != nameTrigramDelta_.adds.end() && !addIt->second.empty();
            if (!hasFlat && !hasAdds) return 0;
            size_t cost = 0;
            if (hasFlat) cost += lr.count;
            if (hasAdds) cost += addIt->second.size();
            if (remIt != nameTrigramDelta_.removes.end())
                cost = (cost > remIt->second.size()) ? cost - remIt->second.size() : 0;
            if (cost == 0) return 0;
            minSize = std::min(minSize, cost);
        }
    } else {
        for (Trigram t : unique) {
            auto it = nameTrigramIndex_.find(t);
            if (it == nameTrigramIndex_.end()) return 0;
            minSize = std::min(minSize, it->second.size());
        }
    }
    return minSize;
}

// ---------------------------------------------------------------------------
// treeWalkDown: walk children from anchor dir toward namePattern
// ---------------------------------------------------------------------------
void SearchEngine::treeWalkDown(uint32_t dirIdx, const ParsedQuery& pq,
                                int fromSegIdx, int toSegIdx,
                                size_t totalSize, const QueryCancelCtx& cancel,
                                std::vector<Match>& merged) const {
    // Build the full lowered directory path for this dir record
    std::string fullDirPath = lowerPathStr(pathPool_, pathIndices_[dirIdx]);
    if (!fullDirPath.empty() && fullDirPath.back() != '/') fullDirPath += '/';
    fullDirPath += std::string(namePool_.data(dirIdx), namePool_.length(dirIdx));

    // Look up children of this directory
    auto it = lowerPathLookup_.find(fullDirPath);
    if (it == lowerPathLookup_.end()) return;

    uint32_t childPathIdx = it->second;
    std::vector<uint32_t> fallbackChildRecords;
    const std::vector<uint32_t>* childRecordsPtr = nullptr;
    if (childPathIdx < pathIdxToRecords_.size()) {
        childRecordsPtr = &pathIdxToRecords_[childPathIdx];
    } else {
        for (uint32_t idx = 0; idx < pathIndices_.size(); idx++) {
            if (types_[idx] != 0 && pathIndices_[idx] == childPathIdx) {
                fallbackChildRecords.push_back(idx);
            }
        }
        childRecordsPtr = &fallbackChildRecords;
    }
    const auto& childRecords = *childRecordsPtr;

    if (fromSegIdx > toSegIdx) {
        // We've walked through all intermediate segments.
        // Now match children against namePattern.
        const auto& namePattern = pq.namePattern;
        for (uint32_t childIdx : childRecords) {
            if (types_[childIdx] == 0) continue;
            const char* nd = namePool_.data(childIdx);
            uint16_t nl = namePool_.length(childIdx);

            if (!namePatternMatches(pq, nd, nl, types_[childIdx])) continue;

            uint8_t q = namePriority(nd, nl, namePattern.data(), namePattern.size());
            uint32_t pLen = static_cast<uint32_t>(pathPool_.length(pathIndices_[childIdx]) + 1 + nl);
            merged.push_back({childIdx, encodeScore(0, q, pLen)});
        }
        return;
    }

    // Walk through intermediate segments: find children whose name matches
    // pathSegments[fromSegIdx], then recurse down
    const auto& segText = pq.pathSegments[fromSegIdx].text;
    for (uint32_t childIdx : childRecords) {
        if (cancel.cancelled()) return;
        if (types_[childIdx] != 2) continue; // must be directory
        const char* nd = namePool_.data(childIdx);
        uint16_t nl = namePool_.length(childIdx);
        if (!segmentTextMatches(std::string_view(nd, nl),
                                segText,
                                pq.pathSegments[fromSegIdx].kind)) continue;

        // Recurse to next level
        treeWalkDown(childIdx, pq, fromSegIdx + 1, toSegIdx, totalSize, cancel, merged);
    }
}

// ---------------------------------------------------------------------------
// queryStructured: SEGMENTS and DIR_EXACT modes (with anchor-selection)
// ---------------------------------------------------------------------------
void SearchEngine::queryStructured(const ParsedQuery& pq,
                                   size_t totalSize, const QueryCancelCtx& cancel,
                                   std::vector<Match>& merged) const {
    const auto& namePattern = pq.namePattern;
    if (namePattern.empty()) return;

    // ------------------------------------------------------------------
    // Step 1: Estimate trigram cost for each segment + namePattern
    // ------------------------------------------------------------------
    size_t numPathSegs = pq.pathSegments.size();
    size_t nameCost = estimateTrigramCost(namePattern);

    size_t bestIdx = numPathSegs; // index into [pathSegs..., namePattern]
    size_t bestCost = nameCost;
    for (size_t i = 0; i < numPathSegs; i++) {
        size_t c = estimateTrigramCost(pq.pathSegments[i].text);
        // cost==0 for a path segment means its trigrams aren't in nameTrigramIndex_,
        // but that doesn't mean zero results — it just can't be used as an anchor.
        if (c > 0 && c < bestCost) {
            bestCost = c;
            bestIdx = i;
        }
    }

    size_t trigramThreshold = totalSize / 4;

    // ------------------------------------------------------------------
    // Step 2: Try anchor-based strategies before falling back to linear
    // ------------------------------------------------------------------
    // Only nameCost==0 guarantees zero results (namePattern must match a file name).
    // Path segment cost==0 just means that segment text isn't indexed as a name.
    if (nameCost == 0) return;

    if (bestCost <= trigramThreshold) {
        if (bestIdx == numPathSegs) {
            // Anchor is namePattern — original trigram path
            if (queryStructuredNameAnchor(pq, totalSize, cancel, merged))
                return;
        } else {
            // Anchor is a path segment — tree-walk strategy
            if (queryStructuredPathAnchor(pq, bestIdx, totalSize, cancel, merged))
                return;
        }
    }

    // ------------------------------------------------------------------
    // Fallback strategy selection:
    // - With path segments: path-first scan (iterate only records under
    //   matching paths via pathIdxToRecords_, then check names)
    // - Without path segments: buffer-scan (SIMD scan namePool_.buffer_)
    // ------------------------------------------------------------------
    bool hasPathSegs = !pq.pathSegments.empty();

    if (hasPathSegs) {
        // Path-first: narrow candidate paths, then iterate only their records.
        // Try path trigram index first (O(candidates) vs O(all_paths)).
        // Find the best segment (longest, >= 3 chars) for trigram lookup.
        int bestSegIdx = -1;
        size_t bestSegLen = 0;
        for (size_t i = 0; i < pq.pathSegments.size(); i++) {
            if (pq.pathSegments[i].text.size() >= 3 &&
                pq.pathSegments[i].text.size() > bestSegLen) {
                bestSegLen = pq.pathSegments[i].text.size();
                bestSegIdx = static_cast<int>(i);
            }
        }

        // Lambda: check name match and emit result for records under pathIdx pi
        auto emitRecordsForPath = [&](uint32_t pi) {
            auto emitRecord = [&](uint32_t idx) {
                if (cancel.cancelled()) return;
                if (types_[idx] == 0) return;

                const char* nd = namePool_.data(idx);
                uint16_t nl = namePool_.length(idx);

                if (!namePatternMatches(pq, nd, nl, types_[idx])) return;

                uint8_t q = namePriority(nd, nl, namePattern.data(), namePattern.size());
                uint32_t pLen = static_cast<uint32_t>(pathPool_.length(pathIndices_[idx]) + 1 + nl);
                merged.push_back({idx, encodeScore(0, q, pLen)});
            };

            if (pi < pathIdxToRecords_.size()) {
                for (uint32_t idx : pathIdxToRecords_[pi]) {
                    emitRecord(idx);
                }
            } else {
                for (uint32_t idx = 0; idx < pathIndices_.size(); idx++) {
                    if (pathIndices_[idx] == pi) emitRecord(idx);
                }
            }
        };

        bool usedTrigram = false;
        if (bestSegIdx >= 0 && !pathTrigramIndex_.empty()) {
            bool allFound = false;
            auto candidatePaths = intersectPostingLists(
                pathTrigramIndex_, pq.pathSegments[bestSegIdx].text, allFound);

            if (allFound) {
                usedTrigram = true;
                for (uint32_t pi : candidatePaths) {
                    if (cancel.cancelled()) return;
                    if (!pathPool_.isLive(pi)) continue;
                    std::string lp = lowerPathStr(pathPool_, pi);
                    if (!pathSegmentsMatch(lp, pq.pathSegments)) continue;
                    emitRecordsForPath(pi);
                }
            }
        }

        if (!usedTrigram) {
            // Linear scan all paths (no usable trigrams)
            uint32_t pathCount = pathPool_.entryCount();
            for (uint32_t pi = 0; pi < pathCount; pi++) {
                if (cancel.cancelled()) return;
                if (!pathPool_.isLive(pi)) continue;
                std::string lp = lowerPathStr(pathPool_, pi);
                if (!pathSegmentsMatch(lp, pq.pathSegments)) continue;
                emitRecordsForPath(pi);
            }
        }
    } else {
        // No path segments: buffer-scan the contiguous namePool_ buffer
        // with SIMD for substring mode; anchored modes scan entries directly.
        if (pq.nameKind == PathSegmentKind::SUBSTRING) {
            std::vector<size_t> hitOffsets;
            me::simdFindAll(namePool_.rawBuffer(), namePool_.rawSize(),
                            namePattern.data(), namePattern.size(), hitOffsets);

            if (cancel.cancelled()) return;

            const auto* entries = namePool_.entries();
            uint32_t entryCount = namePool_.entryCount();

            // hitOffsets are in ascending order. Walk cursor in sync — O(hits + entries).
            uint32_t cursor = 0;
            uint32_t lastIdx = UINT32_MAX;

            for (size_t hitOff : hitOffsets) {
                while (cursor + 1 < entryCount &&
                       entries[cursor + 1].offset <= static_cast<uint32_t>(hitOff)) {
                    cursor++;
                }

                uint32_t entryEnd = entries[cursor].offset + entries[cursor].length;
                if (hitOff < entries[cursor].offset ||
                    hitOff + namePattern.size() > entryEnd)
                    continue;

                if (cursor == lastIdx) continue;
                lastIdx = cursor;

                if (types_[cursor] == 0) continue;

                if (!namePatternMatches(pq, namePool_.data(cursor), namePool_.length(cursor), types_[cursor])) continue;

                uint8_t q = namePriority(namePool_.data(cursor), namePool_.length(cursor),
                                        namePattern.data(), namePattern.size());
                uint32_t pLen = static_cast<uint32_t>(pathPool_.length(pathIndices_[cursor]) + 1 + entries[cursor].length);
                merged.push_back({cursor, encodeScore(0, q, pLen)});
            }
        } else {
            uint32_t entryCount = namePool_.entryCount();
            for (uint32_t idx = 0; idx < entryCount; idx++) {
                if ((idx & 4095) == 0 && cancel.cancelled()) return;
                if (types_[idx] == 0) continue;
                const char* nd = namePool_.data(idx);
                uint16_t nl = namePool_.length(idx);
                if (!namePatternMatches(pq, nd, nl, types_[idx])) continue;
                uint8_t q = namePriority(nd, nl, namePattern.data(), namePattern.size());
                uint32_t pLen = static_cast<uint32_t>(pathPool_.length(pathIndices_[idx]) + 1 + nl);
                merged.push_back({idx, encodeScore(0, q, pLen)});
            }
        }
    }
}

// ---------------------------------------------------------------------------
// queryStructuredNameAnchor: trigram on namePattern, verify path constraints
// Returns true if handled (even if 0 results), false to fall back to linear.
// ---------------------------------------------------------------------------
bool SearchEngine::queryStructuredNameAnchor(const ParsedQuery& pq,
                                             size_t /*totalSize*/, const QueryCancelCtx& cancel,
                                             std::vector<Match>& merged) const {
    const auto& namePattern = pq.namePattern;
    bool allFound = false;
    auto candidates = !nameTrigramFlat_.empty()
        ? intersectPostingListsFlat(nameTrigramFlat_, nameTrigramDelta_, namePattern, allFound)
        : intersectPostingLists(nameTrigramIndex_, namePattern, allFound);
    if (!allFound) return false;

    for (size_t ci = 0; ci < candidates.size(); ci++) {
        if ((ci & 1023) == 0 && cancel.cancelled()) return true;
        uint32_t idx = candidates[ci];
        if (types_[idx] == 0) continue;

        const char* nameData = namePool_.data(idx);
        uint16_t nameLen = namePool_.length(idx);

        if (!namePatternMatches(pq, nameData, nameLen, types_[idx])) continue;

        if (!pq.pathSegments.empty()) {
            if (!pathSegmentsMatch(lowerPathStr(pathPool_, pathIndices_[idx]), pq.pathSegments)) continue;
        }

        uint8_t q = namePriority(nameData, nameLen, namePattern.data(), namePattern.size());
        uint32_t pLen = static_cast<uint32_t>(pathPool_.length(pathIndices_[idx]) + 1 + nameLen);
        merged.push_back({idx, encodeScore(0, q, pLen)});
    }
    return true;
}

// ---------------------------------------------------------------------------
// queryStructuredPathAnchor: trigram on a path segment, tree-walk to name
// Returns true if handled, false to fall back to linear.
// ---------------------------------------------------------------------------
bool SearchEngine::queryStructuredPathAnchor(const ParsedQuery& pq,
                                             size_t anchorIdx,
                                             size_t totalSize, const QueryCancelCtx& cancel,
                                             std::vector<Match>& merged) const {
    size_t numPathSegs = pq.pathSegments.size();

    // Check that all segments from anchor to namePattern are adjacent.
    // If any gap is non-adjacent, we can't tree-walk → fall back.
    if (!pq.pathSegments[anchorIdx].adjacentToNext) return false;
    for (size_t s = anchorIdx + 1; s < numPathSegs; s++) {
        if (!pq.pathSegments[s].adjacentToNext) return false;
    }

    const auto& anchorText = pq.pathSegments[anchorIdx].text;
    bool allFound = false;
    auto candidates = !nameTrigramFlat_.empty()
        ? intersectPostingListsFlat(nameTrigramFlat_, nameTrigramDelta_, anchorText, allFound)
        : intersectPostingLists(nameTrigramIndex_, anchorText, allFound);
    if (!allFound) return false;

    int walkFrom = static_cast<int>(anchorIdx) + 1;
    int walkTo = static_cast<int>(numPathSegs) - 1;

    for (size_t ci = 0; ci < candidates.size(); ci++) {
        if ((ci & 1023) == 0 && cancel.cancelled()) return true;
        uint32_t idx = candidates[ci];
        if (types_[idx] == 0) continue;
        if (types_[idx] != 2) continue; // anchor must be a directory

        const char* nd = namePool_.data(idx);
        uint16_t nl = namePool_.length(idx);
        if (!segmentTextMatches(std::string_view(nd, nl), anchorText, pq.pathSegments[anchorIdx].kind)) continue;

        // Verify ancestor segments [0..anchorIdx-1] against this record's path
        if (anchorIdx > 0) {
            std::string dirPath = lowerPathStr(pathPool_, pathIndices_[idx]);
            std::vector<PathSegment> ancestorSegs(
                pq.pathSegments.begin(),
                pq.pathSegments.begin() + static_cast<int>(anchorIdx));
            if (!pathSegmentsMatch(dirPath, ancestorSegs)) continue;
        }

        // Tree-walk down through remaining path segments to namePattern
        treeWalkDown(idx, pq, walkFrom, walkTo, totalSize, cancel, merged);
    }
    return true;
}

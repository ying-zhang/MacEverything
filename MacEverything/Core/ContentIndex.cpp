#include "ContentIndex.h"
#include "PathUtils.h"
#include "Logger.h"
#include "StringUtils.h"
#include "SIMDSearch.h"
#include "PostingListIntersection.h"
#include "TrigramExtraction.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <cctype>
#include <cstring>
#include <atomic>
#include <thread>
#include <string_view>
#include <sys/sysctl.h>
#include <unistd.h>
#include <dispatch/dispatch.h>
#include <CoreFoundation/CoreFoundation.h>

// --- Magic and version for binary persistence ---
static constexpr char CONTENT_MAGIC[4] = {'M', 'E', 'C', 'I'};
static constexpr uint32_t CONTENT_FORMAT_VERSION = 4;
static constexpr uint64_t kMinContentIndexLoadMemory = 512ULL * 1024 * 1024;
static constexpr uint64_t kMaxContentIndexLoadMemory = 8ULL * 1024 * 1024 * 1024;
static constexpr uint64_t kEstimatedFileInfoBytes = 256;
static constexpr uint64_t kEstimatedBytesPerTrigram = 16;

static uint64_t contentIndexLoadMemoryBudget() {
    uint64_t physicalMemory = 0;
    size_t size = sizeof(physicalMemory);
    if (sysctlbyname("hw.memsize", &physicalMemory, &size, nullptr, 0) != 0 ||
        physicalMemory == 0) {
        return kMinContentIndexLoadMemory;
    }

    const uint64_t adaptiveBudget = physicalMemory / 4 * 3;
    return std::clamp(adaptiveBudget, kMinContentIndexLoadMemory,
                      kMaxContentIndexLoadMemory);
}

static bool isAsciiText(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x80; });
}

static bool unicodeCaseInsensitiveFind(const std::string& text,
                                       const std::string& keyword,
                                       size_t& byteOffset,
                                       bool& validUTF8) {
    validUTF8 = true;
    CFStringRef haystack = CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
        static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false);
    CFStringRef needle = CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(keyword.data()),
        static_cast<CFIndex>(keyword.size()), kCFStringEncodingUTF8, false);
    if (!haystack || !needle) {
        validUTF8 = false;
        if (haystack) CFRelease(haystack);
        if (needle) CFRelease(needle);
        return false;
    }
    CFRange found = CFStringFind(haystack, needle,
        kCFCompareCaseInsensitive | kCFCompareNonliteral);
    CFRelease(needle);
    if (found.location == kCFNotFound) {
        CFRelease(haystack);
        return false;
    }
    CFIndex usedBytes = 0;
    CFStringGetBytes(haystack, CFRangeMake(0, found.location),
                     kCFStringEncodingUTF8, 0, false, nullptr, 0, &usedBytes);
    CFRelease(haystack);
    byteOffset = static_cast<size_t>(usedBytes);
    return true;
}

ContentIndex::ContentIndex() {
    // No default extensions — content indexing is opt-in.
    // Users must configure extensions via Content Settings.
}

// --- Configuration ---

void ContentIndex::setExtensions(const std::vector<std::string>& exts) {
    std::unique_lock lock(mutex_);
    extensions_.clear();
    for (const auto& ext : exts) {
        if (!ext.empty()) extensions_.insert(me::toLower(ext));
    }
}

void ContentIndex::setMaxFileSize(uint64_t bytes) {
    std::unique_lock lock(mutex_);
    maxFileSize_ = bytes;
}

std::vector<std::string> ContentIndex::getExtensions() const {
    std::shared_lock lock(mutex_);
    return std::vector<std::string>(extensions_.begin(), extensions_.end());
}

uint64_t ContentIndex::getMaxFileSize() const {
    std::shared_lock lock(mutex_);
    return maxFileSize_;
}

// --- Helpers ---

uint64_t ContentIndex::hashContent(const std::string& content) {
    // FNV-1a hash
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : content) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool ContentIndex::hasAllowedExtension(const std::string& filename) const {
    std::shared_lock lock(mutex_);
    return hasAllowedExtensionLocked(filename);
}

bool ContentIndex::hasAllowedExtensionLocked(const std::string& filename) const {
    // Find last dot
    size_t dotPos = filename.rfind('.');
    if (dotPos == std::string::npos || dotPos == filename.size() - 1) {
        // No extension — check if "makefile" etc. is in extensions
        std::string lowerName = me::toLower(filename);
        return extensions_.count(lowerName) > 0;
    }

    std::string ext = me::toLower(filename.substr(dotPos + 1));
    return extensions_.count(ext) > 0;
}

std::string ContentIndex::readFileIfText(const std::string& path, uint64_t maxSize) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    if (fileSize <= 0 || static_cast<uint64_t>(fileSize) > maxSize) {
        fclose(f);
        return {};
    }

    fseek(f, 0, SEEK_SET);
    std::string content;
    content.resize(static_cast<size_t>(fileSize));
    size_t bytesRead = fread(content.data(), 1, content.size(), f);
    fclose(f);

    if (bytesRead != content.size()) {
        content.resize(bytesRead);
    }

    // Check for binary (NUL byte in first 8KB)
    size_t checkLen = std::min(bytesRead, size_t(8192));
    for (size_t i = 0; i < checkLen; i++) {
        if (content[i] == '\0') return {};
    }

    return content;
}

// --- Trigram extraction ---

std::vector<Trigram> ContentIndex::extractTrigrams(const std::string& text) {
    return me::extractByteTrigrams(text);
}

// --- Snippet generation ---

std::string ContentIndex::generateSnippet(const std::string& path,
                                           const std::string& keyword,
                                           uint32_t& outOffset,
                                           uint32_t contextChars,
                                           uint64_t maxReadBytes) {
    outOffset = 0;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize <= 0) { fclose(f); return {}; }

    // H5 fix: Read in 64KB chunks instead of 1MB at once.
    // Most matches are in the first chunk, reducing average I/O by ~16x.
    static constexpr size_t kChunkSize = 64 * 1024;
    size_t maxRead = std::min(static_cast<uint64_t>(fileSize), maxReadBytes);
    size_t overlapSize = keyword.size() > 1 ? keyword.size() - 1 : 0;

    const bool asciiKeyword = isAsciiText(keyword);
    // Pre-compute lowercase keyword once for the ASCII fast path.
    std::string lowerKey(keyword);
    me::simdToLowerAscii(lowerKey.data(), lowerKey.size());

    std::string chunk(kChunkSize, '\0');
    std::string lowerChunk(kChunkSize, '\0');
    size_t fileOffset = 0;    // absolute position in file of current chunk start
    size_t globalMatchPos = std::string::npos;
    // Content around the match for snippet extraction
    std::string matchContent;
    size_t matchContentOffset = 0; // file offset where matchContent starts

    while (fileOffset < maxRead) {
        size_t toRead = std::min(kChunkSize, maxRead - fileOffset);
        chunk.resize(toRead);
        size_t bytesRead = fread(chunk.data(), 1, toRead, f);
        if (bytesRead == 0) break;
        chunk.resize(bytesRead);

        size_t pos = std::string::npos;
        if (asciiKeyword && isAsciiText(chunk)) {
            lowerChunk.resize(bytesRead);
            std::memcpy(lowerChunk.data(), chunk.data(), bytesRead);
            me::simdToLowerAscii(lowerChunk.data(), bytesRead);
            size_t found = me::simdFind(lowerChunk.data(), bytesRead,
                                        lowerKey.data(), lowerKey.size());
            if (found < bytesRead) pos = found;
        } else {
            bool validUTF8 = true;
            unicodeCaseInsensitiveFind(chunk, keyword, pos, validUTF8);
            if (!validUTF8 && asciiKeyword) {
                lowerChunk.resize(bytesRead);
                std::memcpy(lowerChunk.data(), chunk.data(), bytesRead);
                me::simdToLowerAscii(lowerChunk.data(), bytesRead);
                size_t found = me::simdFind(lowerChunk.data(), bytesRead,
                                            lowerKey.data(), lowerKey.size());
                if (found < bytesRead) pos = found;
            }
        }

        if (pos != std::string::npos) {
            globalMatchPos = fileOffset + pos;
            matchContent = std::move(chunk);
            matchContentOffset = fileOffset;
            break;
        }

        // Advance with overlap to catch keywords spanning chunk boundaries.
        if (bytesRead < kChunkSize) break; // last chunk, no more data
        // Guard against a keyword >= chunk size: overlapSize == bytesRead makes
        // advance == 0 and the loop would spin forever re-reading the first chunk.
        size_t advance = (overlapSize >= bytesRead) ? bytesRead
                                                    : (bytesRead - overlapSize);
        fileOffset += advance;
        fseek(f, static_cast<long>(fileOffset), SEEK_SET);
    }

    fclose(f);

    if (globalMatchPos == std::string::npos) return {};

    outOffset = static_cast<uint32_t>(globalMatchPos);

    // Extract context around the match (positions relative to matchContent)
    size_t localPos = globalMatchPos - matchContentOffset;
    size_t start = (localPos > contextChars) ? localPos - contextChars : 0;
    size_t end = std::min(localPos + keyword.size() + contextChars, matchContent.size());

    // Adjust start to a line boundary or word boundary if possible
    if (start > 0) {
        size_t newlinePos = matchContent.rfind('\n', localPos);
        if (newlinePos != std::string::npos && newlinePos >= start) {
            start = newlinePos + 1;
        }
    }

    std::string snippet = matchContent.substr(start, end - start);

    // Replace newlines with spaces for single-line display
    for (char& ch : snippet) {
        if (ch == '\n' || ch == '\r') ch = ' ';
    }

    // Trim leading/trailing whitespace
    size_t firstNonSpace = snippet.find_first_not_of(" \t");
    if (firstNonSpace != std::string::npos) {
        snippet = snippet.substr(firstNonSpace);
    }
    size_t lastNonSpace = snippet.find_last_not_of(" \t");
    if (lastNonSpace != std::string::npos) {
        snippet = snippet.substr(0, lastNonSpace + 1);
    }

    // Add ellipsis indicators
    std::string result;
    size_t globalStart = matchContentOffset + start;
    if (globalStart > 0) result += "...";
    result += snippet;
    size_t globalEnd = matchContentOffset + end;
    if (globalEnd < static_cast<size_t>(fileSize)) result += "...";

    return result;
}

// --- Indexing ---

ContentIndexUpdate ContentIndex::indexFile(uint32_t fileIndex, const std::string& fullPath,
                                           time_t modTime) {
    const uint64_t startGeneration = mappingGeneration_.load(std::memory_order_acquire);
    if ((startGeneration & 1U) != 0) return ContentIndexUpdate::Unchanged;

    // Check extension (read lock for config)
    bool allowedExtension = false;
    {
        std::shared_lock lock(mutex_);
        // Extract filename from path
        size_t lastSlash = fullPath.rfind('/');
        std::string filename = (lastSlash != std::string::npos) ? fullPath.substr(lastSlash + 1) : fullPath;
        allowedExtension = !extensions_.empty() && hasAllowedExtensionLocked(filename);
    }
    if (!allowedExtension) {
        std::unique_lock lock(mutex_);
        if (mappingGeneration_.load(std::memory_order_acquire) != startGeneration) {
            return ContentIndexUpdate::Unchanged;
        }
        if (fileInfos_.find(fileIndex) == fileInfos_.end()) return ContentIndexUpdate::Unchanged;
        removeFileInternal(fileIndex);
        return ContentIndexUpdate::Removed;
    }

    // Early exit: if modTime unchanged, skip expensive I/O
    if (modTime > 0) {
        std::shared_lock lock(mutex_);
        auto it = fileInfos_.find(fileIndex);
        if (it != fileInfos_.end() && it->second.lastModTime == modTime) {
            return ContentIndexUpdate::Unchanged;
        }
    }

    // Single I/O: read content and check for binary in one pass
    uint64_t maxSize;
    {
        std::shared_lock lock(mutex_);
        maxSize = maxFileSize_;
    }

    // Single-pass read: open file once, read content, check for binary
    std::string content = readFileIfText(fullPath, maxSize);
    if (content.empty()) {
        std::unique_lock lock(mutex_);
        if (mappingGeneration_.load(std::memory_order_acquire) != startGeneration) {
            return ContentIndexUpdate::Unchanged;
        }
        if (fileInfos_.find(fileIndex) == fileInfos_.end()) {
            return ContentIndexUpdate::Unchanged;
        }
        removeFileInternal(fileIndex);
        return ContentIndexUpdate::Removed;
    }

    uint64_t hash = hashContent(content);
    auto trigrams = extractTrigrams(me::normalizeNFC(me::toLower(content)));

    // Check if already indexed with same hash
    {
        std::shared_lock lock(mutex_);
        auto it = fileInfos_.find(fileIndex);
        if (it != fileInfos_.end() && it->second.contentHash == hash) {
            // Content unchanged — update lastModTime so future runs can skip I/O
            if ((modTime > 0 && it->second.lastModTime != modTime) ||
                it->second.fullPath != fullPath) {
                lock.unlock();
                std::unique_lock wlock(mutex_);
                if (mappingGeneration_.load(std::memory_order_acquire) != startGeneration) {
                    return ContentIndexUpdate::Unchanged;
                }
                auto wit = fileInfos_.find(fileIndex);
                if (wit != fileInfos_.end() && wit->second.contentHash == hash) {
                    wit->second.lastModTime = modTime;
                    wit->second.fullPath = fullPath;
                    return ContentIndexUpdate::Upserted;
                }
                return ContentIndexUpdate::Unchanged;
            }
            return ContentIndexUpdate::Unchanged;
        }
    }

    // Update index (exclusive lock)
    std::unique_lock lock(mutex_);
    if (mappingGeneration_.load(std::memory_order_acquire) != startGeneration) {
        return ContentIndexUpdate::Unchanged;
    }

    // Remove old entry if exists
    auto oldIt = fileInfos_.find(fileIndex);
    if (oldIt != fileInfos_.end()) {
        // Remove from inverted index
        for (Trigram tri : oldIt->second.trigrams) {
            auto& postingList = invertedIndex_[tri];
            postingList.erase(
                std::remove(postingList.begin(), postingList.end(), fileIndex),
                postingList.end()
            );
            if (postingList.empty()) {
                invertedIndex_.erase(tri);
            }
        }
    }

    // Add to inverted index (sorted insertion for O(n+m) set_intersection)
    for (Trigram tri : trigrams) {
        auto& list = invertedIndex_[tri];
        auto pos = std::lower_bound(list.begin(), list.end(), fileIndex);
        if (pos == list.end() || *pos != fileIndex) {
            list.insert(pos, fileIndex);
        }
    }

    // Store file info
    ContentFileInfo info;
    info.fullPath = fullPath;
    info.contentHash = hash;
    info.trigrams = std::move(trigrams);
    info.lastModTime = modTime;
    fileInfos_[fileIndex] = std::move(info);

    return ContentIndexUpdate::Upserted;
}

void ContentIndex::removeFile(uint32_t fileIndex) {
    std::unique_lock lock(mutex_);
    removeFileInternal(fileIndex);
}

void ContentIndex::removeFileInternal(uint32_t fileIndex) {
    auto it = fileInfos_.find(fileIndex);
    if (it == fileInfos_.end()) return;

    // Remove from inverted index
    for (Trigram tri : it->second.trigrams) {
        auto invIt = invertedIndex_.find(tri);
        if (invIt != invertedIndex_.end()) {
            auto& postingList = invIt->second;
            postingList.erase(
                std::remove(postingList.begin(), postingList.end(), fileIndex),
                postingList.end()
            );
            if (postingList.empty()) {
                invertedIndex_.erase(invIt);
            }
        }
    }

    fileInfos_.erase(it);
}

void ContentIndex::remapFileIndices(const std::unordered_map<uint32_t, uint32_t>& remap) {
    std::unique_lock lock(mutex_);

    // Rebuild fileInfos_ with new indices
    std::unordered_map<uint32_t, ContentFileInfo> newFileInfos;
    for (auto& [oldIdx, info] : fileInfos_) {
        auto it = remap.find(oldIdx);
        if (it != remap.end()) {
            newFileInfos[it->second] = std::move(info);
        }
        // If not in remap, the record was tombstoned — drop it
    }
    fileInfos_ = std::move(newFileInfos);

    // Rebuild inverted index from scratch
    invertedIndex_.clear();
    for (auto& [fileIdx, info] : fileInfos_) {
        for (Trigram tri : info.trigrams) {
            auto& list = invertedIndex_[tri];
            auto pos = std::lower_bound(list.begin(), list.end(), fileIdx);
            if (pos == list.end() || *pos != fileIdx) {
                list.insert(pos, fileIdx);
            }
        }
    }
}

void ContentIndex::beginFileIndexRemap() {
    remapMutex_.lock();
    mappingGeneration_.fetch_add(1, std::memory_order_acq_rel);
}

bool ContentIndex::tryBeginFileIndexRemap() {
    if (!remapMutex_.try_lock()) return false;
    mappingGeneration_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

void ContentIndex::endFileIndexRemap() {
    mappingGeneration_.fetch_add(1, std::memory_order_release);
    remapMutex_.unlock();
}

uint32_t ContentIndex::pruneStaleEntries(const std::unordered_set<uint32_t>& validFileIndices) {
    std::unique_lock lock(mutex_);

    std::vector<uint32_t> toRemove;
    for (auto& [fileIdx, info] : fileInfos_) {
        if (validFileIndices.find(fileIdx) == validFileIndices.end()) {
            toRemove.push_back(fileIdx);
        }
    }

    for (uint32_t idx : toRemove) {
        removeFileInternal(idx);
    }

    return static_cast<uint32_t>(toRemove.size());
}

std::vector<std::pair<uint32_t, ContentFileInfo>> ContentIndex::removeByPathPrefix(
    const std::string& pathPrefix) {
    std::unique_lock lock(mutex_);
    std::vector<std::pair<uint32_t, ContentFileInfo>> removed;
    const std::string childPrefix = pathPrefix == "/" ? "/" : pathPrefix + "/";
    for (const auto& [fileIndex, info] : fileInfos_) {
        if (info.fullPath == pathPrefix || info.fullPath.rfind(childPrefix, 0) == 0) {
            removed.emplace_back(fileIndex, info);
        }
    }
    for (const auto& [fileIndex, _] : removed) {
        removeFileInternal(fileIndex);
    }
    return removed;
}

bool ContentIndex::isFileIndexed(uint32_t fileIndex) const {
    std::shared_lock lock(mutex_);
    return fileInfos_.count(fileIndex) > 0;
}

std::vector<uint32_t> ContentIndex::getIndexedFileIndices() const {
    std::shared_lock lock(mutex_);
    std::vector<uint32_t> result;
    result.reserve(fileInfos_.size());
    for (const auto& [idx, _] : fileInfos_) {
        result.push_back(idx);
    }
    return result;
}

void ContentIndex::insertFileInfo(uint32_t fileIndex, uint64_t contentHash,
                                  std::vector<Trigram>&& trigrams, time_t lastModTime,
                                  std::string fullPath) {
    std::unique_lock lock(mutex_);

    // Remove old if exists
    removeFileInternal(fileIndex);

    // Add to inverted index (sorted insertion for O(n+m) set_intersection)
    for (Trigram tri : trigrams) {
        auto& list = invertedIndex_[tri];
        auto pos = std::lower_bound(list.begin(), list.end(), fileIndex);
        if (pos == list.end() || *pos != fileIndex) {
            list.insert(pos, fileIndex);
        }
    }

    ContentFileInfo info;
    info.fullPath = std::move(fullPath);
    info.contentHash = contentHash;
    info.trigrams = std::move(trigrams);
    info.lastModTime = lastModTime;
    fileInfos_[fileIndex] = std::move(info);
}

bool ContentIndex::getFileInfo(uint32_t fileIndex, ContentFileInfo& info) const {
    std::shared_lock lock(mutex_);
    auto it = fileInfos_.find(fileIndex);
    if (it == fileInfos_.end()) return false;
    info = it->second;
    return true;
}

// --- Querying ---

std::vector<ContentMatch> ContentIndex::query(const std::string& keyword,
                                              uint32_t maxResults,
                                              const PathResolver& resolvePath) const {
    if (keyword.empty()) return {};

    std::string lowerKey = me::normalizeNFC(me::toLower(keyword));

    std::shared_lock lock(mutex_);
    const uint64_t maxVerificationBytes = maxFileSize_;

    std::vector<uint32_t> candidates;

    if (lowerKey.size() >= 3) {
        // Extract trigrams from keyword
        auto keyTrigrams = extractTrigrams(lowerKey);
        if (keyTrigrams.empty()) return {};

        // Find the trigram with the smallest posting list (for efficiency)
        const std::vector<uint32_t>* smallest = nullptr;
        for (Trigram tri : keyTrigrams) {
            auto it = invertedIndex_.find(tri);
            if (it == invertedIndex_.end()) {
                // This trigram doesn't exist in any file — no matches possible
                return {};
            }
            if (!smallest || it->second.size() < smallest->size()) {
                smallest = &it->second;
            }
        }

        if (!smallest) return {};

        // Collect all posting list pointers, sorted by size (smallest first)
        struct PostingRef {
            const std::vector<uint32_t>* list;
        };
        std::vector<PostingRef> postings;
        postings.reserve(keyTrigrams.size());
        for (Trigram tri : keyTrigrams) {
            auto it = invertedIndex_.find(tri);
            if (it == invertedIndex_.end()) return {};
            postings.push_back({&it->second});
        }
        std::sort(postings.begin(), postings.end(),
                  [](const PostingRef& a, const PostingRef& b) {
                      return a.list->size() < b.list->size();
                  });

        // Sorted set_intersection across all posting lists
        candidates.assign(postings[0].list->begin(), postings[0].list->end());
        std::vector<uint32_t> temp;
        for (size_t i = 1; i < postings.size() && !candidates.empty(); i++) {
            me::intersectSortedPostingLists(candidates, *postings[i].list, temp);
            candidates.swap(temp);
        }
    } else {
        // C-4 fix: Short keyword (<3 chars) cannot generate trigrams.
        // Returning all indexed files is prohibitively expensive (100k+ files).
        // Consistent with SearchEngine::query() which also requires min 2 chars.
        return {};
    }

    // Snapshot paths before releasing the index lock. fileIndex belongs to the
    // SearchEngine's compactable SoA and must not be resolved there after this
    // lock is released: a concurrent compaction can remap it. ContentFileInfo's
    // path is the stable identity used by persistence and survives remapping.
    struct VerificationCandidate {
        uint32_t fileIndex;
        std::string fullPath;
    };
    std::vector<VerificationCandidate> verificationCandidates;
    verificationCandidates.reserve(candidates.size());
    for (uint32_t fileIndex : candidates) {
        auto info = fileInfos_.find(fileIndex);
        if (info == fileInfos_.end()) continue;
        verificationCandidates.push_back({fileIndex, info->second.fullPath});
    }

    // Release shared lock before doing file I/O for verification.
    lock.unlock();

    std::vector<ContentMatch> results;
    constexpr size_t kVerificationBatchSize = 32;
    results.reserve(std::min<size_t>(verificationCandidates.size(),
        maxResults == 0 ? verificationCandidates.size() : maxResults));

    for (size_t base = 0; base < verificationCandidates.size(); base += kVerificationBatchSize) {
        if (maxResults > 0 && results.size() >= maxResults) break;

        const size_t count = std::min(kVerificationBatchSize,
                                      verificationCandidates.size() - base);
        struct VerificationBatch {
            explicit VerificationBatch(size_t size) : matches(size), valid(size, 0) {}
            std::vector<ContentMatch> matches;
            std::vector<uint8_t> valid;
        };
        auto batch = std::make_shared<VerificationBatch>(count);
        auto verifyCandidate = [&](size_t i) {
            const auto& candidate = verificationCandidates[base + i];
            const uint32_t fileIdx = candidate.fileIndex;
            std::string fullPath = candidate.fullPath;
            if (!resolvePath || !resolvePath(fileIdx, fullPath)) return;
            if (fullPath.empty()) return;

            uint32_t offset = 0;
            std::string snippet = generateSnippet(
                fullPath, keyword, offset, 80, maxVerificationBytes);
            if (snippet.empty()) return;

            batch->matches[i].fileIndex = fileIdx;
            batch->matches[i].snippet = std::move(snippet);
            batch->matches[i].matchOffset = offset;
            batch->valid[i] = 1;
        };
        using VerifyCandidate = decltype(verifyCandidate);
        dispatch_apply_f(count, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
                         &verifyCandidate, [](void* context, size_t i) {
            (*static_cast<VerifyCandidate*>(context))(i);
        });

        for (size_t i = 0; i < count; ++i) {
            if (!batch->valid[i]) continue;
            results.push_back(std::move(batch->matches[i]));
            if (maxResults > 0 && results.size() >= maxResults) break;
        }
    }

    return results;
}

// --- Stats ---

uint32_t ContentIndex::indexedFileCount() const {
    std::shared_lock lock(mutex_);
    return static_cast<uint32_t>(fileInfos_.size());
}

// --- Persistence ---

static bool readU32(FILE* f, uint32_t& v) {
    return fread(&v, sizeof(uint32_t), 1, f) == 1;
}

static bool readU64(FILE* f, uint64_t& v) {
    return fread(&v, sizeof(uint64_t), 1, f) == 1;
}

bool ContentIndex::saveToFile(const std::string& path) const {
    std::shared_lock lock(mutex_);

    std::string tmpPath = path + ".tmp";
    FILE* f = fopen(tmpPath.c_str(), "wb");
    if (!f) return false;

    bool ok = true;
    auto safeWrite = [&](const void* ptr, size_t size, size_t count) {
        if (ok && fwrite(ptr, size, count, f) != count) ok = false;
    };

    // Header
    safeWrite(CONTENT_MAGIC, 1, 4);
    uint32_t version = CONTENT_FORMAT_VERSION;
    safeWrite(&version, sizeof(uint32_t), 1);

    safeWrite(&maxFileSize_, sizeof(uint64_t), 1);
    uint32_t extensionCount = static_cast<uint32_t>(extensions_.size());
    safeWrite(&extensionCount, sizeof(uint32_t), 1);
    for (const auto& ext : extensions_) {
        uint32_t length = static_cast<uint32_t>(ext.size());
        safeWrite(&length, sizeof(uint32_t), 1);
        if (length > 0) safeWrite(ext.data(), 1, length);
    }

    // File count
    uint32_t fileCount = static_cast<uint32_t>(fileInfos_.size());
    safeWrite(&fileCount, sizeof(uint32_t), 1);

    // Per-file: fallback fileIndex + stable path + content metadata.
    for (const auto& [fileIndex, info] : fileInfos_) {
        if (!ok) break;
        safeWrite(&fileIndex, sizeof(uint32_t), 1);
        uint32_t pathLen = static_cast<uint32_t>(info.fullPath.size());
        safeWrite(&pathLen, sizeof(uint32_t), 1);
        if (pathLen > 0) safeWrite(info.fullPath.data(), 1, pathLen);
        safeWrite(&info.contentHash, sizeof(uint64_t), 1);
        uint32_t triCount = static_cast<uint32_t>(info.trigrams.size());
        safeWrite(&triCount, sizeof(uint32_t), 1);
        if (triCount > 0) {
            safeWrite(info.trigrams.data(), sizeof(Trigram), triCount);
        }
        // V2: write lastModTime as int64_t for cross-platform consistency
        int64_t modTime = static_cast<int64_t>(info.lastModTime);
        safeWrite(&modTime, sizeof(int64_t), 1);
    }

    if (ok && (fflush(f) != 0 || fsync(fileno(f)) != 0)) ok = false;
    if (fclose(f) != 0) ok = false;

    if (!ok) {
        remove(tmpPath.c_str());
        return false;
    }

    if (rename(tmpPath.c_str(), path.c_str()) != 0) {
        remove(tmpPath.c_str());
        return false;
    }
    return PathUtils::syncParentDirectory(path);
}

bool ContentIndex::loadFromFile(const std::string& path, const IndexResolver& resolveIndex) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    // Verify magic
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, CONTENT_MAGIC, 4) != 0) {
        fclose(f);
        return false;
    }

    uint32_t version;
    if (!readU32(f, version) || version != CONTENT_FORMAT_VERSION) {
        fclose(f);
        return false;
    }

    uint64_t loadedMaxFileSize = 0;
    if (!readU64(f, loadedMaxFileSize) || loadedMaxFileSize == 0 ||
        loadedMaxFileSize > 100ULL * 1024 * 1024) {
        fclose(f);
        return false;
    }
    uint32_t extensionCount = 0;
    if (!readU32(f, extensionCount) || extensionCount > 10000) {
        fclose(f);
        return false;
    }
    std::unordered_set<std::string> loadedExtensions;
    for (uint32_t i = 0; i < extensionCount; ++i) {
        uint32_t length = 0;
        if (!readU32(f, length) || length == 0 || length > 64) {
            fclose(f);
            return false;
        }
        std::string ext(length, '\0');
        if (fread(ext.data(), 1, length, f) != length) {
            fclose(f);
            return false;
        }
        loadedExtensions.insert(std::move(ext));
    }

    uint32_t fileCount;
    if (!readU32(f, fileCount)) {
        fclose(f);
        return false;
    }

    long entriesStart = ftell(f);
    if (entriesStart < 0 || fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long fileEnd = ftell(f);
    if (fileEnd < entriesStart || fseek(f, entriesStart, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    constexpr uint64_t kMinimumEntryBytes = 4 + 4 + 8 + 4 + 8;
    constexpr uint32_t kMaxReasonableCount = 10'000'000;
    uint64_t remainingBytes = static_cast<uint64_t>(fileEnd - entriesStart);
    if (fileCount > kMaxReasonableCount ||
        static_cast<uint64_t>(fileCount) > remainingBytes / kMinimumEntryBytes) {
        std::cerr << "[ContentIndex] Corrupt index: fileCount=" << fileCount << " exceeds limit\n";
        fclose(f);
        return false;
    }

    // Validate the complete entry layout and estimate the expanded in-memory
    // representation before allocating vectors and hash tables. Each trigram is
    // retained both per file and in a posting list, with allocator overhead.
    const uint64_t loadMemoryBudget = contentIndexLoadMemoryBudget();
    uint64_t estimatedMemory = 0;
    for (uint32_t i = 0; i < fileCount; ++i) {
        uint32_t ignoredIndex = 0;
        uint32_t pathLen = 0;
        if (!readU32(f, ignoredIndex) || !readU32(f, pathLen) || pathLen > 1024 * 1024) {
            fclose(f);
            return false;
        }
        if (fseek(f, static_cast<long>(pathLen), SEEK_CUR) != 0) {
            fclose(f);
            return false;
        }
        uint64_t ignoredHash = 0;
        uint32_t triCount = 0;
        if (!readU64(f, ignoredHash) || !readU32(f, triCount) || triCount > 1'000'000) {
            fclose(f);
            return false;
        }
        const uint64_t entryMemory = kEstimatedFileInfoBytes + pathLen +
            static_cast<uint64_t>(triCount) * kEstimatedBytesPerTrigram;
        if (entryMemory > loadMemoryBudget - estimatedMemory) {
            std::cerr << "[ContentIndex] Index exceeds load memory budget ("
                      << (loadMemoryBudget >> 20) << " MB)\n";
            fclose(f);
            return false;
        }
        estimatedMemory += entryMemory;
        const uint64_t trigramBytes = static_cast<uint64_t>(triCount) * sizeof(Trigram);
        long current = ftell(f);
        if (current < 0 || current > fileEnd || trigramBytes + sizeof(int64_t) >
                static_cast<uint64_t>(fileEnd - current) ||
            fseek(f, static_cast<long>(trigramBytes + sizeof(int64_t)), SEEK_CUR) != 0) {
            fclose(f);
            return false;
        }
    }
    if (fseek(f, entriesStart, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    // Read all file entries
    std::unordered_map<uint32_t, ContentFileInfo> newFileInfos;
    std::unordered_map<Trigram, std::vector<uint32_t>> newInvertedIndex;
    newFileInfos.reserve(fileCount);

    for (uint32_t i = 0; i < fileCount; i++) {
        uint32_t fileIndex;
        uint32_t pathLen;
        uint64_t contentHash;
        uint32_t triCount;

        if (!readU32(f, fileIndex) || !readU32(f, pathLen) || pathLen > 1024 * 1024) {
            fclose(f);
            return false;
        }
        std::string fullPath(pathLen, '\0');
        if (pathLen > 0 && fread(fullPath.data(), 1, pathLen, f) != pathLen) {
            fclose(f);
            return false;
        }
        if (!readU64(f, contentHash) || !readU32(f, triCount)) {
            fclose(f);
            return false;
        }

        // Sanity limit
        if (triCount > 1000000) {
            fclose(f);
            return false;
        }

        std::vector<Trigram> trigrams(triCount);
        if (triCount > 0 && fread(trigrams.data(), sizeof(Trigram), triCount, f) != triCount) {
            fclose(f);
            return false;
        }

        // V3 retains the V2 modification time field.
        time_t lastModTime = 0;
        {
            int64_t modTime64;
            if (fread(&modTime64, sizeof(int64_t), 1, f) != 1) {
                fclose(f);
                return false;
            }
            lastModTime = static_cast<time_t>(modTime64);
        }

        ContentFileInfo info;
        info.fullPath = fullPath;
        info.contentHash = contentHash;
        info.trigrams = std::move(trigrams);
        info.lastModTime = lastModTime;
        uint32_t resolvedIndex = fileIndex;
        if (resolveIndex) {
            resolvedIndex = resolveIndex(fullPath);
            if (resolvedIndex == UINT32_MAX) continue;
        }
        newFileInfos[resolvedIndex] = std::move(info);
        for (Trigram tri : newFileInfos[resolvedIndex].trigrams) {
            newInvertedIndex[tri].push_back(resolvedIndex);
        }
    }

    fclose(f);

    // Sort all posting lists for binary search during query
    for (auto& [tri, list] : newInvertedIndex) {
        std::sort(list.begin(), list.end());
    }

    // Swap into live data
    std::unique_lock lock(mutex_);
    invertedIndex_ = std::move(newInvertedIndex);
    fileInfos_ = std::move(newFileInfos);
    extensions_ = std::move(loadedExtensions);
    maxFileSize_ = loadedMaxFileSize;

    return true;
}

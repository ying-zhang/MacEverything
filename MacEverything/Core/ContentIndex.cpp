#include "ContentIndex.h"
#include "Logger.h"
#include "StringUtils.h"
#include "SIMDSearch.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cctype>
#include <cstring>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <dispatch/dispatch.h>
#include <CoreFoundation/CoreFoundation.h>

// --- Magic and version for binary persistence ---
static constexpr char CONTENT_MAGIC[4] = {'M', 'E', 'C', 'I'};
static constexpr uint32_t CONTENT_FORMAT_VERSION = 2;

ContentIndex::ContentIndex() {
    // No default extensions — content indexing is opt-in.
    // Users must configure extensions via Content Settings.
}

// --- Configuration ---

void ContentIndex::setExtensions(const std::vector<std::string>& exts) {
    std::unique_lock lock(mutex_);
    extensions_.clear();
    for (const auto& ext : exts) {
        extensions_.insert(me::toLower(ext));
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
    if (text.size() < 3) return {};

    // C-1: thread_local bitmap avoids 2MB allocation per call.
    // Only the dirty bits are cleared each call — O(unique_trigrams) instead of O(2^24).
    static constexpr size_t kBitmapSize = 1 << 24;
    thread_local std::vector<bool> seen(kBitmapSize, false);
    thread_local std::vector<Trigram> dirty;

    for (auto t : dirty) seen[t] = false;
    dirty.clear();

    std::vector<Trigram> result;

    for (size_t i = 0; i + 2 < text.size(); i++) {
        uint8_t a = me_ascii::kLowerTable[static_cast<uint8_t>(text[i])];
        uint8_t b = me_ascii::kLowerTable[static_cast<uint8_t>(text[i + 1])];
        uint8_t c = me_ascii::kLowerTable[static_cast<uint8_t>(text[i + 2])];
        Trigram t = makeTrigram(a, b, c);
        if (!seen[t]) {
            seen[t] = true;
            dirty.push_back(t);
            result.push_back(t);
        }
    }

    return result;
}

// --- Snippet generation ---

std::string ContentIndex::generateSnippet(const std::string& path,
                                           const std::string& keyword,
                                           uint32_t& outOffset,
                                           uint32_t contextChars) {
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
    size_t maxRead = std::min(static_cast<size_t>(fileSize), size_t(1024 * 1024));
    size_t overlapSize = keyword.size() > 1 ? keyword.size() - 1 : 0;

    // Pre-compute lowercase keyword once (SIMD for ASCII)
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

        // Lowercase the chunk using SIMD (ASCII fast-path; non-ASCII bytes pass through)
        lowerChunk.resize(bytesRead);
        std::memcpy(lowerChunk.data(), chunk.data(), bytesRead);
        me::simdToLowerAscii(lowerChunk.data(), bytesRead);

        size_t pos = me::simdFind(lowerChunk.data(), bytesRead, lowerKey.data(), lowerKey.size());
        if (pos < bytesRead) {
            globalMatchPos = fileOffset + pos;
            matchContent = std::move(chunk);
            matchContentOffset = fileOffset;
            break;
        }

        // Advance with overlap to catch keywords spanning chunk boundaries
        if (bytesRead < kChunkSize) break; // last chunk, no more data
        size_t advance = bytesRead - overlapSize;
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

bool ContentIndex::indexFile(uint32_t fileIndex, const std::string& fullPath, time_t modTime) {
    // Check extension (read lock for config)
    {
        std::shared_lock lock(mutex_);
        // Extract filename from path
        size_t lastSlash = fullPath.rfind('/');
        std::string filename = (lastSlash != std::string::npos) ? fullPath.substr(lastSlash + 1) : fullPath;
        if (extensions_.empty() || !hasAllowedExtensionLocked(filename)) return false;
    }

    // Early exit: if modTime unchanged, skip expensive I/O
    if (modTime > 0) {
        std::shared_lock lock(mutex_);
        auto it = fileInfos_.find(fileIndex);
        if (it != fileInfos_.end() && it->second.lastModTime == modTime) {
            return false; // file not modified
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
        // File unreadable (deleted/binary/too large) but already indexed:
        // update lastModTime so future runs skip I/O via early exit
        if (modTime > 0) {
            std::shared_lock lock(mutex_);
            auto it = fileInfos_.find(fileIndex);
            if (it != fileInfos_.end() && it->second.lastModTime != modTime) {
                lock.unlock();
                std::unique_lock wlock(mutex_);
                auto wit = fileInfos_.find(fileIndex);
                if (wit != fileInfos_.end()) {
                    wit->second.lastModTime = modTime;
                }
                return true; // signal WAL persist
            }
        }
        return false;
    }

    uint64_t hash = hashContent(content);
    auto trigrams = extractTrigrams(content);

    // Check if already indexed with same hash
    {
        std::shared_lock lock(mutex_);
        auto it = fileInfos_.find(fileIndex);
        if (it != fileInfos_.end() && it->second.contentHash == hash) {
            // Content unchanged — update lastModTime so future runs can skip I/O
            if (modTime > 0 && it->second.lastModTime != modTime) {
                lock.unlock();
                std::unique_lock wlock(mutex_);
                auto wit = fileInfos_.find(fileIndex);
                if (wit != fileInfos_.end()) {
                    wit->second.lastModTime = modTime;
                }
                return true; // signal caller to persist updated modTime
            }
            return false; // already up-to-date, no change made
        }
    }

    // Update index (exclusive lock)
    std::unique_lock lock(mutex_);

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
    info.contentHash = hash;
    info.trigrams = std::move(trigrams);
    info.lastModTime = modTime;
    fileInfos_[fileIndex] = std::move(info);

    return true;
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

void ContentIndex::insertFileInfo(uint32_t fileIndex, uint64_t contentHash, std::vector<Trigram>&& trigrams, time_t lastModTime) {
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

std::vector<ContentMatch> ContentIndex::query(const std::string& keyword, uint32_t maxResults) const {
    if (keyword.empty()) return {};

    std::string lowerKey = me::toLower(keyword);

    std::shared_lock lock(mutex_);

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
            temp.clear();
            std::set_intersection(candidates.begin(), candidates.end(),
                                  postings[i].list->begin(), postings[i].list->end(),
                                  std::back_inserter(temp));
            candidates.swap(temp);
        }
    } else {
        // C-4 fix: Short keyword (<3 chars) cannot generate trigrams.
        // Returning all indexed files is prohibitively expensive (100k+ files).
        // Consistent with SearchEngine::query() which also requires min 2 chars.
        return {};
    }

    // Release shared lock before doing file I/O for verification
    lock.unlock();

    // Verify candidates by actually reading files and generate snippets
    // Use dispatch_apply for parallelism
    std::vector<ContentMatch> results;
    std::mutex resultMutex;
    std::atomic<uint32_t> found{0};

    unsigned numThreads = std::min(static_cast<unsigned>(candidates.size()),
                                    std::max(1u, std::thread::hardware_concurrency()));
    if (numThreads > 16) numThreads = 16;

    // We need the SearchEngine to resolve fileIndex → path.
    // Since we can't access SearchEngine from here, the bridge layer will need to
    // provide path resolution. For now, store the candidates and let the bridge
    // layer do verification + snippet generation.
    //
    // Actually, we return candidates as ContentMatch with empty snippets.
    // The bridge layer fills in snippets using SearchEngine::getRecord().

    for (uint32_t fileIdx : candidates) {
        if (maxResults > 0 && found.load(std::memory_order_relaxed) >= maxResults) break;

        ContentMatch match;
        match.fileIndex = fileIdx;
        match.matchOffset = 0;
        // snippet will be filled by the bridge layer which has access to file paths
        results.push_back(std::move(match));
        found.fetch_add(1, std::memory_order_relaxed);
    }

    return results;
}

// --- Stats ---

uint32_t ContentIndex::indexedFileCount() const {
    std::shared_lock lock(mutex_);
    return static_cast<uint32_t>(fileInfos_.size());
}

// --- Persistence ---

static bool writeU32(FILE* f, uint32_t v) {
    return fwrite(&v, sizeof(uint32_t), 1, f) == 1;
}

static bool writeU64(FILE* f, uint64_t v) {
    return fwrite(&v, sizeof(uint64_t), 1, f) == 1;
}

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

    // File count
    uint32_t fileCount = static_cast<uint32_t>(fileInfos_.size());
    safeWrite(&fileCount, sizeof(uint32_t), 1);

    // Per-file: fileIndex(4) + contentHash(8) + trigramCount(4) + trigrams(4 each) + lastModTime(8)
    for (const auto& [fileIndex, info] : fileInfos_) {
        if (!ok) break;
        safeWrite(&fileIndex, sizeof(uint32_t), 1);
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

    // H-2: fsync before close to ensure data reaches disk before rename
    if (ok) fsync(fileno(f));
    fclose(f);

    if (!ok) {
        remove(tmpPath.c_str());
        return false;
    }

    if (rename(tmpPath.c_str(), path.c_str()) != 0) {
        remove(tmpPath.c_str());
        return false;
    }
    return true;
}

bool ContentIndex::loadFromFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    // Verify magic
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, CONTENT_MAGIC, 4) != 0) {
        fclose(f);
        return false;
    }

    uint32_t version;
    if (!readU32(f, version) || version > CONTENT_FORMAT_VERSION) {
        fclose(f);
        return false;
    }

    uint32_t fileCount;
    if (!readU32(f, fileCount)) {
        fclose(f);
        return false;
    }

    constexpr uint32_t kMaxReasonableCount = 50'000'000;
    if (fileCount > kMaxReasonableCount) {
        std::cerr << "[ContentIndex] Corrupt index: fileCount=" << fileCount << " exceeds limit\n";
        fclose(f);
        return false;
    }

    // Read all file entries
    std::unordered_map<uint32_t, ContentFileInfo> newFileInfos;
    std::unordered_map<Trigram, std::vector<uint32_t>> newInvertedIndex;
    newFileInfos.reserve(fileCount);

    for (uint32_t i = 0; i < fileCount; i++) {
        uint32_t fileIndex;
        uint64_t contentHash;
        uint32_t triCount;

        if (!readU32(f, fileIndex) || !readU64(f, contentHash) || !readU32(f, triCount)) {
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

        // V2: read lastModTime (version 1 compat: default to 0)
        time_t lastModTime = 0;
        if (version >= 2) {
            int64_t modTime64;
            if (fread(&modTime64, sizeof(int64_t), 1, f) != 1) {
                fclose(f);
                return false;
            }
            lastModTime = static_cast<time_t>(modTime64);
        }

        // H4 fix: Use push_back during bulk load — O(1) per insert instead of
        // O(N) sorted insertion. Final sort+dedup happens below after all entries.
        for (Trigram tri : trigrams) {
            newInvertedIndex[tri].push_back(fileIndex);
        }

        ContentFileInfo info;
        info.contentHash = contentHash;
        info.trigrams = std::move(trigrams);
        info.lastModTime = lastModTime;
        newFileInfos[fileIndex] = std::move(info);
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

    return true;
}

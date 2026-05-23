#include "SearchEngine.h"
#include "StringUtils.h"
#include "CompiledGlob.h"
#include "Logger.h"
#include "QueryTokenizer.h"
#include "QueryParser.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <dispatch/dispatch.h>

namespace {

bool containsNonAscii(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 128) return true;
    }
    return false;
}

std::string alternateUnicodeNormalization(const std::string& s) {
    if (!containsNonAscii(s)) return {};

    const auto nfc = me::normalizeNFC(s);
    if (nfc != s) return nfc;

    const auto nfd = me::normalizeNFD(s);
    if (nfd != s) return nfd;

    return {};
}

void mergeQueryResults(std::vector<uint32_t>& primary,
                       const std::vector<uint32_t>& secondary,
                       uint32_t maxResults) {
    if (secondary.empty()) return;

    std::unordered_set<uint32_t> seen;
    seen.reserve(primary.size() + secondary.size());
    for (uint32_t idx : primary) seen.insert(idx);

    for (uint32_t idx : secondary) {
        if (seen.insert(idx).second) {
            primary.push_back(idx);
            if (maxResults > 0 && primary.size() >= maxResults) break;
        }
    }
}

uint32_t normalizationQueryLimit(uint32_t maxResults) {
    if (maxResults == 0) return 0;
    constexpr uint32_t kMinNormalizationMergeLimit = 1000;
    return std::max(maxResults, kMinNormalizationMergeLimit);
}

std::string quoteAliasTerm(const std::string& term) {
    if (term.find_first_of(" \t|!<>\"") == std::string::npos) return term;
    std::string quoted = "\"";
    for (char c : term) {
        if (c != '"') quoted += c;
    }
    quoted += '"';
    return quoted;
}

std::string expandSystemAppAliasQuery(const std::string& original,
                                      const std::string& lower) {
    struct Alias {
        const char* zh;
        std::initializer_list<const char*> names;
    };
    static const Alias aliases[] = {
        {"终端", {"terminal"}},
        {"活动监视器", {"activity monitor"}},
        {"磁盘工具", {"disk utility"}},
        {"系统设置", {"system settings", "system preferences"}},
        {"系统偏好设置", {"system preferences", "system settings"}},
        {"访达", {"finder"}},
        {"文本编辑", {"textedit"}},
        {"预览", {"preview"}},
        {"计算器", {"calculator"}},
        {"日历", {"calendar"}},
        {"通讯录", {"contacts"}},
        {"邮件", {"mail"}},
        {"信息", {"messages"}},
        {"照片", {"photos"}},
        {"音乐", {"music"}},
        {"地图", {"maps"}},
        {"备忘录", {"notes"}},
        {"提醒事项", {"reminders"}},
        {"图书", {"books"}},
        {"播客", {"podcasts"}},
        {"语音备忘录", {"voice memos"}},
        {"快捷指令", {"shortcuts"}},
        {"自动操作", {"automator"}},
        {"脚本编辑器", {"script editor"}},
        {"控制台", {"console"}},
        {"钥匙串访问", {"keychain access"}},
        {"字体册", {"font book"}},
        {"屏幕共享", {"screen sharing"}},
        {"截屏", {"screenshot"}},
        {"数码测色计", {"digital color meter"}},
        {"迁移助理", {"migration assistant"}},
        {"时间机器", {"time machine"}},
        {"启动台", {"launchpad"}},
        {"国际象棋", {"chess"}},
        {"图像捕捉", {"image capture"}},
        {"快速播放器", {"quicktime player"}},
        {"应用商店", {"app store"}}
    };

    for (const auto& alias : aliases) {
        if (lower != alias.zh) continue;
        std::string expanded = "<" + quoteAliasTerm(original);
        for (const char* name : alias.names) {
            expanded += "|";
            expanded += quoteAliasTerm(name);
        }
        expanded += ">";
        return expanded;
    }
    return original;
}

bool isSimpleSystemAppAliasQuery(const std::string& query) {
    return query.find_first_of(" \t|!<>\"/\\:*?") == std::string::npos;
}

std::string stripAppExtension(std::string_view name) {
    constexpr std::string_view ext = ".app";
    if (name.size() >= ext.size() &&
        name.substr(name.size() - ext.size()) == ext) {
        name.remove_suffix(ext.size());
    }
    return std::string(name);
}

} // namespace

// ---------------------------------------------------------------------------
// Match priority
// ---------------------------------------------------------------------------

uint8_t SearchEngine::namePriority(const char* nameData, uint16_t nameLen,
                                   const char* keyData, size_t keyLen) {
    if (nameLen == keyLen && memcmp(nameData, keyData, nameLen) == 0)
        return 0; // exact match
    if (nameLen >= keyLen && memcmp(nameData, keyData, keyLen) == 0)
        return 1; // starts with
    return 2; // contains
}

// ---------------------------------------------------------------------------
// Full path buffer construction
// ---------------------------------------------------------------------------

size_t SearchEngine::buildFullPathBuf(std::vector<char>& buf,
                                      const char* pathData, uint16_t pathLen,
                                      const char* nameData, uint16_t nameLen) {
    size_t fullLen = static_cast<size_t>(pathLen) + 1 + nameLen;
    if (buf.size() < fullLen) buf.resize(fullLen * 2);
    memcpy(buf.data(), pathData, pathLen);
    buf[pathLen] = '/';
    memcpy(buf.data() + pathLen + 1, nameData, nameLen);
    return fullLen;
}

// ---------------------------------------------------------------------------
// Glob matching
// ---------------------------------------------------------------------------

bool SearchEngine::globMatch(const std::string& pattern, const std::string& text) {
    return globMatchImpl(pattern, text);
}

// ---------------------------------------------------------------------------
// queryDirList: DIR_LIST mode — find directory, return its children
// ---------------------------------------------------------------------------
void SearchEngine::queryDirList(const ParsedQuery& pq,
                                size_t totalSize, const QueryCancelCtx& cancel,
                                std::vector<Match>& merged) const {
    const auto& dirName = pq.namePattern;
    if (dirName.empty()) return;

    // Find directory records whose name exactly matches dirName
    std::vector<uint32_t> dirIndices;

    if (dirName.size() >= 3 && !nameTrigramIndex_.empty()) {
        bool allFound = false;
        auto candidates = intersectPostingLists(nameTrigramIndex_, dirName, allFound);
        if (allFound && candidates.size() <= totalSize / 4) {
            for (uint32_t idx : candidates) {
                if (types_[idx] != 2) continue; // must be directory
                const char* nd = namePool_.data(idx);
                uint16_t nl = namePool_.length(idx);
                if (nl != dirName.size()) continue;
                if (std::memcmp(nd, dirName.data(), nl) != 0) continue;
                // Check path constraints
                if (!pq.pathSegments.empty()) {
                    std::string dirPath = lowerPathStr(pathPool_, pathIndices_[idx]);
                    if (!pathSegmentsMatch(dirPath, pq.pathSegments)) continue;
                }
                dirIndices.push_back(idx);
            }
        } else {
            // Fallback: linear scan for directories
            for (size_t i = 0; i < totalSize; i++) {
                if (types_[i] != 2) continue;
                const char* nd = namePool_.data(i);
                uint16_t nl = namePool_.length(i);
                if (nl != dirName.size()) continue;
                if (std::memcmp(nd, dirName.data(), nl) != 0) continue;
                if (!pq.pathSegments.empty()) {
                    std::string dirPath = lowerPathStr(pathPool_, pathIndices_[i]);
                    if (!pathSegmentsMatch(dirPath, pq.pathSegments)) continue;
                }
                dirIndices.push_back(static_cast<uint32_t>(i));
            }
        }
    } else {
        // Short name or no trigram index — linear scan
        for (size_t i = 0; i < totalSize; i++) {
            if (types_[i] != 2) continue;
            const char* nd = namePool_.data(i);
            uint16_t nl = namePool_.length(i);
            if (nl != dirName.size()) continue;
            if (std::memcmp(nd, dirName.data(), nl) != 0) continue;
            if (!pq.pathSegments.empty()) {
                std::string dirPath = lowerPathStr(pathPool_, pathIndices_[i]);
                if (!pathSegmentsMatch(dirPath, pq.pathSegments)) continue;
            }
            dirIndices.push_back(static_cast<uint32_t>(i));
        }
    }

    if (dirIndices.empty()) return;

    // For each matching directory, find its children via pathLookup_ + pathIdxToRecords_
    for (uint32_t dirIdx : dirIndices) {
        if (cancel.cancelled()) return;

        // Build the full directory path: parentPath + "/" + dirName
        std::string parentPath = pathPool_.str(pathIndices_[dirIdx]);
        std::string fullDirPath = parentPath;
        if (!fullDirPath.empty() && fullDirPath.back() != '/') fullDirPath += '/';
        fullDirPath += std::string(namePool_.data(dirIdx), namePool_.length(dirIdx));

        // Look up this full path in pathLookup_ to find its pathPool index
        auto it = pathLookup_.find(fullDirPath);
        if (it == pathLookup_.end()) continue;

        uint32_t childPathIdx = it->second;
        if (childPathIdx < pathIdxToRecords_.size()) {
            const auto& childRecords = pathIdxToRecords_[childPathIdx];
            for (uint32_t childIdx : childRecords) {
                if (types_[childIdx] == 0) continue; // skip tombstones
                uint16_t nl = namePool_.length(childIdx);
                uint8_t priority = 2; // children are all "contains" priority
                uint32_t pLen = static_cast<uint32_t>(pathPool_.length(pathIndices_[childIdx]) + 1 + nl);
                merged.push_back({childIdx, priority, pLen});
            }
        } else {
            for (uint32_t childIdx = 0; childIdx < totalSize; childIdx++) {
                if (types_[childIdx] == 0) continue;
                if (pathIndices_[childIdx] != childPathIdx) continue;
                uint16_t nl = namePool_.length(childIdx);
                uint8_t priority = 2;
                uint32_t pLen = static_cast<uint32_t>(pathPool_.length(pathIndices_[childIdx]) + 1 + nl);
                merged.push_back({childIdx, priority, pLen});
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Query preprocessing — normalise raw user input before routing.
// All transformations that should apply to every query path go here.
// Returns both original-case and pre-lowered text to avoid redundant lowering.
// ---------------------------------------------------------------------------

struct PreprocessedQuery {
    std::string original;  // after trim + tilde expansion (original case)
    std::string lower;     // me::toLower(original) — single canonical lowering
};

static PreprocessedQuery preprocessQuery(const std::string& raw) {
    // 0) Strip leading/trailing whitespace
    auto start = raw.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = raw.find_last_not_of(" \t\r\n");
    std::string result = raw.substr(start, end - start + 1);

    // 1) Expand leading ~/ to the user's home directory so that patterns like
    //    ~/*/*.txt match absolute indexed paths (e.g. /Users/username/Downloads/f1.txt).
    //    A bare "~" remains a literal search term.
    if (result.size() >= 2 && result[0] == '~' && result[1] == '/') {
        const char* home = std::getenv("HOME");
        if (home) {
            result = std::string(home) + result.substr(1);
        }
    }

    // 2) Compute canonical lowercase once — eliminates redundant me::toLower()
    //    calls in parseQuery(), transformSlashTerms(), and makeTerm().
    auto lower = me::toLower(result);
    if (isSimpleSystemAppAliasQuery(result)) {
        auto base = stripAppExtension(lower);
        auto originalBase = stripAppExtension(result);
        auto expanded = expandSystemAppAliasQuery(originalBase, base);
        if (expanded != originalBase) {
            result = std::move(expanded);
            lower = me::toLower(result);
        }
    }
    return { result, lower };
}

// ---------------------------------------------------------------------------
// Main query() entry points
// ---------------------------------------------------------------------------

std::vector<uint32_t> SearchEngine::query(const std::string& keyword, uint32_t maxResults,
                                          bool useTrigram, uint64_t sessionId) const {
    QueryTimingInfo unused;
    return query(keyword, maxResults, useTrigram, unused, sessionId);
}

std::vector<uint32_t> SearchEngine::query(const std::string& keyword, uint32_t maxResults,
                                          bool useTrigram, QueryTimingInfo& timing,
                                          uint64_t sessionId) const {
    // Acquire per-session generation so only same-session queries cancel each other.
    auto [genAtom, myGen] = acquireSessionGeneration(sessionId);

    if (keyword.empty()) return {};

    auto pq = preprocessQuery(keyword);
    if (pq.original.empty()) return {};

    // Check for DIR_LIST mode: /path/* queries list directory children directly
    auto parsedQuery = parseQuery(pq.original, pq.lower);
    if (parsedQuery.mode == QueryMode::DIR_LIST) {
        auto queryStart = std::chrono::steady_clock::now();
        auto beforeLock = std::chrono::steady_clock::now();
        std::shared_lock lock(mutex_);
        auto afterLock = std::chrono::steady_clock::now();

        if (types_.empty()) return {};
        size_t totalSize = types_.size();

        std::vector<Match> merged;
        QueryCancelCtx cancel{genAtom.get(), myGen};
        queryDirList(parsedQuery, totalSize, cancel, merged);

        if (genAtom->load(std::memory_order_relaxed) != myGen) return {};

        auto beforeUnlock = std::chrono::steady_clock::now();
        lock.unlock();

        // Sort by priority then path length
        auto beforeSort = std::chrono::steady_clock::now();
        auto cmp = [](const Match& a, const Match& b) {
            if (a.priority != b.priority) return a.priority < b.priority;
            return a.pathLen < b.pathLen;
        };
        size_t resultCount = merged.size();
        if (maxResults > 0 && resultCount > maxResults) resultCount = maxResults;
        if (resultCount < merged.size()) {
            std::partial_sort(merged.begin(), merged.begin() + resultCount, merged.end(), cmp);
        } else {
            std::sort(merged.begin(), merged.end(), cmp);
        }
        auto afterSort = std::chrono::steady_clock::now();

        std::vector<uint32_t> result;
        result.reserve(resultCount);
        for (size_t i = 0; i < resultCount; i++) {
            result.push_back(merged[i].idx);
        }

        // Populate timing
        auto toMs = [](auto dur) { return std::chrono::duration<double, std::milli>(dur).count(); };
        timing.totalMs = toMs(std::chrono::steady_clock::now() - queryStart);
        timing.lockWaitMs = toMs(afterLock - beforeLock);
        timing.lockHeldMs = toMs(beforeUnlock - afterLock);
        timing.sortMs = toMs(afterSort - beforeSort);
        timing.totalRecords = totalSize;
        timing.resultCount = result.size();
        timing.searchPath = "dir-list";
        return result;
    }

    // All non-DIR_LIST queries go through the unified Advanced path.
    auto alternate = alternateUnicodeNormalization(pq.original);
    uint32_t internalMaxResults = alternate.empty()
        ? maxResults
        : normalizationQueryLimit(maxResults);

    auto result = queryAdvanced(pq.original, internalMaxResults, useTrigram, timing, myGen, genAtom.get());
    if (genAtom->load(std::memory_order_relaxed) != myGen) return {};

    // macOS often stores filenames as decomposed Unicode while users type NFC.
    // Retry the alternate normalization on demand without adding a second index.
    if (!alternate.empty()) {
        QueryTimingInfo altTiming;
        auto altResult = queryAdvanced(alternate, internalMaxResults, useTrigram,
                                       altTiming, myGen, genAtom.get());
        if (genAtom->load(std::memory_order_relaxed) != myGen) return {};
        mergeQueryResults(result, altResult, internalMaxResults);
        if (!altResult.empty()) {
            timing.totalMs += altTiming.totalMs;
            timing.resultCount = result.size();
            timing.searchPath += "+unicode-normalization";
        }

        if (maxResults > 0 && result.size() > maxResults) {
            const auto alternateLower = me::toLower(alternate);
            std::shared_lock lock(mutex_);
            auto rank = [&](uint32_t idx) {
                if (idx >= types_.size() || types_[idx] == 0) {
                    return std::tuple<uint8_t, uint32_t, uint32_t>{255, UINT32_MAX, idx};
                }
                const char* nd = namePool_.data(idx);
                uint16_t nl = namePool_.length(idx);
                uint8_t priority = 3;
                if (!pq.lower.empty() && me::simdContains(nd, nl, pq.lower.data(), pq.lower.size())) {
                    priority = std::min(priority, namePriority(nd, nl, pq.lower.data(), pq.lower.size()));
                }
                if (!alternateLower.empty() &&
                    me::simdContains(nd, nl, alternateLower.data(), alternateLower.size())) {
                    priority = std::min(priority, namePriority(nd, nl, alternateLower.data(), alternateLower.size()));
                }
                uint32_t pathLen = UINT32_MAX;
                if (idx < pathIndices_.size()) {
                    uint32_t pi = pathIndices_[idx];
                    if (pi < pathPool_.entryCount()) {
                        pathLen = static_cast<uint32_t>(pathPool_.length(pi) + 1 + nl);
                    }
                }
                return std::tuple<uint8_t, uint32_t, uint32_t>{priority, pathLen, idx};
            };
            std::sort(result.begin(), result.end(),
                      [&](uint32_t a, uint32_t b) { return rank(a) < rank(b); });
            result.resize(maxResults);
            timing.resultCount = result.size();
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Per-session generation management
// ---------------------------------------------------------------------------

std::pair<std::shared_ptr<std::atomic<uint64_t>>, uint64_t>
SearchEngine::acquireSessionGeneration(uint64_t sessionId) const {
    if (sessionId == 0) {
        // No cancellation: return a dummy atomic that nobody else will check
        auto dummy = std::make_shared<std::atomic<uint64_t>>(0);
        return {dummy, 0};
    }
    std::lock_guard<std::mutex> lock(sessionGenMutex_);
    auto& ptr = sessionGenerations_[sessionId];
    if (!ptr) ptr = std::make_shared<std::atomic<uint64_t>>(0);
    uint64_t gen = ptr->fetch_add(1, std::memory_order_relaxed) + 1;
    return {ptr, gen};
}

void SearchEngine::cancelSession(uint64_t sessionId) const {
    if (sessionId == 0) return;
    std::lock_guard<std::mutex> lock(sessionGenMutex_);
    auto it = sessionGenerations_.find(sessionId);
    if (it != sessionGenerations_.end()) {
        it->second->fetch_add(1, std::memory_order_relaxed);
    }
}

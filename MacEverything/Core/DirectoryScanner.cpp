#include "DirectoryScanner.h"
#include "Logger.h"
#include <sys/attr.h>
#include <sys/vnode.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <memory>
#include <sys/stat.h>
#include <fnmatch.h>
#include <algorithm>

static constexpr size_t ATTR_BUF_SIZE = 1 * 1024 * 1024; // 1 MB per-thread buffer

namespace {

bool pathContainsOrEquals(const std::string& parent, const std::string& child) {
    if (parent.empty()) return false;
    size_t parentLen = parent.size();
    while (parentLen > 1 && parent[parentLen - 1] == '/') {
        parentLen--;
    }
    if (parentLen == 1 && parent[0] == '/') return !child.empty() && child[0] == '/';
    if (child.size() == parentLen && child.compare(0, parentLen, parent) == 0) return true;
    return child.size() > parentLen &&
           child.compare(0, parentLen, parent) == 0 &&
           child[parentLen] == '/';
}

bool pathContainsComponentPath(const std::string& path, const std::string& componentPath) {
    if (componentPath.empty()) return false;
    size_t pos = path.find(componentPath);
    while (pos != std::string::npos) {
        const bool startsAtComponent = pos == 0 || path[pos - 1] == '/';
        const size_t end = pos + componentPath.size();
        const bool endsAtComponent = end == path.size() || path[end] == '/';
        if (startsAtComponent && endsAtComponent) {
            return true;
        }
        pos = path.find(componentPath, pos + 1);
    }
    return false;
}

bool isSystemFilteredPath(const std::string& path) {
    return path == "/System" ||
           path.rfind("/System/", 0) == 0 ||
           path == "/private/var" ||
           path.rfind("/private/var/", 0) == 0 ||
           pathContainsComponentPath(path, "Library/Caches") ||
           pathContainsComponentPath(path, ".Spotlight-V100") ||
           pathContainsComponentPath(path, ".fseventsd") ||
           pathContainsComponentPath(path, ".Trashes");
}

std::vector<std::string> systemAllowedPathsForRoots(const std::vector<std::string>& rootPaths) {
    std::vector<std::string> allowed;
    allowed.reserve(rootPaths.size());
    for (const auto& root : rootPaths) {
        if (!root.empty() && root != "/" && isSystemFilteredPath(root)) {
            allowed.push_back(root);
        }
    }
    return allowed;
}

std::string normalizedRootPath(const std::string& path) {
    size_t len = path.size();
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }
    return path.substr(0, len);
}

std::vector<std::string> normalizedRootPaths(const std::vector<std::string>& rootPaths) {
    std::vector<std::string> normalized;
    normalized.reserve(rootPaths.size());
    for (const auto& rootPath : rootPaths) {
        if (!rootPath.empty()) {
            normalized.push_back(normalizedRootPath(rootPath));
        }
    }
    return normalized;
}

bool hasSystemAllowedPath(const ScanConfig& config, const std::string& path) {
    for (const auto& allowed : config.systemAllowedPaths) {
        if (pathContainsOrEquals(allowed, path)) {
            return true;
        }
    }
    return false;
}

} // namespace

void DirectoryScanner::scan(const std::string& rootPath) {
    scan(std::vector<std::string>{rootPath}, {});
}

void DirectoryScanner::scan(const std::vector<std::string>& rootPaths, const ScanConfig& config) {
    // Reset state so scanner can be reused across multiple scans
    const auto roots = normalizedRootPaths(rootPaths);
    config_ = config;
    if (config_.systemAllowedPaths.empty()) {
        config_.systemAllowedPaths = systemAllowedPathsForRoots(roots);
    }
    done_.store(false, std::memory_order_relaxed);
    cancelled_.store(false, std::memory_order_relaxed);
    activeTasks_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(dedupMutex_);
        visitedDirs_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!workQueue_.empty()) workQueue_.pop();
    }
    threadResults_.clear();
    stats_.fileCount.store(0, std::memory_order_relaxed);
    stats_.dirCount.store(0, std::memory_order_relaxed);
    stats_.symlinkCount.store(0, std::memory_order_relaxed);
    stats_.otherCount.store(0, std::memory_order_relaxed);
    stats_.errorCount.store(0, std::memory_order_relaxed);

    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads < 4) numThreads = 4;
    if (numThreads > 32) numThreads = 32;

    LOG_INFO("Scanner", "Scanning from " << rootPaths.size() << " root(s)"
        << " (using " << numThreads << " threads)");

    threadResults_.resize(numThreads);
    for (auto& v : threadResults_) {
        v.reserve(50000);
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (const auto& rootPath : roots) {
            struct stat rootStat;
            if (stat(rootPath.c_str(), &rootStat) != 0) continue;
            if (!S_ISDIR(rootStat.st_mode)) continue;
            if (!tryVisitDirectory(rootStat.st_dev, rootStat.st_ino)) continue;
            workQueue_.push({rootPath, rootStat.st_dev});
        }
    }

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (unsigned i = 0; i < numThreads; i++) {
        threads.emplace_back(&DirectoryScanner::workerThread, this, static_cast<int>(i));
    }

    queueCV_.notify_all();

    for (auto& t : threads) {
        t.join();
    }
}

std::vector<FileRecord> DirectoryScanner::takeResults() {
    std::vector<FileRecord> merged;
    size_t total = 0;
    for (auto& v : threadResults_) total += v.size();
    merged.reserve(total);
    for (auto& v : threadResults_) {
        merged.insert(merged.end(),
                      std::make_move_iterator(v.begin()),
                      std::make_move_iterator(v.end()));
        v.clear();
        v.shrink_to_fit();
    }
    threadResults_.clear();
    return merged;
}

void DirectoryScanner::workerThread(int threadIndex) {
    auto buffer = std::make_unique<char[]>(ATTR_BUF_SIZE);

    for (;;) {
        WorkItem work;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this] {
                return !workQueue_.empty()
                    || cancelled_.load(std::memory_order_relaxed)
                    || (activeTasks_.load(std::memory_order_acquire) == 0 && workQueue_.empty());
            });

            if (cancelled_.load(std::memory_order_relaxed)) {
                done_ = true;
                queueCV_.notify_all();
                return;
            }

            if (workQueue_.empty() && activeTasks_.load(std::memory_order_acquire) == 0) {
                done_ = true;
                queueCV_.notify_all();
                return;
            }

            if (done_) return;

            work = std::move(workQueue_.front());
            workQueue_.pop();
            // INVARIANT: activeTasks_ is incremented inside queueMutex_ (before
            // popping work), and decremented outside the lock (after scanDirectory
            // returns and any new work has been pushed under queueMutex_). This
            // ensures the termination condition (activeTasks_==0 && workQueue_.empty())
            // cannot fire while discoverable work remains.
            activeTasks_.fetch_add(1, std::memory_order_acq_rel);
        }

        scanDirectory(work.path, work.rootDev, buffer.get(), threadIndex);

        activeTasks_.fetch_sub(1, std::memory_order_acq_rel);
        queueCV_.notify_all();
    }
}

void DirectoryScanner::scanDirectory(const std::string& dirPath, dev_t rootDev,
                                     char* buffer, int threadIndex) {
    if (cancelled_.load(std::memory_order_relaxed)) return;

    int dirfd = open(dirPath.c_str(), O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) {
        if (errno == EACCES || errno == EPERM) {
            stats_.errorCount.fetch_add(1, std::memory_order_relaxed);
        } else if (errno != ENOENT && errno != ENOTDIR) {
            stats_.errorCount.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    struct attrlist attrList;
    memset(&attrList, 0, sizeof(attrList));
    attrList.bitmapcount = ATTR_BIT_MAP_COUNT;
    attrList.commonattr = ATTR_CMN_RETURNED_ATTRS
                        | ATTR_CMN_NAME
                        | ATTR_CMN_ERROR
                        | ATTR_CMN_DEVID
                        | ATTR_CMN_OBJTYPE
                        | ATTR_CMN_MODTIME
                        | ATTR_CMN_FILEID;
    attrList.fileattr = ATTR_FILE_DATALENGTH;

    // Collect subdirectories locally, then batch-push to work queue
    // to reduce queueMutex_ contention (one lock per batch, not per directory).
    std::vector<WorkItem> pendingDirs;

    for (;;) {
        if (cancelled_.load(std::memory_order_relaxed)) break;

        int retcount = getattrlistbulk(dirfd, &attrList, buffer, ATTR_BUF_SIZE, FSOPT_NOFOLLOW);

        if (retcount == -1) {
            if (errno == EACCES || errno == EPERM) {
                stats_.errorCount.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
        if (retcount == 0) {
            break;
        }

        char* entry = buffer;
        for (int i = 0; i < retcount; i++) {
            char* field = entry;

            // 1. Entry length
            uint32_t entryLength;
            memcpy(&entryLength, field, sizeof(uint32_t));
            char* nextEntry = entry + entryLength;
            field += sizeof(uint32_t);

            // 2. Returned attributes
            attribute_set_t returned;
            memcpy(&returned, field, sizeof(attribute_set_t));
            field += sizeof(attribute_set_t);

            // 3. Error (special position after returned_attrs)
            uint32_t error = 0;
            if (returned.commonattr & ATTR_CMN_ERROR) {
                memcpy(&error, field, sizeof(uint32_t));
                field += sizeof(uint32_t);
            }

            // 4. Name reference (ATTR_CMN_NAME = 0x01)
            char* nameRefPtr = nullptr;
            attrreference_t nameRef = {};
            if (returned.commonattr & ATTR_CMN_NAME) {
                nameRefPtr = field;
                memcpy(&nameRef, field, sizeof(attrreference_t));
                field += sizeof(attrreference_t);
            }

            // 5. Device ID (ATTR_CMN_DEVID = 0x02)
            dev_t devid = 0;
            if (returned.commonattr & ATTR_CMN_DEVID) {
                memcpy(&devid, field, sizeof(dev_t));
                field += sizeof(dev_t);
            }

            // 6. Object type (ATTR_CMN_OBJTYPE = 0x08)
            fsobj_type_t objtype = VNON;
            if (returned.commonattr & ATTR_CMN_OBJTYPE) {
                memcpy(&objtype, field, sizeof(fsobj_type_t));
                field += sizeof(fsobj_type_t);
            }

            // 7. Modification time (ATTR_CMN_MODTIME = 0x400)
            struct timespec modtime = {};
            if (returned.commonattr & ATTR_CMN_MODTIME) {
                memcpy(&modtime, field, sizeof(struct timespec));
                field += sizeof(struct timespec);
            }

            // 8. File ID (ATTR_CMN_FILEID = 0x02000000)
            uint64_t fileid = 0;
            if (returned.commonattr & ATTR_CMN_FILEID) {
                memcpy(&fileid, field, sizeof(uint64_t));
                field += sizeof(uint64_t);
            }

            // 9. Data length (ATTR_FILE_DATALENGTH = 0x200, file attr, only for VREG)
            off_t datalength = 0;
            if (returned.fileattr & ATTR_FILE_DATALENGTH) {
                memcpy(&datalength, field, sizeof(off_t));
                field += sizeof(off_t);
            }

            // Skip entries with errors
            if (error != 0) {
                entry = nextEntry;
                continue;
            }

            // Extract name string
            if (!nameRefPtr) {
                entry = nextEntry;
                continue;
            }
            const char* name = nameRefPtr + nameRef.attr_dataoffset;
            if (name[0] == '\0') {
                entry = nextEntry;
                continue;
            }

            std::string childPath = dirPath;
            if (childPath.back() != '/') childPath += '/';
            childPath += name;

            // Process entry
            if (objtype == VDIR) {
                // Skip cross-mount directories (autofs, devfs, NFS, etc.)
                // These can block indefinitely on open() or produce irrelevant results.
                if (devid != rootDev) {
                    entry = nextEntry;
                    continue;
                }

                if (shouldExclude(childPath, name, true)) {
                    entry = nextEntry;
                    continue;
                }

                if (tryVisitDirectory(devid, fileid)) {
                    // Detect .app bundles — record as type 5, skip recursion
                    size_t nameLen = strlen(name);
                    bool isAppBundle = (nameLen > 4 &&
                        name[nameLen-4] == '.' &&
                        tolower(name[nameLen-3]) == 'a' &&
                        tolower(name[nameLen-2]) == 'p' &&
                        tolower(name[nameLen-1]) == 'p');

                    if (!isAppBundle || config_.includeAppBundleContents) {
                        pendingDirs.push_back({std::move(childPath), rootDev});
                    }

                    stats_.dirCount.fetch_add(1, std::memory_order_relaxed);
                    threadResults_[threadIndex].push_back({name, dirPath,
                        static_cast<uint8_t>(isAppBundle ? 5 : 2),
                        0, modtime.tv_sec, fileid, static_cast<int32_t>(devid)});
                }
            } else if (objtype == VREG) {
                if (shouldExclude(childPath, name, false)) {
                    entry = nextEntry;
                    continue;
                }
                stats_.fileCount.fetch_add(1, std::memory_order_relaxed);
                threadResults_[threadIndex].push_back({name, dirPath, 1, static_cast<uint64_t>(datalength), modtime.tv_sec, fileid, static_cast<int32_t>(devid)});
            } else if (objtype == VLNK) {
                if (shouldExclude(childPath, name, false)) {
                    entry = nextEntry;
                    continue;
                }
                stats_.symlinkCount.fetch_add(1, std::memory_order_relaxed);
                threadResults_[threadIndex].push_back({name, dirPath, 3, 0, modtime.tv_sec, fileid, static_cast<int32_t>(devid)});
            } else {
                if (shouldExclude(childPath, name, false)) {
                    entry = nextEntry;
                    continue;
                }
                stats_.otherCount.fetch_add(1, std::memory_order_relaxed);
                threadResults_[threadIndex].push_back({name, dirPath, 4, 0, modtime.tv_sec, fileid, static_cast<int32_t>(devid)});
            }

            entry = nextEntry;
        }
    }

    close(dirfd);

    // Batch-push all discovered subdirectories in a single lock acquisition
    if (!pendingDirs.empty()) {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            for (auto& dir : pendingDirs) {
                workQueue_.push(std::move(dir));
            }
        }
        queueCV_.notify_all();
    }
}

bool DirectoryScanner::tryVisitDirectory(dev_t dev, uint64_t ino) {
    InodeKey key{dev, ino};
    std::lock_guard<std::mutex> lock(dedupMutex_);
    return visitedDirs_.insert(key).second;
}

bool DirectoryScanner::shouldExclude(const std::string& fullPath,
                                     const std::string& name,
                                     bool isDirectory) const {
    if (!config_.includeHidden && !name.empty() && name[0] == '.') {
        return true;
    }

    if (!config_.includeSystem &&
        isSystemFilteredPath(fullPath) &&
        !hasSystemAllowedPath(config_, fullPath)) {
        return true;
    }

    for (const auto& excluded : config_.excludedPaths) {
        if (excluded.empty()) continue;
        if (fullPath == excluded) return true;
        if (fullPath.size() > excluded.size() &&
            fullPath.compare(0, excluded.size(), excluded) == 0 &&
            fullPath[excluded.size()] == '/') {
            return true;
        }
    }

    for (const auto& pattern : config_.excludedPatterns) {
        if (pattern.empty()) continue;
        if (fnmatch(pattern.c_str(), name.c_str(), FNM_CASEFOLD) == 0) {
            return true;
        }
        if (isDirectory && pattern == name) {
            return true;
        }
    }

    return false;
}

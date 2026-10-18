#pragma once
#include <string>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <CoreFoundation/CoreFoundation.h>

/// Path and OS helpers. Volume identity uses CoreFoundation; there is no
/// Objective-C or Foundation dependency.
namespace PathUtils {

/// Return true when child is parent itself or is below parent at a path
/// component boundary. Both paths may have trailing slashes.
inline bool pathContainsOrEquals(const std::string& parent,
                                 const std::string& child) {
    if (parent.empty()) return false;
    size_t parentLen = parent.size();
    while (parentLen > 1 && parent[parentLen - 1] == '/') parentLen--;
    if (parentLen == 1 && parent[0] == '/') {
        return !child.empty() && child[0] == '/';
    }
    if (child.size() == parentLen && child.compare(0, parentLen, parent) == 0) {
        return true;
    }
    return child.size() > parentLen &&
           child.compare(0, parentLen, parent) == 0 &&
           child[parentLen] == '/';
}

/// How a `/Volumes/<name>` root behaved the last time it was mounted.
/// External means a local disk that is not internal (USB, Thunderbolt, SD).
/// Network means SMB, NFS, or another non-local filesystem. Fixed means an
/// internal volume or an ordinary directory. Unknown must not be pruned.
enum class VolumeKind {
    Unknown = 0,
    External,
    Network,
    Fixed,
};

/// Last mounted identity of a `/Volumes/<name>` root, plus the identity of the
/// records currently stored for that name. Pruning requires both UUIDs to match.
struct VolumeClassification {
    VolumeKind kind = VolumeKind::Unknown;
    std::string uuid;
    std::string indexedUuid;
};

struct MountedVolumeTraits {
    VolumeKind kind = VolumeKind::Unknown;
    std::string uuid;
};

/// Test seam. When set, volume inspection skips CoreFoundation.
using MountedVolumeInspector = MountedVolumeTraits (*)(const std::string& path);

struct VolumeKindStore {
    std::mutex mu;
    std::unordered_map<std::string, VolumeClassification> kinds;
    std::string path;
    MountedVolumeInspector inspector = nullptr;
};

inline VolumeKindStore& volumeKindStore() {
    static VolumeKindStore store;
    return store;
}

inline std::string trimTrailingSlash(std::string path) {
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

/// `/Volumes/Name` for a path under that volume, otherwise empty.
inline std::string volumeRootForPath(const std::string& path) {
    constexpr const char* prefix = "/Volumes/";
    constexpr size_t prefixLen = 9;
    if (path.size() <= prefixLen || path.compare(0, prefixLen, prefix) != 0) return {};
    const size_t slash = path.find('/', prefixLen);
    return path.substr(0, slash == std::string::npos ? path.size() : slash);
}

/// True when `scannedPath` replaced every configured root on its
/// `/Volumes/<name>`. A narrower subtree must not claim that volume's indexed
/// UUID, because a sibling root may still hold rows from another disk.
inline bool scannedPathCoversConfiguredVolumeRoots(
    const std::string& scannedPath, const std::vector<std::string>& roots) {
    const std::string scanned = trimTrailingSlash(scannedPath);
    const std::string volume = volumeRootForPath(scanned);
    if (volume.empty()) return false;
    bool covered = false;
    for (const auto& root : roots) {
        const std::string trimmed = trimTrailingSlash(root);
        if (volumeRootForPath(trimmed) != volume) continue;
        if (!pathContainsOrEquals(scanned, trimmed)) return false;
        covered = true;
    }
    return covered;
}

inline const char* volumeKindToken(VolumeKind kind) {
    switch (kind) {
        case VolumeKind::External: return "external";
        case VolumeKind::Network: return "network";
        case VolumeKind::Fixed: return "fixed";
        case VolumeKind::Unknown: break;
    }
    return nullptr;
}

inline VolumeKind volumeKindFromToken(const std::string& token) {
    if (token == "external") return VolumeKind::External;
    if (token == "network") return VolumeKind::Network;
    if (token == "fixed") return VolumeKind::Fixed;
    return VolumeKind::Unknown;
}

inline void writeVolumeField(std::ostream& out, const std::string& value) {
    out << ' ' << value.size() << ' ' << value;
}

inline bool readVolumeField(const std::string& line, size_t& pos, std::string& out) {
    if (pos >= line.size() || line[pos] != ' ') return false;
    ++pos;
    if (pos >= line.size()) return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long length = std::strtoul(line.c_str() + pos, &end, 10);
    if (end == line.c_str() + pos || errno == ERANGE) return false;
    const size_t numberEnd = static_cast<size_t>(end - line.c_str());
    if (numberEnd >= line.size() || line[numberEnd] != ' ') return false;
    const size_t valuePos = numberEnd + 1;
    // Compare against the remaining bytes. `pos + length` can wrap and then
    // pass a bounds check, which makes assign() request a huge string.
    if (length > line.size() - valuePos) return false;
    out.assign(line, valuePos, static_cast<size_t>(length));
    pos = valuePos + static_cast<size_t>(length);
    return true;
}

inline void writeVolumeKindsLocked(VolumeKindStore& store) {
    if (store.path.empty()) return;
    const std::string tmp = store.path + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "v2\n";
    for (const auto& entry : store.kinds) {
        const char* token = volumeKindToken(entry.second.kind);
        if (!token || entry.first.empty()) continue;
        out << token;
        writeVolumeField(out, entry.second.uuid);
        writeVolumeField(out, entry.second.indexedUuid);
        writeVolumeField(out, entry.first);
        out << '\n';
    }
    out.close();
    if (!out) {
        ::unlink(tmp.c_str());
        return;
    }
    if (::rename(tmp.c_str(), store.path.c_str()) != 0) ::unlink(tmp.c_str());
}

inline bool copyVolumeBool(CFURLRef url, CFStringRef key, bool& out) {
    CFTypeRef value = nullptr;
    if (!CFURLCopyResourcePropertyForKey(url, key, &value, nullptr) || !value) return false;
    const bool isBool = CFGetTypeID(value) == CFBooleanGetTypeID();
    if (isBool) out = CFBooleanGetValue(static_cast<CFBooleanRef>(value));
    CFRelease(value);
    return isBool;
}

enum class MountLookup { Error, Absent, Present };

struct MountTableEntry {
    std::string mountPoint;
    uint32_t flags = 0;
};

struct MountSnapshot {
    bool ok = false;
    std::vector<MountTableEntry> entries;
};

struct MountTableOverride {
    std::mutex mu;
    bool active = false;
    bool failed = false;
    std::vector<MountTableEntry> entries;
};

inline MountTableOverride& mountTableOverride() {
    static MountTableOverride overrideTable;
    return overrideTable;
}

/// `active == false` reads the live mount table. A failed override, or a real
/// `getmntinfo_r_np()` error, yields `ok == false` rather than an empty table.
inline void setMountTableOverrideForTests(bool active, bool failed,
                                         std::vector<MountTableEntry> entries = {}) {
    auto& overrideTable = mountTableOverride();
    std::lock_guard<std::mutex> lock(overrideTable.mu);
    overrideTable.active = active;
    overrideTable.failed = failed;
    overrideTable.entries = std::move(entries);
}

inline MountSnapshot loadMountSnapshot() {
    bool active = false;
    bool failed = false;
    std::vector<MountTableEntry> overridden;
    {
        auto& overrideTable = mountTableOverride();
        std::lock_guard<std::mutex> lock(overrideTable.mu);
        active = overrideTable.active;
        failed = overrideTable.failed;
        if (active && !failed) overridden = overrideTable.entries;
    }
    MountSnapshot snapshot;
    if (active) {
        if (failed) return snapshot;
        snapshot.ok = true;
        snapshot.entries = std::move(overridden);
        return snapshot;
    }

    // getmntinfo() keeps one process-wide buffer and returns 0 on error.
    // The reentrant call hands ownership of a fresh buffer to the caller.
    struct statfs* mounts = nullptr;
    const int mountCount = getmntinfo_r_np(&mounts, MNT_NOWAIT);
    struct MountBuffer {
        struct statfs* ptr;
        ~MountBuffer() { std::free(ptr); }
    } buffer{mounts};
    if (mountCount <= 0 || !mounts) return snapshot;
    snapshot.ok = true;
    snapshot.entries.reserve(static_cast<size_t>(mountCount));
    for (int i = 0; i < mountCount; ++i) {
        const char* mountPoint = mounts[i].f_mntonname;
        if (!mountPoint || mountPoint[0] == '\0') continue;
        snapshot.entries.push_back(MountTableEntry{mountPoint, mounts[i].f_flags});
    }
    return snapshot;
}

inline MountLookup volumeMountLookup(const std::string& volumeRoot) {
    const MountSnapshot snapshot = loadMountSnapshot();
    // A failed lookup must not look like "nothing is mounted".
    if (!snapshot.ok) return MountLookup::Error;
    for (const auto& entry : snapshot.entries) {
        if (volumeRoot == entry.mountPoint) return MountLookup::Present;
    }
    return MountLookup::Absent;
}

/// Switch the classification file. A different path drops in-memory entries so
/// one cache cannot leak into another. Call `loadVolumeKinds()` afterwards.
inline void setVolumeKindStorePath(VolumeKindStore& store, const std::string& path) {
    std::lock_guard<std::mutex> lock(store.mu);
    if (store.path == path) return;
    store.path = path;
    store.kinds.clear();
}

inline void setVolumeKindStorePath(const std::string& path) {
    setVolumeKindStorePath(volumeKindStore(), path);
}

/// Replace in-memory classifications with the current file. A missing or
/// unreadable file leaves the map empty. v1 rows have no UUID and cannot prune.
inline void loadVolumeKinds(VolumeKindStore& store) {
    std::lock_guard<std::mutex> lock(store.mu);
    std::unordered_map<std::string, VolumeClassification> loaded;
    if (!store.path.empty()) {
        std::ifstream in(store.path);
        std::string header;
        if (in && std::getline(in, header) && (header == "v1" || header == "v2")) {
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                const size_t kindEnd = line.find(' ');
                if (kindEnd == std::string::npos) continue;
                const VolumeKind kind = volumeKindFromToken(line.substr(0, kindEnd));
                if (kind == VolumeKind::Unknown) continue;
                size_t pos = kindEnd;
                VolumeClassification record;
                record.kind = kind;
                std::string path;
                if (header == "v1") {
                    if (!readVolumeField(line, pos, path) || path.empty() || pos != line.size()) continue;
                } else {
                    if (!readVolumeField(line, pos, record.uuid) ||
                        !readVolumeField(line, pos, record.indexedUuid) ||
                        !readVolumeField(line, pos, path) ||
                        path.empty() || pos != line.size()) {
                        continue;
                    }
                }
                loaded[path] = std::move(record);
            }
        }
    }
    store.kinds.swap(loaded);
}

inline void loadVolumeKinds() {
    loadVolumeKinds(volumeKindStore());
}

inline void setMountedVolumeInspectorForTests(VolumeKindStore& store,
                                              MountedVolumeInspector inspector) {
    std::lock_guard<std::mutex> lock(store.mu);
    store.inspector = inspector;
}

inline void setMountedVolumeInspectorForTests(MountedVolumeInspector inspector) {
    setMountedVolumeInspectorForTests(volumeKindStore(), inspector);
}

inline VolumeKind volumeKindFromTraits(bool gotLocal, bool local,
                                       bool gotInternal, bool internal) {
    if (!gotLocal) return VolumeKind::Unknown;
    if (!local) return VolumeKind::Network;
    if (!gotInternal) return VolumeKind::Unknown;
    return internal ? VolumeKind::Fixed : VolumeKind::External;
}

inline void rememberVolumeKind(VolumeKindStore& store, const std::string& volumePath,
                               VolumeKind kind, const std::string& uuid) {
    const std::string volume = volumeRootForPath(trimTrailingSlash(volumePath));
    if (volume.empty() || kind == VolumeKind::Unknown) return;
    std::lock_guard<std::mutex> lock(store.mu);
    auto& record = store.kinds[volume];
    if (record.kind == kind && record.uuid == uuid) return;
    record.kind = kind;
    record.uuid = uuid;
    writeVolumeKindsLocked(store);
}

inline void rememberVolumeKind(const std::string& volumePath, VolumeKind kind,
                               const std::string& uuid) {
    rememberVolumeKind(volumeKindStore(), volumePath, kind, uuid);
}

/// Record that the cached rows for this volume were read from `uuid`.
inline void noteIndexedVolumeIdentity(VolumeKindStore& store, const std::string& volumePath,
                                      const std::string& uuid) {
    const std::string volume = volumeRootForPath(trimTrailingSlash(volumePath));
    if (volume.empty() || uuid.empty()) return;
    std::lock_guard<std::mutex> lock(store.mu);
    auto& record = store.kinds[volume];
    if (record.indexedUuid == uuid) return;
    record.indexedUuid = uuid;
    writeVolumeKindsLocked(store);
}

inline void noteIndexedVolumeIdentity(const std::string& volumePath, const std::string& uuid) {
    noteIndexedVolumeIdentity(volumeKindStore(), volumePath, uuid);
}

inline VolumeClassification rememberedVolumeClassification(VolumeKindStore& store,
                                                          const std::string& volumePath) {
    const std::string volume = volumeRootForPath(trimTrailingSlash(volumePath));
    std::lock_guard<std::mutex> lock(store.mu);
    const auto it = store.kinds.find(volume);
    return it == store.kinds.end() ? VolumeClassification{} : it->second;
}

inline VolumeClassification rememberedVolumeClassification(const std::string& volumePath) {
    return rememberedVolumeClassification(volumeKindStore(), volumePath);
}

inline VolumeKind rememberedVolumeKind(VolumeKindStore& store, const std::string& volumePath) {
    return rememberedVolumeClassification(store, volumePath).kind;
}

inline VolumeKind rememberedVolumeKind(const std::string& volumePath) {
    return rememberedVolumeKind(volumeKindStore(), volumePath);
}

inline std::string cfStringToStd(CFStringRef value) {
    if (!value) return {};
    char buffer[512];
    if (CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        return buffer;
    }
    return {};
}

/// Classify a mounted path and read its volume UUID.
inline MountedVolumeTraits inspectMountedVolume(VolumeKindStore& store, const std::string& path) {
    MountedVolumeInspector inspector = nullptr;
    {
        std::lock_guard<std::mutex> lock(store.mu);
        inspector = store.inspector;
    }
    if (inspector) return inspector(path);
    MountedVolumeTraits traits;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(path.data()),
        static_cast<CFIndex>(path.size()),
        true);
    if (!url) return traits;
    bool local = false;
    bool internal = false;
    const bool gotLocal = copyVolumeBool(url, kCFURLVolumeIsLocalKey, local);
    const bool gotInternal = copyVolumeBool(url, kCFURLVolumeIsInternalKey, internal);
    CFTypeRef uuidValue = nullptr;
    if (CFURLCopyResourcePropertyForKey(url, kCFURLVolumeUUIDStringKey, &uuidValue, nullptr) &&
        uuidValue) {
        if (CFGetTypeID(uuidValue) == CFStringGetTypeID()) {
            traits.uuid = cfStringToStd(static_cast<CFStringRef>(uuidValue));
        }
        CFRelease(uuidValue);
    }
    CFRelease(url);
    traits.kind = volumeKindFromTraits(gotLocal, local, gotInternal, internal);
    return traits;
}

inline MountedVolumeTraits inspectMountedVolume(const std::string& path) {
    return inspectMountedVolume(volumeKindStore(), path);
}

/// Record the volume kind and UUID of a reachable root. Paths outside
/// `/Volumes` are ignored. This does not claim that cached records belong to
/// the UUID; only a completed scan does that.
inline void observeVolumeKind(VolumeKindStore& store, const std::string& rawPath) {
    const std::string path = trimTrailingSlash(rawPath);
    const std::string volume = volumeRootForPath(path);
    if (volume.empty()) return;
    const MountedVolumeTraits traits = inspectMountedVolume(store, path);
    if (traits.kind == VolumeKind::Unknown) return;
    rememberVolumeKind(store, volume, traits.kind, traits.uuid);
}

inline void observeVolumeKind(const std::string& rawPath) {
    observeVolumeKind(volumeKindStore(), rawPath);
}

inline bool isReachableDirectory(const std::string& rawPath) {
    const std::string path = trimTrailingSlash(rawPath);
    struct stat st{};
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/// True only when `path` is an unplugged local external disk and the cached
/// records were indexed from that same volume UUID.
///
/// A reachable directory is never unplugged, including a `/Volumes` symlink
/// such as `/Volumes/Macintosh HD` and a normal folder created under
/// `/Volumes`. An offline network share, a reused mount name whose UUID does
/// not match the indexed records, a path we have not classified, a missing
/// child of a volume that is still mounted, and a `getmntinfo()` failure all
/// return false so callers keep the cached index.
inline bool isUnpluggedRemovableVolume(VolumeKindStore& store, const std::string& rawPath) {
    const std::string path = trimTrailingSlash(rawPath);
    const std::string volume = volumeRootForPath(path);
    if (volume.empty()) return false;

    struct stat st{};
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) observeVolumeKind(store, path);
        return false;
    }
    const int statError = errno;
    if (statError != ENOENT && statError != ENOTDIR) return false;

    // The volume name is still in the mount table: this is a missing child,
    // not an unplugged disk. A lookup error must not be treated as unplugged.
    if (volumeMountLookup(volume) != MountLookup::Absent) return false;
    const VolumeClassification record = rememberedVolumeClassification(store, volume);
    return record.kind == VolumeKind::External &&
           !record.uuid.empty() &&
           record.uuid == record.indexedUuid;
}

inline bool isUnpluggedRemovableVolume(const std::string& rawPath) {
    return isUnpluggedRemovableVolume(volumeKindStore(), rawPath);
}

/// Detect a mounted filesystem that is not marked local (SMB, NFS, etc.).
inline bool isNetworkFilesystem(const std::string& path) {
    struct statfs fsInfo = {};
    if (statfs(path.c_str(), &fsInfo) == 0) {
        return (fsInfo.f_flags & MNT_LOCAL) == 0;
    }

    // statfs() fails when a configured child has not appeared yet. Consult
    // the mount table so an unavailable path below an already mounted SMB/NFS
    // volume still gets the network scan policy. A lookup error keeps the
    // local-disk policy instead of guessing from an empty table.
    const MountSnapshot snapshot = loadMountSnapshot();
    if (!snapshot.ok) return false;
    size_t bestMountLength = 0;
    bool network = false;
    for (const auto& entry : snapshot.entries) {
        if (!pathContainsOrEquals(entry.mountPoint, path)) continue;
        if (entry.mountPoint.size() < bestMountLength) continue;
        bestMountLength = entry.mountPoint.size();
        network = (entry.flags & MNT_LOCAL) == 0;
    }
    return network;
}

inline std::string getHomeDirectory() {
    const char* home = std::getenv("HOME");
    if (home && home[0] == '/') return home;

    struct passwd pwd{};
    struct passwd* result = nullptr;
    long bufferSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufferSize < 1024) bufferSize = 16 * 1024;
    std::string buffer(static_cast<size_t>(bufferSize), '\0');
    if (getpwuid_r(getuid(), &pwd, buffer.data(), buffer.size(), &result) == 0 &&
        result && result->pw_dir && result->pw_dir[0] == '/') {
        return result->pw_dir;
    }
    return {};
}

/// Get "~/Library/Caches/com.maceverything.app" expanded.
inline std::string getDefaultCachePath() {
    const std::string home = getHomeDirectory();
    return home.empty() ? std::string{} : home + "/Library/Caches/com.maceverything.app";
}

/// Get "~/Library/Logs/MacEverything" expanded.
inline std::string getDefaultLogPath() {
    const std::string home = getHomeDirectory();
    return home.empty() ? std::string{} : home + "/Library/Logs/MacEverything";
}

/// Get "~/Library/Application Support/com.maceverything.app" expanded.
inline std::string getDefaultAppSupportPath() {
    const std::string home = getHomeDirectory();
    return home.empty() ? std::string{} : home + "/Library/Application Support/com.maceverything.app";
}

/// Get "~/Library/Application Support/com.maceverything.app/.http_token" expanded.
/// This file stores the random local HTTP API token with 0600 permissions.
/// GUI normal search never uses HTTP; only mace, MCP, and external curl need it.
inline std::string getHttpTokenPath() {
    const std::string support = getDefaultAppSupportPath();
    return support.empty() ? std::string{} : support + "/.http_token";
}

/// Persist a rename by syncing the directory entry that contains the target.
inline bool syncParentDirectory(const std::string& path, int* error = nullptr) {
    const auto slash = path.rfind('/');
    const std::string parent = slash == std::string::npos ? "." :
        (slash == 0 ? "/" : path.substr(0, slash));
    int fd = open(parent.c_str(), O_RDONLY);
    if (fd < 0) {
        if (error) *error = errno;
        return false;
    }
    bool ok = fsync(fd) == 0;
    int savedError = ok ? 0 : errno;
    close(fd);
    if (error) *error = savedError;
    return ok;
}

inline bool isUnsupportedDirectorySyncError(int error) {
    return error == EINVAL || error == ENOTSUP || error == EOPNOTSUPP;
}
/// Uses ProductVersion from kern.osproductversion (macOS 10.13.4+).
inline std::string getOSVersionString() {
    // Try kern.osproductversion first (available macOS 10.13.4+)
    char buf[64] = {};
    size_t len = sizeof(buf);
    if (sysctlbyname("kern.osproductversion", buf, &len, nullptr, 0) == 0) {
        return std::string(buf);
    }
    // Fallback to uname
    struct utsname info;
    if (uname(&info) == 0) {
        return std::string(info.release);
    }
    return "unknown";
}

} // namespace PathUtils

#include "SearchEngine.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>

// --- Persistence format constants ---

static constexpr char MAGIC[4] = {'M', 'E', 'I', 'D'};
static constexpr uint32_t FORMAT_VERSION_V1 = 1;
static constexpr uint32_t FORMAT_VERSION_V2 = 2;
static constexpr uint32_t FORMAT_VERSION_V3 = 3;

// --- Helper: write/read a length-prefixed string ---
static bool writeString(FILE* f, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    if (fwrite(&len, sizeof(uint32_t), 1, f) != 1) return false;
    if (len > 0 && fwrite(s.data(), 1, len, f) != len) return false;
    return true;
}

static bool readString(FILE* f, std::string& s) {
    uint32_t len;
    if (fread(&len, sizeof(uint32_t), 1, f) != 1) return false;
    if (len > 65536) return false; // sanity limit matching WAL reader
    s.resize(len);
    if (len > 0 && fread(s.data(), 1, len, f) != len) return false;
    return true;
}

// --- Write a single record (v2/v3 format with inode+devId) ---
// resolvedPath: the actual directory path (from PathTable), since record.path
// may be cleared after path interning.
static bool writeRecord(FILE* f, const FileRecord& r, const std::string& resolvedPath) {
    if (!writeString(f, r.name)) return false;
    if (!writeString(f, resolvedPath)) return false;
    if (fwrite(&r.type, sizeof(uint8_t), 1, f) != 1) return false;
    if (fwrite(&r.size, sizeof(uint64_t), 1, f) != 1) return false;
    int64_t mod = static_cast<int64_t>(r.modTime);
    if (fwrite(&mod, sizeof(int64_t), 1, f) != 1) return false;
    if (fwrite(&r.inode, sizeof(uint64_t), 1, f) != 1) return false;
    if (fwrite(&r.devId, sizeof(int32_t), 1, f) != 1) return false;
    return true;
}

// --- Read a single record, version-aware ---
static bool readRecordFromFile(FILE* f, FileRecord& r, uint32_t version) {
    if (!readString(f, r.name)) return false;
    if (!readString(f, r.path)) return false;
    if (fread(&r.type, sizeof(uint8_t), 1, f) != 1) return false;
    if (fread(&r.size, sizeof(uint64_t), 1, f) != 1) return false;
    int64_t mod;
    if (fread(&mod, sizeof(int64_t), 1, f) != 1) return false;
    r.modTime = static_cast<time_t>(mod);
    if (version >= FORMAT_VERSION_V2) {
        if (fread(&r.inode, sizeof(uint64_t), 1, f) != 1) return false;
        if (fread(&r.devId, sizeof(int32_t), 1, f) != 1) return false;
    }
    return true;
}

// --- v3 save: extensible metadata ---
bool SearchEngine::saveToFile(const std::string& filePath, const IndexMetadata& metadata) const {
    std::shared_lock lock(mutex_);

    std::string tmpPath = filePath + ".tmp";
    FILE* f = fopen(tmpPath.c_str(), "wb");
    if (!f) return false;

    // P2: Bypass page cache to avoid evicting hot search data after compaction
    fcntl(fileno(f), F_NOCACHE, 1);

    // Helper to check fwrite return values
    bool writeOk = true;
    auto safeWrite = [&](const void* ptr, size_t size, size_t count) {
        if (writeOk && fwrite(ptr, size, count, f) != count) writeOk = false;
    };

    // Header: MAGIC(4) + version(4) + timestamp(8) + lastEventId(8)
    safeWrite(MAGIC, 1, 4);
    uint32_t version = FORMAT_VERSION_V3;
    safeWrite(&version, sizeof(uint32_t), 1);
    int64_t ts = metadata.timestamp > 0 ? metadata.timestamp : static_cast<int64_t>(time(nullptr));
    safeWrite(&ts, sizeof(int64_t), 1);
    safeWrite(&metadata.lastEventId, sizeof(uint64_t), 1);

    // Metadata section: metadataCount(4) + [keyLen(4) + key + valueLen(4) + value] ...
    uint32_t metaCount = static_cast<uint32_t>(metadata.extra.size());
    safeWrite(&metaCount, sizeof(uint32_t), 1);
    for (const auto& [key, value] : metadata.extra) {
        if (!writeOk) break;
        if (!writeString(f, key) || !writeString(f, value)) writeOk = false;
    }

    // Record count — count actual live records to ensure consistency
    uint32_t totalCount = static_cast<uint32_t>(types_.size());
    uint32_t liveCount = 0;
    for (uint32_t i = 0; i < totalCount; i++) {
        if (types_[i] != 0) liveCount++;
    }
    safeWrite(&liveCount, sizeof(uint32_t), 1);

    if (!writeOk) {
        fclose(f);
        remove(tmpPath.c_str());
        return false;
    }

    // Records (same binary layout as v2) — reconstruct from SoA columns
    for (uint32_t i = 0; i < totalCount; i++) {
        if (types_[i] == 0) continue;
        FileRecord r;
        r.name = origNamePool_.str(i);
        r.type = types_[i];
        r.size = sizes_[i];
        r.modTime = static_cast<time_t>(modTimes_[i]);
        r.inode = inodes_[i];
        r.devId = devIds_[i];
        std::string resolvedPath = pathPool_.str(pathIndices_[i]);
        if (!writeRecord(f, r, resolvedPath)) {
            fclose(f);
            remove(tmpPath.c_str());
            return false;
        }
    }

    // H-2: fsync before close to ensure data reaches disk before rename
    fsync(fileno(f));
    fclose(f);

    if (rename(tmpPath.c_str(), filePath.c_str()) != 0) {
        remove(tmpPath.c_str());
        return false;
    }
    return true;
}

// --- Legacy save overload ---
bool SearchEngine::saveToFile(const std::string& filePath, uint64_t lastEventId) const {
    IndexMetadata meta;
    meta.lastEventId = lastEventId;
    return saveToFile(filePath, meta);
}

// --- Load: supports v1, v2, and v3 ---
bool SearchEngine::loadFromFile(const std::string& filePath, IndexMetadata* outMetadata) {
    FILE* f = fopen(filePath.c_str(), "rb");
    if (!f) return false;

    // Read & verify magic
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, MAGIC, 4) != 0) {
        fclose(f);
        return false;
    }

    uint32_t version;
    if (fread(&version, sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
    if (version < FORMAT_VERSION_V1 || version > FORMAT_VERSION_V3) {
        fclose(f);
        return false;
    }

    IndexMetadata meta;
    meta.formatVersion = version;

    // Timestamp (all versions)
    if (fread(&meta.timestamp, sizeof(int64_t), 1, f) != 1) { fclose(f); return false; }

    // lastEventId (v2+)
    if (version >= FORMAT_VERSION_V2) {
        if (fread(&meta.lastEventId, sizeof(uint64_t), 1, f) != 1) { fclose(f); return false; }
    }

    // Extended metadata (v3+)
    if (version >= FORMAT_VERSION_V3) {
        uint32_t metaCount;
        if (fread(&metaCount, sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
        for (uint32_t m = 0; m < metaCount; m++) {
            std::string key, value;
            if (!readString(f, key) || !readString(f, value)) { fclose(f); return false; }
            meta.extra[key] = value;
        }
    }

    // Record count
    uint32_t count;
    if (fread(&count, sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }

    constexpr uint32_t kMaxReasonableCount = 50'000'000;
    if (count > kMaxReasonableCount) {
        std::cerr << "[SearchEnginePersistence] Corrupt index: count=" << count << " exceeds limit\n";
        fclose(f);
        return false;
    }

    std::vector<FileRecord> records;
    records.reserve(count);

    for (uint32_t i = 0; i < count; i++) {
        FileRecord r;
        if (!readRecordFromFile(f, r, version)) { fclose(f); return false; }
        records.push_back(std::move(r));
    }

    fclose(f);

    if (outMetadata) *outMetadata = std::move(meta);
    loadRecords(std::move(records));
    return true;
}

// --- Legacy load overload ---
bool SearchEngine::loadFromFile(const std::string& filePath, uint64_t* outLastEventId) {
    IndexMetadata meta;
    bool ok = loadFromFile(filePath, &meta);
    if (ok && outLastEventId) *outLastEventId = meta.lastEventId;
    return ok;
}

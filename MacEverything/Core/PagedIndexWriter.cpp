#include "PagedIndexWriter.h"
#include "PathUtils.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <algorithm>

// ─── Helpers: same format as SearchEnginePersistence ───

static bool writeString(FILE* f, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    if (fwrite(&len, sizeof(uint32_t), 1, f) != 1) return false;
    if (len > 0 && fwrite(s.data(), 1, len, f) != len) return false;
    return true;
}


// ─── Serialization to in-memory buffer ───

static void appendU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v),
               reinterpret_cast<uint8_t*>(&v) + 4);
}

static void appendU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v),
               reinterpret_cast<uint8_t*>(&v) + 2);
}

// v5 record serializer: pathIndex + origName + lowerName + fixed fields
static void appendRecordV5(std::vector<uint8_t>& buf,
                            const FileRecord& r, uint32_t pathIdx,
                            const char* lowerName, uint16_t lowerNameLen) {
    appendU32(buf, pathIdx);
    // Original name
    uint16_t origLen = static_cast<uint16_t>(std::min<size_t>(r.name.size(), UINT16_MAX));
    appendU16(buf, origLen);
    buf.insert(buf.end(), r.name.begin(), r.name.begin() + origLen);
    // Lowercase name
    appendU16(buf, lowerNameLen);
    buf.insert(buf.end(), lowerName, lowerName + lowerNameLen);
    // Fixed fields
    buf.push_back(r.type);
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&r.size),
               reinterpret_cast<const uint8_t*>(&r.size) + 8);
    int64_t mod = static_cast<int64_t>(r.modTime);
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&mod),
               reinterpret_cast<const uint8_t*>(&mod) + 8);
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&r.inode),
               reinterpret_cast<const uint8_t*>(&r.inode) + 8);
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&r.devId),
               reinterpret_cast<const uint8_t*>(&r.devId) + 4);
}

// ─── StringPool I/O helpers ───

bool PagedIndexWriter::writeStringPool(FILE* f, const StringPool& pool) {
    // Buffer
    uint32_t bufSize = static_cast<uint32_t>(pool.rawSize());
    if (fwrite(&bufSize, 4, 1, f) != 1) return false;
    if (bufSize > 0 && fwrite(pool.rawBuffer(), 1, bufSize, f) != bufSize) return false;

    // Entries (each: offset(4) + length(2) = 6 bytes)
    uint32_t entryCount = pool.entryCount();
    if (fwrite(&entryCount, 4, 1, f) != 1) return false;
    if (entryCount > 0 && fwrite(pool.entries(), sizeof(StringPool::Entry), entryCount, f) != entryCount)
        return false;

    // CRC32 over buffer + entries
    std::vector<uint8_t> crcData;
    crcData.reserve(bufSize + entryCount * sizeof(StringPool::Entry));
    if (bufSize > 0)
        crcData.insert(crcData.end(), pool.rawBuffer(), pool.rawBuffer() + bufSize);
    if (entryCount > 0) {
        auto ep = reinterpret_cast<const uint8_t*>(pool.entries());
        crcData.insert(crcData.end(), ep, ep + entryCount * sizeof(StringPool::Entry));
    }
    uint32_t crc = IndexWAL::crc32(crcData.data(), crcData.size());
    if (fwrite(&crc, 4, 1, f) != 1) return false;

    return true;
}

bool PagedIndexWriter::readStringPool(FILE* f, StringPool& pool) {
    static constexpr uint64_t kMaxExpandedPoolBytes = 512ULL * 1024 * 1024;
    uint32_t bufSize;
    if (fread(&bufSize, 4, 1, f) != 1) return false;
    if (bufSize > 500'000'000) return false; // sanity: 500MB

    long bufferStart = ftell(f);
    if (bufferStart < 0 || fseek(f, 0, SEEK_END) != 0) return false;
    long fileEnd = ftell(f);
    if (fileEnd < bufferStart || static_cast<uint64_t>(fileEnd - bufferStart) <
            static_cast<uint64_t>(bufSize) + sizeof(uint32_t) + sizeof(uint32_t) ||
        fseek(f, bufferStart, SEEK_SET) != 0) return false;

    std::vector<char> buffer(bufSize);
    if (bufSize > 0 && fread(buffer.data(), 1, bufSize, f) != bufSize) return false;

    uint32_t entryCount;
    if (fread(&entryCount, 4, 1, f) != 1) return false;
    if (entryCount > 50'000'000) return false; // sanity: 50M entries

    const uint64_t entryBytes = static_cast<uint64_t>(entryCount) * sizeof(StringPool::Entry);
    long entriesStart = ftell(f);
    if (entriesStart < 0 || fileEnd < entriesStart ||
        entryBytes + sizeof(uint32_t) > static_cast<uint64_t>(fileEnd - entriesStart) ||
        static_cast<uint64_t>(bufSize) + entryBytes > kMaxExpandedPoolBytes) return false;

    std::vector<StringPool::Entry> entries(entryCount);
    if (entryCount > 0 && fread(entries.data(), sizeof(StringPool::Entry), entryCount, f) != entryCount)
        return false;

    // Verify CRC32
    uint32_t storedCrc;
    if (fread(&storedCrc, 4, 1, f) != 1) return false;

    std::vector<uint8_t> crcData;
    crcData.reserve(bufSize + entryCount * sizeof(StringPool::Entry));
    if (bufSize > 0)
        crcData.insert(crcData.end(), buffer.data(), buffer.data() + bufSize);
    if (entryCount > 0) {
        auto ep = reinterpret_cast<const uint8_t*>(entries.data());
        crcData.insert(crcData.end(), ep, ep + entryCount * sizeof(StringPool::Entry));
    }
    uint32_t computedCrc = IndexWAL::crc32(crcData.data(), crcData.size());
    if (computedCrc != storedCrc) {
        std::cerr << "[PagedIndexWriter] StringPool CRC mismatch (expected "
                  << storedCrc << ", got " << computedCrc << ")\n";
        return false;
    }

    pool.loadRaw(buffer.data(), bufSize, entries.data(), entryCount);
    return true;
}

// ─── PagedIndexWriter ───

PagedIndexWriter::PagedIndexWriter(const std::string& pagesPath, const std::string& ptablePath)
    : pagesPath_(pagesPath), ptablePath_(ptablePath) {}

bool PagedIndexWriter::exists() const {
    return access(ptablePath_.c_str(), R_OK) == 0 &&
           access(pagesPath_.c_str(), R_OK) == 0;
}

// ─── deserializePageV5 ───

bool PagedIndexWriter::deserializePageV5(
    const uint8_t* data, size_t len,
    uint16_t expectedCount,
    V5PageResult& out)
{
    const uint8_t* p = data;
    const uint8_t* end = data + len;
    out.records.reserve(expectedCount);
    out.lowerNames.reserve(expectedCount);
    out.pathIndices.reserve(expectedCount);

    for (uint16_t i = 0; i < expectedCount; i++) {
        // pathIndex(4)
        if (p + 4 > end) return false;
        uint32_t pathIdx;
        memcpy(&pathIdx, p, 4); p += 4;
        out.pathIndices.push_back(pathIdx);

        // origNameLen(2) + origName
        if (p + 2 > end) return false;
        uint16_t origLen;
        memcpy(&origLen, p, 2); p += 2;
        if (p + origLen > end) return false;
        std::string origName(reinterpret_cast<const char*>(p), origLen);
        p += origLen;

        // lowerNameLen(2) + lowerName
        if (p + 2 > end) return false;
        uint16_t lowerLen;
        memcpy(&lowerLen, p, 2); p += 2;
        if (p + lowerLen > end) return false;
        std::string lowerName(reinterpret_cast<const char*>(p), lowerLen);
        p += lowerLen;

        // Fixed fields: type(1) + size(8) + modTime(8) + inode(8) + devId(4) = 29 bytes
        if (p + 29 > end) return false;
        FileRecord r;
        r.name = std::move(origName);
        r.type = *p++;
        memcpy(&r.size, p, 8); p += 8;
        int64_t mod;
        memcpy(&mod, p, 8); p += 8;
        r.modTime = static_cast<time_t>(mod);
        memcpy(&r.inode, p, 8); p += 8;
        memcpy(&r.devId, p, 4); p += 4;

        out.records.push_back(std::move(r));
        out.lowerNames.push_back(std::move(lowerName));
    }
    return true;
}

// ─── writePtable ───

bool PagedIndexWriter::writePtable(const IndexMetadata& meta, uint32_t totalRecords) const {
    std::string tmpPath = ptablePath_ + ".tmp";
    FILE* f = fopen(tmpPath.c_str(), "wb");
    if (!f) return false;

    bool ok = true;
    auto safeWrite = [&](const void* ptr, size_t size, size_t count) {
        if (ok && fwrite(ptr, size, count, f) != count) ok = false;
    };

    // Header
    uint32_t magic = kPtableMagic;
    safeWrite(&magic, 4, 1);
    uint32_t ver = kVersion; // always write current version (v5)
    safeWrite(&ver, 4, 1);
    int64_t ts = meta.timestamp > 0 ? meta.timestamp : static_cast<int64_t>(time(nullptr));
    safeWrite(&ts, 8, 1);
    safeWrite(&meta.lastEventId, 8, 1);

    // Metadata KV pairs
    uint32_t metaCount = static_cast<uint32_t>(meta.extra.size());
    safeWrite(&metaCount, 4, 1);
    for (const auto& [key, value] : meta.extra) {
        if (!ok) break;
        if (!writeString(f, key) || !writeString(f, value)) ok = false;
    }

    // Page table header
    uint32_t pageSize = SearchEngine::kRecordsPerPage;
    safeWrite(&pageSize, 4, 1);
    safeWrite(&totalRecords, 4, 1);
    uint32_t pageCount = static_cast<uint32_t>(pageEntries_.size());
    safeWrite(&pageCount, 4, 1);

    // Page entries
    for (const auto& pe : pageEntries_) {
        safeWrite(&pe.offset, 8, 1);
        safeWrite(&pe.byteLength, 4, 1);
        safeWrite(&pe.recordCount, 2, 1);
        safeWrite(&pe.crc32, 4, 1);
    }

    // v5: Path dictionaries (after page entries)
    if (ok) {
        if (!writeStringPool(f, pathDict_)) ok = false;
    }
    if (ok) {
        if (!writeStringPool(f, lowerPathDict_)) ok = false;
    }

    if (!ok) {
        fclose(f);
        remove(tmpPath.c_str());
        return false;
    }

    bool durable = fflush(f) == 0;
    if (durable) durable = fsync(fileno(f)) == 0;
    if (fclose(f) != 0) durable = false;
    if (!durable) {
        remove(tmpPath.c_str());
        return false;
    }

    if (rename(tmpPath.c_str(), ptablePath_.c_str()) != 0) {
        remove(tmpPath.c_str());
        return false;
    }
    return PathUtils::syncParentDirectory(ptablePath_);
}

// ─── load ───

bool PagedIndexWriter::load(SearchEngine& engine, IndexMetadata* outMeta) {
    FILE* ptf = fopen(ptablePath_.c_str(), "rb");
    if (!ptf) return false;

    // Read ptable header
    uint32_t magic;
    if (fread(&magic, 4, 1, ptf) != 1 || magic != kPtableMagic) { fclose(ptf); return false; }
    uint32_t ver;
    if (fread(&ver, 4, 1, ptf) != 1 || ver != kVersionV5) {
        if (ver == kVersionV4)
            std::cerr << "[PagedIndexWriter] v4 format no longer supported; will rebuild index\n";
        fclose(ptf);
        return false;
    }

    loadedVersion_ = ver;

    IndexMetadata meta;
    meta.formatVersion = ver;
    if (fread(&meta.timestamp, 8, 1, ptf) != 1) { fclose(ptf); return false; }
    if (fread(&meta.lastEventId, 8, 1, ptf) != 1) { fclose(ptf); return false; }

    uint32_t metaCount;
    if (fread(&metaCount, 4, 1, ptf) != 1) { fclose(ptf); return false; }
    if (metaCount > 1000) { fclose(ptf); return false; }
    for (uint32_t i = 0; i < metaCount; i++) {
        std::string key, value;
        uint32_t klen;
        if (fread(&klen, 4, 1, ptf) != 1 || klen > 65536) { fclose(ptf); return false; }
        key.resize(klen);
        if (klen > 0 && fread(key.data(), 1, klen, ptf) != klen) { fclose(ptf); return false; }
        uint32_t vlen;
        if (fread(&vlen, 4, 1, ptf) != 1 || vlen > 65536) { fclose(ptf); return false; }
        value.resize(vlen);
        if (vlen > 0 && fread(value.data(), 1, vlen, ptf) != vlen) { fclose(ptf); return false; }
        meta.extra[key] = value;
    }

    uint32_t pageSize, totalRecords, pageCount;
    if (fread(&pageSize, 4, 1, ptf) != 1) { fclose(ptf); return false; }
    if (fread(&totalRecords, 4, 1, ptf) != 1) { fclose(ptf); return false; }
    if (fread(&pageCount, 4, 1, ptf) != 1) { fclose(ptf); return false; }

    if (totalRecords > 50'000'000 || pageCount > 100'000) { fclose(ptf); return false; }

    std::vector<PageEntry> entries(pageCount);
    for (uint32_t i = 0; i < pageCount; i++) {
        auto& pe = entries[i];
        if (fread(&pe.offset, 8, 1, ptf) != 1) { fclose(ptf); return false; }
        if (fread(&pe.byteLength, 4, 1, ptf) != 1) { fclose(ptf); return false; }
        if (fread(&pe.recordCount, 2, 1, ptf) != 1) { fclose(ptf); return false; }
        if (fread(&pe.crc32, 4, 1, ptf) != 1) { fclose(ptf); return false; }
    }

    // Read path dictionaries from ptable
    StringPool pathDict, lowerPathDict;
    if (!readStringPool(ptf, pathDict)) { fclose(ptf); return false; }
    if (!readStringPool(ptf, lowerPathDict)) { fclose(ptf); return false; }
    fclose(ptf);

    // Read pages file
    FILE* pf = fopen(pagesPath_.c_str(), "rb");
    if (!pf) return false;

    // Verify pages magic
    uint32_t pagesMagic;
    if (fread(&pagesMagic, 4, 1, pf) != 1 || pagesMagic != kPagesMagic) {
        fclose(pf);
        return false;
    }

    fseek(pf, 0, SEEK_END);
    uint64_t fileSize = static_cast<uint64_t>(ftell(pf));

    {
        // Deserialize v5 pages into separated data, call loadRecordsV5
        std::vector<FileRecord> allRecords;
        std::vector<std::string> allLowerNames;
        std::vector<uint32_t> allPathIndices;
        allRecords.reserve(totalRecords);
        allLowerNames.reserve(totalRecords);
        allPathIndices.reserve(totalRecords);

        for (uint32_t i = 0; i < pageCount; i++) {
            const auto& pe = entries[i];
            if (pe.offset + pe.byteLength > fileSize) { fclose(pf); return false; }

            std::vector<uint8_t> buf(pe.byteLength);
            fseek(pf, static_cast<long>(pe.offset), SEEK_SET);
            if (fread(buf.data(), 1, pe.byteLength, pf) != pe.byteLength) { fclose(pf); return false; }

            uint32_t crc = IndexWAL::crc32(buf.data(), buf.size());
            if (crc != pe.crc32) {
                std::cerr << "[PagedIndexWriter] CRC mismatch on page " << i
                          << " (expected " << pe.crc32 << ", got " << crc << ")\n";
                fclose(pf);
                return false;
            }

            V5PageResult pageResult;
            if (!deserializePageV5(buf.data(), buf.size(), pe.recordCount, pageResult)) {
                fclose(pf);
                return false;
            }
            allRecords.insert(allRecords.end(),
                              std::make_move_iterator(pageResult.records.begin()),
                              std::make_move_iterator(pageResult.records.end()));
            allLowerNames.insert(allLowerNames.end(),
                                 std::make_move_iterator(pageResult.lowerNames.begin()),
                                 std::make_move_iterator(pageResult.lowerNames.end()));
            allPathIndices.insert(allPathIndices.end(),
                                  pageResult.pathIndices.begin(),
                                  pageResult.pathIndices.end());
        }
        fclose(pf);

        pageEntries_ = std::move(entries);
        pagesFileSize_ = fileSize;
        pathDict_ = pathDict; // keep a copy for next flush
        lowerPathDict_ = lowerPathDict;

        if (outMeta) *outMeta = std::move(meta);
        engine.loadRecordsV5(std::move(allRecords), std::move(allLowerNames),
                             std::move(allPathIndices), std::move(pathDict), std::move(lowerPathDict));
        return true;
    }
}

// ─── flushDirtyPages ───

bool PagedIndexWriter::flushDirtyPages(SearchEngine& engine, const IndexMetadata& meta) {
    auto dirtyPages = engine.getDirtyPageNumbers();
    if (dirtyPages.empty() && !engine.needsFullRewrite()) {
        return true; // nothing to do
    }

    if (engine.needsFullRewrite()) {
        return fullRewrite(engine, meta);
    }

    uint32_t totalRecords = engine.recordCount();
    uint32_t pageCount = (totalRecords + SearchEngine::kRecordsPerPage - 1) / SearchEngine::kRecordsPerPage;

    // Ensure pageEntries_ is sized correctly
    if (pageEntries_.size() < pageCount) {
        pageEntries_.resize(pageCount, PageEntry{0, 0, 0, 0});
    }

    // Snapshot path dictionaries for v5 ptable
    pathDict_ = engine.pathPoolSnapshot();
    {
        // Generate lower path dict on-the-fly from pathPool (lowerPathPool_ eliminated)
        StringPool lpd;
        for (uint32_t i = 0; i < pathDict_.entryCount(); i++) {
            if (pathDict_.isLive(i)) {
                lpd.append(SearchEngine::lowerPathStr(pathDict_, i));
            } else {
                lpd.append("");
                lpd.tombstone(i);
            }
        }
        lowerPathDict_ = std::move(lpd);
    }

    // Open pages file for appending (create if not exists)
    FILE* pf = fopen(pagesPath_.c_str(), "r+b");
    bool newFile = false;
    if (!pf) {
        pf = fopen(pagesPath_.c_str(), "wb");
        newFile = true;
    }
    if (!pf) return false;

    fcntl(fileno(pf), F_NOCACHE, 1);

    if (newFile) {
        uint32_t magic = kPagesMagic;
        if (fwrite(&magic, 4, 1, pf) != 1) { fclose(pf); return false; }
        pagesFileSize_ = 4;
    } else {
        fseek(pf, 0, SEEK_END);
        pagesFileSize_ = static_cast<uint64_t>(ftell(pf));
        if (pagesFileSize_ == 0) {
            uint32_t magic = kPagesMagic;
            if (fwrite(&magic, 4, 1, pf) != 1) { fclose(pf); return false; }
            pagesFileSize_ = 4;
        }
    }

    // Serialize and append each dirty page using v5 format
    for (uint32_t pageNum : dirtyPages) {
        if (pageNum >= pageCount) continue;

        uint32_t startIdx = pageNum * SearchEngine::kRecordsPerPage;
        uint32_t count = std::min(SearchEngine::kRecordsPerPage,
                                  totalRecords - startIdx);

        std::vector<uint8_t> buf;
        buf.reserve(count * 80); // v5 records are smaller

        engine.forEachRecordInRangeV5(startIdx, count,
            [&](uint32_t, const FileRecord& r, uint32_t pathIdx,
                const char* lowerName, uint16_t lowerNameLen) {
                appendRecordV5(buf, r, pathIdx, lowerName, lowerNameLen);
            });

        uint32_t crc = IndexWAL::crc32(buf.data(), buf.size());

        uint64_t offset = pagesFileSize_;
        if (fwrite(buf.data(), 1, buf.size(), pf) != buf.size()) {
            fclose(pf);
            return false;
        }
        pagesFileSize_ += buf.size();

        auto& pe = pageEntries_[pageNum];
        pe.offset = offset;
        pe.byteLength = static_cast<uint32_t>(buf.size());
        pe.recordCount = static_cast<uint16_t>(count);
        pe.crc32 = crc;
    }

    bool durable = fflush(pf) == 0;
    if (durable) durable = fsync(fileno(pf)) == 0;
    if (fclose(pf) != 0) durable = false;
    if (!durable) return false;

    if (!writePtable(meta, totalRecords)) {
        return false;
    }

    engine.clearDirtyPages();
    return true;
}

// ─── fullRewrite ───

bool PagedIndexWriter::fullRewrite(SearchEngine& engine, const IndexMetadata& meta) {
    const uint64_t rewriteGeneration = engine.fullRewriteGeneration();
    uint32_t totalRecords = engine.recordCount();
    uint32_t pageCount = (totalRecords + SearchEngine::kRecordsPerPage - 1) / SearchEngine::kRecordsPerPage;
    if (totalRecords == 0) pageCount = 0;

    // Snapshot path dictionaries for v5 ptable
    pathDict_ = engine.pathPoolSnapshot();
    {
        // Generate lower path dict on-the-fly from pathPool (lowerPathPool_ eliminated)
        StringPool lpd;
        for (uint32_t i = 0; i < pathDict_.entryCount(); i++) {
            if (pathDict_.isLive(i)) {
                lpd.append(SearchEngine::lowerPathStr(pathDict_, i));
            } else {
                lpd.append("");
                lpd.tombstone(i);
            }
        }
        lowerPathDict_ = std::move(lpd);
    }

    std::string tmpPages = pagesPath_ + ".tmp";
    FILE* pf = fopen(tmpPages.c_str(), "wb");
    if (!pf) return false;

    fcntl(fileno(pf), F_NOCACHE, 1);

    // Write pages magic
    uint32_t magic = kPagesMagic;
    if (fwrite(&magic, 4, 1, pf) != 1) { fclose(pf); remove(tmpPages.c_str()); return false; }
    uint64_t writePos = 4;

    std::vector<PageEntry> newEntries;
    newEntries.reserve(pageCount);

    for (uint32_t page = 0; page < pageCount; page++) {
        uint32_t startIdx = page * SearchEngine::kRecordsPerPage;
        uint32_t count = std::min(SearchEngine::kRecordsPerPage,
                                  totalRecords - startIdx);

        std::vector<uint8_t> buf;
        buf.reserve(count * 80);

        engine.forEachRecordInRangeV5(startIdx, count,
            [&](uint32_t, const FileRecord& r, uint32_t pathIdx,
                const char* lowerName, uint16_t lowerNameLen) {
                appendRecordV5(buf, r, pathIdx, lowerName, lowerNameLen);
            });

        uint32_t crc = IndexWAL::crc32(buf.data(), buf.size());

        if (fwrite(buf.data(), 1, buf.size(), pf) != buf.size()) {
            fclose(pf);
            remove(tmpPages.c_str());
            return false;
        }

        PageEntry pe;
        pe.offset = writePos;
        pe.byteLength = static_cast<uint32_t>(buf.size());
        pe.recordCount = static_cast<uint16_t>(count);
        pe.crc32 = crc;
        newEntries.push_back(pe);

        writePos += buf.size();
    }

    bool durable = fflush(pf) == 0;
    if (durable) durable = fsync(fileno(pf)) == 0;
    if (fclose(pf) != 0) durable = false;
    if (!durable) {
        remove(tmpPages.c_str());
        return false;
    }

    pageEntries_ = std::move(newEntries);
    pagesFileSize_ = writePos;

    // Publish page data before the table that references its new offsets.
    if (rename(tmpPages.c_str(), pagesPath_.c_str()) != 0) {
        remove(tmpPages.c_str());
        return false;
    }
    if (!PathUtils::syncParentDirectory(pagesPath_)) return false;

    if (!writePtable(meta, totalRecords)) {
        return false;
    }

    engine.clearDirtyPages();
    engine.acknowledgeFullRewrite(rewriteGeneration);
    return true;
}

// ─── deadSpaceRatio ───

double PagedIndexWriter::deadSpaceRatio() const {
    if (pagesFileSize_ <= 4) return 0.0;
    uint64_t liveBytes = 0;
    for (const auto& pe : pageEntries_) {
        liveBytes += pe.byteLength;
    }
    uint64_t dataSize = pagesFileSize_ - 4;
    if (dataSize == 0) return 0.0;
    return 1.0 - static_cast<double>(liveBytes) / static_cast<double>(dataSize);
}

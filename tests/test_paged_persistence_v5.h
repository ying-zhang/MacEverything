#pragma once
// Part 53: v5 paged persistence tests
// Verifies v5 format: path dictionary + namePool direct write,
// CRC integrity, v4→v5 migration, tombstones, case preservation.

#include <filesystem>
namespace fs = std::filesystem;

static void testV5BasicRoundTrip() {
    std::cout << "\n--- v5: basic round-trip (2048 records, 2 pages) ---\n";

    std::string tmpDir = "/tmp/test_v5_basic_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 2048; i++) {
        FileRecord rec;
        rec.name = "File" + std::to_string(i) + ".TXT"; // mixed case
        rec.path = "/data/dir" + std::to_string(i % 5);
        rec.type = 1;
        rec.size = i * 10;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }
    check(engine->liveRecordCount() == 2048, "P53-1: engine has 2048 records");

    PagedIndexWriter writer(pagesPath, ptablePath);
    IndexMetadata meta;
    meta.lastEventId = 42;
    check(writer.fullRewrite(*engine, meta), "P53-1: fullRewrite succeeds");

    // Verify version is v5
    {
        FILE* f = fopen(ptablePath.c_str(), "rb");
        check(f != nullptr, "P53-1: can open ptable");
        uint32_t magic, ver;
        fread(&magic, 4, 1, f);
        fread(&ver, 4, 1, f);
        fclose(f);
        check(magic == PagedIndexWriter::kPtableMagic, "P53-1: ptable magic correct");
        check(ver == PagedIndexWriter::kVersionV5, "P53-1: ptable version is v5 (2)");
    }

    // Load into fresh engine
    auto engine2 = std::make_shared<SearchEngine>();
    PagedIndexWriter reader(pagesPath, ptablePath);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "P53-1: load succeeds");
    check(loadedMeta.lastEventId == 42, "P53-1: lastEventId preserved");
    check(engine2->liveRecordCount() == 2048, "P53-1: loaded 2048 records");

    // Verify search works (case-insensitive)
    auto results = engine2->query("file100.txt", 10);
    bool found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "File100.TXT" && rec.size == 1000) { found = true; break; }
    }
    check(found, "P53-1: file100.txt found via case-insensitive search, original case preserved");

    // Verify last record
    results = engine2->query("file2047.txt", 10);
    found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "File2047.TXT") { found = true; break; }
    }
    check(found, "P53-1: last record found");

    // Verify path resolution
    results = engine2->query("file0.txt", 10);
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "File0.TXT") {
            std::string path = engine2->resolveRecordPath(idx);
            check(path == "/data/dir0", "P53-1: path resolved correctly for File0.TXT");
            break;
        }
    }

    fs::remove_all(tmpDir);
}

static void testV5IncrementalFlush() {
    std::cout << "\n--- v5: incremental flush with new paths ---\n";

    std::string tmpDir = "/tmp/test_v5_incr_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 2048; i++) {
        FileRecord rec;
        rec.name = "f" + std::to_string(i) + ".txt";
        rec.path = "/data/original";
        rec.type = 1;
        rec.size = 100;
        rec.modTime = 1000000;
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    PagedIndexWriter writer(pagesPath, ptablePath);
    IndexMetadata meta;
    meta.lastEventId = 1;
    check(writer.fullRewrite(*engine, meta), "P53-2: initial fullRewrite");
    auto sizeAfterFull = fs::file_size(pagesPath);

    // Add records with NEW paths (new pathPool entries)
    engine->clearDirtyPages();
    for (uint32_t i = 0; i < 10; i++) {
        FileRecord rec;
        rec.name = "new" + std::to_string(i) + ".txt";
        rec.path = "/data/newdir" + std::to_string(i); // new unique paths
        rec.type = 1;
        rec.size = 200;
        rec.modTime = 2000000;
        rec.inode = 10000 + i;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    meta.lastEventId = 2;
    check(writer.flushDirtyPages(*engine, meta), "P53-2: flushDirtyPages succeeds");

    auto sizeAfterFlush = fs::file_size(pagesPath);
    check(sizeAfterFlush > sizeAfterFull, "P53-2: .pages grew after incremental flush");

    // Reload and verify
    auto engine2 = std::make_shared<SearchEngine>();
    PagedIndexWriter reader(pagesPath, ptablePath);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "P53-2: reload succeeds");
    check(loadedMeta.lastEventId == 2, "P53-2: lastEventId updated");
    check(engine2->liveRecordCount() == 2058, "P53-2: 2058 live records (2048 + 10 new)");

    // Verify new records with new paths
    auto results = engine2->query("new5.txt", 10);
    bool found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "new5.txt") {
            std::string path = engine2->resolveRecordPath(idx);
            check(path == "/data/newdir5", "P53-2: new path resolved correctly");
            found = true;
            break;
        }
    }
    check(found, "P53-2: new record with new path found after flush");

    fs::remove_all(tmpDir);
}

static void testV5PathDictionaryIntegrity() {
    std::cout << "\n--- v5: path dictionary integrity (1000+ unique paths) ---\n";

    std::string tmpDir = "/tmp/test_v5_pathdict_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";

    auto engine = std::make_shared<SearchEngine>();
    // Create records with many unique paths
    for (uint32_t i = 0; i < 1500; i++) {
        FileRecord rec;
        rec.name = "item" + std::to_string(i) + ".dat";
        rec.path = "/root/level1_" + std::to_string(i / 100) +
                   "/level2_" + std::to_string(i / 10) +
                   "/level3_" + std::to_string(i);
        rec.type = 1;
        rec.size = i;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    PagedIndexWriter writer(pagesPath, ptablePath);
    IndexMetadata meta;
    meta.lastEventId = 99;
    check(writer.fullRewrite(*engine, meta), "P53-3: fullRewrite with 1500 unique paths");

    // Reload
    auto engine2 = std::make_shared<SearchEngine>();
    PagedIndexWriter reader(pagesPath, ptablePath);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "P53-3: load succeeds");
    check(engine2->liveRecordCount() == 1500, "P53-3: 1500 records loaded");

    // Verify a sample of paths
    bool allCorrect = true;
    for (uint32_t i : {0u, 42u, 100u, 500u, 999u, 1499u}) {
        auto results = engine2->query("item" + std::to_string(i) + ".dat", 5);
        bool found = false;
        for (uint32_t idx : results) {
            auto rec = engine2->getRecord(idx);
            if (rec.name == "item" + std::to_string(i) + ".dat") {
                std::string expectedPath = "/root/level1_" + std::to_string(i / 100) +
                                           "/level2_" + std::to_string(i / 10) +
                                           "/level3_" + std::to_string(i);
                std::string actualPath = engine2->resolveRecordPath(idx);
                if (actualPath != expectedPath) {
                    std::cout << "  MISMATCH at i=" << i << ": expected=" << expectedPath
                              << " actual=" << actualPath << "\n";
                    allCorrect = false;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            std::cout << "  NOT FOUND: item" << i << ".dat\n";
            allCorrect = false;
        }
    }
    check(allCorrect, "P53-3: all sampled paths resolve correctly");

    fs::remove_all(tmpDir);
}

static void testV5CRCCorruptionPathDict() {
    std::cout << "\n--- v5: CRC corruption in path dictionary ---\n";

    std::string tmpDir = "/tmp/test_v5_crc_pdict_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 100; i++) {
        FileRecord rec;
        rec.name = "crc" + std::to_string(i) + ".txt";
        rec.path = "/test/path" + std::to_string(i % 10);
        rec.type = 1;
        rec.size = 50;
        rec.modTime = 1000000;
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    PagedIndexWriter writer(pagesPath, ptablePath);
    IndexMetadata meta;
    meta.lastEventId = 10;
    check(writer.fullRewrite(*engine, meta), "P53-4: write for CRC test");

    // Tamper with the path dictionary region in ptable (near the end of file)
    {
        auto ptableSize = fs::file_size(ptablePath);
        FILE* f = fopen(ptablePath.c_str(), "r+b");
        check(f != nullptr, "P53-4: open ptable for tampering");
        if (f) {
            // Corrupt bytes near end (path dictionary area)
            long tamperOffset = static_cast<long>(ptableSize) - 20;
            if (tamperOffset > 0) {
                fseek(f, tamperOffset, SEEK_SET);
                uint8_t garbage[] = {0xFF, 0xFF, 0xFF, 0xFF};
                fwrite(garbage, 1, sizeof(garbage), f);
            }
            fclose(f);
        }
    }

    auto engine2 = std::make_shared<SearchEngine>();
    PagedIndexWriter reader(pagesPath, ptablePath);
    IndexMetadata loadedMeta;
    std::cerr.setstate(std::ios_base::failbit);
    bool loadResult = reader.load(*engine2, &loadedMeta);
    std::cerr.clear();
    check(!loadResult, "P53-4: load fails when path dictionary CRC is corrupted");

    fs::remove_all(tmpDir);
}

static void testV5CRCCorruptionPageBlob() {
    std::cout << "\n--- v5: CRC corruption in page blob ---\n";

    std::string tmpDir = "/tmp/test_v5_crc_page_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 100; i++) {
        FileRecord rec;
        rec.name = "blob" + std::to_string(i) + ".txt";
        rec.path = "/blobtest";
        rec.type = 1;
        rec.size = 50;
        rec.modTime = 1000000;
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    PagedIndexWriter writer(pagesPath, ptablePath);
    IndexMetadata meta;
    meta.lastEventId = 5;
    check(writer.fullRewrite(*engine, meta), "P53-5: write for page blob CRC test");

    // Tamper with .pages data (past the 4-byte magic)
    {
        FILE* f = fopen(pagesPath.c_str(), "r+b");
        check(f != nullptr, "P53-5: open .pages for tampering");
        if (f) {
            fseek(f, 20, SEEK_SET);
            uint8_t garbage[] = {0xFF, 0xFF, 0xFF, 0xFF};
            fwrite(garbage, 1, sizeof(garbage), f);
            fclose(f);
        }
    }

    auto engine2 = std::make_shared<SearchEngine>();
    PagedIndexWriter reader(pagesPath, ptablePath);
    IndexMetadata loadedMeta;
    std::cerr.setstate(std::ios_base::failbit);
    bool loadResult = reader.load(*engine2, &loadedMeta);
    std::cerr.clear();
    check(!loadResult, "P53-5: load fails when .pages data is corrupted");

    fs::remove_all(tmpDir);
}

static void testV4ToV5Migration() {
    std::cout << "\n--- v5: v4 → v5 migration ---\n";

    std::string tmpDir = "/tmp/test_v5_migrate_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";

    // Write v4 format by manually creating a v4-style ptable
    // Use saveToFile (v3) then IndexPersistence to migrate to v4 paged format
    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 500; i++) {
        FileRecord rec;
        rec.name = "v4file" + std::to_string(i) + ".txt";
        rec.path = "/legacy/dir" + std::to_string(i % 5);
        rec.type = 1;
        rec.size = i * 5;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    // Write v4 format: manually write ptable with version=1 and v4 page blobs
    {
        // Write pages file with v4 record format
        FILE* pf = fopen(pagesPath.c_str(), "wb");
        check(pf != nullptr, "P53-6: create .pages");
        uint32_t magic = PagedIndexWriter::kPagesMagic;
        fwrite(&magic, 4, 1, pf);

        uint32_t totalRecords = engine->recordCount();
        uint32_t pageCount = (totalRecords + SearchEngine::kRecordsPerPage - 1)
                             / SearchEngine::kRecordsPerPage;

        struct TmpPageEntry {
            uint64_t offset;
            uint32_t byteLength;
            uint16_t recordCount;
            uint32_t crc32;
        };
        std::vector<TmpPageEntry> entries;
        uint64_t writePos = 4;

        for (uint32_t page = 0; page < pageCount; page++) {
            uint32_t startIdx = page * SearchEngine::kRecordsPerPage;
            uint32_t count = std::min(SearchEngine::kRecordsPerPage,
                                      totalRecords - startIdx);

            std::vector<uint8_t> buf;
            buf.reserve(count * 100);

            engine->forEachRecordInRange(startIdx, count,
                [&](uint32_t, const FileRecord& r, const std::string& path) {
                    // v4 format: appendRecordV4 equivalent
                    auto appendU32 = [&](uint32_t v) {
                        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v),
                                   reinterpret_cast<uint8_t*>(&v) + 4);
                    };
                    auto appendStr = [&](const std::string& s) {
                        uint32_t len = static_cast<uint32_t>(s.size());
                        appendU32(len);
                        buf.insert(buf.end(), s.begin(), s.end());
                    };
                    appendStr(r.name);
                    appendStr(path);
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
                });

            uint32_t crc = IndexWAL::crc32(buf.data(), buf.size());
            fwrite(buf.data(), 1, buf.size(), pf);

            TmpPageEntry pe;
            pe.offset = writePos;
            pe.byteLength = static_cast<uint32_t>(buf.size());
            pe.recordCount = static_cast<uint16_t>(count);
            pe.crc32 = crc;
            entries.push_back(pe);
            writePos += buf.size();
        }
        fsync(fileno(pf));
        fclose(pf);

        // Write ptable with version=1 (v4) — NO path dictionaries
        FILE* ptf = fopen(ptablePath.c_str(), "wb");
        check(ptf != nullptr, "P53-6: create .ptable");
        magic = PagedIndexWriter::kPtableMagic;
        fwrite(&magic, 4, 1, ptf);
        uint32_t ver = PagedIndexWriter::kVersionV4; // v4
        fwrite(&ver, 4, 1, ptf);
        int64_t ts = static_cast<int64_t>(time(nullptr));
        fwrite(&ts, 8, 1, ptf);
        uint64_t evtId = 77;
        fwrite(&evtId, 8, 1, ptf);
        uint32_t metaCount = 0;
        fwrite(&metaCount, 4, 1, ptf);
        uint32_t pageSize = SearchEngine::kRecordsPerPage;
        fwrite(&pageSize, 4, 1, ptf);
        fwrite(&totalRecords, 4, 1, ptf);
        fwrite(&pageCount, 4, 1, ptf);
        for (auto& pe : entries) {
            fwrite(&pe.offset, 8, 1, ptf);
            fwrite(&pe.byteLength, 4, 1, ptf);
            fwrite(&pe.recordCount, 2, 1, ptf);
            fwrite(&pe.crc32, 4, 1, ptf);
        }
        // No path dictionaries for v4
        fsync(fileno(ptf));
        fclose(ptf);
    }

    // v4 format is no longer supported — load should fail gracefully
    auto engine2 = std::make_shared<SearchEngine>();
    PagedIndexWriter loader(pagesPath, ptablePath);
    IndexMetadata loadedMeta;
    check(!loader.load(*engine2, &loadedMeta), "P53-6: v4 format correctly rejected");
    check(engine2->liveRecordCount() == 0, "P53-6: no records loaded from rejected v4");

    fs::remove_all(tmpDir);
}

static void testV5TombstonePreservation() {
    std::cout << "\n--- v5: tombstone preservation ---\n";

    std::string tmpDir = "/tmp/test_v5_tomb_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 100; i++) {
        FileRecord rec;
        rec.name = "tomb" + std::to_string(i) + ".txt";
        rec.path = "/graveyard";
        rec.type = 1;
        rec.size = 100;
        rec.modTime = 1000000;
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    // Remove some records (creates tombstones)
    engine->removeByPath("/graveyard/tomb10.txt");
    engine->removeByPath("/graveyard/tomb50.txt");
    engine->removeByPath("/graveyard/tomb99.txt");
    check(engine->liveRecordCount() == 97, "P53-7: 97 live after 3 removals");

    PagedIndexWriter writer(pagesPath, ptablePath);
    IndexMetadata meta;
    meta.lastEventId = 1;
    check(writer.fullRewrite(*engine, meta), "P53-7: fullRewrite with tombstones");

    // Reload
    auto engine2 = std::make_shared<SearchEngine>();
    PagedIndexWriter reader(pagesPath, ptablePath);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "P53-7: load succeeds");
    // After reload, tombstones are preserved (type=0) but liveCount excludes them
    check(engine2->liveRecordCount() == 97, "P53-7: 97 live after reload (tombstones preserved)");

    // Verify removed records are NOT searchable
    auto results = engine2->query("tomb10.txt", 10);
    bool foundRemoved = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "tomb10.txt") { foundRemoved = true; break; }
    }
    check(!foundRemoved, "P53-7: tombstoned record tomb10.txt not searchable");

    // Verify live records ARE searchable
    results = engine2->query("tomb11.txt", 10);
    bool foundLive = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "tomb11.txt") { foundLive = true; break; }
    }
    check(foundLive, "P53-7: live record tomb11.txt searchable");

    fs::remove_all(tmpDir);
}

static void testV5NameCasePreservation() {
    std::cout << "\n--- v5: name case preservation ---\n";

    std::string tmpDir = "/tmp/test_v5_case_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";

    auto engine = std::make_shared<SearchEngine>();
    // Add records with mixed case names
    std::vector<std::string> names = {
        "README.md", "Makefile", "CMakeLists.txt",
        "MyClass.CPP", "MyClass.H", "UPPERCASE.TXT",
        "lowercase.txt", "MiXeD_CaSe.DaT"
    };
    for (uint32_t i = 0; i < names.size(); i++) {
        FileRecord rec;
        rec.name = names[i];
        rec.path = "/src";
        rec.type = 1;
        rec.size = i * 100;
        rec.modTime = 1000000;
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    PagedIndexWriter writer(pagesPath, ptablePath);
    IndexMetadata meta;
    meta.lastEventId = 1;
    check(writer.fullRewrite(*engine, meta), "P53-8: fullRewrite with mixed case names");

    auto engine2 = std::make_shared<SearchEngine>();
    PagedIndexWriter reader(pagesPath, ptablePath);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "P53-8: load succeeds");

    // Verify each name preserves its original case
    bool allCaseCorrect = true;
    for (const auto& origName : names) {
        // Search using lowercase
        std::string lower = origName;
        for (auto& c : lower) c = std::tolower(c);
        auto results = engine2->query(lower, 10);
        bool found = false;
        for (uint32_t idx : results) {
            auto rec = engine2->getRecord(idx);
            if (rec.name == origName) { found = true; break; }
        }
        if (!found) {
            std::cout << "  CASE MISMATCH: expected '" << origName << "'\n";
            allCaseCorrect = false;
        }
    }
    check(allCaseCorrect, "P53-8: all names preserve original case after v5 round-trip");

    fs::remove_all(tmpDir);
}

static void testV5EmptyEngine() {
    std::cout << "\n--- v5: empty engine round-trip ---\n";

    std::string tmpDir = "/tmp/test_v5_empty_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";

    auto engine = std::make_shared<SearchEngine>();
    // No records

    PagedIndexWriter writer(pagesPath, ptablePath);
    IndexMetadata meta;
    meta.lastEventId = 0;
    check(writer.fullRewrite(*engine, meta), "P53-9: fullRewrite with 0 records");

    auto engine2 = std::make_shared<SearchEngine>();
    PagedIndexWriter reader(pagesPath, ptablePath);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "P53-9: load empty v5 index");
    check(engine2->liveRecordCount() == 0, "P53-9: 0 records after loading empty v5");

    fs::remove_all(tmpDir);
}

static void testV5DeadSpaceReclamation() {
    std::cout << "\n--- v5: dead space reclamation ---\n";

    std::string tmpDir = "/tmp/test_v5_dead_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 1024; i++) {
        FileRecord rec;
        rec.name = "dead" + std::to_string(i) + ".txt";
        rec.path = "/test";
        rec.type = 1;
        rec.size = 100;
        rec.modTime = 1000000;
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    PagedIndexWriter writer(pagesPath, ptablePath);
    IndexMetadata meta;
    meta.lastEventId = 1;
    check(writer.fullRewrite(*engine, meta), "P53-10: initial fullRewrite");
    check(writer.deadSpaceRatio() < 0.01, "P53-10: no dead space after fullRewrite");

    auto sizeAfterFull = fs::file_size(pagesPath);

    // Create dead space via repeated dirty flushes
    for (int round = 0; round < 5; round++) {
        FileRecord updated;
        updated.name = "dead0.txt";
        updated.path = "/test";
        updated.type = 1;
        updated.size = 200 + round;
        updated.modTime = 2000000;
        updated.inode = 1;
        updated.devId = 1;
        engine->updateByPath("/test/dead0.txt", std::move(updated));

        meta.lastEventId = static_cast<uint64_t>(2 + round);
        check(writer.flushDirtyPages(*engine, meta),
              ("P53-10: flush round " + std::to_string(round)).c_str());
    }

    auto sizeAfterFlushes = fs::file_size(pagesPath);
    check(sizeAfterFlushes > sizeAfterFull, "P53-10: .pages grew from repeated flushes");
    check(writer.deadSpaceRatio() > 0.1, "P53-10: dead space ratio > 10%");

    // Reclaim
    meta.lastEventId = 100;
    check(writer.fullRewrite(*engine, meta), "P53-10: fullRewrite to reclaim");
    auto sizeAfterReclaim = fs::file_size(pagesPath);
    check(sizeAfterReclaim < sizeAfterFlushes, "P53-10: .pages shrunk after reclaim");
    check(writer.deadSpaceRatio() < 0.01, "P53-10: dead space ratio ~0 after reclaim");

    // Verify data integrity after reclaim
    auto engine2 = std::make_shared<SearchEngine>();
    PagedIndexWriter reader(pagesPath, ptablePath);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "P53-10: load after reclaim succeeds");
    // All 1024 records should survive (updates modified dead0.txt in-place, no removals)
    check(engine2->liveRecordCount() == 1024, "P53-10: all 1024 records present after reclaim");

    fs::remove_all(tmpDir);
}

static void runPagedPersistenceV5Tests() {
    std::cout << "═══ Part 53: Paged Persistence v5 ═══\n";

    testV5BasicRoundTrip();
    testV5IncrementalFlush();
    testV5PathDictionaryIntegrity();
    testV5CRCCorruptionPathDict();
    testV5CRCCorruptionPageBlob();
    testV4ToV5Migration();
    testV5TombstonePreservation();
    testV5NameCasePreservation();
    testV5EmptyEngine();
    testV5DeadSpaceReclamation();

    std::cout << "\n";
}

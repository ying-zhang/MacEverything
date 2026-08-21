#pragma once
// v6 Flat SoA persistence tests
// Verifies v6 format: single-file columnar layout, CRC integrity,
// two-stage Phase 2 startup, WAL replay, v5->v6 migration, concurrency.

// Headers included via test_all.cpp: SearchEngine.h, FlatIndexWriter.h, etc.
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: Basic roundtrip — 2048 records, fullRewrite + load
// ─────────────────────────────────────────────────────────────────────────────
static void testV6BasicRoundtrip() {
    std::cout << "\n--- v6: basic round-trip (2048 records) ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_basic").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 2048; i++) {
        FileRecord rec;
        rec.name = "File" + std::to_string(i) + ".TXT";
        rec.path = "/data/dir" + std::to_string(i % 5);
        rec.type = 1;
        rec.size = i * 10;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }
    check(engine->liveRecordCount() == 2048, "V6-1: engine has 2048 records");

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 42;
    check(writer.fullRewrite(*engine, meta), "V6-1: fullRewrite succeeds");

    // Load into fresh engine
    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "V6-1: load succeeds");
    check(loadedMeta.lastEventId == 42, "V6-1: lastEventId preserved");
    check(engine2->recordCount() == 2048, "V6-1: recordCount matches");
    check(engine2->liveRecordCount() == 2048, "V6-1: liveCount matches");

    // Complete Phase 2 so query works with trigrams
    engine2->completePhase2();
    check(!engine2->isPhase2Pending(), "V6-1: Phase 2 completed");

    // Search by name (case-insensitive)
    auto results = engine2->query("file100.txt", 10);
    bool found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "File100.TXT" && rec.size == 1000) { found = true; break; }
    }
    check(found, "V6-1: file100.txt found via search, original case preserved");

    // Path resolution
    results = engine2->query("file0.txt", 10);
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "File0.TXT") {
            std::string path = engine2->resolveRecordPath(idx);
            check(path == "/data/dir0", "V6-1: path resolved correctly for File0.TXT");
            break;
        }
    }

    // Last record
    results = engine2->query("file2047.txt", 10);
    found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "File2047.TXT") { found = true; break; }
    }
    check(found, "V6-1: last record found");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Tombstone preservation
// ─────────────────────────────────────────────────────────────────────────────
static void testV6TombstonePreservation() {
    std::cout << "\n--- v6: tombstone preservation (100 records, 30 deleted) ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_tomb").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";

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

    // Delete 30 records (indices 0-29)
    for (uint32_t i = 0; i < 30; i++) {
        engine->removeByPath("/graveyard/tomb" + std::to_string(i) + ".txt");
    }
    check(engine->liveRecordCount() == 70, "V6-2: 70 live after 30 removals");

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 1;
    check(writer.fullRewrite(*engine, meta), "V6-2: fullRewrite with tombstones");

    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "V6-2: load succeeds");
    check(engine2->liveRecordCount() == 70, "V6-2: liveCount matches (70 live)");

    // Complete Phase 2 for search
    engine2->completePhase2();

    // Verify tombstoned records are not searchable
    auto results = engine2->query("tomb5.txt", 10);
    bool foundRemoved = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "tomb5.txt") { foundRemoved = true; break; }
    }
    check(!foundRemoved, "V6-2: tombstoned record tomb5.txt not searchable");

    // Verify live records are searchable
    results = engine2->query("tomb50.txt", 10);
    bool foundLive = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "tomb50.txt") { foundLive = true; break; }
    }
    check(foundLive, "V6-2: live record tomb50.txt searchable");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: CRC corruption detection
// ─────────────────────────────────────────────────────────────────────────────
static void testV6CRCCorruption() {
    std::cout << "\n--- v6: CRC corruption detection ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_crc").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 100; i++) {
        FileRecord rec;
        rec.name = "crc" + std::to_string(i) + ".txt";
        rec.path = "/test/crc";
        rec.type = 1;
        rec.size = 50;
        rec.modTime = 1000000;
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 10;
    check(writer.fullRewrite(*engine, meta), "V6-3: write for CRC test");

    // Corrupt 1 byte in a section body (past header + section table)
    // Header = 64 bytes, section table = 11 * 24 = 264 bytes, data starts at 328
    {
        auto fileSize = fs::file_size(v6Path);
        check(fileSize > 400, "V6-3: file large enough for corruption test");
        FILE* f = fopen(v6Path.c_str(), "r+b");
        check(f != nullptr, "V6-3: open v6 file for tampering");
        if (f) {
            // Corrupt a byte in the first section data area
            fseek(f, 350, SEEK_SET);
            uint8_t garbage = 0xFF;
            fwrite(&garbage, 1, 1, f);
            fclose(f);
        }
    }

    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    std::cerr.setstate(std::ios_base::failbit);
    bool loadResult = reader.load(*engine2, &loadedMeta);
    std::cerr.clear();
    check(!loadResult, "V6-3: load fails when section data is corrupted");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: Empty engine roundtrip
// ─────────────────────────────────────────────────────────────────────────────
static void testV6EmptyEngine() {
    std::cout << "\n--- v6: empty engine round-trip ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_empty").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";

    auto engine = std::make_shared<SearchEngine>();
    // No records added

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 0;
    check(writer.fullRewrite(*engine, meta), "V6-4: fullRewrite with 0 records");

    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "V6-4: load empty v6 index");
    check(engine2->recordCount() == 0, "V6-4: 0 records after loading empty v6");
    check(engine2->liveRecordCount() == 0, "V6-4: 0 live records after loading empty v6");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: Large scale — 100K+ records
// ─────────────────────────────────────────────────────────────────────────────
static void testV6LargeScale() {
    std::cout << "\n--- v6: large scale (100K records) ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_large").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";

    auto engine = std::make_shared<SearchEngine>();
    constexpr uint32_t N = 100000;
    for (uint32_t i = 0; i < N; i++) {
        FileRecord rec;
        rec.name = "large" + std::to_string(i) + ".dat";
        rec.path = "/vol/dir" + std::to_string(i % 200);
        rec.type = (i % 10 == 0) ? static_cast<uint8_t>(2) : static_cast<uint8_t>(1);
        rec.size = i * 3;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = static_cast<int32_t>(i % 3);
        engine->addRecord(std::move(rec));
    }
    check(engine->liveRecordCount() == N, "V6-5: engine has 100K records");

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 999;
    check(writer.fullRewrite(*engine, meta), "V6-5: fullRewrite 100K records");

    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "V6-5: load 100K records");
    check(loadedMeta.lastEventId == 999, "V6-5: lastEventId preserved");
    check(engine2->liveRecordCount() == N, "V6-5: liveCount matches 100K");

    // Complete Phase 2 and spot-check
    engine2->completePhase2();
    auto results = engine2->query("large50000.dat", 5);
    bool found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "large50000.dat" && rec.size == 50000 * 3) { found = true; break; }
    }
    check(found, "V6-5: spot-check record large50000.dat found with correct size");

    // Verify a record near the end
    results = engine2->query("large99999.dat", 5);
    found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "large99999.dat") { found = true; break; }
    }
    check(found, "V6-5: last record large99999.dat found");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: Phase 2 completion — trigram search deferred
// ─────────────────────────────────────────────────────────────────────────────
static void testV6Phase2Completion() {
    std::cout << "\n--- v6: Phase 2 completion ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_phase2").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 500; i++) {
        FileRecord rec;
        rec.name = "phase" + std::to_string(i) + ".cpp";
        rec.path = "/src/module" + std::to_string(i % 10);
        rec.type = 1;
        rec.size = i * 100;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 5;
    check(writer.fullRewrite(*engine, meta), "V6-6: fullRewrite for Phase 2 test");

    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "V6-6: load succeeds");

    // Phase 2 should be pending after v6 load
    check(engine2->isPhase2Pending(), "V6-6: isPhase2Pending==true after load");

    // Queries should still work (linear scan fallback)
    auto results = engine2->query("phase100.cpp", 5, false); // useTrigram=false
    bool found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "phase100.cpp") { found = true; break; }
    }
    check(found, "V6-6: linear scan query works before Phase 2");

    // Complete Phase 2
    engine2->completePhase2();
    check(!engine2->isPhase2Pending(), "V6-6: isPhase2Pending==false after completePhase2");

    // Now trigram search should work
    results = engine2->query("phase200.cpp", 5, true); // useTrigram=true
    found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "phase200.cpp") { found = true; break; }
    }
    check(found, "V6-6: trigram search works after Phase 2");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7: Phase 2 concurrent mutations
// ─────────────────────────────────────────────────────────────────────────────
static void testV6Phase2ConcurrentMutations() {
    std::cout << "\n--- v6: Phase 2 concurrent mutations ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_p2mut").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 1000; i++) {
        FileRecord rec;
        rec.name = "mut" + std::to_string(i) + ".txt";
        rec.path = "/data/mut";
        rec.type = 1;
        rec.size = 100;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 1;
    check(writer.fullRewrite(*engine, meta), "V6-7: fullRewrite for mutation test");

    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "V6-7: load succeeds");
    check(engine2->isPhase2Pending(), "V6-7: Phase 2 pending after load");

    // Add records while Phase 2 hasn't completed
    for (uint32_t i = 0; i < 50; i++) {
        FileRecord rec;
        rec.name = "new_during_p2_" + std::to_string(i) + ".txt";
        rec.path = "/data/new";
        rec.type = 1;
        rec.size = 200;
        rec.modTime = 2000000 + static_cast<time_t>(i);
        rec.inode = 10000 + i;
        rec.devId = 2;
        engine2->addRecord(std::move(rec));
    }

    // Delete some original records while Phase 2 hasn't completed
    for (uint32_t i = 0; i < 20; i++) {
        engine2->removeByPath("/data/mut/mut" + std::to_string(i) + ".txt");
    }

    // Now complete Phase 2 (should replay mutations)
    engine2->completePhase2();
    check(!engine2->isPhase2Pending(), "V6-7: Phase 2 completed after mutations");

    // Verify added records are searchable via trigram
    auto results = engine2->query("new_during_p2_25.txt", 5, true);
    bool found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "new_during_p2_25.txt") { found = true; break; }
    }
    check(found, "V6-7: record added during Phase 2 is searchable");

    // Verify deleted records are not searchable
    results = engine2->query("mut5.txt", 10, true);
    bool foundDeleted = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "mut5.txt") { foundDeleted = true; break; }
    }
    check(!foundDeleted, "V6-7: record deleted during Phase 2 is NOT searchable");

    // Verify overall count
    uint32_t expectedLive = 1000 - 20 + 50; // 1030
    check(engine2->liveRecordCount() == expectedLive,
          "V6-7: liveCount reflects mutations (1030)");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8: WAL replay after v6 load
// ─────────────────────────────────────────────────────────────────────────────
static void testV6WALReplay() {
    std::cout << "\n--- v6: WAL replay after v6 load ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_wal").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";
    std::string walPath = tmpDir + "/index.wal";

    // Create initial engine and persist v6
    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 200; i++) {
        FileRecord rec;
        rec.name = "wal" + std::to_string(i) + ".txt";
        rec.path = "/data/wal";
        rec.type = 1;
        rec.size = i * 10;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 100;
    check(writer.fullRewrite(*engine, meta), "V6-8: fullRewrite for WAL test");

    // Create WAL entries (simulating mutations after v6 snapshot)
    {
        auto wal = std::make_shared<IndexWAL>();
        check(wal->open(walPath), "V6-8: WAL open succeeds");

        // Add a new record
        FileRecord newRec;
        newRec.name = "wal_new.txt";
        newRec.path = "/data/wal";
        newRec.type = 1;
        newRec.size = 9999;
        newRec.modTime = 3000000;
        newRec.inode = 5000;
        newRec.devId = 1;
        wal->append(WALOp::Add, "/data/wal/wal_new.txt", newRec);

        // Remove a record
        wal->append(WALOp::Remove, "/data/wal/wal0.txt");

        wal->sync();
        wal->close();
    }

    // Load v6 into fresh engine, then replay WAL
    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "V6-8: load v6 succeeds");
    engine2->completePhase2();

    // Replay WAL
    auto walEntries = IndexWAL::readAll(walPath);
    check(walEntries.size() == 2, "V6-8: WAL has 2 entries");
    engine2->replayWALEntries(std::move(walEntries));

    // Verify new record added by WAL
    auto results = engine2->query("wal_new.txt", 5);
    bool found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "wal_new.txt" && rec.size == 9999) { found = true; break; }
    }
    check(found, "V6-8: WAL-added record wal_new.txt found");

    // Verify removed record is gone
    results = engine2->query("wal0.txt", 10);
    bool foundRemoved = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "wal0.txt") { foundRemoved = true; break; }
    }
    check(!foundRemoved, "V6-8: WAL-removed record wal0.txt not found");

    // Verify liveCount = 200 - 1 + 1 = 200
    check(engine2->liveRecordCount() == 200, "V6-8: liveCount correct after WAL replay (200)");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9: v5->v6 migration
// ─────────────────────────────────────────────────────────────────────────────
static void testV6MigrationFromV5() {
    std::cout << "\n--- v6: v5 -> v6 migration ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_migrate").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string pagesPath  = tmpDir + "/index.pages";
    std::string ptablePath = tmpDir + "/index.ptable";
    std::string v6Path     = tmpDir + "/index.v6";

    // Create records and persist as v5 paged format
    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 500; i++) {
        FileRecord rec;
        rec.name = "Migrate" + std::to_string(i) + ".TXT";
        rec.path = "/legacy/dir" + std::to_string(i % 5);
        rec.type = 1;
        rec.size = i * 7;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    PagedIndexWriter v5Writer(pagesPath, ptablePath);
    IndexMetadata v5Meta;
    v5Meta.lastEventId = 77;
    check(v5Writer.fullRewrite(*engine, v5Meta), "V6-9: v5 fullRewrite succeeds");

    // v6 load of v5 file should fail (wrong magic/format)
    {
        auto engineFail = std::make_shared<SearchEngine>();
        FlatIndexWriter v6Reader(pagesPath);
        IndexMetadata failMeta;
        std::cerr.setstate(std::ios_base::failbit);
        bool loadFailed = v6Reader.load(*engineFail, &failMeta);
        std::cerr.clear();
        check(!loadFailed, "V6-9: v6 load of v5 paged file fails as expected");
    }

    // Load via v5 into fresh engine
    auto engineV5 = std::make_shared<SearchEngine>();
    PagedIndexWriter v5Reader(pagesPath, ptablePath);
    IndexMetadata loadedV5Meta;
    check(v5Reader.load(*engineV5, &loadedV5Meta), "V6-9: v5 load succeeds");
    check(engineV5->liveRecordCount() == 500, "V6-9: v5 loaded 500 records");

    // Now fullRewrite as v6
    FlatIndexWriter v6Writer(v6Path);
    IndexMetadata v6Meta;
    v6Meta.lastEventId = loadedV5Meta.lastEventId;
    check(v6Writer.fullRewrite(*engineV5, v6Meta), "V6-9: v6 fullRewrite from v5 data");

    // Load v6
    auto engineV6 = std::make_shared<SearchEngine>();
    FlatIndexWriter v6Reader2(v6Path);
    IndexMetadata loadedV6Meta;
    check(v6Reader2.load(*engineV6, &loadedV6Meta), "V6-9: v6 load succeeds");
    check(engineV6->liveRecordCount() == 500, "V6-9: v6 has 500 records");
    check(loadedV6Meta.lastEventId == 77, "V6-9: lastEventId preserved through migration");

    // Verify data consistency
    engineV6->completePhase2();
    auto results = engineV6->query("migrate250.txt", 5);
    bool found = false;
    for (uint32_t idx : results) {
        auto rec = engineV6->getRecord(idx);
        if (rec.name == "Migrate250.TXT" && rec.size == 250 * 7) {
            std::string path = engineV6->resolveRecordPath(idx);
            check(path == "/legacy/dir0", "V6-9: path preserved through migration");
            found = true;
            break;
        }
    }
    check(found, "V6-9: data consistent after v5->v6 migration");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 10: Path pool integrity
// ─────────────────────────────────────────────────────────────────────────────
static void testV6PathPoolIntegrity() {
    std::cout << "\n--- v6: path pool integrity ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_pathpool").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";

    auto engine = std::make_shared<SearchEngine>();
    // Create records with many unique directory paths
    for (uint32_t i = 0; i < 1000; i++) {
        FileRecord rec;
        rec.name = "pp" + std::to_string(i) + ".dat";
        rec.path = "/root/level1_" + std::to_string(i / 100)
                   + "/level2_" + std::to_string(i / 10)
                   + "/level3_" + std::to_string(i);
        rec.type = 1;
        rec.size = i;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 55;
    check(writer.fullRewrite(*engine, meta), "V6-10: fullRewrite with 1000 unique paths");

    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "V6-10: load succeeds");
    check(engine2->liveRecordCount() == 1000, "V6-10: 1000 records loaded");

    engine2->completePhase2();

    // Verify pathPool and lowerPathPool roundtrip for a sample of records
    bool allPathsCorrect = true;
    for (uint32_t i : {0u, 42u, 100u, 500u, 777u, 999u}) {
        auto results = engine2->query("pp" + std::to_string(i) + ".dat", 5);
        bool found = false;
        for (uint32_t idx : results) {
            auto rec = engine2->getRecord(idx);
            if (rec.name == "pp" + std::to_string(i) + ".dat") {
                std::string expectedPath = "/root/level1_" + std::to_string(i / 100)
                                           + "/level2_" + std::to_string(i / 10)
                                           + "/level3_" + std::to_string(i);
                std::string actualPath = engine2->resolveRecordPath(idx);
                if (actualPath != expectedPath) {
                    std::cout << "  PATH MISMATCH at i=" << i << ": expected=" << expectedPath
                              << " actual=" << actualPath << "\n";
                    allPathsCorrect = false;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            std::cout << "  NOT FOUND: pp" << i << ".dat\n";
            allPathsCorrect = false;
        }
    }
    check(allPathsCorrect, "V6-10: all sampled paths resolve correctly after v6 roundtrip");

    // Verify internPath works on loaded engine: add a record with an existing path
    {
        FileRecord rec;
        rec.name = "pp_extra.dat";
        rec.path = "/root/level1_0/level2_0/level3_0"; // existing path
        rec.type = 1;
        rec.size = 12345;
        rec.modTime = 9999999;
        rec.inode = 99999;
        rec.devId = 1;
        engine2->addRecord(std::move(rec));
    }
    auto results = engine2->query("pp_extra.dat", 5);
    bool found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "pp_extra.dat") {
            std::string path = engine2->resolveRecordPath(idx);
            check(path == "/root/level1_0/level2_0/level3_0",
                  "V6-10: internPath works on loaded engine (existing path reused)");
            found = true;
            break;
        }
    }
    check(found, "V6-10: record with interned path found after addRecord on loaded engine");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 11: Phase 2 concurrent queries (no crash)
// ─────────────────────────────────────────────────────────────────────────────
static void testV6Phase2ConcurrentQueries() {
    std::cout << "\n--- v6: Phase 2 concurrent queries ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_p2query").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";

    auto engine = std::make_shared<SearchEngine>();
    for (uint32_t i = 0; i < 5000; i++) {
        FileRecord rec;
        rec.name = "conc" + std::to_string(i) + ".txt";
        rec.path = "/data/conc" + std::to_string(i % 50);
        rec.type = 1;
        rec.size = i * 5;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = i + 1;
        rec.devId = 1;
        engine->addRecord(std::move(rec));
    }

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 1;
    check(writer.fullRewrite(*engine, meta), "V6-11: fullRewrite for concurrent query test");

    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "V6-11: load succeeds");

    // Spawn query threads that run concurrently with Phase 2 completion
    std::atomic<bool> crashed{false};
    std::atomic<int> queriesRun{0};

    std::vector<std::thread> queryThreads;
    for (int t = 0; t < 4; t++) {
        queryThreads.emplace_back([&engine2, &crashed, &queriesRun, t] {
            try {
                for (int q = 0; q < 50; q++) {
                    std::string keyword = "conc" + std::to_string(t * 100 + q) + ".txt";
                    // Use useTrigram=false since Phase 2 may not be done
                    auto results = engine2->query(keyword, 5, false);
                    queriesRun.fetch_add(1, std::memory_order_relaxed);
                    // Small sleep to interleave with Phase 2
                    if (q % 10 == 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                }
            } catch (...) {
                crashed.store(true, std::memory_order_relaxed);
            }
        });
    }

    // Run Phase 2 concurrently
    std::thread phase2Thread([&engine2] {
        engine2->completePhase2();
    });

    phase2Thread.join();
    for (auto& th : queryThreads) th.join();

    check(!crashed.load(), "V6-11: no crash during concurrent queries + Phase 2");
    check(queriesRun.load() == 200, "V6-11: all 200 queries completed");
    check(!engine2->isPhase2Pending(), "V6-11: Phase 2 completed");

    // Verify engine is still functional after concurrent access
    auto results = engine2->query("conc2500.txt", 5);
    bool found = false;
    for (uint32_t idx : results) {
        auto rec = engine2->getRecord(idx);
        if (rec.name == "conc2500.txt") { found = true; break; }
    }
    check(found, "V6-11: engine functional after concurrent queries + Phase 2");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 12: inode/devId roundtrip
// ─────────────────────────────────────────────────────────────────────────────
static void testV6InodeDevIdRoundtrip() {
    std::cout << "\n--- v6: inode/devId roundtrip ---\n";

    std::string tmpDir = (fs::temp_directory_path() / "test_v6_inode").string()
                         + "_" + std::to_string(getpid());
    fs::create_directories(tmpDir);
    std::string v6Path = tmpDir + "/index.v6";

    auto engine = std::make_shared<SearchEngine>();

    // Use specific, distinctive inode/devId values
    struct TestCase {
        uint64_t inode;
        int32_t devId;
    };
    std::vector<TestCase> testCases = {
        {0, 0},
        {1, 1},
        {UINT64_MAX, INT32_MAX},
        {UINT64_MAX - 1, INT32_MIN},
        {12345678901234ULL, -42},
        {0xDEADBEEFCAFEULL, 0x7FFF},
        {1ULL << 48, -1},
        {999999999999ULL, 32767},
    };

    for (uint32_t i = 0; i < testCases.size(); i++) {
        FileRecord rec;
        rec.name = "inode_test_" + std::to_string(i) + ".bin";
        rec.path = "/dev/test";
        rec.type = 1;
        rec.size = i * 1000;
        rec.modTime = 1000000 + static_cast<time_t>(i);
        rec.inode = testCases[i].inode;
        rec.devId = testCases[i].devId;
        engine->addRecord(std::move(rec));
    }

    FlatIndexWriter writer(v6Path);
    IndexMetadata meta;
    meta.lastEventId = 1;
    check(writer.fullRewrite(*engine, meta), "V6-12: fullRewrite for inode/devId test");

    auto engine2 = std::make_shared<SearchEngine>();
    FlatIndexWriter reader(v6Path);
    IndexMetadata loadedMeta;
    check(reader.load(*engine2, &loadedMeta), "V6-12: load succeeds");

    engine2->completePhase2();

    // Verify each inode/devId value is preserved
    bool allMatch = true;
    for (uint32_t i = 0; i < testCases.size(); i++) {
        auto results = engine2->query("inode_test_" + std::to_string(i) + ".bin", 5);
        bool found = false;
        for (uint32_t idx : results) {
            auto rec = engine2->getRecord(idx);
            if (rec.name == "inode_test_" + std::to_string(i) + ".bin") {
                if (rec.inode != testCases[i].inode) {
                    std::cout << "  INODE MISMATCH at i=" << i
                              << ": expected=" << testCases[i].inode
                              << " actual=" << rec.inode << "\n";
                    allMatch = false;
                }
                if (rec.devId != testCases[i].devId) {
                    std::cout << "  DEVID MISMATCH at i=" << i
                              << ": expected=" << testCases[i].devId
                              << " actual=" << rec.devId << "\n";
                    allMatch = false;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            std::cout << "  NOT FOUND: inode_test_" << i << ".bin\n";
            allMatch = false;
        }
    }
    check(allMatch, "V6-12: all inode/devId values preserved after roundtrip");

    fs::remove_all(tmpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Runner
// ─────────────────────────────────────────────────────────────────────────────
static void runFlatPersistenceV6Tests() {
    std::cout << "\n=== Flat Persistence v6 Tests ===\n";

    testV6BasicRoundtrip();            // Test 1
    testV6TombstonePreservation();     // Test 2
    testV6CRCCorruption();             // Test 3
    testV6EmptyEngine();               // Test 4
    testV6LargeScale();                // Test 5
    testV6Phase2Completion();          // Test 6
    testV6Phase2ConcurrentMutations(); // Test 7
    testV6WALReplay();                 // Test 8
    testV6MigrationFromV5();           // Test 9
    testV6PathPoolIntegrity();         // Test 10
    testV6Phase2ConcurrentQueries();   // Test 11
    testV6InodeDevIdRoundtrip();       // Test 12

    std::cout << "\n";
}

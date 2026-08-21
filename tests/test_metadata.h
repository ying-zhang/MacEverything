#pragma once
// ═══════════════════════════════════════════════════════
//  Part 3c: Index Metadata & Version Tests
// ═══════════════════════════════════════════════════════

static void runIndexMetadataTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 3c: Index Metadata & Version Tests\n";
    std::cout << "========================================\n\n";

    std::string tmpFile = "/tmp/maceverything_meta_test_" + std::to_string(getpid()) + ".bin";

    // ── Test 1: Save and load v3 with metadata ──
    std::cout << "  --- Save/Load v3 with metadata ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"file1.txt", "/tmp", 1, 100, 1000, 12345, 1});
        records.push_back({"file2.cpp", "/src", 1, 200, 2000, 67890, 1});
        records.push_back({"mydir", "/var", 2, 0, 3000, 11111, 2});
        engine.loadRecords(std::move(records));

        IndexMetadata meta;
        meta.lastEventId = 42;
        meta.extra[IndexMetadata::kScanRoot] = "/";
        meta.extra[IndexMetadata::kAppVersion] = "1.1.0";
        meta.extra[IndexMetadata::kOSVersion] = "14.5.0";
        meta.extra[IndexMetadata::kRecordFormat] = "v3_inode";
        meta.extra["custom_key"] = "custom_value";

        bool saved = engine.saveToFile(tmpFile, meta);
        check(saved, "v3 saveToFile succeeded");
    }

    {
        SearchEngine engine;
        IndexMetadata loadedMeta;
        bool loaded = engine.loadFromFile(tmpFile, &loadedMeta);
        check(loaded, "v3 loadFromFile succeeded");
        check(loadedMeta.formatVersion == 3, "v3: formatVersion == 3");
        check(loadedMeta.lastEventId == 42, "v3: lastEventId == 42");
        check(loadedMeta.timestamp > 0, "v3: timestamp > 0");
        check(engine.liveRecordCount() == 3, "v3: 3 live records");

        // Check metadata key-value pairs
        check(loadedMeta.extra.count(IndexMetadata::kScanRoot) == 1, "v3: has scan_root key");
        check(loadedMeta.extra[IndexMetadata::kScanRoot] == "/", "v3: scan_root == '/'");
        check(loadedMeta.extra[IndexMetadata::kAppVersion] == "1.1.0", "v3: app_version == '1.1.0'");
        check(loadedMeta.extra[IndexMetadata::kOSVersion] == "14.5.0", "v3: os_version == '14.5.0'");
        check(loadedMeta.extra[IndexMetadata::kRecordFormat] == "v3_inode", "v3: record_format == 'v3_inode'");
        check(loadedMeta.extra["custom_key"] == "custom_value", "v3: custom_key preserved");

        // Verify record data integrity
        auto res = engine.query("file1");
        check(res.size() == 1, "v3 load: file1 queryable");
        const auto& r = engine.getRecord(res[0]);
        check(r.inode == 12345, "v3 load: inode preserved");
        check(r.devId == 1, "v3 load: devId preserved");
    }

    // ── Test 2: Legacy overload (uint64_t lastEventId) still works ──
    std::cout << "\n  --- Legacy overload compatibility ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"legacy.txt", "/old", 1, 50, 500});
        engine.loadRecords(std::move(records));

        bool saved = engine.saveToFile(tmpFile, (uint64_t)99);
        check(saved, "Legacy save overload succeeded");
    }

    {
        SearchEngine engine;
        uint64_t lastEventId = 0;
        bool loaded = engine.loadFromFile(tmpFile, &lastEventId);
        check(loaded, "Legacy load overload succeeded");
        check(lastEventId == 99, "Legacy load: lastEventId == 99");
        check(engine.liveRecordCount() == 1, "Legacy load: 1 record");
    }

    // ── Test 3: Empty metadata ──
    std::cout << "\n  --- Empty metadata ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"empty_meta.txt", "/test", 1, 10, 100});
        engine.loadRecords(std::move(records));

        IndexMetadata meta;
        meta.lastEventId = 0;
        // No extra metadata
        bool saved = engine.saveToFile(tmpFile, meta);
        check(saved, "Empty metadata save succeeded");
    }

    {
        SearchEngine engine;
        IndexMetadata loadedMeta;
        bool loaded = engine.loadFromFile(tmpFile, &loadedMeta);
        check(loaded, "Empty metadata load succeeded");
        check(loadedMeta.formatVersion == 3, "Empty metadata: formatVersion == 3");
        check(loadedMeta.extra.empty(), "Empty metadata: no extra keys");
    }

    // ── Test 4: Unknown version rejection ──
    std::cout << "\n  --- Unknown version rejection ---\n";
    {
        // Write a file with version 99
        FILE* f = fopen(tmpFile.c_str(), "wb");
        char magic[4] = {'M', 'E', 'I', 'D'};
        fwrite(magic, 1, 4, f);
        uint32_t badVersion = 99;
        fwrite(&badVersion, sizeof(uint32_t), 1, f);
        fclose(f);

        SearchEngine engine;
        bool loaded = engine.loadFromFile(tmpFile);
        check(!loaded, "Unknown version 99: loadFromFile returns false");
    }

    // Cleanup
    fs::remove(tmpFile);
    std::cout << "\n";
}

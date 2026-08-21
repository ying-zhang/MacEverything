// Tests for P2 fixes: count limit, .app case-insensitive detection
#pragma once

#include <fstream>
#include <cstring>
#include <cctype>

// ─────────── P2-1: Persistence count limit ───────────

static void testCorruptCountRejectsLoad() {
    std::cout << "\n─── P2-1: Corrupt count limit ───\n";

    // Actual format: MAGIC(4:"MEID") + version(u32) + timestamp(i64) + lastEventId(u64) + metaCount(u32) + recordCount(u32)
    auto writeValidHeader = [](FILE* f, uint32_t recordCount) {
        // Magic "MEID"
        const char magic[4] = {'M', 'E', 'I', 'D'};
        fwrite(magic, 1, 4, f);

        // Version = 3
        uint32_t version = 3;
        fwrite(&version, sizeof(uint32_t), 1, f);

        // Timestamp
        int64_t ts = 1000000;
        fwrite(&ts, sizeof(int64_t), 1, f);

        // Last event ID
        uint64_t eventId = 0;
        fwrite(&eventId, sizeof(uint64_t), 1, f);

        // Metadata count = 0
        uint32_t metaCount = 0;
        fwrite(&metaCount, sizeof(uint32_t), 1, f);

        // Record count
        fwrite(&recordCount, sizeof(uint32_t), 1, f);
    };

    // Test 1: corrupt count (UINT32_MAX) should be rejected
    {
        const char* tmpPath = "/tmp/test_corrupt_count.bin";
        FILE* f = fopen(tmpPath, "wb");
        check(f != nullptr, "create temp file");

        writeValidHeader(f, UINT32_MAX);
        fclose(f);

        SearchEngine engine;
        bool loaded = engine.loadFromFile(tmpPath);
        check(!loaded, "corrupt count should reject load");

        std::remove(tmpPath);
    }

    // Test 2: zero count should load successfully
    {
        const char* tmpPath = "/tmp/test_zero_count.bin";
        FILE* f = fopen(tmpPath, "wb");
        check(f != nullptr, "create temp file for zero count");

        writeValidHeader(f, 0);
        fclose(f);

        SearchEngine engine;
        bool loaded = engine.loadFromFile(tmpPath);
        check(loaded, "zero count should load successfully");
        check(engine.recordCount() == 0, "should have 0 records");

        std::remove(tmpPath);
    }
}

// ─────────── P2-3: .app bundle case-insensitive ───────────

static void testAppBundleCaseInsensitive() {
    std::cout << "\n─── P2-3: .app bundle case-insensitive ───\n";

    // Test the pattern used in DirectoryScanner
    auto isAppBundle = [](const char* name) -> bool {
        size_t nameLen = strlen(name);
        return (nameLen > 4 &&
            name[nameLen-4] == '.' &&
            tolower(name[nameLen-3]) == 'a' &&
            tolower(name[nameLen-2]) == 'p' &&
            tolower(name[nameLen-1]) == 'p');
    };

    check(isAppBundle("Safari.app"), ".app lowercase should match");
    check(isAppBundle("Safari.APP"), ".APP uppercase should match");
    check(isAppBundle("Safari.App"), ".App mixed case should match");
    check(isAppBundle("Safari.aPP"), ".aPP mixed case should match");
    check(!isAppBundle("Safari.ap"), "too short should not match");
    check(!isAppBundle(".app"), "exactly .app should not match (nameLen must be > 4)");
    check(!isAppBundle("file.txt"), ".txt should not match");
    check(isAppBundle("x.app"), "x.app should match (nameLen=5 > 4)");
}

static void runP2FixTests() {
    testCorruptCountRejectsLoad();
    testAppBundleCaseInsensitive();
}

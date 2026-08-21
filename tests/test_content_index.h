#pragma once
// Part 7f: ContentIndex basic tests

static void runContentIndexTests() {
    std::cout << "═══ Part 7f: ContentIndex Basic Tests ═══\n\n";

    // Test trigram extraction
    auto trigrams = ContentIndex::extractTrigrams("hello world");
    check(!trigrams.empty(), "ContentIndex: extractTrigrams returns non-empty for 'hello world'");
    check(trigrams.size() == 9, "ContentIndex: 'hello world' has 9 unique trigrams");

    // Test short string
    auto shortTri = ContentIndex::extractTrigrams("ab");
    check(shortTri.empty(), "ContentIndex: extractTrigrams empty for string < 3 chars");

    // Test binary file detection
    std::string tmpDir = "/tmp/maceverything_ci_test_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    // Create text file
    {
        std::ofstream ofs(tmpDir + "/text.txt");
        ofs << "This is a plain text file with content for trigram indexing.";
    }

    // Create binary file (with NUL byte)
    {
        FILE* f = fopen((tmpDir + "/binary.bin").c_str(), "wb");
        const char data[] = "some\0binary";
        fwrite(data, 1, sizeof(data) - 1, f);
        fclose(f);
    }

    // isBinaryFile was removed — binary detection is handled internally during indexing
    ContentIndex ci;
    ci.setExtensions({"txt", "", "bin"});
    auto configuredExtensions = ci.getExtensions();
    check(std::find(configuredExtensions.begin(), configuredExtensions.end(), "") ==
              configuredExtensions.end(),
          "ContentIndex: empty extensions are discarded before persistence");

    auto indexed = ci.indexFile(0, tmpDir + "/text.txt");
    check(indexed == ContentIndexUpdate::Upserted, "ContentIndex: indexFile succeeds for text file");

    auto binaryIndexed = ci.indexFile(1, tmpDir + "/binary.bin");
    check(binaryIndexed == ContentIndexUpdate::Unchanged, "ContentIndex: indexFile rejects binary file");
    check(ci.indexedFileCount() == 1, "ContentIndex: 1 file indexed");

    auto resolver = [&](uint32_t idx, std::string& path) {
        if (idx != 0) return false;
        path = tmpDir + "/text.txt";
        return true;
    };
    auto matches = ci.query("trigram", 100, resolver);
    check(!matches.empty(), "ContentIndex: query 'trigram' returns matches");
    check(matches[0].fileIndex == 0, "ContentIndex: match has correct fileIndex");

    auto noMatch = ci.query("zzzznotfound", 100, resolver);
    check(noMatch.empty(), "ContentIndex: query for non-existent keyword returns empty");

    // Invalid UTF-8 elsewhere in a text-like file must not hide an ASCII match.
    {
        const std::string invalidPath = tmpDir + "/invalid-utf8.txt";
        FILE* f = fopen(invalidPath.c_str(), "wb");
        const unsigned char bytes[] = {
            'p', 'r', 'e', 'f', 'i', 'x', ' ', 0xff, ' ',
            'H', 'e', 'L', 'L', 'o', ' ', 's', 'u', 'f', 'f', 'i', 'x'
        };
        fwrite(bytes, 1, sizeof(bytes), f);
        fclose(f);

        ContentIndex invalidIndex;
        invalidIndex.setExtensions({"txt"});
        check(invalidIndex.indexFile(9, invalidPath) == ContentIndexUpdate::Upserted,
              "ContentIndex: text-like invalid UTF-8 fixture indexed");
        auto invalidMatches = invalidIndex.query("hello", 10,
            [&](uint32_t idx, std::string& path) {
                if (idx != 9) return false;
                path = invalidPath;
                return true;
            });
        check(invalidMatches.size() == 1,
              "ContentIndex: ASCII match survives surrounding invalid UTF-8");
    }

    // Test persistence
    std::string savePath = tmpDir + "/ci.bin";
    bool saved = ci.saveToFile(savePath);
    check(saved, "ContentIndex: saveToFile succeeds");

    ContentIndex ci2;
    bool loaded = ci2.loadFromFile(savePath);
    check(loaded, "ContentIndex: loadFromFile succeeds");
    check(ci2.indexedFileCount() == 1, "ContentIndex: loaded index has 1 file");

    auto matches2 = ci2.query("trigram", 100, resolver);
    check(!matches2.empty(), "ContentIndex: loaded index can query successfully");
    check(ci2.getExtensions() == std::vector<std::string>{"txt", "bin"} ||
          ci2.getExtensions() == std::vector<std::string>{"bin", "txt"},
          "ContentIndex: persisted extension configuration is restored");

    // Unicode lowercasing and canonical equivalence must agree between index and query.
    {
        const std::string unicodePath = tmpDir + "/unicode.txt";
        { std::ofstream out(unicodePath); out << "CAF\xC3\x89 re\xCC\x81sume\xCC\x81"; }
        ContentIndex unicodeIndex;
        unicodeIndex.setExtensions({"txt"});
        check(unicodeIndex.indexFile(7, unicodePath) == ContentIndexUpdate::Upserted,
              "ContentIndex: Unicode fixture indexed");
        auto unicodeMatches = unicodeIndex.query("caf\xC3\xA9 R\xC3\x89SUM\xC3\x89", 10,
            [&](uint32_t idx, std::string& path) {
                if (idx != 7) return false;
                path = unicodePath;
                return true;
            });
        check(unicodeMatches.size() == 1,
              "ContentIndex: Unicode case and NFC/NFD variants match end to end");
    }

    // Test: indexFile returns false for unchanged file (no spurious WAL writes)
    ContentIndex ci3;
    ci3.setExtensions({"txt"});
    auto first = ci3.indexFile(0, tmpDir + "/text.txt");
    check(first == ContentIndexUpdate::Upserted, "ContentIndex: first indexFile returns updated");
    auto second = ci3.indexFile(0, tmpDir + "/text.txt");
    check(second == ContentIndexUpdate::Unchanged, "ContentIndex: second indexFile (unchanged) returns unchanged");
    check(ci3.indexedFileCount() == 1, "ContentIndex: still 1 file indexed after duplicate call");

    // A sparse but structurally valid file must be rejected by the preflight
    // memory budget before any large trigram vectors are allocated.
    {
        const std::string oversizedPath = tmpDir + "/oversized-ci.bin";
        FILE* f = fopen(oversizedPath.c_str(), "wb");
        const char magic[4] = {'M', 'E', 'C', 'I'};
        const uint32_t version = 4;
        const uint64_t maxFileSize = 1024 * 1024;
        const uint32_t extensionCount = 0;
        const uint32_t fileCount = 1'000;
        fwrite(magic, 1, sizeof(magic), f);
        fwrite(&version, sizeof(version), 1, f);
        fwrite(&maxFileSize, sizeof(maxFileSize), 1, f);
        fwrite(&extensionCount, sizeof(extensionCount), 1, f);
        fwrite(&fileCount, sizeof(fileCount), 1, f);
        for (uint32_t i = 0; i < fileCount; ++i) {
            const uint32_t pathLen = 0;
            const uint64_t hash = 0;
            const uint32_t triCount = 1'000'000;
            const int64_t modTime = 0;
            fwrite(&i, sizeof(i), 1, f);
            fwrite(&pathLen, sizeof(pathLen), 1, f);
            fwrite(&hash, sizeof(hash), 1, f);
            fwrite(&triCount, sizeof(triCount), 1, f);
            fseek(f, static_cast<long>(triCount * sizeof(Trigram)), SEEK_CUR);
            fwrite(&modTime, sizeof(modTime), 1, f);
        }
        fclose(f);

        ContentIndex oversizedIndex;
        check(!oversizedIndex.loadFromFile(oversizedPath),
              "ContentIndex: load rejects files exceeding the memory budget");
    }

    fs::remove_all(tmpDir);
    std::cout << "\n";
}

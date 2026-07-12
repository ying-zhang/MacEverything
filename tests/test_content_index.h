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
    ci.setExtensions({"txt", "bin"});

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

    // Test: indexFile returns false for unchanged file (no spurious WAL writes)
    ContentIndex ci3;
    ci3.setExtensions({"txt"});
    auto first = ci3.indexFile(0, tmpDir + "/text.txt");
    check(first == ContentIndexUpdate::Upserted, "ContentIndex: first indexFile returns updated");
    auto second = ci3.indexFile(0, tmpDir + "/text.txt");
    check(second == ContentIndexUpdate::Unchanged, "ContentIndex: second indexFile (unchanged) returns unchanged");
    check(ci3.indexedFileCount() == 1, "ContentIndex: still 1 file indexed after duplicate call");

    fs::remove_all(tmpDir);
    std::cout << "\n";
}

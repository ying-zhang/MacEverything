#pragma once
// Part 7: C1 — compactRecords + ContentIndex fileIndex remap

static void runCompactContentIndexTest() {
    std::cout << "═══ Part 7: CompactRecords + ContentIndex Remap ═══\n\n";

    // Create temp dir with test files
    std::string tmpDir = "/tmp/maceverything_c1_test_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    // Create test text files
    for (int i = 0; i < 5; i++) {
        std::string path = tmpDir + "/file" + std::to_string(i) + ".txt";
        std::ofstream ofs(path);
        ofs << "This is test file number " + std::to_string(i) + " with enough content to generate trigrams for indexing purposes.";
    }

    auto engine = std::make_shared<SearchEngine>();
    auto contentIndex = std::make_shared<ContentIndex>();
    contentIndex->setExtensions({"txt"});

    // Add records and index their content
    std::vector<uint32_t> indices;
    for (int i = 0; i < 5; i++) {
        FileRecord rec;
        rec.name = "file" + std::to_string(i) + ".txt";
        rec.path = tmpDir;
        rec.type = 1;
        rec.size = 100;
        rec.modTime = time(nullptr);
        uint32_t idx = engine->addRecord(std::move(rec));
        indices.push_back(idx);

        std::string fullPath = tmpDir + "/file" + std::to_string(i) + ".txt";
        contentIndex->indexFile(idx, fullPath);
    }

    check(contentIndex->indexedFileCount() == 5, "C1: All 5 files indexed in ContentIndex");

    // Remove files 1 and 3 to create tombstones
    engine->removeByPath(tmpDir + "/file1.txt");
    engine->removeByPath(tmpDir + "/file3.txt");
    contentIndex->removeFile(indices[1]);
    contentIndex->removeFile(indices[3]);

    check(engine->liveRecordCount() == 3, "C1: 3 live records after removal");
    check(contentIndex->indexedFileCount() == 3, "C1: 3 content-indexed files after removal");

    // Compact records (removes tombstones)
    engine->compactRecords();

    // Verify surviving files are still in SearchEngine
    for (int i : {0, 2, 4}) {
        std::string name = "file" + std::to_string(i) + ".txt";
        std::string fullPath = tmpDir + "/" + name;
        uint32_t newIdx = engine->indexForPath(fullPath);
        check(newIdx != UINT32_MAX, ("C1: file" + std::to_string(i) + " still in SearchEngine").c_str());
    }

    // Verify removed files are NOT in ContentIndex with old indices
    check(!contentIndex->isFileIndexed(indices[1]), "C1: file1 old index not in ContentIndex");
    check(!contentIndex->isFileIndexed(indices[3]), "C1: file3 old index not in ContentIndex");

    // Test via IndexPersistence.compact() integration
    std::string basePath = tmpDir + "/test_index.bin";
    std::string walPath = tmpDir + "/test_index.wal";
    auto persistence = std::make_unique<IndexPersistence>(engine, basePath, walPath, pagesPathFor(basePath), ptablePathFor(basePath), basePath + ".v6");
    persistence->attachWAL();

    // Add a new record, remove it, then compact via IndexPersistence
    FileRecord newRec;
    newRec.name = "extra.txt";
    newRec.path = tmpDir;
    newRec.type = 1;
    newRec.size = 50;
    newRec.modTime = time(nullptr);
    engine->addRecord(std::move(newRec));
    engine->removeByPath(tmpDir + "/extra.txt");

    IndexMetadata meta;
    meta.lastEventId = 42;
    persistence->compact(meta, /*force=*/true);

    check(contentIndex->indexedFileCount() == 3, "C1: ContentIndex still has 3 files after IndexPersistence::compact()");

    persistence.reset();
    fs::remove_all(tmpDir);
    std::cout << "\n";
}

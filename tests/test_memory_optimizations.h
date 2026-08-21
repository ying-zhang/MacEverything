#pragma once
// ═══════════════════════════════════════════════════════
//  Part 21: Memory Optimization Integration Tests
//  Tests PathTable interning, recordTrigrams_ elimination,
//  forEachRecordWithPath zero-copy, and persistence roundtrip.
// ═══════════════════════════════════════════════════════

static void runMemoryOptimizationTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 21: Memory Optimization Tests\n";
    std::cout << "========================================\n\n";

    // -- Test 1: Path deduplication in loadRecords --
    std::cout << "  --- Path deduplication in loadRecords ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // 100 files in same directory — path should be dedup'd to 1 entry
        for (int i = 0; i < 100; i++) {
            records.push_back({"file_" + std::to_string(i) + ".txt", "/shared/dir", 1,
                               static_cast<uint64_t>(i * 10), static_cast<time_t>(1000 + i)});
        }
        // 50 files in another directory
        for (int i = 0; i < 50; i++) {
            records.push_back({"doc_" + std::to_string(i) + ".md", "/other/dir", 1,
                               static_cast<uint64_t>(i * 20), static_cast<time_t>(2000 + i)});
        }
        engine.loadRecords(std::move(records));

        check(engine.recordCount() == 150, "PathDedup: 150 records loaded");
        check(engine.liveRecordCount() == 150, "PathDedup: 150 live records");
        check(engine.pathTableSnapshot().size() == 2, "PathDedup: only 2 unique paths in PathTable");

        // Verify getRecord reconstructs path correctly
        auto r0 = engine.getRecord(0);
        check(r0.path == "/shared/dir", "PathDedup: getRecord(0).path reconstructed");
        check(r0.name == "file_0.txt", "PathDedup: getRecord(0).name correct");

        auto r100 = engine.getRecord(100);
        check(r100.path == "/other/dir", "PathDedup: getRecord(100).path reconstructed");
        check(r100.name == "doc_0.md", "PathDedup: getRecord(100).name correct");
    }

    // -- Test 2: Query results unchanged after path dedup --
    std::cout << "\n  --- Query correctness after path dedup ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"hello.txt", "/tmp", 1, 100, 1000});
        records.push_back({"world.cpp", "/tmp", 1, 200, 2000});
        records.push_back({"readme.md", "/home/user", 1, 300, 3000});
        records.push_back({"test.py", "/home/user/projects", 1, 400, 4000});
        engine.loadRecords(std::move(records));

        auto res = engine.query("hello");
        check(res.size() == 1, "Query: 'hello' finds 1 result");
        check(engine.getRecord(res[0]).name == "hello.txt", "Query: 'hello' correct file");

        res = engine.query("readme");
        check(res.size() == 1, "Query: 'readme' finds 1 result");

        // Path-based match — node-centric: name must contain "user"
        // No record has "user" in its name, so 0 results
        res = engine.query("/home/user");
        check(res.size() == 0, "Query: '/home/user' returns 0 (no node name contains 'user')");

        // Case insensitive
        res = engine.query("README");
        check(res.size() == 1, "Query: case-insensitive 'README' finds 1 result");
    }

    // -- Test 3: Mutation preserves path dedup --
    std::cout << "\n  --- Mutations with path dedup ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"a.txt", "/dir1", 1, 100, 1000});
        records.push_back({"b.txt", "/dir1", 1, 200, 2000});
        engine.loadRecords(std::move(records));

        // addRecord with existing path
        engine.addRecord({"c.txt", "/dir1", 1, 300, 3000});
        auto r = engine.getRecord(2);
        check(r.name == "c.txt", "Mutation: addRecord name correct");
        check(r.path == "/dir1", "Mutation: addRecord path reconstructed from PathTable");

        // addRecord with new path
        engine.addRecord({"d.txt", "/dir2", 1, 400, 4000});
        r = engine.getRecord(3);
        check(r.path == "/dir2", "Mutation: addRecord new path correct");

        // removeByPath
        bool removed = engine.removeByPath("/dir1/a.txt");
        check(removed, "Mutation: removeByPath returns true");
        check(engine.liveRecordCount() == 3, "Mutation: liveCount after remove");

        auto res = engine.query("a.txt");
        check(res.empty(), "Mutation: removed file not queryable");

        // updateByPath
        engine.updateByPath("/dir1/b.txt", {"b_v2.txt", "/dir1", 1, 999, 5000});
        res = engine.query("b_v2");
        check(res.size() == 1, "Mutation: updateByPath new name queryable");
        r = engine.getRecord(res[0]);
        check(r.path == "/dir1", "Mutation: updateByPath path correct");
    }

    // -- Test 4: removeByPathPrefix with path dedup --
    std::cout << "\n  --- removeByPathPrefix with path dedup ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"x.txt", "/prefix/sub", 1, 100, 1000});
        records.push_back({"y.txt", "/prefix/sub", 1, 200, 2000});
        records.push_back({"z.txt", "/other", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        uint32_t removed = engine.removeByPathPrefix("/prefix");
        check(removed == 2, "removeByPathPrefix: removed 2 records");
        check(engine.liveRecordCount() == 1, "removeByPathPrefix: 1 live record");

        auto res = engine.query("z.txt");
        check(res.size() == 1, "removeByPathPrefix: non-removed file still queryable");
    }

    // -- Test 5: Compaction rebuilds PathTable --
    std::cout << "\n  --- Compaction rebuilds PathTable ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 10; i++) {
            records.push_back({"file_" + std::to_string(i) + ".txt", "/dir_" + std::to_string(i % 3),
                               1, static_cast<uint64_t>(i * 100), static_cast<time_t>(i)});
        }
        engine.loadRecords(std::move(records));
        check(engine.pathTableSnapshot().size() == 3, "Compact: 3 unique paths before");

        // Remove all files in /dir_0
        for (int i = 0; i < 10; i += 3) {
            engine.removeByPath("/dir_" + std::to_string(i % 3) + "/file_" + std::to_string(i) + ".txt");
        }

        auto remapResult = engine.compactRecords();
        check(!remapResult.empty(), "Compact: remap not empty");

        // After compaction, /dir_0 should be gone from PathTable if all its records were removed
        // But other paths should still work
        auto res = engine.query("file_1");
        check(res.size() == 1, "Compact: file_1 still queryable after compaction");
        auto r = engine.getRecord(res[0]);
        check(r.path == "/dir_1", "Compact: path correctly reconstructed after compaction");
    }

    // -- Test 6: forEachRecordWithPath matches getRecord --
    std::cout << "\n  --- forEachRecordWithPath vs getRecord ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"alpha.txt", "/path/a", 1, 100, 1000});
        records.push_back({"beta.txt", "/path/b", 1, 200, 2000});
        records.push_back({"gamma.txt", "/path/c", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // Collect via forEachRecordWithPath
        struct RecordInfo {
            std::string name;
            std::string path;
            uint8_t type;
            uint64_t size;
        };
        std::vector<RecordInfo> batchResults;
        std::vector<uint32_t> indices = {0, 1, 2};
        engine.forEachRecordWithPath(indices, [&](uint32_t, const FileRecord& r, const std::string& path) {
            batchResults.push_back({r.name, path, r.type, r.size});
        });

        check(batchResults.size() == 3, "forEachRecordWithPath: 3 results");

        // Compare with getRecord
        for (uint32_t i = 0; i < 3; i++) {
            auto single = engine.getRecord(i);
            check(batchResults[i].name == single.name,
                  ("forEachRecordWithPath: name[" + std::to_string(i) + "] matches getRecord").c_str());
            check(batchResults[i].path == single.path,
                  ("forEachRecordWithPath: path[" + std::to_string(i) + "] matches getRecord").c_str());
            check(batchResults[i].type == single.type,
                  ("forEachRecordWithPath: type[" + std::to_string(i) + "] matches getRecord").c_str());
            check(batchResults[i].size == single.size,
                  ("forEachRecordWithPath: size[" + std::to_string(i) + "] matches getRecord").c_str());
        }
    }

    // -- Test 7: forEachRecordWithPath skips tombstones --
    std::cout << "\n  --- forEachRecordWithPath skips tombstones ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"keep.txt", "/dir", 1, 100, 1000});
        records.push_back({"remove.txt", "/dir", 1, 200, 2000});
        records.push_back({"also_keep.txt", "/dir", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        engine.removeByPath("/dir/remove.txt");

        std::vector<std::string> names;
        std::vector<uint32_t> allIdx = {0, 1, 2};
        engine.forEachRecordWithPath(allIdx, [&](uint32_t, const FileRecord& r, const std::string&) {
            names.push_back(r.name);
        });

        check(names.size() == 2, "forEachRecordWithPath: skips tombstoned record");
        check(names[0] == "keep.txt", "forEachRecordWithPath: first live record correct");
        check(names[1] == "also_keep.txt", "forEachRecordWithPath: second live record correct");
    }

    // -- Test 8: Persistence roundtrip (save + load) --
    std::cout << "\n  --- Persistence roundtrip ---\n";
    {
        std::string tmpFile = "/tmp/test_memopt_persist_" + std::to_string(getpid()) + ".bin";

        // Build and save
        {
            SearchEngine engine;
            std::vector<FileRecord> records;
            records.push_back({"persist_a.txt", "/save/dir1", 1, 111, 1001, 9001, 42});
            records.push_back({"persist_b.txt", "/save/dir1", 1, 222, 2002, 9002, 42});
            records.push_back({"persist_c.txt", "/save/dir2", 1, 333, 3003, 9003, 43});
            engine.loadRecords(std::move(records));

            IndexMetadata meta;
            meta.lastEventId = 12345;
            meta.extra["test_key"] = "test_value";
            bool saved = engine.saveToFile(tmpFile, meta);
            check(saved, "Persistence: saveToFile succeeded");
        }

        // Load and verify
        {
            SearchEngine engine;
            IndexMetadata meta;
            bool loaded = engine.loadFromFile(tmpFile, &meta);
            check(loaded, "Persistence: loadFromFile succeeded");
            check(meta.lastEventId == 12345, "Persistence: lastEventId preserved");
            check(meta.extra.count("test_key") && meta.extra["test_key"] == "test_value",
                  "Persistence: extra metadata preserved");

            check(engine.recordCount() == 3, "Persistence: 3 records loaded");
            check(engine.pathTableSnapshot().size() == 2, "Persistence: 2 unique paths after load");

            auto r0 = engine.getRecord(0);
            check(r0.name == "persist_a.txt", "Persistence: record[0].name correct");
            check(r0.path == "/save/dir1", "Persistence: record[0].path reconstructed");
            check(r0.size == 111, "Persistence: record[0].size correct");
            check(r0.inode == 9001, "Persistence: record[0].inode correct");
            check(r0.devId == 42, "Persistence: record[0].devId correct");

            auto r2 = engine.getRecord(2);
            check(r2.name == "persist_c.txt", "Persistence: record[2].name correct");
            check(r2.path == "/save/dir2", "Persistence: record[2].path reconstructed");

            // Query works after load
            auto res = engine.query("persist_b");
            check(res.size() == 1, "Persistence: query works after load");
        }

        remove(tmpFile.c_str());
    }

    // -- Test 9: WAL replay correctness --
    std::cout << "\n  --- WAL replay correctness ---\n";
    {
        std::string walPath = "/tmp/test_memopt_wal_" + std::to_string(getpid()) + ".wal";

        // Phase 1: create engine, attach WAL, mutate
        {
            SearchEngine engine;
            std::vector<FileRecord> records;
            records.push_back({"base.txt", "/wal/dir", 1, 100, 1000});
            engine.loadRecords(std::move(records));

            auto wal = std::make_shared<IndexWAL>();
            wal->open(walPath);
            engine.attachWAL(wal);

            engine.addRecord({"added.txt", "/wal/dir", 1, 200, 2000});
            engine.removeByPath("/wal/dir/base.txt");
            engine.addRecord({"final.txt", "/wal/other", 1, 300, 3000});

            engine.detachWAL();
        }

        // Phase 2: replay WAL into fresh engine
        {
            SearchEngine engine;
            std::vector<FileRecord> records;
            records.push_back({"base.txt", "/wal/dir", 1, 100, 1000});
            engine.loadRecords(std::move(records));

            auto entries = IndexWAL::readAll(walPath);
            check(entries.size() == 3, "WAL replay: 3 entries in WAL");

            for (const auto& entry : entries) {
                if (entry.op == WALOp::Add) {
                    engine.addRecord(FileRecord{entry.record});
                } else if (entry.op == WALOp::Remove) {
                    engine.removeByPath(entry.fullPath);
                }
            }

            check(engine.liveRecordCount() == 2, "WAL replay: 2 live records after replay");

            auto res = engine.query("added");
            check(res.size() == 1, "WAL replay: 'added' queryable");
            auto r = engine.getRecord(res[0]);
            check(r.path == "/wal/dir", "WAL replay: added record path correct");

            res = engine.query("base");
            check(res.empty(), "WAL replay: 'base' removed");

            res = engine.query("final");
            check(res.size() == 1, "WAL replay: 'final' queryable");
            r = engine.getRecord(res[0]);
            check(r.path == "/wal/other", "WAL replay: final record path correct");
        }

        remove(walPath.c_str());
    }

    // -- Test 10: Trigram index after recordTrigrams_ elimination --
    std::cout << "\n  --- Trigram without recordTrigrams_ ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"document.pdf", "/home", 1, 100, 1000});
        records.push_back({"dockerfile", "/app", 1, 200, 2000});
        records.push_back({"readme.md", "/home", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // Trigram search should work (3+ chars)
        auto res = engine.query("doc");
        check(res.size() == 2, "Trigram-noStore: 'doc' matches 2 files");

        // Add and search
        engine.addRecord({"doc_new.txt", "/home", 1, 400, 4000});
        res = engine.query("doc");
        check(res.size() == 3, "Trigram-noStore: 'doc' matches 3 after add");

        // Remove and verify trigram cleanup works without stored trigrams
        engine.removeByPath("/home/document.pdf");
        res = engine.query("document");
        check(res.empty(), "Trigram-noStore: 'document' not found after remove");

        // Remaining 'doc' matches should still work
        res = engine.query("doc");
        check(res.size() == 2, "Trigram-noStore: 'doc' matches 2 after remove");

        // Compaction should rebuild trigram index correctly
        engine.compactRecords();
        res = engine.query("doc");
        check(res.size() == 2, "Trigram-noStore: 'doc' matches 2 after compaction");
        res = engine.query("readme");
        check(res.size() == 1, "Trigram-noStore: 'readme' matches 1 after compaction");
    }

    // -- Test 11: resolveRecordPath accessor --
    std::cout << "\n  --- resolveRecordPath ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"a.txt", "/resolve/test", 1, 100, 1000});
        records.push_back({"b.txt", "/resolve/test", 1, 200, 2000});
        records.push_back({"c.txt", "/resolve/other", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // forEachRecordWithPath should provide the resolved path
        std::vector<std::string> paths;
        std::vector<uint32_t> allIdx = {0, 1, 2};
        engine.forEachRecordWithPath(allIdx, [&](uint32_t, const FileRecord&, const std::string& p) {
            paths.push_back(p);
        });

        check(paths[0] == "/resolve/test", "resolveRecordPath: idx 0 correct");
        check(paths[1] == "/resolve/test", "resolveRecordPath: idx 1 same path");
        check(paths[2] == "/resolve/other", "resolveRecordPath: idx 2 different path");
    }

    // -- Test 12: indexForPath still works --
    std::cout << "\n  --- indexForPath ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"target.txt", "/lookup/dir", 1, 100, 1000});
        records.push_back({"other.txt", "/lookup/dir", 1, 200, 2000});
        engine.loadRecords(std::move(records));

        uint32_t idx = engine.indexForPath("/lookup/dir/target.txt");
        check(idx == 0, "indexForPath: found correct index");

        idx = engine.indexForPath("/nonexistent/path.txt");
        check(idx == UINT32_MAX, "indexForPath: returns UINT32_MAX for missing");
    }

    // -- Test 12b: pathIndex hash mode preserves last-wins and case-insensitive lookup --
    std::cout << "\n  --- hashed pathIndex semantics ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"Target.txt", "/Lookup/Dir", 1, 100, 1000});
        records.push_back({"target.txt", "/lookup/dir", 1, 200, 2000});
        records.push_back({"other.txt", "/lookup/dir", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        uint32_t idx = engine.indexForPath("/LOOKUP/DIR/TARGET.TXT");
        check(idx != UINT32_MAX, "HashedPathIndex: case-insensitive lookup finds duplicate path");
        auto rec = engine.getRecord(idx);
        check(rec.size == 200, "HashedPathIndex: duplicate full path keeps last record");

        auto mem = engine.memoryBreakdown();
        check(mem.pathIndexEntries == 2, "HashedPathIndex: unique live paths counted");
        check(mem.pathIndexApproxBytes < 1024,
              "HashedPathIndex: small fixture pathIndex avoids storing full path strings");
    }

    // -- Test 13: PathTable::resolve bounds check --
    std::cout << "\n  --- PathTable bounds check ---\n";
    {
        PathTable pt;
        pt.intern("/a");
        pt.intern("/b");
        check(pt.size() == 2, "PathTable bounds: 2 entries");
        check(pt.resolve(0) == "/a", "PathTable bounds: resolve(0) works");
        check(pt.resolve(1) == "/b", "PathTable bounds: resolve(1) works");
        check(pt.resolve(999).empty(), "PathTable bounds: out-of-range returns empty");
        check(pt.resolve(UINT32_MAX).empty(), "PathTable bounds: UINT32_MAX returns empty");
    }

    // -- Test 14: resolveRecordPath returns value copy, bounds safe --
    std::cout << "\n  --- resolveRecordPath value copy + bounds ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"x.txt", "/val/copy", 1, 100, 1000});
        engine.loadRecords(std::move(records));

        std::string path = engine.resolveRecordPath(0);
        check(path == "/val/copy", "resolveRecordPath: valid index returns correct path");

        std::string oob = engine.resolveRecordPath(9999);
        check(oob.empty(), "resolveRecordPath: out-of-bounds returns empty");
    }

    // -- Test 15: Compressed memory mode disables optional accelerators but keeps pathIndex --
    std::cout << "\n  --- Compressed memory optional index suppression ---\n";
    {
        SearchEngine engine(SearchEngineOptions::compressedMemoryMode());
        std::vector<FileRecord> records;
        records.push_back({"alpha.txt", "/Users/test/Documents", 1, 100, 1000});
        records.push_back({"beta.txt", "/Users/test/Downloads", 1, 200, 2000});
        records.push_back({"report.pdf", "/Users/test/Documents", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        auto mem = engine.memoryBreakdown();
        check(mem.pathIndexEntries == 3, "CompressedMemory: full-path index remains enabled");
        check(mem.pinyinTrigramEntries == 0, "CompressedMemory: pinyin trigram index disabled");
        check(mem.pathTrigramEntries == 0, "CompressedMemory: path trigram index disabled");
        check(mem.pathIdxToRecordsBytes == 0, "CompressedMemory: pathIdxToRecords disabled");

        auto res = engine.query("alpha");
        check(res.size() == 1, "CompressedMemory: filename query still works");
        check(engine.indexForPath("/Users/test/Documents/alpha.txt") == res[0],
              "CompressedMemory: indexForPath uses retained pathIndex");
        check(engine.removeByPath("/Users/test/Documents/alpha.txt"),
              "CompressedMemory: removeByPath uses retained pathIndex");
        res = engine.query("alpha");
        check(res.empty(), "CompressedMemory: removed record is no longer queryable");
    }

    std::cout << "\n";
}

#pragma once

static void runReviewRegressionTests() {
    std::cout << "\n=== Part 78: Review Regression Tests ===\n";

    // Filename trigrams must remain usable when path acceleration is disabled.
    {
        SearchEngineOptions options;
        options.enablePathTrigramIndex = false;
        SearchEngine engine(options);
        std::vector<FileRecord> records;
        for (int i = 0; i < 500; ++i) {
            records.push_back({"generic_" + std::to_string(i) + ".dat", "/test", 1, 10, 100});
        }
        for (int i = 0; i < 8; ++i) {
            records.push_back({"xqztarget_" + std::to_string(i) + ".txt", "/test", 1, 10, 100});
        }
        engine.loadRecords(std::move(records));
        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("xqztarget", 100, true, timing);
        check(results.size() == 8, "Review: name query works without path trigram index");
        check(timing.usedTrigram, "Review: name trigram is used without path acceleration");
    }

    // A completed rewrite may acknowledge only requests that existed before it
    // started. A later WAL failure must remain pending for the next flush.
    {
        SearchEngine engine;
        engine.loadRecords({{"base.txt", "/tmp", 1, 10, 100}});
        engine.attachWAL(std::make_shared<IndexWAL>()); // unopened: append fails deterministically
        engine.addRecord({"first.txt", "/tmp", 1, 10, 101});
        uint64_t firstGeneration = engine.fullRewriteGeneration();
        engine.addRecord({"second.txt", "/tmp", 1, 10, 102});
        uint64_t secondGeneration = engine.fullRewriteGeneration();
        engine.acknowledgeFullRewrite(firstGeneration);
        check(secondGeneration > firstGeneration && engine.needsFullRewrite(),
              "Review: newer WAL failure survives an older rewrite acknowledgement");
        engine.acknowledgeFullRewrite(secondGeneration);
        check(!engine.needsFullRewrite(),
              "Review: latest rewrite generation can be acknowledged");
    }

    // Each AND term may be satisfied by either the filename or its directory path.
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 1000; ++i) {
            records.push_back({"generic_" + std::to_string(i) + ".dat", "/noise", 1, 10, 100});
        }
        records.push_back({"report_2024_archive.txt", "/archive", 1, 10, 100});
        records.push_back({"report.pdf", "/Users/test/2024", 1, 10, 100});
        engine.loadRecords(std::move(records));
        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("report 2024", 100, true, timing);
        bool foundCrossField = false;
        for (uint32_t idx : results) {
            auto record = engine.getRecord(idx);
            if (record.name == "report.pdf" && record.path == "/Users/test/2024") {
                foundCrossField = true;
            }
        }
        check(foundCrossField, "Review: multi-term AND unions filename and path candidates per term");
    }

    // An oversized posting list must invalidate the whole multi-term accelerator.
    // A pinyin posting for only the best term is not a complete AND candidate set.
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 200; ++i) {
            records.push_back({"log_noise_" + std::to_string(i) + ".txt", "/tmp", 1, 10, 100});
        }
        for (int i = 0; i < 800; ++i) {
            records.push_back({"other_" + std::to_string(i) + ".txt", "/tmp", 1, 10, 100});
        }
        records.push_back({"zywj-log-target.txt", "/tmp", 1, 10, 100});
        records.push_back({"重要文件.txt", "/tmp", 1, 10, 100});
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("zywj log", 100, true, timing);
        bool foundLiteralMatch = false;
        for (uint32_t idx : results) {
            if (engine.getRecord(idx).name == "zywj-log-target.txt") {
                foundLiteralMatch = true;
            }
        }
        check(foundLiteralMatch,
              "Review: oversized multi-term candidates fall back without pinyin takeover");
    }

    // A wildcard gap belongs to the segment before it when matching right-to-left.
    {
        SearchEngine engine;
        engine.loadRecords({{"bin-tool", "/Users/x/usr/y/local", 1, 10, 100}});
        auto results = engine.query("/usr/*/local/bin");
        check(results.size() == 1, "Review: structured wildcard permits non-adjacent path components");
    }

    // Mutation lookups and loaded path dictionaries must share Unicode lowering.
    {
        const std::string upperDir = "/Users/\xC3\x89mile";
        const std::string lowerFile = "/Users/\xC3\xA9mile/report.pdf";
        SearchEngine engine;
        engine.loadRecords({{"report.pdf", upperDir, 1, 10, 100}});
        FileRecord updated{"report.pdf", upperDir, 1, 99, 200};
        engine.updateByPath(lowerFile, std::move(updated));
        check(engine.liveRecordCount() == 1, "Review: Unicode path update replaces existing record");
        check(engine.removeByPath(lowerFile), "Review: Unicode-lowercase path removes existing record");
        check(engine.liveRecordCount() == 0, "Review: Unicode path removal leaves no live duplicate");
    }

    // A compaction generation mismatch must prevent stale indices from being resolved.
    {
        SearchEngine engine;
        engine.loadRecords({{"old.txt", "/tmp", 1, 10, 100},
                            {"keep.txt", "/tmp", 1, 10, 100}});
        std::vector<uint32_t> stale{1};
        uint64_t generation = engine.compactionGeneration();
        engine.removeByPath("/tmp/old.txt");
        engine.compactRecords();
        bool visited = false;
        bool stable = engine.forEachRecordWithPathIfGeneration(
            stale, generation, [&](uint32_t, const FileRecord&, const std::string&) { visited = true; });
        check(!stable && !visited, "Review: stale result indices are rejected after compaction");
    }

    // Content readers can identify the entire cross-index remapping interval.
    {
        ContentIndex index;
        uint64_t before = index.mappingGeneration();
        index.beginFileIndexRemap();
        uint64_t during = index.mappingGeneration();
        index.endFileIndexRemap();
        uint64_t after = index.mappingGeneration();
        check((before & 1U) == 0 && (during & 1U) == 1 && after == before + 2,
              "Review: content mapping generation brackets remapping");

        auto tmpDir = fs::temp_directory_path() /
            ("maceverything_remap_guard_" + std::to_string(getpid()));
        fs::create_directories(tmpDir);
        auto path = (tmpDir / "guard.txt").string();
        { std::ofstream out(path); out << "generation guard content"; }
        index.setExtensions({"txt"});
        index.beginFileIndexRemap();
        auto update = index.indexFile(7, path);
        index.endFileIndexRemap();
        check(update == ContentIndexUpdate::Unchanged && index.indexedFileCount() == 0,
              "Review: content mutation is rejected while file indices are remapping");
        fs::remove_all(tmpDir);
    }

    // File-index mutations hold a lease that prevents compaction/remapping from
    // changing the numeric identity between path lookup and content mutation.
    {
        ContentIndex index;
        auto lease = index.acquireFileIndexMappingLease();
        bool remapEntered = true;
        std::thread remapper([&] {
            remapEntered = index.tryBeginFileIndexRemap();
            if (remapEntered) index.endFileIndexRemap();
        });
        remapper.join();
        check(!remapEntered,
              "Review: mapping lease blocks cross-index remapping");
        lease.unlock();
        check(index.tryBeginFileIndexRemap(),
              "Review: remapping resumes after mapping lease release");
        index.endFileIndexRemap();
    }

    // V6 Phase 1 has no extension index yet, so ext: must use the linear fallback.
    {
        auto tmpDir = fs::temp_directory_path() /
            ("maceverything_v6_review_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        auto v6Path = (tmpDir / "index.v6").string();

        auto source = std::make_shared<SearchEngine>();
        std::vector<FileRecord> records;
        for (int i = 0; i < 50; ++i) {
            records.push_back({"report_" + std::to_string(i) + ".csv", "/new", 1, 10, 100});
        }
        source->loadRecords(std::move(records));
        FlatIndexWriter writer(v6Path);
        IndexMetadata metadata;
        metadata.lastEventId = 1;
        check(writer.fullRewrite(*source, metadata), "Review: wrote V6 fixture");

        auto loaded = std::make_shared<SearchEngine>();
        std::vector<FileRecord> oldRecords;
        for (int i = 0; i < 30; ++i) {
            oldRecords.push_back({"old_" + std::to_string(i) + ".dat", "/old", 1, 10, 100});
        }
        loaded->loadRecords(std::move(oldRecords));
        FlatIndexWriter reader(v6Path);
        IndexMetadata loadedMetadata;
        check(reader.load(*loaded, &loadedMetadata), "Review: loaded V6 fixture into reused engine");
        check(loaded->isPhase2Pending(), "Review: V6 secondary indexes are pending");

        QueryTimingInfo timing;
        auto pendingResults = loaded->queryAdvanced("ext:csv", 100, true, timing);
        check(pendingResults.size() == 50,
              "Review: ext filter falls back correctly while Phase 2 is pending");
        fs::remove_all(tmpDir);
    }

    // A false-positive trigram candidate before a real match must not consume maxResults.
    {
        auto tmpDir = fs::temp_directory_path() /
            ("maceverything_verified_content_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        std::vector<std::string> paths = {
            (tmpDir / "false.txt").string(),
            (tmpDir / "real.txt").string(),
        };
        { std::ofstream out(paths[0]); out << "abaXbab"; }
        { std::ofstream out(paths[1]); out << "contains abab here"; }

        ContentIndex index;
        index.setExtensions({"txt"});
        index.indexFile(0, paths[0]);
        index.indexFile(1, paths[1]);
        std::atomic<bool> receivedStablePath{false};
        auto resolver = [&](uint32_t idx, std::string& path) {
            if (idx >= paths.size()) return false;
            if (path == paths[idx]) {
                receivedStablePath.store(true, std::memory_order_relaxed);
            }
            path = paths[idx];
            return true;
        };
        auto matches = index.query("abab", 1, resolver);
        check(matches.size() == 1 && matches[0].fileIndex == 1,
              "Review: content limit counts verified matches only");
        check(!matches.empty() && !matches[0].snippet.empty(),
              "Review: verified content match includes snippet");
        check(receivedStablePath.load(std::memory_order_relaxed),
              "Review: content verification receives a path snapshot, not a remapped index lookup");
        fs::remove_all(tmpDir);
    }

    // Verification must honor the configured index size, not stop at the 1 MiB default.
    {
        auto tmpDir = fs::temp_directory_path() /
            ("maceverything_large_content_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        auto path = (tmpDir / "large.txt").string();
        constexpr size_t kMatchOffset = 1024 * 1024 + 128 * 1024;
        {
            std::ofstream out(path, std::ios::binary);
            std::string padding(kMatchOffset, 'a');
            out.write(padding.data(), static_cast<std::streamsize>(padding.size()));
            out << "needle_beyond_one_mib";
        }

        ContentIndex index;
        index.setExtensions({"txt"});
        index.setMaxFileSize(2 * 1024 * 1024);
        check(index.indexFile(0, path) == ContentIndexUpdate::Upserted,
              "Review: large content fixture is indexed");
        auto matches = index.query("needle_beyond_one_mib", 1,
            [&](uint32_t idx, std::string& resolved) {
                if (idx != 0) return false;
                resolved = path;
                return true;
            });
        check(matches.size() == 1 && matches[0].matchOffset == kMatchOffset,
              "Review: content verification honors configured max file size");
        fs::remove_all(tmpDir);
    }

    std::cout << "\n";

    // A directory with no indexed children must not dereference a missing
    // lowerPathLookup_ entry in DIR_LIST mode.
    {
        SearchEngine engine;
        engine.loadRecords({{"Dir", "/review_parent", 2, 0, 100}});
        auto results = engine.query("/review_parent/Dir/*", 100, false);
        check(results.empty(), "Review: DIR_LIST missing child path returns empty safely");
    }

    // Content persistence resolves stable paths to the current runtime index.
    {
        auto tmpDir = fs::temp_directory_path() /
            ("maceverything_content_identity_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        auto filePath = (tmpDir / "identity.txt").string();
        { std::ofstream out(filePath); out << "stable path identity"; }
        auto basePath = (tmpDir / "content.bin").string();

        ContentIndex saved;
        saved.setExtensions({"txt"});
        check(saved.indexFile(5, filePath) == ContentIndexUpdate::Upserted,
              "Review: content identity fixture indexed");
        check(saved.saveToFile(basePath), "Review: path-keyed content base saved");

        ContentIndex loaded;
        check(loaded.loadFromFile(basePath, [&](const std::string& path) {
                  return path == filePath ? uint32_t{42} : UINT32_MAX;
              }), "Review: path-keyed content base loaded");
        ContentFileInfo info;
        check(loaded.getFileInfo(42, info) && info.fullPath == filePath,
              "Review: content path resolves to remapped runtime index");
        fs::remove_all(tmpDir);
    }
}

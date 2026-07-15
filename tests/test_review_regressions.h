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
        auto resolver = [&](uint32_t idx, std::string& path) {
            if (idx >= paths.size()) return false;
            path = paths[idx];
            return true;
        };
        auto matches = index.query("abab", 1, resolver);
        check(matches.size() == 1 && matches[0].fileIndex == 1,
              "Review: content limit counts verified matches only");
        check(!matches.empty() && !matches[0].snippet.empty(),
              "Review: verified content match includes snippet");
        fs::remove_all(tmpDir);
    }

    std::cout << "\n";
}

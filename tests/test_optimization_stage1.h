#pragma once

static void runOptimizationStage1Tests() {
    std::cout << "\n=== Part 78: Stage 1 Optimization Tests ===\n\n";

    // 1a. extractTrigrams correctness with lookup table
    {
        std::cout << "  [78.1] extractTrigrams correctness (lookup table lower)\n";
        auto t1 = ContentIndex::extractTrigrams("ABCdef");
        check(!t1.empty(), "extractTrigrams non-empty for 'ABCdef'");
        auto t2 = ContentIndex::extractTrigrams("abcdef");
        check(t1.size() == t2.size(), "case-insensitive trigrams same count");
        // Verify exact match of trigram values
        std::sort(t1.begin(), t1.end());
        std::sort(t2.begin(), t2.end());
        check(t1 == t2, "case-insensitive trigrams identical");

        // Non-ASCII passes through unchanged
        auto t3 = ContentIndex::extractTrigrams("\xC3\xA9\xC3\xA0\xC3\xBC");
        // UTF-8 multi-byte: should produce trigrams from raw bytes
        // Just verify no crash
        (void)t3;
        check(true, "non-ASCII trigram extraction no crash");

        // Short strings
        auto t4 = ContentIndex::extractTrigrams("ab");
        check(t4.empty(), "2-char string returns empty trigrams");
        auto t5 = ContentIndex::extractTrigrams("abc");
        check(t5.size() == 1, "3-char string returns 1 trigram");

        // Verify makeTrigram consistency
        auto expected = ContentIndex::makeTrigram('a', 'b', 'c');
        check(t5[0] == expected, "trigram value matches makeTrigram");
    }

    // 1b. intersectPostingLists correctness (double-buffer)
    {
        std::cout << "  [78.2] Intersection correctness (double-buffer)\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Create records with known names for trigram testing
        for (int i = 0; i < 100; i++) {
            FileRecord r;
            r.name = "document_" + std::to_string(i) + ".txt";
            r.path = "/test";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        // Add a few with different names
        for (int i = 0; i < 10; i++) {
            FileRecord r;
            r.name = "readme_special_" + std::to_string(i) + ".md";
            r.path = "/test";
            r.type = 1;
            r.size = 200;
            r.modTime = 2000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results1 = engine.queryAdvanced("document", 0, true, timing);
        check(results1.size() == 100, "trigram query 'document' finds all 100");

        auto results2 = engine.queryAdvanced("readme_special", 0, true, timing);
        check(results2.size() == 10, "trigram query 'readme_special' finds all 10");

        // Multi-word AND intersection
        auto results3 = engine.queryAdvanced("document txt", 0, true, timing);
        check(results3.size() == 100, "multi-word AND 'document txt' correct");
    }

    // 1c. Path expansion early exit (verified via correctness of path trigram queries)
    {
        std::cout << "  [78.3] Path expansion early exit\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 100; i++) {
            FileRecord r;
            r.name = "pathtest_" + std::to_string(i) + ".cpp";
            r.path = "/workspace/project/src";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        for (int i = 0; i < 50; i++) {
            FileRecord r;
            r.name = "other_item_" + std::to_string(i) + ".txt";
            r.path = "/different/location";
            r.type = 1;
            r.size = 200;
            r.modTime = 2000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("pathtest", 0, true, timing);
        check(results.size() == 100, "path expansion: pathtest finds 100");
        auto results2 = engine.queryAdvanced("other_item", 0, true, timing);
        check(results2.size() == 50, "path expansion: other_item finds 50");
    }

    // 1d. Lower path cache correctness
    {
        std::cout << "  [78.4] Lower path cache consistency\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Many records sharing same path (cache should help)
        for (int i = 0; i < 200; i++) {
            FileRecord r;
            r.name = "item_" + std::to_string(i) + ".dat";
            r.path = "/Shared/DataStore/Category";
            r.type = 1;
            r.size = i * 10;
            r.modTime = 1000 + i;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        // Run query twice — cache populated on first, used on second
        auto r1 = engine.queryAdvanced("item", 0, true, timing);
        auto r2 = engine.queryAdvanced("item", 0, true, timing);
        check(r1.size() == r2.size(), "cached query returns same count");
        // Verify actual results match
        std::sort(r1.begin(), r1.end());
        std::sort(r2.begin(), r2.end());
        check(r1 == r2, "cached query returns identical results");

        // Path-based query with mixed case path
        auto r3 = engine.queryAdvanced("datastore", 0, true, timing);
        check(r3.size() == 200, "case-insensitive path query with cache correct");
    }

    std::cout << "\n";
}

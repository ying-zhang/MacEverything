#pragma once

static void runBigramIndexTests() {
    std::cout << "\n=== Part 79: Bigram Index & AND Selectivity Tests ===\n\n";

    // 2a. ASCII bigram index — 2-char queries use bigram candidates
    {
        std::cout << "  [79.1] 2-char query uses bigram index\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 500; i++) {
            FileRecord r;
            r.name = "document_" + std::to_string(i) + ".txt";
            r.path = "/data";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        // Add records with "qw" in name (rare bigram)
        for (int i = 0; i < 5; i++) {
            FileRecord r;
            r.name = "qwerty_" + std::to_string(i) + ".log";
            r.path = "/data";
            r.type = 1;
            r.size = 200;
            r.modTime = 2000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        // 2-char query "qw" — should use bigram index
        auto results = engine.queryAdvanced("qw", 0, true, timing);
        check(results.size() == 5, "2-char query 'qw' finds all 5 matches");

        // 2-char query with common bigram
        auto results2 = engine.queryAdvanced("do", 0, true, timing);
        check(results2.size() == 500, "2-char query 'do' finds all 500 documents");
    }

    // 2a. Bigram index maintained through add/remove
    {
        std::cout << "  [79.2] Bigram index maintained through mutations\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 10; i++) {
            FileRecord r;
            r.name = "zx_file_" + std::to_string(i) + ".dat";
            r.path = "/test";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto r1 = engine.queryAdvanced("zx", 0, true, timing);
        check(r1.size() == 10, "initial 'zx' query finds 10");

        // Add a new record
        FileRecord newRec;
        newRec.name = "zx_new.txt";
        newRec.path = "/test";
        newRec.type = 1;
        newRec.size = 50;
        newRec.modTime = 3000;
        engine.addRecord(std::move(newRec));

        auto r2 = engine.queryAdvanced("zx", 0, true, timing);
        check(r2.size() == 11, "after add, 'zx' query finds 11");

        // Remove a record
        engine.removeByPath("/test/zx_file_0.dat");

        auto r3 = engine.queryAdvanced("zx", 0, true, timing);
        check(r3.size() == 10, "after remove, 'zx' query finds 10");
    }

    // 2a. Bigram index survives compaction
    {
        std::cout << "  [79.3] Bigram index survives compaction\n";
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 20; i++) {
            FileRecord r;
            r.name = "jk_item_" + std::to_string(i) + ".bin";
            r.path = "/compact";
            r.type = 1;
            r.size = 100;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        // Remove half
        for (int i = 0; i < 10; i++) {
            engine.removeByPath("/compact/jk_item_" + std::to_string(i) + ".bin");
        }

        // Compact
        engine.compactRecords();

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("jk", 0, true, timing);
        check(results.size() == 10, "after compaction, 'jk' query finds 10 remaining");
    }

    // 2a. extractAsciiBigrams correctness
    {
        std::cout << "  [79.4] extractAsciiBigrams correctness\n";
        auto bg1 = SearchEngine::extractAsciiBigrams("abc", 3);
        check(bg1.size() == 2, "3-char string has 2 bigrams");
        check(bg1[0] == SearchEngine::packBigram('a', 'b'), "first bigram is 'ab'");
        check(bg1[1] == SearchEngine::packBigram('b', 'c'), "second bigram is 'bc'");

        // Case insensitive
        auto bg2 = SearchEngine::extractAsciiBigrams("ABC", 3);
        check(bg1 == bg2, "bigrams are case insensitive");

        // 1-char string
        auto bg3 = SearchEngine::extractAsciiBigrams("a", 1);
        check(bg3.empty(), "1-char string has no bigrams");

        // Deduplication
        auto bg4 = SearchEngine::extractAsciiBigrams("aaa", 3);
        check(bg4.size() == 1, "repeated chars produce deduplicated bigrams");
    }

    // 2b. AND selectivity reordering
    {
        std::cout << "  [79.5] AND selectivity reordering\n";
        // Parse a query with mixed term and filter nodes
        auto ast = QueryParser::parse("foo ext:txt size:>100");
        check(ast != nullptr, "parsed 'foo ext:txt size:>100'");
        // The AST should be AND with children
        check(ast->type == QueryNodeType::AND, "root is AND");
        check(ast->children.size() >= 2, "AND has multiple children");

        // Verify query still returns correct results after reordering
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 50; i++) {
            FileRecord r;
            r.name = "foo_" + std::to_string(i) + ".txt";
            r.path = "/mixed";
            r.type = 1;
            r.size = 200;
            r.modTime = 1000;
            records.push_back(std::move(r));
        }
        for (int i = 0; i < 30; i++) {
            FileRecord r;
            r.name = "foo_" + std::to_string(i) + ".cpp";
            r.path = "/mixed";
            r.type = 1;
            r.size = 50;
            r.modTime = 2000;
            records.push_back(std::move(r));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("foo ext:txt", 0, true, timing);
        check(results.size() == 50, "AND query 'foo ext:txt' finds 50 .txt files");

        auto results2 = engine.queryAdvanced("foo ext:cpp size:>100", 0, true, timing);
        check(results2.empty(), "AND query 'foo ext:cpp size:>100' finds 0 (size 50 < 100)");

        auto results3 = engine.queryAdvanced("foo ext:txt size:>100", 0, true, timing);
        check(results3.size() == 50, "AND query 'foo ext:txt size:>100' finds 50");
    }

    std::cout << "\n";
}

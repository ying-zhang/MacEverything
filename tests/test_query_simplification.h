#pragma once
#include <cassert>
#include <iostream>
#include <string>
#include <filesystem>
#include "../MacEverything/Core/SearchEngine.h"
#include "../MacEverything/Core/ASTGlobTransform.h"
#include "../MacEverything/Core/CompiledGlob.h"
#include "../MacEverything/Core/QueryAST.h"
#include "../MacEverything/Core/QueryParser.h"
#include "../MacEverything/Core/QueryTokenizer.h"

namespace fs = std::filesystem;

// ── Part 67: Query Simplification Tests ──
// Verifies that all queries (simple keywords, globs, paths, filters) now
// route through the unified Advanced path after removing the Simple path.

static void runQuerySimplificationTests() {
    std::cout << "── Part 67: Query Simplification ──\n";
    int localPassed = 0, localFailed = 0;

    auto check = [&](bool cond, const std::string& msg) {
        if (cond) { localPassed++; passed++; std::cout << "    [PASS] " << msg << "\n"; }
        else { localFailed++; failed++; std::cout << "    [FAIL] " << msg << "\n"; }
    };

    // ── 66.1 textLower precomputation in QueryNode ──
    {
        auto node = QueryNode::makeTerm("Hello World");
        check(node->textLower == "hello world", "66.1 textLower precomputed");
        check(node->text == "Hello World", "66.1 text preserved");
    }

    // ── 66.2 transformGlobTerms: *.cpp → GLOB mode ──
    {
        auto node = QueryNode::makeTerm("*.cpp", MatchMode::SUBSTRING);
        node = transformGlobTerms(std::move(node));
        check(node->mode == MatchMode::GLOB, "66.2 *.cpp → GLOB mode");
    }

    // ── 66.3 transformGlobTerms: test?? → GLOB mode ──
    {
        auto node = QueryNode::makeTerm("test??", MatchMode::SUBSTRING);
        node = transformGlobTerms(std::move(node));
        check(node->mode == MatchMode::GLOB, "66.3 test?? → GLOB mode");
    }

    // ── 66.4 transformGlobTerms: plain text stays SUBSTRING ──
    {
        auto node = QueryNode::makeTerm("hello", MatchMode::SUBSTRING);
        node = transformGlobTerms(std::move(node));
        check(node->mode == MatchMode::SUBSTRING, "66.4 plain text stays SUBSTRING");
    }

    // ── 66.5 transformGlobTerms: REGEX mode not changed ──
    {
        auto node = QueryNode::makeTerm(".*\\.cpp", MatchMode::REGEX);
        node = transformGlobTerms(std::move(node));
        check(node->mode == MatchMode::REGEX, "66.5 REGEX not changed to GLOB");
    }

    // ── 66.6 transformGlobTerms: recurse into AND children ──
    {
        std::vector<std::unique_ptr<QueryNode>> kids;
        kids.push_back(QueryNode::makeTerm("*.h", MatchMode::SUBSTRING));
        kids.push_back(QueryNode::makeTerm("test", MatchMode::SUBSTRING));
        auto andNode = QueryNode::makeAnd(std::move(kids));
        andNode = transformGlobTerms(std::move(andNode));
        check(andNode->children[0]->mode == MatchMode::GLOB, "66.6 child *.h → GLOB");
        check(andNode->children[1]->mode == MatchMode::SUBSTRING, "66.6 child test stays SUBSTRING");
    }

    // ── 66.7 CompiledGlob: SUFFIX pattern ──
    {
        auto cg = compileGlob("*.cpp");
        check(cg.type == CompiledGlob::SUFFIX, "66.7 *.cpp → SUFFIX");
        check(cg.fixed == ".cpp", "66.7 fixed = .cpp");
        check(compiledGlobMatch(cg, "test.cpp", 8), "66.7 matches test.cpp");
        check(!compiledGlobMatch(cg, "test.h", 6), "66.7 no match test.h");
    }

    // ── 66.8 CompiledGlob: PREFIX pattern ──
    {
        auto cg = compileGlob("test*");
        check(cg.type == CompiledGlob::PREFIX, "66.8 test* → PREFIX");
        check(compiledGlobMatch(cg, "test_file.h", 11), "66.8 matches test_file.h");
        check(!compiledGlobMatch(cg, "mytest", 6), "66.8 no match mytest");
    }

    // ── 66.9 CompiledGlob: CONTAINS pattern ──
    {
        auto cg = compileGlob("*key*");
        check(cg.type == CompiledGlob::CONTAINS, "66.9 *key* → CONTAINS");
        check(compiledGlobMatch(cg, "mykeyword", 9), "66.9 matches mykeyword");
        check(!compiledGlobMatch(cg, "value", 5), "66.9 no match value");
    }

    // ── 66.10 CompiledGlob: GENERIC pattern ──
    {
        auto cg = compileGlob("t*s*.h");
        check(cg.type == CompiledGlob::GENERIC, "66.10 t*s*.h → GENERIC");
        check(compiledGlobMatch(cg, "test_case.h", 11), "66.10 matches test_case.h");
        check(!compiledGlobMatch(cg, "test_case.cpp", 13), "66.10 no match test_case.cpp");
    }

    // ── 66.11 End-to-end: simple keyword query via SearchEngine ──
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        FileRecord r1; r1.name = "hello.txt"; r1.path = "/tmp"; r1.type = 1;
        FileRecord r2; r2.name = "world.txt"; r2.path = "/tmp"; r2.type = 1;
        FileRecord r3; r3.name = "hello_world.cpp"; r3.path = "/tmp"; r3.type = 1;
        records.push_back(std::move(r1));
        records.push_back(std::move(r2));
        records.push_back(std::move(r3));
        engine.loadRecords(std::move(records));

        auto results = engine.query("hello", 100, false);
        check(results.size() == 2, "66.11 'hello' finds 2 results");
    }

    // ── 66.12 End-to-end: glob query *.cpp via unified path ──
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        FileRecord r1; r1.name = "main.cpp"; r1.path = "/src"; r1.type = 1;
        FileRecord r2; r2.name = "main.h"; r2.path = "/src"; r2.type = 1;
        FileRecord r3; r3.name = "util.cpp"; r3.path = "/src"; r3.type = 1;
        records.push_back(std::move(r1));
        records.push_back(std::move(r2));
        records.push_back(std::move(r3));
        engine.loadRecords(std::move(records));

        auto results = engine.query("*.cpp", 100, false);
        check(results.size() == 2, "66.12 '*.cpp' finds 2 .cpp files");
    }

    // ── 66.13 End-to-end: space query "foo bar" → AND semantics ──
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        FileRecord r1; r1.name = "foo_bar.txt"; r1.path = "/tmp"; r1.type = 1;
        FileRecord r2; r2.name = "foo.txt"; r2.path = "/tmp"; r2.type = 1;
        FileRecord r3; r3.name = "bar.txt"; r3.path = "/tmp"; r3.type = 1;
        records.push_back(std::move(r1));
        records.push_back(std::move(r2));
        records.push_back(std::move(r3));
        engine.loadRecords(std::move(records));

        auto results = engine.query("foo bar", 100, false);
        // "foo bar" now = AND("foo", "bar") — only foo_bar.txt matches both
        check(results.size() == 1, "66.13 'foo bar' AND semantics: 1 result");
    }

    // ── 66.14 End-to-end: filter query ext:h ──
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        FileRecord r1; r1.name = "main.cpp"; r1.path = "/src"; r1.type = 1;
        FileRecord r2; r2.name = "main.h"; r2.path = "/src"; r2.type = 1;
        FileRecord r3; r3.name = "util.h"; r3.path = "/src"; r3.type = 1;
        records.push_back(std::move(r1));
        records.push_back(std::move(r2));
        records.push_back(std::move(r3));
        engine.loadRecords(std::move(records));

        auto results = engine.query("ext:h", 100, false);
        check(results.size() == 2, "66.14 'ext:h' finds 2 .h files");
    }

    // ── 66.15 End-to-end: boolean OR query ──
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        FileRecord r1; r1.name = "alpha.txt"; r1.path = "/tmp"; r1.type = 1;
        FileRecord r2; r2.name = "beta.txt"; r2.path = "/tmp"; r2.type = 1;
        FileRecord r3; r3.name = "gamma.txt"; r3.path = "/tmp"; r3.type = 1;
        records.push_back(std::move(r1));
        records.push_back(std::move(r2));
        records.push_back(std::move(r3));
        engine.loadRecords(std::move(records));

        auto results = engine.query("alpha | beta", 100, false);
        check(results.size() == 2, "66.15 'alpha | beta' OR finds 2 results");
    }

    // ── 66.16 End-to-end: DIR_LIST via /tmp/* ──
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Create a directory named "mydir" under /testroot
        FileRecord d1; d1.name = "mydir"; d1.path = "/testroot"; d1.type = 2;
        // Create children under /testroot/mydir
        FileRecord r1; r1.name = "child1.txt"; r1.path = "/testroot/mydir"; r1.type = 1;
        FileRecord r2; r2.name = "child2.txt"; r2.path = "/testroot/mydir"; r2.type = 1;
        // Create a file at same level as mydir (should NOT appear)
        FileRecord r3; r3.name = "sibling.txt"; r3.path = "/testroot"; r3.type = 1;
        records.push_back(std::move(d1));
        records.push_back(std::move(r1));
        records.push_back(std::move(r2));
        records.push_back(std::move(r3));
        engine.loadRecords(std::move(records));

        auto results = engine.query("/testroot/mydir/*", 100, false);
        check(results.size() == 2, "66.16 DIR_LIST finds 2 children");
    }

    // ── 66.17 QueryTimingInfo searchPath for simple queries ──
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        FileRecord r1; r1.name = "test_file.txt"; r1.path = "/tmp"; r1.type = 1;
        records.push_back(std::move(r1));
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results = engine.query("test", 100, false, timing);
        // Simple query should now go through advanced path
        check(!timing.searchPath.empty(), "66.17 searchPath is set");
        check(!timing.searchPath.empty(), "66.17 timing populated");
    }

    // ── 66.18 globMatchImpl via CompiledGlob.h ──
    {
        check(globMatchImpl("*.txt", "hello.txt"), "66.18 globMatchImpl *.txt");
        check(!globMatchImpl("*.txt", "hello.cpp"), "66.18 globMatchImpl *.txt no match");
        check(globMatchImpl("test??.h", "test01.h"), "66.18 globMatchImpl test??.h");
        check(!globMatchImpl("test??.h", "test1.h"), "66.18 globMatchImpl test?.h no match (too short)");
    }

    // ── 66.19 Unicode NFC/NFD query fallback ──
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        FileRecord r1; r1.name = "Cafe\xCC\x81" ".txt"; r1.path = "/tmp"; r1.type = 1;
        FileRecord r2; r2.name = "plain.txt"; r2.path = "/tmp"; r2.type = 1;
        records.push_back(std::move(r1));
        records.push_back(std::move(r2));
        engine.loadRecords(std::move(records));

        auto results = engine.query("Caf\xC3\xA9", 100, false);
        check(results.size() == 1, "66.19 NFC query matches NFD filename");
    }

    // ── 66.20 Plain terms can be split across path and filename ──
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        FileRecord target; target.name = "xx.pdf"; target.path = "/Users/ying/xx"; target.type = 1;
        FileRecord distractor; distractor.name = "ying-note.txt"; distractor.path = "/tmp"; distractor.type = 1;
        records.push_back(std::move(target));
        records.push_back(std::move(distractor));
        for (int i = 0; i < 20; i++) {
            FileRecord filler;
            filler.name = "filler_" + std::to_string(i) + ".txt";
            filler.path = "/tmp/filler";
            filler.type = 1;
            records.push_back(std::move(filler));
        }
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results = engine.query("ying pdf", 100, true, timing);
        bool foundTarget = false;
        for (uint32_t idx : results) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "xx.pdf" && rec.path == "/Users/ying/xx") foundTarget = true;
        }
        check(foundTarget, "66.20 'ying pdf' matches path term + filename term");
    }

    std::cout << "  Part 67 summary: " << localPassed << " passed, " << localFailed << " failed\n";
}

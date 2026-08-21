// Part 73 — CompiledGlob pre-compilation + zero-alloc evalTerm GLOB tests
// Verifies that:
// 1. ASTGlobTransform sets compiledGlob on GLOB nodes
// 2. compiledGlobMatch works with raw char* (zero-alloc) for all pattern types
// 3. globMatchImpl(pattern, char*, len) overload works correctly

#pragma once

#include <vector>
#include <string>
#include "../MacEverything/Core/CompiledGlob.h"
#include "../MacEverything/Core/QueryAST.h"
#include "../MacEverything/Core/ASTGlobTransform.h"

static void runCompiledGlobEvaltermTests() {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║  Part 73: CompiledGlob evalTerm Tests    ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n\n";

    int localPassed = 0, localFailed = 0;
    auto check = [&](bool cond, const std::string& msg) {
        if (cond) { localPassed++; passed++; }
        else { localFailed++; failed++; std::cout << "  FAIL: " << msg << "\n"; }
    };

    // ── Test 1: ASTGlobTransform sets compiledGlob ──
    {
        auto node = QueryNode::makeTerm("*.cpp");
        check(!node->compiledGlob.has_value(), "73.1 before transform: no compiledGlob");
        node = transformGlobTerms(std::move(node));
        check(node->mode == MatchMode::GLOB, "73.1 mode changed to GLOB");
        check(node->compiledGlob.has_value(), "73.1 compiledGlob is set after transform");
        check(node->compiledGlob->type == CompiledGlob::SUFFIX, "73.1 *.cpp → SUFFIX");
    }

    // ── Test 2: Non-glob terms not modified ──
    {
        auto node = QueryNode::makeTerm("hello");
        node = transformGlobTerms(std::move(node));
        check(node->mode == MatchMode::SUBSTRING, "73.2 'hello' stays SUBSTRING");
        check(!node->compiledGlob.has_value(), "73.2 no compiledGlob for non-glob");
    }

    // ── Test 3: compiledGlobMatch SUFFIX zero-alloc ──
    {
        auto cg = compileGlob(".cpp");  // pattern without *, simulates after stripping
        // Test with real suffix pattern
        auto cg2 = compileGlob("*.cpp");
        check(cg2.type == CompiledGlob::SUFFIX, "73.3 *.cpp is SUFFIX");
        const char* name1 = "main.cpp";
        check(compiledGlobMatch(cg2, name1, strlen(name1)), "73.3 main.cpp matches *.cpp");
        const char* name2 = "main.h";
        check(!compiledGlobMatch(cg2, name2, strlen(name2)), "73.3 main.h doesn't match *.cpp");
        const char* name3 = ".cpp";
        check(compiledGlobMatch(cg2, name3, strlen(name3)), "73.3 .cpp matches *.cpp");
    }

    // ── Test 4: compiledGlobMatch PREFIX zero-alloc ──
    {
        auto cg = compileGlob("test*");
        check(cg.type == CompiledGlob::PREFIX, "73.4 test* is PREFIX");
        const char* name1 = "test_main.cpp";
        check(compiledGlobMatch(cg, name1, strlen(name1)), "73.4 test_main.cpp matches test*");
        const char* name2 = "main_test.cpp";
        check(!compiledGlobMatch(cg, name2, strlen(name2)), "73.4 main_test.cpp doesn't match test*");
    }

    // ── Test 5: compiledGlobMatch CONTAINS zero-alloc ──
    {
        auto cg = compileGlob("*config*");
        check(cg.type == CompiledGlob::CONTAINS, "73.5 *config* is CONTAINS");
        const char* name1 = "my_config_file.txt";
        check(compiledGlobMatch(cg, name1, strlen(name1)), "73.5 my_config_file.txt matches *config*");
        const char* name2 = "readme.md";
        check(!compiledGlobMatch(cg, name2, strlen(name2)), "73.5 readme.md doesn't match *config*");
    }

    // ── Test 6: compiledGlobMatch EXACT zero-alloc ──
    {
        auto cg = compileGlob("makefile");
        check(cg.type == CompiledGlob::EXACT, "73.6 makefile is EXACT");
        const char* name1 = "makefile";
        check(compiledGlobMatch(cg, name1, strlen(name1)), "73.6 makefile matches makefile");
        const char* name2 = "Makefile";
        check(!compiledGlobMatch(cg, name2, strlen(name2)), "73.6 Makefile doesn't match makefile (case)");
        const char* name3 = "makefile.bak";
        check(!compiledGlobMatch(cg, name3, strlen(name3)), "73.6 makefile.bak doesn't match makefile");
    }

    // ── Test 7: compiledGlobMatch GENERIC zero-alloc ──
    {
        auto cg = compileGlob("*test*.cpp");
        check(cg.type == CompiledGlob::GENERIC, "73.7 *test*.cpp is GENERIC");
        const char* name1 = "my_test_file.cpp";
        check(compiledGlobMatch(cg, name1, strlen(name1)), "73.7 my_test_file.cpp matches *test*.cpp");
        const char* name2 = "my_test_file.h";
        check(!compiledGlobMatch(cg, name2, strlen(name2)), "73.7 my_test_file.h doesn't match *test*.cpp");
        const char* name3 = "test.cpp";
        check(compiledGlobMatch(cg, name3, strlen(name3)), "73.7 test.cpp matches *test*.cpp");
    }

    // ── Test 8: globMatchImpl zero-alloc overload ──
    {
        std::string pattern = "*.h";
        const char* text1 = "stdio.h";
        check(globMatchImpl(pattern, text1, strlen(text1)), "73.8 globMatchImpl(*.h, stdio.h) → true");
        const char* text2 = "stdio.cpp";
        check(!globMatchImpl(pattern, text2, strlen(text2)), "73.8 globMatchImpl(*.h, stdio.cpp) → false");

        // Complex pattern with ? wildcard
        std::string pattern2 = "test??.txt";
        const char* text3 = "test01.txt";
        check(globMatchImpl(pattern2, text3, strlen(text3)), "73.8 globMatchImpl(test??.txt, test01.txt) → true");
        const char* text4 = "test1.txt";
        check(!globMatchImpl(pattern2, text4, strlen(text4)), "73.8 globMatchImpl(test??.txt, test1.txt) → false");
    }

    // ── Test 9: compiledGlob with nested AND/OR AST ──
    {
        auto a = QueryNode::makeTerm("*.py");
        auto b = QueryNode::makeTerm("config");
        std::vector<std::unique_ptr<QueryNode>> kids;
        kids.push_back(std::move(a));
        kids.push_back(std::move(b));
        auto andNode = QueryNode::makeAnd(std::move(kids));
        andNode = transformGlobTerms(std::move(andNode));
        // *.py child should have compiledGlob, config should not
        auto& child0 = andNode->children[0];
        auto& child1 = andNode->children[1];
        check(child0->mode == MatchMode::GLOB, "73.9 *.py → GLOB in AND");
        check(child0->compiledGlob.has_value(), "73.9 *.py has compiledGlob in AND");
        check(child0->compiledGlob->type == CompiledGlob::SUFFIX, "73.9 *.py → SUFFIX in AND");
        check(child1->mode == MatchMode::SUBSTRING, "73.9 config stays SUBSTRING in AND");
        check(!child1->compiledGlob.has_value(), "73.9 config has no compiledGlob in AND");
    }

    // ── Test 10: Empty and edge cases ──
    {
        // Empty text
        auto cg = compileGlob("*.h");
        check(!compiledGlobMatch(cg, "", 0), "73.10 empty string doesn't match *.h");

        // Single char
        auto cg2 = compileGlob("*");
        const char* text1 = "anything";
        check(compiledGlobMatch(cg2, text1, strlen(text1)), "73.10 anything matches *");
        check(compiledGlobMatch(cg2, "", 0), "73.10 empty matches *");
    }

    std::cout << "  Part 73 done: " << localPassed << " passed, "
              << localFailed << " failed\n";
}

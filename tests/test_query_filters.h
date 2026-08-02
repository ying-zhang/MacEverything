#pragma once
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

// ── Part 56: Query Filter Tests ──
// Tests for Phase 2 core filters: ext:, size:, file:, folder:, path:, nopath:, parent:, depth:, len:

static void runQueryFilterTests() {
    std::cout << "── Part 56: Query Filters (Phase 2) ──\n";
    int localPassed = 0, localFailed = 0;

    auto check = [&](bool cond, const std::string& msg) {
        if (cond) { localPassed++; passed++; }
        else { localFailed++; failed++; std::cout << "  FAIL: " << msg << "\n"; }
    };

    // Create a temp dir with test files
    auto tmpDir = std::filesystem::temp_directory_path() / "me_filter_tests";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir / "subdir" / "deep");

    // Create test files with known sizes
    auto writeFile = [](const std::filesystem::path& p, size_t size) {
        std::ofstream f(p, std::ios::binary);
        std::string data(size, 'x');
        f.write(data.data(), data.size());
    };

    writeFile(tmpDir / "hello.cpp", 1025);        // just over 1 KB
    writeFile(tmpDir / "world.h", 512);            // 512 B
    writeFile(tmpDir / "readme.md", 2048);         // 2 KB
    writeFile(tmpDir / "big.txt", 1024 * 1024);    // 1 MB
    writeFile(tmpDir / "small.log", 10);           // 10 B
    writeFile(tmpDir / "subdir" / "nested.cpp", 100);
    writeFile(tmpDir / "subdir" / "deep" / "buried.h", 50);

    // Scan the temp dir into a SearchEngine
    SearchEngine engine;
    {
        DirectoryScanner scanner;
        scanner.scan(tmpDir.string());
        auto records = scanner.takeResults();
        engine.loadRecords(std::move(records));
    }

    // Helper: query and collect result names
    auto queryNames = [&](const std::string& q) -> std::vector<std::string> {
        QueryTimingInfo timing;
        auto indices = engine.queryAdvanced(q, 100, false, timing);
        std::vector<std::string> names;
        engine.forEachRecordWithPath(indices, [&](uint32_t, const FileRecord& rec, const std::string&) {
            names.push_back(rec.name);
        });
        return names;
    };

    auto contains = [](const std::vector<std::string>& v, const std::string& s) {
        for (auto& x : v) if (x == s) return true;
        return false;
    };

    // ── 56.1 ext: single extension ──
    {
        auto r = queryNames("ext:cpp");
        check(contains(r, "hello.cpp"), "56.1a ext:cpp finds hello.cpp");
        check(contains(r, "nested.cpp"), "56.1b ext:cpp finds nested.cpp");
        check(!contains(r, "world.h"), "56.1c ext:cpp excludes world.h");
        check(!contains(r, "readme.md"), "56.1d ext:cpp excludes readme.md");
    }

    // ── 56.2 ext: multiple extensions with semicolon ──
    {
        auto r = queryNames("ext:cpp;h");
        check(contains(r, "hello.cpp"), "56.2a ext:cpp;h finds .cpp");
        check(contains(r, "world.h"), "56.2b ext:cpp;h finds .h");
        check(contains(r, "nested.cpp"), "56.2c ext:cpp;h finds nested.cpp");
        check(contains(r, "buried.h"), "56.2d ext:cpp;h finds buried.h");
        check(!contains(r, "readme.md"), "56.2e ext:cpp;h excludes .md");
    }

    // ── 56.3 size: greater than ──
    {
        auto r = queryNames("size:>1kb");
        check(!contains(r, "small.log"), "56.3a size:>1kb excludes 10B file");
        check(!contains(r, "world.h"), "56.3b size:>1kb excludes 512B file");
        check(contains(r, "hello.cpp"), "56.3c size:>1kb includes 1KB file");
        check(contains(r, "readme.md"), "56.3d size:>1kb includes 2KB file");
        check(contains(r, "big.txt"), "56.3e size:>1kb includes 1MB file");
    }

    // ── 56.4 size: less than ──
    {
        auto r = queryNames("size:<1kb");
        check(contains(r, "small.log"), "56.4a size:<1kb includes 10B");
        check(contains(r, "world.h"), "56.4b size:<1kb includes 512B");
        check(!contains(r, "hello.cpp"), "56.4c size:<1kb excludes 1KB");
        check(!contains(r, "big.txt"), "56.4d size:<1kb excludes 1MB");
    }

    // ── 56.5 size: range ──
    {
        auto r = queryNames("size:500b..2kb");
        check(!contains(r, "small.log"), "56.5a range excludes 10B");
        check(contains(r, "world.h"), "56.5b range includes 512B");
        check(contains(r, "hello.cpp"), "56.5c range includes 1KB");
        check(!contains(r, "big.txt"), "56.5d range excludes 1MB");
    }

    // ── 56.6 file: filter ──
    {
        auto r = queryNames("file:");
        // Should include files only, not directories
        check(contains(r, "hello.cpp"), "56.6a file: includes files");
        check(!contains(r, "subdir"), "56.6b file: excludes dirs");
    }

    // ── 56.7 folder: filter ──
    {
        auto r = queryNames("folder:");
        check(contains(r, "subdir"), "56.7a folder: includes dirs");
        check(contains(r, "deep"), "56.7b folder: includes nested dirs");
        check(!contains(r, "hello.cpp"), "56.7c folder: excludes files");
    }

    // ── 56.8 len: filter ──
    {
        // "hello.cpp" = 9 chars, "big.txt" = 7 chars, "small.log" = 9 chars
        auto r = queryNames("len:<=7");
        check(contains(r, "big.txt"), "56.8a len:<=7 includes big.txt (7)");
        check(contains(r, "world.h"), "56.8b len:<=7 includes world.h (7)");
        check(!contains(r, "hello.cpp"), "56.8c len:<=7 excludes hello.cpp (9)");
    }

    // ── 56.9 depth: filter ──
    // tmpDir / hello.cpp -> depth relative to root
    // We test relative depth differences
    {
        // depth depends on absolute path; just test that the filter doesn't crash
        // and that deeper files have higher depth values
        auto shallow = queryNames("depth:<=2");
        auto deep = queryNames("depth:>=100");
        // At least some files should match shallow depth
        // depth depends on absolute path; tmpdir is at depth ~3-4, so depth:<=2 may be empty
        // But depth:>=100 should definitely be empty (no files that deep)
        check(deep.empty(), "56.9a depth:>=100 finds no files at extreme depth");
        // depth:<=10 should capture files in /tmp/me_filter_tests/...
        auto reasonable = queryNames("depth:<=10");
        check(!reasonable.empty(), "56.9b depth:<=10 finds files in tmpdir");
    }

    // ── 56.10 Combined: ext + size ──
    {
        auto r = queryNames("ext:cpp size:>500b");
        check(contains(r, "hello.cpp"), "56.10a ext:cpp size:>500b includes 1KB cpp");
        check(!contains(r, "nested.cpp"), "56.10b ext:cpp size:>500b excludes 100B cpp");
        check(!contains(r, "world.h"), "56.10c combined excludes .h file");
    }

    // ── 56.11 Combined with text: hello ext:cpp ──
    {
        auto r = queryNames("hello ext:cpp");
        check(contains(r, "hello.cpp"), "56.11a text+ext finds match");
        check(!contains(r, "nested.cpp"), "56.11b text+ext excludes non-matching name");
    }

    // ── 56.12 ext: case insensitive ──
    {
        auto r = queryNames("ext:CPP");
        check(contains(r, "hello.cpp"), "56.12 ext:CPP matches .cpp (case insensitive)");
    }

    // ── 56.13 path: filter ──
    {
        auto r = queryNames("path:subdir");
        check(contains(r, "nested.cpp"), "56.13a path:subdir finds nested file");
        check(contains(r, "buried.h"), "56.13b path:subdir finds deeply nested file");
        check(!contains(r, "hello.cpp"), "56.13c path:subdir excludes root level file");
    }

    // ── 56.14 nopath: filter ──
    {
        auto r = queryNames("nopath:subdir");
        check(!contains(r, "nested.cpp"), "56.14a nopath:subdir excludes nested");
        check(!contains(r, "buried.h"), "56.14b nopath:subdir excludes deep nested");
        check(contains(r, "hello.cpp"), "56.14c nopath:subdir includes root file");
    }

    // ── 56.15 parent: filter ──
    {
        auto parentPath = (tmpDir / "subdir").string();
        auto r = queryNames("parent:" + parentPath);
        check(contains(r, "nested.cpp"), "56.15a parent: finds direct child");
        check(!contains(r, "buried.h"), "56.15b parent: excludes grandchild");
        check(!contains(r, "hello.cpp"), "56.15c parent: excludes other dirs");
    }

    // ── 56.16 Filter argument parsing (AST level) ──
    {
        // ext: with semicolons
        auto ast = QueryParser::parse("ext:cpp;h;hpp");
        check(ast != nullptr, "56.16a ext list parse non-null");
        check(ast->type == QueryNodeType::FILTER, "56.16b is FILTER");
        check(ast->extList.size() == 3, "56.16c extList has 3 entries");
        if (ast->extList.size() == 3) {
            check(ast->extList[0] == "cpp", "56.16d extList[0]=cpp");
            check(ast->extList[1] == "h", "56.16e extList[1]=h");
            check(ast->extList[2] == "hpp", "56.16f extList[2]=hpp");
        }
    }

    // ── 56.17 Size parsing ──
    {
        auto ast = QueryParser::parse("size:>1mb");
        check(ast != nullptr, "56.17a size parse non-null");
        check(ast->op == CompareOp::GT, "56.17b op is GT");
        check(ast->numVal1 == 1024ULL * 1024, "56.17c numVal1 = 1MB");
    }

    // ── 56.18 Size range parsing ──
    {
        auto ast = QueryParser::parse("size:100kb..1mb");
        check(ast != nullptr, "56.18a range parse non-null");
        check(ast->op == CompareOp::RANGE, "56.18b op is RANGE");
        check(ast->numVal1 == 100ULL * 1024, "56.18c low = 100KB");
        check(ast->numVal2 == 1024ULL * 1024, "56.18d high = 1MB");
    }

    // ── 56.18b Oversized values saturate without undefined conversion ──
    {
        auto ast = QueryParser::parse("size:>99999999999999999999gb");
        check(ast != nullptr, "56.18d oversized size parses");
        check(ast->numVal1 == UINT64_MAX, "56.18e oversized size saturates");
    }

    // ── 56.19 Len parsing ──
    {
        auto ast = QueryParser::parse("len:>=10");
        check(ast != nullptr, "56.19a len parse non-null");
        check(ast->op == CompareOp::GE, "56.19b op is GE");
        check(ast->numVal1 == 10, "56.19c numVal1 = 10");
    }

    // ── 56.20 Depth parsing ──
    {
        auto ast = QueryParser::parse("depth:<3");
        check(ast != nullptr, "56.20a depth parse non-null");
        check(ast->op == CompareOp::LT, "56.20b op is LT");
        check(ast->numVal1 == 3, "56.20c numVal1 = 3");
    }

    // ── 56.21 NOT + filter: !ext:cpp ──
    {
        auto r = queryNames("!ext:cpp");
        check(!contains(r, "hello.cpp"), "56.21a !ext:cpp excludes cpp files");
        check(contains(r, "world.h"), "56.21b !ext:cpp includes h files");
        check(contains(r, "readme.md"), "56.21c !ext:cpp includes md files");
    }

    // ── 56.22 OR + filter: ext:cpp | ext:h ──
    {
        auto r = queryNames("ext:cpp | ext:h");
        check(contains(r, "hello.cpp"), "56.22a OR ext finds cpp");
        check(contains(r, "world.h"), "56.22b OR ext finds h");
        check(!contains(r, "readme.md"), "56.22c OR ext excludes md");
    }

    // ── 56.23 type: shorthand ──
    {
        auto r = queryNames("type:file");
        check(contains(r, "hello.cpp"), "56.23a type:file includes files");
        check(!contains(r, "subdir"), "56.23b type:file excludes dirs");

        auto r2 = queryNames("type:folder");
        check(contains(r2, "subdir"), "56.23c type:folder includes dirs");
        check(!contains(r2, "hello.cpp"), "56.23d type:folder excludes files");
    }

    // Cleanup
    std::filesystem::remove_all(tmpDir);

    std::cout << "  Passed: " << localPassed << "  Failed: " << localFailed << "\n\n";
}

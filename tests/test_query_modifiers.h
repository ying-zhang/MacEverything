#pragma once
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

// ── Part 59: Query Modifier & Macro Tests ──
// Tests for Phase 4 modifiers: case:, nocase:, regex:, ww:, wfn:
// and macros: audio:, video:, pic:, doc:, exe:, zip:

static void runQueryModifierTests() {
    std::cout << "── Part 59: Query Modifiers & Macros (Phase 4) ──\n";
    int localPassed = 0, localFailed = 0;

    auto check = [&](bool cond, const std::string& msg) {
        if (cond) { localPassed++; passed++; }
        else { localFailed++; failed++; std::cout << "  FAIL: " << msg << "\n"; }
    };

    // ═══════════════════════════════════════════
    // Section A: Modifier parsing tests (AST level)
    // ═══════════════════════════════════════════

    // A1: case:foo → TERM("foo", SUBSTRING, caseSensitive=true)
    {
        auto node = QueryNode::makeFilter("case", "foo");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::TERM, "A1: case:foo → TERM type");
        check(node->text == "foo", "A1: case:foo text = 'foo'");
        check(node->mode == MatchMode::SUBSTRING, "A1: case:foo mode = SUBSTRING");
        check(node->caseSensitive == true, "A1: case:foo caseSensitive = true");
    }

    // A2: nocase:Bar → TERM("Bar", SUBSTRING, caseSensitive=false)
    {
        auto node = QueryNode::makeFilter("nocase", "Bar");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::TERM, "A2: nocase:Bar → TERM type");
        check(node->text == "Bar", "A2: nocase:Bar text = 'Bar'");
        check(node->mode == MatchMode::SUBSTRING, "A2: nocase:Bar mode = SUBSTRING");
        check(node->caseSensitive == false, "A2: nocase:Bar caseSensitive = false");
    }

    // A3: regex:^test → TERM("^test", REGEX)
    {
        auto node = QueryNode::makeFilter("regex", "^test");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::TERM, "A3: regex:^test → TERM type");
        check(node->text == "^test", "A3: regex:^test text = '^test'");
        check(node->mode == MatchMode::REGEX, "A3: regex:^test mode = REGEX");
    }

    // A3b: regex:a|b c → TERM("a|b c", REGEX), not an OR/AND query
    {
        auto node = QueryParser::parse("regex:a|b c");
        check(node != nullptr, "A3b: regex:a|b c parses");
        if (node) {
            check(node->type == QueryNodeType::TERM, "A3b: regex:a|b c → TERM type");
            check(node->text == "a|b c", "A3b: regex:a|b c text preserves operators and spaces");
            check(node->mode == MatchMode::REGEX, "A3b: regex:a|b c mode = REGEX");
        }
    }

    // A4: ww:hello → TERM("hello", WHOLEWORD)
    {
        auto node = QueryNode::makeFilter("ww", "hello");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::TERM, "A4: ww:hello → TERM type");
        check(node->text == "hello", "A4: ww:hello text = 'hello'");
        check(node->mode == MatchMode::WHOLEWORD, "A4: ww:hello mode = WHOLEWORD");
    }

    // A5: wholeword:test → TERM("test", WHOLEWORD)
    {
        auto node = QueryNode::makeFilter("wholeword", "test");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::TERM, "A5: wholeword:test → TERM type");
        check(node->text == "test", "A5: wholeword:test text");
        check(node->mode == MatchMode::WHOLEWORD, "A5: wholeword:test mode = WHOLEWORD");
    }

    // A6: wfn:readme → TERM("readme", WHOLEFILENAME)
    {
        auto node = QueryNode::makeFilter("wfn", "readme");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::TERM, "A6: wfn:readme → TERM type");
        check(node->text == "readme", "A6: wfn:readme text = 'readme'");
        check(node->mode == MatchMode::WHOLEFILENAME, "A6: wfn:readme mode = WHOLEFILENAME");
    }

    // A7: wholefilename:makefile → TERM("makefile", WHOLEFILENAME)
    {
        auto node = QueryNode::makeFilter("wholefilename", "makefile");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::TERM, "A7: wholefilename:makefile → TERM type");
        check(node->text == "makefile", "A7: wholefilename text");
        check(node->mode == MatchMode::WHOLEFILENAME, "A7: wholefilename mode = WHOLEFILENAME");
    }

    // ═══════════════════════════════════════════
    // Section B: Macro expansion tests (AST level)
    // ═══════════════════════════════════════════

    // B1: audio: → FILTER(ext:mp3;wav;...)
    {
        auto node = QueryNode::makeFilter("audio", "");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::FILTER, "B1: audio: → FILTER type");
        check(node->filterName == "ext", "B1: audio: filterName = 'ext'");
        check(!node->extList.empty(), "B1: audio: extList non-empty");
        // Verify some key extensions
        bool hasMp3 = false, hasWav = false, hasFlac = false;
        for (auto& e : node->extList) {
            if (e == "mp3") hasMp3 = true;
            if (e == "wav") hasWav = true;
            if (e == "flac") hasFlac = true;
        }
        check(hasMp3, "B1: audio: contains mp3");
        check(hasWav, "B1: audio: contains wav");
        check(hasFlac, "B1: audio: contains flac");
    }

    // B2: video: → FILTER(ext:mp4;avi;...)
    {
        auto node = QueryNode::makeFilter("video", "");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::FILTER, "B2: video: → FILTER type");
        check(node->filterName == "ext", "B2: video: filterName = 'ext'");
        bool hasMp4 = false, hasMkv = false;
        for (auto& e : node->extList) {
            if (e == "mp4") hasMp4 = true;
            if (e == "mkv") hasMkv = true;
        }
        check(hasMp4, "B2: video: contains mp4");
        check(hasMkv, "B2: video: contains mkv");
    }

    // B3: pic: → FILTER(ext:jpg;png;...)
    {
        auto node = QueryNode::makeFilter("pic", "");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::FILTER, "B3: pic: → FILTER type");
        check(node->filterName == "ext", "B3: pic: filterName = 'ext'");
        bool hasJpg = false, hasPng = false, hasHeic = false;
        for (auto& e : node->extList) {
            if (e == "jpg") hasJpg = true;
            if (e == "png") hasPng = true;
            if (e == "heic") hasHeic = true;
        }
        check(hasJpg, "B3: pic: contains jpg");
        check(hasPng, "B3: pic: contains png");
        check(hasHeic, "B3: pic: contains heic");
    }

    // B4: doc: → FILTER(ext:pdf;doc;...)
    {
        auto node = QueryNode::makeFilter("doc", "");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::FILTER, "B4: doc: → FILTER type");
        check(node->filterName == "ext", "B4: doc: filterName = 'ext'");
        bool hasPdf = false, hasMd = false;
        for (auto& e : node->extList) {
            if (e == "pdf") hasPdf = true;
            if (e == "md") hasMd = true;
        }
        check(hasPdf, "B4: doc: contains pdf");
        check(hasMd, "B4: doc: contains md");
    }

    // B5: exe: → FILTER(ext:app;dmg;...)
    {
        auto node = QueryNode::makeFilter("exe", "");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::FILTER, "B5: exe: → FILTER type");
        check(node->filterName == "ext", "B5: exe: filterName = 'ext'");
        bool hasApp = false, hasDmg = false;
        for (auto& e : node->extList) {
            if (e == "app") hasApp = true;
            if (e == "dmg") hasDmg = true;
        }
        check(hasApp, "B5: exe: contains app");
        check(hasDmg, "B5: exe: contains dmg");
    }

    // B6: zip: → FILTER(ext:zip;rar;...)
    {
        auto node = QueryNode::makeFilter("zip", "");
        QueryFilterParser::parse(*node);
        check(node->type == QueryNodeType::FILTER, "B6: zip: → FILTER type");
        check(node->filterName == "ext", "B6: zip: filterName = 'ext'");
        bool hasZip = false, has7z = false, hasTar = false;
        for (auto& e : node->extList) {
            if (e == "zip") hasZip = true;
            if (e == "7z") has7z = true;
            if (e == "tar") hasTar = true;
        }
        check(hasZip, "B6: zip: contains zip");
        check(has7z, "B6: zip: contains 7z");
        check(hasTar, "B6: zip: contains tar");
    }

    // ═══════════════════════════════════════════
    // Section C: Integrated search engine tests
    // ═══════════════════════════════════════════

    // Create temp dir with diverse test files
    auto tmpDir = std::filesystem::temp_directory_path() / "me_modifier_tests";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir);

    auto writeFile = [](const std::filesystem::path& p, size_t sz) {
        std::ofstream f(p, std::ios::binary);
        std::string data(sz, 'x');
        f.write(data.data(), data.size());
    };

    writeFile(tmpDir / "Hello_World.cpp", 100);
    writeFile(tmpDir / "hello.TXT", 50);
    writeFile(tmpDir / "README.md", 200);
    writeFile(tmpDir / "test_data.json", 300);
    writeFile(tmpDir / "photo.jpg", 150);
    writeFile(tmpDir / "music.mp3", 400);
    writeFile(tmpDir / "archive.zip", 500);

    // Scan into SearchEngine
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
        auto indices = engine.query(q, 100, false, timing);
        std::vector<std::string> names;
        engine.forEachRecordWithPath(indices, [&](uint32_t, const FileRecord& r, const std::string&) {
            names.push_back(r.name);
        });
        std::sort(names.begin(), names.end());
        return names;
    };

    auto containsName = [](const std::vector<std::string>& v, const std::string& n) {
        return std::find(v.begin(), v.end(), n) != v.end();
    };

    // C1: case:Hello — should match Hello_World.cpp (contains "Hello" with capital H)
    {
        auto results = queryNames("case:Hello");
        check(containsName(results, "Hello_World.cpp"), "C1: case:Hello matches Hello_World.cpp");
    }

    // C2: case:hello — should match hello.TXT but NOT Hello_World.cpp (H≠h)
    {
        auto results = queryNames("case:hello");
        check(containsName(results, "hello.TXT"), "C2: case:hello matches hello.TXT");
        check(!containsName(results, "Hello_World.cpp"), "C2: case:hello excludes Hello_World.cpp");
    }

    // C3: wfn:README.md — should only match README.md (whole filename)
    {
        auto results = queryNames("wfn:README.md");
        check(containsName(results, "README.md"), "C3: wfn:README.md matches README.md");
        check(results.size() == 1, "C3: wfn:README.md matches exactly 1 file");
    }

    // C4: wfn:readme.md — should match README.md (case insensitive by default)
    {
        auto results = queryNames("wfn:readme.md");
        check(containsName(results, "README.md"), "C4: wfn:readme.md matches README.md (case-insensitive)");
        check(results.size() == 1, "C4: wfn:readme.md matches exactly 1 file");
    }

    // C5: ww:test — should match test_data.json (test is a whole word, bounded by _ )
    {
        auto results = queryNames("ww:test");
        check(containsName(results, "test_data.json"), "C5: ww:test matches test_data.json");
    }

    // C6: regex:^test — should match test_data.json (name starts with test)
    {
        auto results = queryNames("regex:^test");
        check(containsName(results, "test_data.json"), "C6: regex:^test matches test_data.json");
        check(!containsName(results, "Hello_World.cpp"), "C6: regex:^test excludes Hello_World.cpp");
    }

    // C7: regex:\.json$ — should match test_data.json (name ends with .json)
    {
        auto results = queryNames("regex:\\.json$");
        check(containsName(results, "test_data.json"), "C7: regex:\\.json$ matches test_data.json");
        check(results.size() == 1, "C7: regex:\\.json$ matches exactly 1 file");
    }

    // C8: pic: — should match photo.jpg
    {
        auto results = queryNames("pic:");
        check(containsName(results, "photo.jpg"), "C8: pic: matches photo.jpg");
        check(!containsName(results, "music.mp3"), "C8: pic: excludes music.mp3");
    }

    // C9: audio: — should match music.mp3
    {
        auto results = queryNames("audio:");
        check(containsName(results, "music.mp3"), "C9: audio: matches music.mp3");
        check(!containsName(results, "photo.jpg"), "C9: audio: excludes photo.jpg");
    }

    // C10: zip: — should match archive.zip
    {
        auto results = queryNames("zip:");
        check(containsName(results, "archive.zip"), "C10: zip: matches archive.zip");
        check(!containsName(results, "music.mp3"), "C10: zip: excludes music.mp3");
    }

    // C11: doc: — should match README.md (md in doc list)
    {
        auto results = queryNames("doc:");
        check(containsName(results, "README.md"), "C11: doc: matches README.md");
    }

    // C12: combo — case:Hello ext:cpp
    {
        auto results = queryNames("case:Hello ext:cpp");
        check(containsName(results, "Hello_World.cpp"), "C12: case:Hello ext:cpp matches Hello_World.cpp");
        check(!containsName(results, "hello.TXT"), "C12: case:Hello ext:cpp excludes hello.TXT");
    }

    // C13: !audio: — exclude audio files
    {
        auto results = queryNames("!audio:");
        check(!containsName(results, "music.mp3"), "C13: !audio: excludes music.mp3");
        check(containsName(results, "photo.jpg"), "C13: !audio: includes photo.jpg");
        check(containsName(results, "archive.zip"), "C13: !audio: includes archive.zip");
    }

    // C14: pic: | audio: — match either picture or audio
    {
        auto results = queryNames("pic: | audio:");
        check(containsName(results, "photo.jpg"), "C14: pic:|audio: matches photo.jpg");
        check(containsName(results, "music.mp3"), "C14: pic:|audio: matches music.mp3");
        check(!containsName(results, "archive.zip"), "C14: pic:|audio: excludes archive.zip");
    }

    // C15: regex with case: insensitive by default
    {
        auto results = queryNames("regex:^hello");
        check(containsName(results, "hello.TXT"), "C15: regex:^hello matches hello.TXT (case-insensitive)");
        check(containsName(results, "Hello_World.cpp"), "C15: regex:^hello matches Hello_World.cpp (case-insensitive)");
    }

    // C16: ext:cpp Hello — scoring should use "hello" not "ext:cpp hello"
    // Hello_World.cpp should rank first (starts-with "hello" in name)
    {
        // Order-preserving query to verify priority scoring
        QueryTimingInfo timing;
        auto indices = engine.query("ext:cpp Hello", 100, false, timing);
        std::vector<std::string> ordered;
        engine.forEachRecordWithPath(indices, [&](uint32_t, const FileRecord& r, const std::string&) {
            ordered.push_back(r.name);
        });
        check(!ordered.empty(), "C16: ext:cpp Hello has results");
        if (!ordered.empty()) {
            check(ordered[0] == "Hello_World.cpp",
                  "C16: ext:cpp Hello ranks Hello_World.cpp first (got: " + ordered[0] + ")");
        }
    }

    // C17: audio: — pure macro query (no SUBSTRING TERM), should not crash
    // and should return results with default priority (sorted by path length)
    {
        auto results = queryNames("audio:");
        check(containsName(results, "music.mp3"), "C17: audio: matches music.mp3");
        check(!containsName(results, "Hello_World.cpp"), "C17: audio: excludes Hello_World.cpp");
    }

    // Cleanup
    std::filesystem::remove_all(tmpDir);

    std::cout << "  Part 59 result: " << localPassed << " passed, " << localFailed << " failed\n";
}

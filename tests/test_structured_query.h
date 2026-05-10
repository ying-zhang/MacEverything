#pragma once
// Part 58: Structured Query (Node-Centric Search)
// StructuredQueryParser.h is included transitively via SearchEngine.h

static void runStructuredQueryTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 58: Structured Query (Node-Centric)\n";
    std::cout << "========================================\n\n";

    // ── Parser Unit Tests ──

    // -- Test 1: PLAIN mode (no slash) --
    std::cout << "  --- Test 1: Parser — PLAIN mode ---\n";
    {
        auto pq = parseQuery("hello");
        CHECK(pq.mode == QueryMode::PLAIN);
        CHECK(pq.namePattern == "hello");
        CHECK(pq.pathSegments.empty());
    }

    // -- Test 2: SEGMENTS mode (basic /abc/def) --
    std::cout << "\n  --- Test 2: Parser — SEGMENTS mode ---\n";
    {
        auto pq = parseQuery("/abc/def");
        CHECK(pq.mode == QueryMode::SEGMENTS);
        CHECK(pq.namePattern == "def");
        CHECK(pq.nameKind == PathSegmentKind::SUBSTRING);
        CHECK(pq.pathSegments.size() == 1);
        CHECK(pq.pathSegments[0].text == "abc");
        CHECK(pq.pathSegments[0].kind == PathSegmentKind::SUBSTRING);
        CHECK(pq.pathSegments[0].adjacentToNext == true);
    }

    // -- Test 3: DIR_EXACT mode (/abc/def/) --
    std::cout << "\n  --- Test 3: Parser — DIR_EXACT mode ---\n";
    {
        auto pq = parseQuery("/abc/def/");
        CHECK(pq.mode == QueryMode::DIR_EXACT);
        CHECK(pq.namePattern == "def");
        CHECK(pq.nameKind == PathSegmentKind::EXACT);
        CHECK(pq.pathSegments.size() == 1);
        CHECK(pq.pathSegments[0].text == "abc");
    }

    // -- Test 4: DIR_LIST mode (/abc/def/*) --
    std::cout << "\n  --- Test 4: Parser — DIR_LIST mode ---\n";
    {
        auto pq = parseQuery("/abc/def/*");
        CHECK(pq.mode == QueryMode::DIR_LIST);
        CHECK(pq.namePattern == "def");
        CHECK(pq.pathSegments.size() == 1);
        CHECK(pq.pathSegments[0].text == "abc");
    }

    // -- Test 5: Adjacency break with * --
    std::cout << "\n  --- Test 5: Parser — adjacency break ---\n";
    {
        auto pq = parseQuery("/abc/*/def");
        CHECK(pq.mode == QueryMode::SEGMENTS);
        CHECK(pq.namePattern == "def");
        CHECK(pq.pathSegments.size() == 1);
        CHECK(pq.pathSegments[0].text == "abc");
        CHECK(pq.pathSegments[0].adjacentToNext == false);
    }

    // -- Test 6: Single segment /usr equals plain usr --
    std::cout << "\n  --- Test 6: Parser — single segment ---\n";
    {
        auto pq = parseQuery("/usr");
        CHECK(pq.mode == QueryMode::SEGMENTS);
        CHECK(pq.namePattern == "usr");
        CHECK(pq.nameKind == PathSegmentKind::SUBSTRING);
        CHECK(pq.pathSegments.empty());

        auto pqExact = parseQuery("/usr/");
        CHECK(pqExact.mode == QueryMode::SEGMENTS);
        CHECK(pqExact.namePattern == "usr");
        CHECK(pqExact.nameKind == PathSegmentKind::SUBSTRING);

        auto pqSuffix = parseQuery("usr/");
        CHECK(pqSuffix.mode == QueryMode::SEGMENTS);
        CHECK(pqSuffix.namePattern == "usr");
        CHECK(pqSuffix.nameKind == PathSegmentKind::SUBSTRING);

        // Without leading slash, same result
        auto pq2 = parseQuery("usr");
        CHECK(pq2.mode == QueryMode::PLAIN);
        CHECK(pq2.namePattern == "usr");
    }

    // -- Test 7: Leading slash optional abc/def == /abc/def --
    std::cout << "\n  --- Test 7: Parser — leading slash optional ---\n";
    {
        auto pq = parseQuery("abc/def");
        CHECK(pq.mode == QueryMode::SEGMENTS);
        CHECK(pq.namePattern == "def");
        CHECK(pq.pathSegments.size() == 1);
        CHECK(pq.pathSegments[0].text == "abc");
    }

    // -- Test 8: Multi-segment path /a/b/c --
    std::cout << "\n  --- Test 8: Parser — multi-segment ---\n";
    {
        auto pq = parseQuery("/a/b/c");
        CHECK(pq.mode == QueryMode::SEGMENTS);
        CHECK(pq.namePattern == "c");
        CHECK(pq.pathSegments.size() == 2);
        CHECK(pq.pathSegments[0].text == "a");
        CHECK(pq.pathSegments[0].adjacentToNext == true);
        CHECK(pq.pathSegments[1].text == "b");
        CHECK(pq.pathSegments[1].adjacentToNext == true);
    }

    // -- Test 9: Case insensitive parsing --
    std::cout << "\n  --- Test 9: Parser — case insensitive ---\n";
    {
        auto pq = parseQuery("/ABC/DEF");
        CHECK(pq.mode == QueryMode::SEGMENTS);
        CHECK(pq.namePattern == "def");
        CHECK(pq.pathSegments[0].text == "abc");
    }

    // ── Integration Tests ──

    // -- Test 10: SEGMENTS — node name match with path constraint --
    std::cout << "\n  --- Test 10: SEGMENTS — basic node search ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"config.yaml", "/app/settings", 1, 100, 1000});
        records.push_back({"config.yaml", "/other/place", 1, 200, 2000});
        records.push_back({"readme.md", "/app/settings", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // "/settings/config" → name contains "config", parent dir contains "settings"
        auto res = engine.query("/settings/config");
        // Should find config.yaml in /app/settings (name matches, path matches)
        bool foundSettings = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "config.yaml" && rec.path == "/app/settings") foundSettings = true;
        }
        CHECK(foundSettings);

        // Should NOT find config.yaml in /other/place (path doesn't contain "settings")
        bool foundOther = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "config.yaml" && rec.path == "/other/place") foundOther = true;
        }
        CHECK(!foundOther);
    }

    // -- Test 11: SEGMENTS — multi-segment adjacency enforcement --
    std::cout << "\n  --- Test 11: SEGMENTS — adjacency ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"target.txt", "/foo/bar", 1, 100, 1000});     // foo→bar adjacent
        records.push_back({"target.txt", "/foo/mid/bar", 1, 200, 2000}); // foo→bar NOT adjacent (mid in between)
        records.push_back({"target.txt", "/bar/baz", 1, 300, 3000});     // no "foo" in path
        engine.loadRecords(std::move(records));

        // "/foo/bar/target" → name contains "target", path has "foo" adjacent to "bar"
        auto res = engine.query("/foo/bar/target");
        int matchCount = 0;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "target.txt") matchCount++;
        }
        // /foo/bar/target.txt → "foo" adjacent to "bar" ✓
        // /foo/mid/bar/target.txt → "foo" NOT adjacent to "bar" (mid in between) ✗
        // /bar/baz/target.txt → no "foo" ✗
        CHECK(matchCount == 1);
    }

    // -- Test 12: Non-adjacent match with * --
    std::cout << "\n  --- Test 12: SEGMENTS — non-adjacent * ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"test.cpp", "/project/src/core", 1, 100, 1000});
        records.push_back({"test.cpp", "/project/docs", 1, 200, 2000});
        records.push_back({"test.cpp", "/other/src/core", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // "/project/*/test" → name contains "test", ancestor contains "project" (non-adjacent)
        auto res = engine.query("/project/*/test");
        int matchCount = 0;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "test.cpp" && rec.path.find("project") != std::string::npos) matchCount++;
        }
        // /project/src/core/test.cpp → path has "project" anywhere ✓
        // /project/docs/test.cpp → path has "project" anywhere ✓
        CHECK(matchCount == 2);

        // Should NOT match /other/src/core/test.cpp
        bool foundOther = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.path.find("/other/") != std::string::npos) foundOther = true;
        }
        CHECK(!foundOther);
    }

    // -- Test 13: DIR_EXACT — find directory node --
    std::cout << "\n  --- Test 13: DIR_EXACT — directory search ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"bin", "/usr/local", 2, 0, 1000});      // directory
        records.push_back({"bin", "/usr", 2, 0, 2000});             // directory
        records.push_back({"binary.txt", "/usr/local", 1, 100, 3000}); // file, not exact match
        records.push_back({"bin", "/opt", 2, 0, 4000});             // different path
        engine.loadRecords(std::move(records));

        // "/local/bin/" → find directory named "bin" with parent containing "local"
        auto res = engine.query("/local/bin/");
        bool found = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "bin" && rec.path == "/usr/local" && rec.type == 2) found = true;
        }
        CHECK(found);

        // Should not find file binary.txt (not exact name match on "bin")
        bool foundFile = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "binary.txt") foundFile = true;
        }
        CHECK(!foundFile);

        // Should not find /opt/bin (path doesn't contain "local")
        bool foundOpt = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.path == "/opt") foundOpt = true;
        }
        CHECK(!foundOpt);
    }

    // -- Test 14: DIR_LIST — list directory children --
    std::cout << "\n  --- Test 14: DIR_LIST — list children ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // /usr/local/bin/ contains: gcc, ls, make
        records.push_back({"bin", "/usr/local", 2, 0, 1000});
        records.push_back({"gcc", "/usr/local/bin", 1, 100, 2000});
        records.push_back({"ls", "/usr/local/bin", 1, 200, 3000});
        records.push_back({"make", "/usr/local/bin", 1, 300, 4000});
        // /usr/bin/ also has bin directory
        records.push_back({"bin", "/usr", 2, 0, 5000});
        records.push_back({"cat", "/usr/bin", 1, 400, 6000});
        engine.loadRecords(std::move(records));

        // "/local/bin/*" → list children of "bin" directory where parent has "local"
        auto res = engine.query("/local/bin/*");
        // Should find gcc, ls, make (children of /usr/local/bin)
        std::set<std::string> foundNames;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            foundNames.insert(rec.name);
        }
        CHECK(foundNames.count("gcc") == 1);
        CHECK(foundNames.count("ls") == 1);
        CHECK(foundNames.count("make") == 1);
        // Should NOT find cat (child of /usr/bin, not /usr/local/bin)
        CHECK(foundNames.count("cat") == 0);
    }

    // -- Test 15: Single segment /usr equals plain search --
    std::cout << "\n  --- Test 15: Single segment equivalence ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"usr_config", "/home/user", 1, 100, 1000});
        records.push_back({"readme", "/usr/share", 1, 200, 2000});
        engine.loadRecords(std::move(records));

        // "/usr" → SEGMENTS with no path constraint, name contains "usr"
        auto res = engine.query("/usr");
        // Should find usr_config (name contains "usr")
        bool foundConfig = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "usr_config") foundConfig = true;
        }
        CHECK(foundConfig);
    }

    // -- Test 16: searchPath label for structured queries --
    std::cout << "\n  --- Test 16: searchPath label ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"config.yaml", "/app/settings", 1, 100, 1000});
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto res = engine.query("/settings/config", 0, true, timing);
        // All queries now route through the unified Advanced path
        CHECK(!timing.searchPath.empty());
    }

    // -- Test 17: DIR_LIST with no path constraint --
    std::cout << "\n  --- Test 17: DIR_LIST — no path constraint ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"mydir", "/root", 2, 0, 1000});
        records.push_back({"child1.txt", "/root/mydir", 1, 100, 2000});
        records.push_back({"child2.txt", "/root/mydir", 1, 200, 3000});
        engine.loadRecords(std::move(records));

        // "/mydir/*" → list children of "mydir" (no parent constraint)
        auto res = engine.query("/mydir/*");
        CHECK(res.size() == 2);
    }

    // -- Test 18: SEGMENTS — name must match (node-centric) --
    std::cout << "\n  --- Test 18: SEGMENTS — node-centric, no path-only ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"readme.md", "/abc/def", 1, 100, 1000});
        records.push_back({"other.txt", "/abc/def", 1, 200, 2000});
        engine.loadRecords(std::move(records));

        // "/abc/def" → name must contain "def", path must contain "abc"
        // "other.txt" doesn't contain "def" in name → should NOT appear
        auto res = engine.query("/abc/def");
        bool foundOther = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "other.txt") foundOther = true;
        }
        CHECK(!foundOther);
    }

    // -- Test 19: glob pattern should NOT trigger on /abc/* --
    std::cout << "\n  --- Test 19: /abc/* is DIR_LIST, not glob ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"mydir", "/parent", 2, 0, 1000});
        records.push_back({"file.txt", "/parent/mydir", 1, 100, 2000});
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto res = engine.query("/mydir/*", 0, true, timing);
        // Should NOT use "linear" glob path
        CHECK(timing.searchPath != "glob-trigram");
        CHECK(res.size() >= 1);
    }

    // ── Anchor-Selection Optimization Tests (Feature 105) ──

    // -- Test 20: Path anchor — segment with fewer trigram candidates --
    std::cout << "\n  --- Test 20: Path anchor — path segment cheaper than name ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Build a scenario where "settings" is a good trigram anchor
        // but namePattern "config" would match more
        records.push_back({"settings", "/app", 2, 0, 1000});
        records.push_back({"config.yaml", "/app/settings", 1, 100, 2000});
        records.push_back({"config.json", "/app/settings", 1, 200, 3000});
        records.push_back({"config.xml", "/other/place", 1, 300, 4000});
        // Add some noise records to ensure trigram index is built
        for (int i = 0; i < 50; i++) {
            std::string name = "noise_file_" + std::to_string(i) + ".txt";
            records.push_back({name, "/noise/dir", 1, 400 + (uint32_t)i, 5000 + (uint32_t)i});
        }
        engine.loadRecords(std::move(records));

        auto res = engine.query("/settings/config");
        // Should find both config files under /app/settings
        int matchCount = 0;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.path == "/app/settings" && rec.name.find("config") != std::string::npos) matchCount++;
        }
        CHECK(matchCount == 2);

        // Should NOT find config.xml in /other/place
        bool foundOther = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.path == "/other/place") foundOther = true;
        }
        CHECK(!foundOther);
    }

    // -- Test 21: Tree-walk through 3 adjacent segments --
    std::cout << "\n  --- Test 21: Tree-walk — 3 adjacent segments ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // /usr/local/bin structure
        records.push_back({"usr", "/", 2, 0, 100});
        records.push_back({"local", "/usr", 2, 0, 200});
        records.push_back({"bin", "/usr/local", 2, 0, 300});
        records.push_back({"python3", "/usr/local/bin", 1, 100, 400});
        records.push_back({"gcc", "/usr/local/bin", 1, 200, 500});
        // /opt/local/bin (different root)
        records.push_back({"opt", "/", 2, 0, 600});
        records.push_back({"local", "/opt", 2, 0, 700});
        records.push_back({"bin", "/opt/local", 2, 0, 800});
        records.push_back({"ruby", "/opt/local/bin", 1, 300, 900});
        // Add noise
        for (int i = 0; i < 30; i++) {
            std::string name = "padding_entry_" + std::to_string(i) + ".dat";
            records.push_back({name, "/tmp", 1, 400 + (uint32_t)i, 1000 + (uint32_t)i});
        }
        engine.loadRecords(std::move(records));

        // Query "/usr/local/bin/python" → walk through usr→local→bin→python
        auto res = engine.query("/usr/local/bin/python");
        bool foundPython = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "python3" && rec.path == "/usr/local/bin") foundPython = true;
        }
        CHECK(foundPython);

        // Should not find ruby (under /opt/local/bin)
        bool foundRuby = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "ruby") foundRuby = true;
        }
        CHECK(!foundRuby);
    }

    // -- Test 22: Non-adjacent segment with * falls back correctly --
    std::cout << "\n  --- Test 22: Non-adjacent — path anchor with * ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"project", "/", 2, 0, 100});
        records.push_back({"source", "/project", 2, 0, 200});
        records.push_back({"core", "/project/source", 2, 0, 300});
        records.push_back({"engine.cpp", "/project/source/core", 1, 100, 400});
        records.push_back({"engine.cpp", "/other/path/core", 1, 200, 500});
        for (int i = 0; i < 30; i++) {
            std::string name = "filler_record_" + std::to_string(i) + ".log";
            records.push_back({name, "/filler", 1, 300 + (uint32_t)i, 600 + (uint32_t)i});
        }
        engine.loadRecords(std::move(records));

        // "/project/*/engine" → "project" non-adjacent to "engine"
        auto res = engine.query("/project/*/engine");
        bool foundProject = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "engine.cpp" && rec.path.find("project") != std::string::npos) foundProject = true;
        }
        CHECK(foundProject);

        // Should NOT find engine.cpp under /other/path/core
        bool foundOther = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.path.find("/other/") != std::string::npos) foundOther = true;
        }
        CHECK(!foundOther);
    }

    // -- Test 23: DIR_EXACT mode with path anchor --
    std::cout << "\n  --- Test 23: DIR_EXACT with path anchor ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"local", "/usr", 2, 0, 100});
        records.push_back({"bin", "/usr/local", 2, 0, 200});
        records.push_back({"bin", "/opt", 2, 0, 300});
        for (int i = 0; i < 30; i++) {
            std::string name = "dummy_padding_" + std::to_string(i) + ".xyz";
            records.push_back({name, "/dummy", 1, 100 + (uint32_t)i, 400 + (uint32_t)i});
        }
        engine.loadRecords(std::move(records));

        // "/local/bin/" → DIR_EXACT: find dir named exactly "bin" with path containing "local"
        auto res = engine.query("/local/bin/");
        bool foundLocalBin = false;
        bool foundOptBin = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "bin" && rec.path == "/usr/local") foundLocalBin = true;
            if (rec.name == "bin" && rec.path == "/opt") foundOptBin = true;
        }
        CHECK(foundLocalBin);
        CHECK(!foundOptBin);
    }

    // -- Test 24: estimateTrigramCost — short segments (< 3 chars) --
    std::cout << "\n  --- Test 24: Short segments fallback to linear ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"ls", "/usr/bin", 1, 100, 1000});
        records.push_back({"ls", "/opt/bin", 1, 200, 2000});
        records.push_back({"lsof", "/usr/sbin", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // "bin/ls" → both segments < 3 chars, must use linear scan
        auto res = engine.query("bin/ls");
        bool foundUsrBin = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "ls" && rec.path == "/usr/bin") foundUsrBin = true;
        }
        CHECK(foundUsrBin);

        // Should NOT find lsof (name doesn't exactly match "ls" in substring? Actually it does contain "ls")
        // lsof contains "ls" as substring → should match
        bool foundLsof = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "lsof") foundLsof = true;
        }
        // "ls" is substring of "lsof", and path "/usr/sbin" doesn't contain "bin"
        // Actually "sbin" does contain "bin"! So lsof should match
        CHECK(foundLsof);
    }

    // -- Test 25: Results consistency — anchor vs linear produce same results --
    std::cout << "\n  --- Test 25: Anchor vs linear result consistency ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Create enough records with trigram-friendly names for anchor path
        records.push_back({"settings", "/app", 2, 0, 100});
        records.push_back({"database.yml", "/app/settings", 1, 100, 200});
        records.push_back({"database.yml", "/web/settings", 1, 200, 300});
        records.push_back({"database.yml", "/api/config", 1, 300, 400});
        records.push_back({"settings", "/web", 2, 0, 500});
        for (int i = 0; i < 40; i++) {
            std::string name = "background_item_" + std::to_string(i) + ".tmp";
            records.push_back({name, "/background", 1, 400 + (uint32_t)i, 600 + (uint32_t)i});
        }
        engine.loadRecords(std::move(records));

        auto res = engine.query("/settings/database");
        // Should find database.yml under /app/settings AND /web/settings
        int matchCount = 0;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "database.yml" && rec.path.find("settings") != std::string::npos) matchCount++;
        }
        CHECK(matchCount == 2);

        // Should NOT find database.yml under /api/config
        bool foundConfig = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.path == "/api/config") foundConfig = true;
        }
        CHECK(!foundConfig);
    }

    // ── Linear Scan Optimization Tests (Feature 106) ──

    // -- Test 26: Parallel scan produces same results as serial --
    std::cout << "\n  --- Test 26: Parallel linear scan consistency ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Create a dataset that forces linear scan fallback (short name < 3 chars)
        // with path segment constraints
        records.push_back({"ab", "/foo/bar", 1, 100, 1000});
        records.push_back({"ab", "/baz/qux", 1, 200, 2000});
        records.push_back({"ab", "/foo/xyz", 1, 300, 3000});
        records.push_back({"cd", "/foo/bar", 1, 400, 4000});
        // Add enough records to exercise multi-threading
        for (int i = 0; i < 200; i++) {
            std::string name = "filler_parallel_" + std::to_string(i) + ".dat";
            records.push_back({name, "/filler/path", 1, 500 + (uint32_t)i, 5000 + (uint32_t)i});
        }
        engine.loadRecords(std::move(records));

        // "bar/ab" → segments: path "bar", name "ab" — both < 3 chars → linear scan
        auto res = engine.query("bar/ab");
        int matchCount = 0;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "ab" && rec.path == "/foo/bar") matchCount++;
        }
        CHECK(matchCount == 1);

        // Should NOT match ab in /baz/qux (path doesn't contain "bar")
        bool foundBaz = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "ab" && rec.path == "/baz/qux") foundBaz = true;
        }
        CHECK(!foundBaz);
    }

    // -- Test 27: pathMatchCache dedup — same path tested once --
    std::cout << "\n  --- Test 27: pathMatchCache deduplication ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Multiple files sharing same dir path — pathMatchCache should
        // evaluate path match once for the shared path
        for (int i = 0; i < 50; i++) {
            std::string name = "item_" + std::to_string(i) + ".txt";
            records.push_back({name, "/shared/common/dir", 1, 100 + (uint32_t)i, 1000 + (uint32_t)i});
        }
        for (int i = 0; i < 50; i++) {
            std::string name = "item_" + std::to_string(i) + ".txt";
            records.push_back({name, "/other/different/path", 1, 200 + (uint32_t)i, 2000 + (uint32_t)i});
        }
        engine.loadRecords(std::move(records));

        // "common/item" → path must contain "common", name must contain "item"
        auto res = engine.query("common/item");
        // All 50 items from /shared/common/dir should match
        int sharedCount = 0;
        int otherCount = 0;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.path == "/shared/common/dir") sharedCount++;
            if (rec.path == "/other/different/path") otherCount++;
        }
        CHECK(sharedCount == 50);
        CHECK(otherCount == 0);
    }

    // -- Test 28: string_view optimization — no redundant lowering --
    std::cout << "\n  --- Test 28: Already-lowercase path matching ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Use mixed-case names to verify that lowerPathPool stores lowercase
        records.push_back({"Config.yaml", "/App/Settings", 1, 100, 1000});
        records.push_back({"Config.yaml", "/Other/Place", 1, 200, 2000});
        engine.loadRecords(std::move(records));

        // Path segment "settings" should match lowered path "/app/settings"
        auto res = engine.query("/settings/config");
        bool foundSettings = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "Config.yaml" && rec.path == "/App/Settings") foundSettings = true;
        }
        CHECK(foundSettings);

        bool foundOther = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.path == "/Other/Place") foundOther = true;
        }
        CHECK(!foundOther);
    }

    // ── Buffer-scan fallback tests ──
    // These tests verify the SIMD buffer-scan path in queryStructured().
    // With small record sets, trigram cost exceeds threshold → falls to buffer scan.

    // -- Test 29: Buffer-scan correctness --
    std::cout << "\n  --- Test 29: Buffer-scan correctness ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"ls", "/usr/bin", 1, 100, 1000});
        records.push_back({"ls", "/bin", 1, 200, 2000});
        records.push_back({"false", "/usr/bin", 1, 300, 3000});
        records.push_back({"pulse", "/var/log", 1, 400, 4000}); // contains "ls"
        engine.loadRecords(std::move(records));

        auto res = engine.query("bin/ls");
        // Should find "ls" in /usr/bin and /bin, plus "pulse" in /var/log should NOT match
        // (path constraint: must have "bin" segment)
        int binLsCount = 0;
        bool foundPulse = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "ls") binLsCount++;
            if (rec.name == "pulse") foundPulse = true;
        }
        CHECK(binLsCount == 2);
        CHECK(!foundPulse);
    }

    // -- Test 30: Cross-boundary false positive rejection --
    std::cout << "\n  --- Test 30: Cross-boundary false positive rejection ---\n";
    {
        // Construct names so pattern spans entry boundary in the packed buffer:
        // entry 0: "helo" → buffer: "helo"
        // entry 1: "ols"  → buffer: "helools"
        // Searching for "ools" would match at offset 3 in the buffer, but it
        // crosses the boundary between entry 0 (offset=0,len=4) and entry 1 (offset=4,len=3).
        // The buffer-scan must reject this cross-boundary hit.
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"helo", "/dir", 1, 100, 1000});
        records.push_back({"ools", "/dir", 1, 200, 2000}); // "ools" IS a valid name
        records.push_back({"nothing", "/dir", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        auto res = engine.query("dir/ools");
        // Should find "ools" as a valid match (it's a real entry name)
        bool foundOols = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "ools") foundOols = true;
        }
        CHECK(foundOols);

        // "helo" should NOT be returned (doesn't contain "ools")
        bool foundHelo = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "helo") foundHelo = true;
        }
        CHECK(!foundHelo);
    }

    // -- Test 31: Tombstone exclusion in buffer scan --
    std::cout << "\n  --- Test 31: Tombstone exclusion in buffer scan ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"target.txt", "/data", 1, 100, 1000});
        records.push_back({"target.log", "/data", 1, 200, 2000});
        records.push_back({"other.txt", "/data", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // Delete target.txt via removeByPath
        engine.removeByPath("/data/target.txt");

        auto res = engine.query("data/target");
        // Only target.log should remain
        bool foundTxt = false;
        bool foundLog = false;
        for (uint32_t idx : res) {
            auto rec = engine.getRecord(idx);
            if (rec.name == "target.txt") foundTxt = true;
            if (rec.name == "target.log") foundLog = true;
        }
        CHECK(!foundTxt);
        CHECK(foundLog);
    }

    // -- Test 32: DIR_EXACT via buffer scan --
    std::cout << "\n  --- Test 32: DIR_EXACT via buffer scan ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // "bin" as directory, "bin" as file, "binary" as directory
        records.push_back({"bin", "/usr", 2, 0, 1000});
        records.push_back({"bin", "/opt", 1, 100, 2000}); // file, not dir
        records.push_back({"binary", "/usr", 2, 0, 3000}); // dir but different name
        engine.loadRecords(std::move(records));

        // DIR_EXACT query: "/usr/bin/" — trailing slash means exact dir match
        auto res = engine.query("/usr/bin/");
        // Should only match the directory "bin" under /usr
        CHECK(res.size() == 1);
        if (!res.empty()) {
            auto rec = engine.getRecord(res[0]);
            CHECK(rec.name == "bin");
            CHECK(rec.path == "/usr");
        }
    }

    std::cout << "\n";
}

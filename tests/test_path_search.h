#pragma once
// ═══════════════════════════════════════════════════════
//  Part 3b: Path-based Search Tests
// ═══════════════════════════════════════════════════════

static void runPathSearchTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 3b: Path-based Search Tests\n";
    std::cout << "========================================\n\n";

    SearchEngine engine;

    std::vector<FileRecord> records;
    records.push_back({"main.cpp", "/usr/local/src", 1, 100, 1000});
    records.push_back({"config.json", "/etc/myapp", 1, 200, 2000});
    records.push_back({"readme.md", "/home/user/projects", 1, 300, 3000});
    records.push_back({"libfoo.dylib", "/usr/local/lib", 1, 400, 4000});
    records.push_back({"include", "/usr/local", 2, 0, 5000});
    engine.loadRecords(std::move(records));

    // Plain keyword search by path component (no slash → PLAIN mode → path trigram)
    // "local" matches all 3 files under /usr/local/ via path trigram
    auto res = engine.query("local");
    check(res.size() == 3, "Path search 'local': 3 matches (path contains 'local')");

    // Slash query with node-centric semantics:
    // "/usr/local" → SEGMENTS: name contains "local", path contains "usr"
    // "include" (name) doesn't contain "local", "main.cpp" doesn't, "libfoo.dylib" doesn't
    // So 0 results — this is expected node-centric behavior.
    // To find files UNDER /usr/local, users search "local" (plain) not "/usr/local"
    res = engine.query("/usr/local");
    // Node-centric: name must contain "local" — no file names do
    check(res.size() == 0, "Path search '/usr/local': 0 (node-centric, no name contains 'local')");

    // "/etc/config" → name contains "config", path contains "etc"
    res = engine.query("/etc/config");
    check(res.size() == 1, "Path search '/etc/config': 1 match");
    check(engine.getRecord(res[0]).name == "config.json", "Path search '/etc/config': correct file");

    // Search by directory name only (plain, no slash)
    res = engine.query("projects");
    check(res.size() == 1, "Dir name search 'projects': 1 match");
    check(engine.getRecord(res[0]).name == "readme.md", "Dir name search 'projects': correct file");

    // Glob on path (still works for PLAIN mode glob patterns)
    res = engine.query("*/lib/*");
    check(res.size() == 1, "Glob '*/lib/*': 1 match");
    check(engine.getRecord(res[0]).name == "libfoo.dylib", "Glob '*/lib/*': correct file");

    // Path search after addRecord (plain search)
    engine.addRecord({"newlib.a", "/usr/local/lib", 1, 500, 6000});
    // "newlib" (plain) finds the new file
    res = engine.query("newlib");
    check(res.size() == 1, "After addRecord: 'newlib' returns 1 match");

    // Path search after removeByPath
    engine.removeByPath("/usr/local/lib/libfoo.dylib");
    res = engine.query("libfoo");
    check(res.size() == 0, "After removeByPath: 'libfoo' returns 0 match");

    // Path search after updateByPath
    engine.updateByPath("/usr/local/lib/newlib.a", {"newlib_v2.a", "/opt/lib", 1, 600, 7000});
    res = engine.query("newlib_v2");
    check(res.size() == 1, "After updateByPath: 'newlib_v2' returns 1 match");

    std::cout << "\n";
}

// ═══════════════════════════════════════════════════════
//  Part 3b-2: lowerPathPool_ / Dedup Path Matching Tests
// ═══════════════════════════════════════════════════════

static void runLowerPathPoolTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 3b-2: lowerPathPool & Dedup Path Match\n";
    std::cout << "========================================\n\n";

    // --- 1. Case-insensitive path search via lowerPathPool_ ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"App.swift", "/Users/Dev/MyProject/Sources", 1, 100, 1000});
        records.push_back({"main.cpp", "/Volumes/DATA/CppProject/src", 1, 200, 2000});
        records.push_back({"readme.txt", "/home/User/Documents", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // Plain keyword path search (no slash) should be case-insensitive
        auto res = engine.query("myproject");
        check(res.size() == 1, "lowerPathPool: 'myproject' matches '/Users/Dev/MyProject/Sources'");
        check(engine.getRecord(res[0]).name == "App.swift", "lowerPathPool: correct file for 'myproject'");

        res = engine.query("CPPPROJECT");
        check(res.size() == 1, "lowerPathPool: 'CPPPROJECT' matches path with mixed case");

        res = engine.query("documents");
        check(res.size() == 1, "lowerPathPool: 'documents' matches '/home/User/Documents'");

        // Short keyword (2 chars) triggers linear scan path
        res = engine.query("de");
        check(res.size() >= 1, "lowerPathPool: 2-char search 'de' finds path match");
    }

    // --- 2. addRecord preserves lowerPathPool_ invariant ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"base.txt", "/root", 1, 100, 1000});
        engine.loadRecords(std::move(records));

        // Add record with mixed-case path
        engine.addRecord({"NewFile.py", "/Users/Admin/PyProject", 1, 200, 2000});

        auto res = engine.query("pyproject");
        check(res.size() == 1, "addRecord lowerPathPool: 'pyproject' matches after addRecord");
        check(engine.getRecord(res[0]).name == "NewFile.py", "addRecord lowerPathPool: correct file");

        res = engine.query("admin");
        check(res.size() == 1, "addRecord lowerPathPool: 'admin' matches path '/Users/Admin/...'");
    }

    // --- 3. updateByPath preserves lowerPathPool_ invariant ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"old.txt", "/Data/OldDir", 1, 100, 1000});
        engine.loadRecords(std::move(records));

        engine.updateByPath("/Data/OldDir/old.txt", {"new.txt", "/Data/NewDir/SubFolder", 1, 200, 2000});

        auto res = engine.query("olddir");
        check(res.size() == 0, "updateByPath lowerPathPool: old path no longer matches");

        res = engine.query("subfolder");
        check(res.size() == 1, "updateByPath lowerPathPool: new path matches");
        check(engine.getRecord(res[0]).name == "new.txt", "updateByPath lowerPathPool: correct updated file");
    }

    // --- 4. compaction preserves lowerPathPool_ and path search ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"a.txt", "/Path/Alpha", 1, 100, 1000});
        records.push_back({"b.txt", "/Path/Beta", 1, 200, 2000});
        records.push_back({"c.txt", "/Path/Gamma", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // Remove middle record to create tombstone
        engine.removeByPath("/Path/Beta/b.txt");
        check(engine.liveRecordCount() == 2, "compaction setup: 2 live records");

        // Compact
        auto mapping = engine.compactRecords();
        check(!mapping.empty(), "compaction occurred");
        check(engine.liveRecordCount() == 2, "compaction: still 2 live records");

        // Path search still works after compaction with case-insensitive match
        auto res = engine.query("alpha");
        check(res.size() == 1, "post-compaction lowerPathPool: 'alpha' matches");
        check(engine.getRecord(res[0]).name == "a.txt", "post-compaction: correct file for 'alpha'");

        res = engine.query("gamma");
        check(res.size() == 1, "post-compaction lowerPathPool: 'gamma' matches");

        res = engine.query("beta");
        check(res.size() == 0, "post-compaction: removed path 'beta' no longer matches");
    }

    // --- 5. Slash-boundary query with node-centric semantics ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Path ends with "foo", name starts with "bar"
        records.push_back({"bar_file.txt", "/data/foo", 1, 100, 1000});
        records.push_back({"other.txt", "/data/baz", 1, 200, 2000});
        engine.loadRecords(std::move(records));

        // "foo/bar" → node-centric: name contains "bar", path contains "foo"
        auto res = engine.query("foo/bar");
        check(res.size() == 1, "slash-boundary: 'foo/bar' matches bar_file.txt (name has 'bar', path has 'foo')");
        check(engine.getRecord(res[0]).name == "bar_file.txt", "slash-boundary: correct file");

        // Another test: "settings/config"
        SearchEngine engine2;
        std::vector<FileRecord> records2;
        records2.push_back({"config.yml", "/app/Settings", 1, 100, 1000});
        records2.push_back({"data.json", "/app/Cache", 1, 200, 2000});
        engine2.loadRecords(std::move(records2));

        // "settings/config" → name contains "config", path contains "settings"
        res = engine2.query("settings/config");
        check(res.size() == 1, "slash-boundary: 'settings/config' matches config.yml");
    }

    // --- 6. resolveRecordPath returns original casing (not lowered) ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"Test.cpp", "/Users/Dev/MyProject", 1, 100, 1000});
        engine.loadRecords(std::move(records));

        std::string path = engine.resolveRecordPath(0);
        check(path == "/Users/Dev/MyProject", "resolveRecordPath returns original casing, not lowered");
    }

    std::cout << "\n";
}

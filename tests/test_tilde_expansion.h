#pragma once
// ═══════════════════════════════════════════════════════
//  Part 65: Tilde (~) Expansion in Query
// ═══════════════════════════════════════════════════════

static void runTildeExpansionTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 65: Tilde (~) Expansion Tests\n";
    std::cout << "========================================\n\n";

    const char* home = std::getenv("HOME");
    if (!home) {
        std::cout << "  SKIP: HOME not set\n";
        return;
    }
    std::string homeStr(home);

    SearchEngine engine;

    // Create records under the user's home directory structure
    std::vector<FileRecord> records;
    records.push_back({"f1.txt", homeStr + "/Downloads", 1, 100, 1000});
    records.push_back({"notes.txt", homeStr + "/Documents", 1, 200, 2000});
    records.push_back({"photo.jpg", homeStr + "/Pictures/vacation", 1, 300, 3000});
    records.push_back({"readme.md", homeStr + "/projects/myapp", 1, 400, 4000});
    records.push_back({"other.txt", "/tmp", 1, 500, 5000});
    engine.loadRecords(std::move(records));

    // ~/*/*.txt should expand to /Users/<user>/*/*.txt
    // and match f1.txt (Downloads) and notes.txt (Documents)
    auto res = engine.query("~/*/*.txt");
    check(res.size() == 2, "Tilde glob '~/*/*.txt': 2 matches (f1.txt, notes.txt)");

    // ~/Downloads/*.txt should match only f1.txt
    res = engine.query("~/Downloads/*.txt");
    check(res.size() == 1, "Tilde glob '~/Downloads/*.txt': 1 match");
    if (!res.empty()) {
        check(engine.getRecord(res[0]).name == "f1.txt", "Tilde glob '~/Downloads/*.txt': correct file");
    }

    // Bare ~ should remain a literal search term, not expand to the user's home path.
    res = engine.query("~");
    check(res.size() == 0, "Bare tilde '~': no home expansion");

    // ~/*.jpg — the glob '*' in this engine matches across '/' boundaries,
    // so this matches photo.jpg even though it's nested deeper.
    res = engine.query("~/*.jpg");
    check(res.size() == 1, "Tilde glob '~/*.jpg': 1 match (glob * crosses / boundaries)");

    // ~/Pictures/*/*.jpg should match photo.jpg
    res = engine.query("~/Pictures/*/*.jpg");
    check(res.size() == 1, "Tilde glob '~/Pictures/*/*.jpg': 1 match");
    if (!res.empty()) {
        check(engine.getRecord(res[0]).name == "photo.jpg", "Tilde glob '~/Pictures/*/*.jpg': correct file");
    }

    // Tilde should NOT expand in the middle of a string
    res = engine.query("foo~bar");
    check(res.size() == 0, "Tilde mid-string 'foo~bar': no expansion, 0 matches");

    std::cout << "  All tilde expansion tests passed!\n\n";
}

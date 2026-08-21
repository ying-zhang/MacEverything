#pragma once
// ═══════════════════════════════════════════════════════
//  Part 66: Whitespace Trim in preprocessQuery()
// ═══════════════════════════════════════════════════════

static void runWhitespaceTrimTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 66: Whitespace Trim Tests\n";
    std::cout << "========================================\n\n";

    const char* home = std::getenv("HOME");
    std::string homeStr(home ? home : "/tmp");

    SearchEngine engine;

    // Create test records
    std::vector<FileRecord> records;
    records.push_back({"hello.txt", "/tmp/docs", 1, 100, 1000});
    records.push_back({"world.txt", "/tmp/docs", 1, 200, 2000});
    records.push_back({"hello world.txt", "/tmp/docs", 1, 300, 3000});
    records.push_back({"f1.txt", homeStr + "/Downloads", 1, 400, 4000});
    records.push_back({"notes.txt", homeStr + "/Documents", 1, 500, 5000});
    engine.loadRecords(std::move(records));

    // Leading/trailing spaces should be stripped — same results as untrimmed
    auto trimmed = engine.query("  hello  ");
    auto plain   = engine.query("hello");
    check(trimmed.size() == plain.size(),
          "Trimmed '  hello  ' matches same count as 'hello'");

    // Tabs and newlines should be stripped
    auto tabbed = engine.query("\t hello \n");
    check(tabbed.size() == plain.size(),
          "Trimmed '\\t hello \\n' matches same count as 'hello'");

    // All-whitespace query should return 0 results
    auto allSpace = engine.query("   ");
    check(allSpace.size() == 0,
          "All-whitespace '   ' returns 0 results");

    // Single space should return 0 results
    auto singleSpace = engine.query(" ");
    check(singleSpace.size() == 0,
          "Single space ' ' returns 0 results");

    // Interior spaces should be preserved — "hello world" is a valid query
    auto interior = engine.query("hello world");
    auto interiorPadded = engine.query("  hello world  ");
    check(interior.size() == interiorPadded.size(),
          "Interior space preserved: '  hello world  ' same as 'hello world'");

    // Trim + tilde expansion combined: "  ~/Downloads/*.txt  "
    if (home) {
        auto tildeTrimmed = engine.query("  ~/Downloads/*.txt  ");
        auto tildeClean   = engine.query("~/Downloads/*.txt");
        check(tildeTrimmed.size() == tildeClean.size(),
              "Trim + tilde: '  ~/Downloads/*.txt  ' same as '~/Downloads/*.txt'");
        check(tildeTrimmed.size() == 1,
              "Trim + tilde: matches f1.txt");
    } else {
        std::cout << "  SKIP: tilde tests (HOME not set)\n";
    }

    std::cout << "  All whitespace trim tests passed!\n\n";
}

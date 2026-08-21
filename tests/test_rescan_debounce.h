#pragma once
// ═══════════════════════════════════════════════════════
//  Part 29: Rescan Debounce Tests
// ═══════════════════════════════════════════════════════

#include "../MacEverything/Core/RescanDebounce.h"

static void runRescanDebounceTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 29: Rescan Debounce Tests\n";
    std::cout << "========================================\n\n";

    // ── isPathSubsumedBy ──

    check(isPathSubsumedBy("/", "/"), "exact match root");
    check(isPathSubsumedBy("/Users", "/"), "root subsumes /Users");
    check(isPathSubsumedBy("/Users/username", "/"), "root subsumes nested path");
    check(isPathSubsumedBy("/Users/username", "/Users"), "/Users subsumes /Users/username");
    check(isPathSubsumedBy("/Users", "/Users"), "exact match non-root");
    check(!isPathSubsumedBy("/Users2", "/Users"), "boundary: /Users does NOT subsume /Users2");
    check(!isPathSubsumedBy("/var", "/Users"), "disjoint paths");
    check(!isPathSubsumedBy("/", "/Users"), "parent is NOT subsumed by child");
    check(isPathSubsumedBy("/a/b/c/d", "/a/b"), "deep nesting");
    check(!isPathSubsumedBy("/a/bc", "/a/b"), "boundary: /a/b does NOT subsume /a/bc");

    std::cout << "  isPathSubsumedBy: all passed\n\n";

    // ── mergeRescanPaths ──

    {
        // Empty + empty
        std::set<std::string> empty;
        auto r = mergeRescanPaths(empty, {});
        check(r.empty(), "merge: empty + empty = empty");
    }
    {
        // Empty + single
        std::set<std::string> empty;
        auto r = mergeRescanPaths(empty, {"/Users"});
        check(r.size() == 1 && r.count("/Users"), "merge: empty + /Users");
    }
    {
        // Parent subsumes child: pending={/Users}, new={/}
        std::set<std::string> pending = {"/Users"};
        auto r = mergeRescanPaths(pending, {"/"});
        check(r.size() == 1 && r.count("/"), "merge: /Users + / => /");
    }
    {
        // Child subsumed by pending: pending={/}, new={/Users}
        std::set<std::string> pending = {"/"};
        auto r = mergeRescanPaths(pending, {"/Users"});
        check(r.size() == 1 && r.count("/"), "merge: / + /Users => /");
    }
    {
        // Disjoint
        std::set<std::string> pending = {"/Users", "/var"};
        auto r = mergeRescanPaths(pending, {"/tmp"});
        check(r.size() == 3, "merge: disjoint paths preserved");
    }
    {
        // New path subsumes multiple pending
        std::set<std::string> pending = {"/a/b", "/c"};
        auto r = mergeRescanPaths(pending, {"/a"});
        check(r.size() == 2 && r.count("/a") && r.count("/c"),
              "merge: /a subsumes /a/b but not /c");
    }
    {
        // Exact duplicate
        std::set<std::string> pending = {"/Users"};
        auto r = mergeRescanPaths(pending, {"/Users"});
        check(r.size() == 1 && r.count("/Users"), "merge: dedup");
    }
    {
        // Intra-batch subsumption: new contains both /a/b and /a
        std::set<std::string> empty;
        auto r = mergeRescanPaths(empty, {"/a/b", "/a", "/c"});
        check(r.size() == 2 && r.count("/a") && r.count("/c"),
              "merge: intra-batch subsumption");
    }
    {
        // Root subsumes all
        std::set<std::string> pending = {"/Users", "/var", "/tmp"};
        auto r = mergeRescanPaths(pending, {"/"});
        check(r.size() == 1 && r.count("/"), "merge: root subsumes all");
    }
    {
        // Boundary correctness in merge
        std::set<std::string> pending = {"/Users"};
        auto r = mergeRescanPaths(pending, {"/Users2"});
        check(r.size() == 2, "merge: /Users and /Users2 are disjoint");
    }

    std::cout << "  mergeRescanPaths: all passed\n\n";

    // ── minimizeRescanPaths ──

    {
        auto r = minimizeRescanPaths({"/a/b/c", "/a", "/a/b", "/c/d", "/c"});
        check(r.size() == 2 && r[0] == "/a" && r[1] == "/c",
              "minimize: parent paths cover descendants");
    }
    {
        auto r = minimizeRescanPaths({"/Users", "/Users2", "/Users/name", "/Users"});
        check(r.size() == 2 && r[0] == "/Users" && r[1] == "/Users2",
              "minimize: dedup and boundary correctness");
    }
    {
        auto r = minimizeRescanPaths({"", "/tmp/a", "/tmp/a/b"});
        check(r.size() == 1 && r[0] == "/tmp/a", "minimize: ignores empty paths");
    }

    std::cout << "  minimizeRescanPaths: all passed\n\n";

    // ── shouldThrottleRescan ──

    {
        // Empty map => not throttled
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> empty;
        check(!shouldThrottleRescan("/", empty, 300.0), "throttle: empty map => false");
    }
    {
        // Just recorded => throttled
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m;
        m["/"] = std::chrono::steady_clock::now();
        check(shouldThrottleRescan("/", m, 300.0), "throttle: just recorded => true");
    }
    {
        // Long ago => not throttled
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m;
        m["/"] = std::chrono::steady_clock::now() - std::chrono::seconds(600);
        check(!shouldThrottleRescan("/", m, 300.0), "throttle: 600s ago with 300s interval => false");
    }
    {
        // Different path => not throttled
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m;
        m["/Users"] = std::chrono::steady_clock::now();
        check(!shouldThrottleRescan("/var", m, 300.0), "throttle: different path => false");
    }

    std::cout << "  shouldThrottleRescan: all passed\n\n";
}

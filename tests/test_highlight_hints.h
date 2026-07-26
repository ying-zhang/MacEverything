// tests/test_highlight_hints.h — Unit tests for HighlightHintExtractor
// Part 70

#include "../MacEverything/Core/HighlightHintExtractor.h"
#include <cstdlib>

inline void runHighlightHintTests() {
    std::cout << "\n═══ Part 70: Highlight Hint Extraction ═══\n\n";

    // --- Basic TERM ---
    {
        std::cout << "  test: basic substring term\n";
        auto hints = extractHighlightHints("hello");
        check(hints.size() == 1, "should produce 1 hint");
        check(hints[0].text == "hello", "text should be 'hello'");
        check(hints[0].mode == MatchMode::SUBSTRING, "mode should be SUBSTRING");
        check(hints[0].field == HintField::ANY, "field should be ANY");
        check(!hints[0].caseSensitive, "should not be case-sensitive");
    }

    // --- Modifier: case: ---
    {
        std::cout << "  test: case: modifier\n";
        auto hints = extractHighlightHints("case:Hello");
        check(hints.size() == 1, "should produce 1 hint");
        check(hints[0].text == "Hello", "text should be 'Hello'");
        check(hints[0].caseSensitive, "should be case-sensitive");
        check(hints[0].mode == MatchMode::SUBSTRING, "mode should be SUBSTRING");
    }

    // --- Modifier: regex: ---
    {
        std::cout << "  test: regex: modifier\n";
        auto hints = extractHighlightHints("regex:test.*");
        check(hints.size() == 1, "should produce 1 hint");
        check(hints[0].text == "test.*", "text should be 'test.*'");
        check(hints[0].mode == MatchMode::REGEX, "mode should be REGEX");
    }

    // --- Modifier: ww: ---
    {
        std::cout << "  test: ww: modifier (whole word)\n";
        auto hints = extractHighlightHints("ww:test");
        check(hints.size() == 1, "should produce 1 hint");
        check(hints[0].text == "test", "text should be 'test'");
        check(hints[0].mode == MatchMode::WHOLEWORD, "mode should be WHOLEWORD");
    }

    // --- Modifier: wfn: ---
    {
        std::cout << "  test: wfn: modifier (whole filename)\n";
        auto hints = extractHighlightHints("wfn:readme");
        check(hints.size() == 1, "should produce 1 hint");
        check(hints[0].text == "readme", "text should be 'readme'");
        check(hints[0].mode == MatchMode::WHOLEFILENAME, "mode should be WHOLEFILENAME");
    }

    // --- NOT: negated terms should NOT appear ---
    {
        std::cout << "  test: NOT operator excludes negated term\n";
        auto hints = extractHighlightHints("hello !secret");
        check(hints.size() == 1, "should produce 1 hint (only 'hello')");
        check(hints[0].text == "hello", "hint should be 'hello'");
        // "secret" must not appear
        for (auto& h : hints) {
            check(h.text != "secret", "'secret' must not appear as hint");
        }
    }

    // --- Multi-keyword ---
    {
        std::cout << "  test: multi-keyword produces multiple hints\n";
        auto hints = extractHighlightHints("hello world");
        check(hints.size() == 2, "should produce 2 hints");
        check(hints[0].text == "hello", "first hint should be 'hello'");
        check(hints[1].text == "world", "second hint should be 'world'");
    }

    // --- Tilde expansion ---
    {
        std::cout << "  test: tilde expansion in query\n";
        const char* home = std::getenv("HOME");
        if (home) {
            auto hints = extractHighlightHints("~/test");
            // After tilde expansion + slash transform, we should get a hint for "test"
            bool foundTest = false;
            for (auto& h : hints) {
                if (h.text == "test") foundTest = true;
            }
            check(foundTest, "should contain hint for 'test' after ~ expansion");
        } else {
            std::cout << "    (skipped — HOME not set)\n";
        }
    }

    // --- Glob pattern ---
    {
        std::cout << "  test: glob pattern\n";
        auto hints = extractHighlightHints("*.cpp");
        check(hints.size() == 1, "should produce 1 hint");
        check(hints[0].mode == MatchMode::GLOB, "mode should be GLOB");
        check(hints[0].text == "*.cpp", "text should be '*.cpp'");
    }

    // --- path: filter ---
    {
        std::cout << "  test: path: filter produces PATH hint\n";
        auto hints = extractHighlightHints("path:downloads");
        check(hints.size() == 1, "should produce 1 hint");
        check(hints[0].field == HintField::PATH, "field should be PATH");
        check(hints[0].mode == MatchMode::SUBSTRING, "mode should be SUBSTRING");
    }

    // --- file: filter ---
    {
        std::cout << "  test: file: filter produces NAME hint\n";
        auto hints = extractHighlightHints("file:readme");
        check(hints.size() == 1, "should produce 1 hint");
        check(hints[0].field == HintField::NAME, "field should be NAME");
    }

    // --- Compound: ext:cpp + case:Hello ---
    {
        std::cout << "  test: compound ext:cpp + case:Hello\n";
        auto hints = extractHighlightHints("ext:cpp case:Hello");
        // ext: is a filter (not highlightable), case: becomes a TERM
        check(hints.size() == 1, "should produce 1 hint (ext: is not highlighted)");
        check(hints[0].text == "Hello", "hint text should be 'Hello'");
        check(hints[0].caseSensitive, "should be case-sensitive");
    }

    // --- Slash query: /usr/local/test ---
    {
        std::cout << "  test: slash query /usr/local/test\n";
        auto hints = extractHighlightHints("/usr/local/test");
        // transformSlashTerms converts to AND(__pathseg, TERM("test"))
        bool foundTest = false;
        for (auto& h : hints) {
            if (h.text == "test") {
                foundTest = true;
                check(h.field == HintField::NAME, "slash query name part should be NAME field");
            }
        }
        check(foundTest, "should contain hint for 'test'");
    }

    // --- Empty query ---
    {
        std::cout << "  test: empty query\n";
        auto hints = extractHighlightHints("");
        check(hints.empty(), "empty query should produce no hints");
    }

    // --- Whitespace-only query ---
    {
        std::cout << "  test: whitespace-only query\n";
        auto hints = extractHighlightHints("   ");
        check(hints.empty(), "whitespace-only query should produce no hints");
    }

    // --- Pure filter (no highlightable text) ---
    {
        std::cout << "  test: pure ext: filter produces no hints\n";
        auto hints = extractHighlightHints("ext:cpp");
        check(hints.empty(), "ext: filter should produce no hints");
    }

    // --- Quoted phrase ---
    {
        std::cout << "  test: quoted phrase\n";
        auto hints = extractHighlightHints("\"hello world\"");
        check(hints.size() == 1, "should produce 1 hint");
        check(hints[0].text == "hello world", "text should be 'hello world'");
    }

    // --- OR operator ---
    {
        std::cout << "  test: OR operator\n";
        auto hints = extractHighlightHints("hello | world");
        check(hints.size() == 2, "should produce 2 hints");
        bool hasHello = false, hasWorld = false;
        for (auto& h : hints) {
            if (h.text == "hello") hasHello = true;
            if (h.text == "world") hasWorld = true;
        }
        check(hasHello && hasWorld, "should have both 'hello' and 'world'");
    }

    // --- size: filter (non-text filter) ---
    {
        std::cout << "  test: size: filter produces no hints\n";
        auto hints = extractHighlightHints("size:>1mb");
        check(hints.empty(), "size: filter should produce no hints");
    }

    // --- Mixed: keyword + multiple filters ---
    {
        std::cout << "  test: mixed keyword + filters\n";
        auto hints = extractHighlightHints("readme ext:md path:docs");
        // "readme" → TERM, ext:md → no hint, path:docs → PATH hint
        check(hints.size() == 2, "should produce 2 hints");
        bool hasReadme = false, hasDocs = false;
        for (auto& h : hints) {
            if (h.text == "readme") hasReadme = true;
            if (h.text == "docs" && h.field == HintField::PATH) hasDocs = true;
        }
        check(hasReadme, "should have 'readme' hint");
        check(hasDocs, "should have 'docs' PATH hint");
    }

    // --- nopath: should NOT produce hints ---
    {
        std::cout << "  test: nopath: filter produces no hints\n";
        auto hints = extractHighlightHints("test nopath:bin");
        // "test" → TERM hint, nopath:bin → no hint (exclusion filter)
        check(hints.size() == 1, "should produce 1 hint");
        check(hints[0].text == "test", "only 'test' should be a hint");
    }

    std::cout << "\n  Part 70 complete.\n";
}

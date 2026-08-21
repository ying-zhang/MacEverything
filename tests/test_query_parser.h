#pragma once
#include <cassert>
#include <iostream>
#include <string>

// ── Part 55: Query Parser Tests ──

static void runQueryParserTests() {
    std::cout << "── Part 55: Query Parser ──\n";
    int localPassed = 0, localFailed = 0;

    auto check = [&](bool cond, const std::string& msg) {
        if (cond) { localPassed++; passed++; }
        else { localFailed++; failed++; std::cout << "  FAIL: " << msg << "\n"; }
    };

    // 55.1 Single word → TERM
    {
        auto ast = QueryParser::parse("hello");
        check(ast != nullptr, "55.1 non-null");
        check(ast->type == QueryNodeType::TERM, "55.1 type TERM");
        check(ast->text == "hello", "55.1 text");
    }

    // 55.2 Two words → AND(TERM, TERM)
    {
        auto ast = QueryParser::parse("foo bar");
        check(ast != nullptr, "55.2 non-null");
        check(ast->type == QueryNodeType::AND, "55.2 type AND");
        check(ast->children.size() == 2, "55.2 two children");
        check(ast->children[0]->type == QueryNodeType::TERM && ast->children[0]->text == "foo", "55.2 child0");
        check(ast->children[1]->type == QueryNodeType::TERM && ast->children[1]->text == "bar", "55.2 child1");
    }

    // 55.3 OR: foo | bar → OR(TERM, TERM)
    {
        auto ast = QueryParser::parse("foo | bar");
        check(ast != nullptr, "55.3 non-null");
        check(ast->type == QueryNodeType::OR, "55.3 type OR");
        check(ast->children.size() == 2, "55.3 two children");
        check(ast->children[0]->text == "foo", "55.3 child0");
        check(ast->children[1]->text == "bar", "55.3 child1");
    }

    // 55.4 NOT: !foo → NOT(TERM)
    {
        auto ast = QueryParser::parse("!foo");
        check(ast != nullptr, "55.4 non-null");
        check(ast->type == QueryNodeType::NOT, "55.4 type NOT");
        check(ast->children.size() == 1, "55.4 one child");
        check(ast->children[0]->text == "foo", "55.4 child text");
    }

    // 55.5 AND + NOT: foo !bar → AND(TERM, NOT(TERM))
    {
        auto ast = QueryParser::parse("foo !bar");
        check(ast != nullptr, "55.5 non-null");
        check(ast->type == QueryNodeType::AND, "55.5 type AND");
        check(ast->children.size() == 2, "55.5 two children");
        check(ast->children[0]->type == QueryNodeType::TERM, "55.5 child0 TERM");
        check(ast->children[1]->type == QueryNodeType::NOT, "55.5 child1 NOT");
        check(ast->children[1]->children[0]->text == "bar", "55.5 negated term");
    }

    // 55.6 Grouping: <foo | bar> baz → AND(OR(TERM, TERM), TERM)
    {
        auto ast = QueryParser::parse("<foo | bar> baz");
        check(ast != nullptr, "55.6 non-null");
        check(ast->type == QueryNodeType::AND, "55.6 type AND");
        check(ast->children.size() == 2, "55.6 two children");
        check(ast->children[0]->type == QueryNodeType::OR, "55.6 child0 OR");
        check(ast->children[0]->children.size() == 2, "55.6 OR has 2 kids");
        check(ast->children[1]->type == QueryNodeType::TERM && ast->children[1]->text == "baz", "55.6 child1 baz");
    }

    // 55.7 Quoted phrase → TERM
    {
        auto ast = QueryParser::parse("\"hello world\"");
        check(ast != nullptr, "55.7 non-null");
        check(ast->type == QueryNodeType::TERM, "55.7 type TERM");
        check(ast->text == "hello world", "55.7 text");
    }

    // 55.8 Filter → FILTER node
    {
        auto ast = QueryParser::parse("ext:cpp");
        check(ast != nullptr, "55.8 non-null");
        check(ast->type == QueryNodeType::FILTER, "55.8 type FILTER");
        check(ast->filterName == "ext", "55.8 filter name");
        check(ast->filterArg == "cpp", "55.8 filter arg");
    }

    // 55.9 Complex: foo ext:h !bar | baz
    // Precedence: AND binds tighter than OR
    // → OR(AND(foo, ext:h, NOT(bar)), TERM(baz))
    {
        auto ast = QueryParser::parse("foo ext:h !bar | baz");
        check(ast != nullptr, "55.9 non-null");
        check(ast->type == QueryNodeType::OR, "55.9 type OR");
        check(ast->children.size() == 2, "55.9 two OR children");
        auto& left = ast->children[0];
        check(left->type == QueryNodeType::AND, "55.9 left AND");
        check(left->children.size() == 3, "55.9 AND has 3 kids");
        check(left->children[0]->type == QueryNodeType::TERM, "55.9 AND kid0 TERM");
        check(left->children[1]->type == QueryNodeType::FILTER, "55.9 AND kid1 FILTER");
        check(left->children[2]->type == QueryNodeType::NOT, "55.9 AND kid2 NOT");
        check(ast->children[1]->type == QueryNodeType::TERM && ast->children[1]->text == "baz", "55.9 right baz");
    }

    // 55.10 Multiple OR: a | b | c → OR(a, b, c)
    {
        auto ast = QueryParser::parse("a | b | c");
        check(ast != nullptr, "55.10 non-null");
        check(ast->type == QueryNodeType::OR, "55.10 type OR");
        check(ast->children.size() == 3, "55.10 three children");
    }

    // 55.11 Nested groups: <<a | b> c>
    {
        auto ast = QueryParser::parse("<<a | b> c>");
        check(ast != nullptr, "55.11 non-null");
        check(ast->type == QueryNodeType::AND, "55.11 type AND");
        check(ast->children[0]->type == QueryNodeType::OR, "55.11 nested OR");
    }

    // 55.12 Empty input → nullptr
    {
        auto ast = QueryParser::parse("");
        check(ast == nullptr, "55.12 empty returns null");
    }

    // 55.13 Only whitespace → nullptr
    {
        auto ast = QueryParser::parse("   ");
        check(ast == nullptr, "55.13 whitespace returns null");
    }

    // 55.14 Three terms AND: a b c → AND(a, b, c)
    {
        auto ast = QueryParser::parse("a b c");
        check(ast != nullptr, "55.14 non-null");
        check(ast->type == QueryNodeType::AND, "55.14 type AND");
        check(ast->children.size() == 3, "55.14 three children");
    }

    // 55.15 Mixed AND/OR with groups: <a b> | <c d>
    {
        auto ast = QueryParser::parse("<a b> | <c d>");
        check(ast != nullptr, "55.15 non-null");
        check(ast->type == QueryNodeType::OR, "55.15 type OR");
        check(ast->children.size() == 2, "55.15 two OR children");
        check(ast->children[0]->type == QueryNodeType::AND, "55.15 child0 AND");
        check(ast->children[1]->type == QueryNodeType::AND, "55.15 child1 AND");
    }

    // 55.16 dump() doesn't crash
    {
        auto ast = QueryParser::parse("foo !bar | <baz ext:cpp>");
        check(ast != nullptr, "55.16 non-null");
        std::string d = ast->dump();
        check(!d.empty(), "55.16 dump not empty");
    }

    // 55.17 NOT with group: !<a | b> → NOT(OR(a, b))
    {
        auto ast = QueryParser::parse("!<a | b>");
        check(ast != nullptr, "55.17 non-null");
        check(ast->type == QueryNodeType::NOT, "55.17 type NOT");
        check(ast->children[0]->type == QueryNodeType::OR, "55.17 child is OR");
    }

    // 55.18 Bare ! at end → TERM("!")
    {
        auto ast = QueryParser::parse("foo !");
        check(ast != nullptr, "55.18 non-null");
        // Should be AND(TERM("foo"), TERM("!"))
        check(ast->type == QueryNodeType::AND, "55.18 type AND");
        check(ast->children[1]->type == QueryNodeType::TERM, "55.18 bare bang becomes TERM");
        check(ast->children[1]->text == "!", "55.18 text is !");
    }

    // 55.19 Adversarial grouping is bounded and remains parseable.
    {
        std::string deeplyNested(10'000, '<');
        deeplyNested += "needle";
        deeplyNested.append(10'000, '>');
        auto ast = QueryParser::parse(deeplyNested);
        check(ast != nullptr, "55.19 deeply nested query is bounded");
    }

    std::cout << "  Passed: " << localPassed << "  Failed: " << localFailed << "\n\n";
}

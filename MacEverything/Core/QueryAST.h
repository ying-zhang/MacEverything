#pragma once
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include "StructuredQueryParser.h"
#include "StringUtils.h"
#include "CompiledGlob.h"
#include <optional>

/// AST node types for the Everything-style query parser.
enum class QueryNodeType {
    TERM,       // text match (substring / glob / regex / wholeword / wholefilename)
    AND,        // all children must match
    OR,         // any child matches
    NOT,        // negate a single child
    FILTER,     // function filter (ext:, size:, dm:, file:, folder:, etc.)
};

/// Comparison operator for filter nodes.
enum class CompareOp { EQ, LT, LE, GT, GE, RANGE };

/// How a TERM node matches against text.
enum class MatchMode { SUBSTRING, GLOB, REGEX, WHOLEWORD, WHOLEFILENAME };

/// A single node in the parsed query AST.
struct QueryNode {
    QueryNodeType type;

    // --- TERM fields ---
    std::string text;
    std::string textLower;  // pre-computed lowercase of text (avoids per-record toLower)
    MatchMode mode = MatchMode::SUBSTRING;
    bool caseSensitive = false;
    bool matchPath = false;  // path: modifier
    bool nameOnly = false;   // match name only (set by transformSlashTerms)
    bool useNameKind = false; // apply anchored slash-query name semantics
    PathSegmentKind nameKind = PathSegmentKind::SUBSTRING;
    std::optional<CompiledGlob> compiledGlob;  // pre-compiled glob for zero-alloc matching

    // --- AND / OR / NOT: children ---
    std::vector<std::unique_ptr<QueryNode>> children;

    // --- FILTER fields ---
    std::string filterName;  // "ext", "size", "dm", etc.
    std::string filterArg;   // raw argument string
    CompareOp op = CompareOp::EQ;
    uint64_t numVal1 = 0, numVal2 = 0;
    bool filterValid = true;
    std::vector<std::string> extList;  // for ext: filter

    // --- Path segment constraint (for __pathseg internal filter) ---
    std::vector<PathSegment> pathSegments;
    QueryMode structuredMode = QueryMode::PLAIN;

    // --- Factory helpers ---

    static std::unique_ptr<QueryNode> makeTerm(const std::string& text,
                                                MatchMode mode = MatchMode::SUBSTRING) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryNodeType::TERM;
        n->text = text;
        n->textLower = me::toLower(text);
        n->mode = mode;
        return n;
    }

    /// Overload with pre-computed lowercase to avoid redundant me::toLower().
    static std::unique_ptr<QueryNode> makeTerm(const std::string& text,
                                                const std::string& precomputedLower,
                                                MatchMode mode) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryNodeType::TERM;
        n->text = text;
        n->textLower = precomputedLower;
        n->mode = mode;
        return n;
    }

    static std::unique_ptr<QueryNode> makeAnd(std::vector<std::unique_ptr<QueryNode>>&& kids) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryNodeType::AND;
        n->children = std::move(kids);
        return n;
    }

    static std::unique_ptr<QueryNode> makeOr(std::vector<std::unique_ptr<QueryNode>>&& kids) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryNodeType::OR;
        n->children = std::move(kids);
        return n;
    }

    static std::unique_ptr<QueryNode> makeNot(std::unique_ptr<QueryNode> child) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryNodeType::NOT;
        n->children.push_back(std::move(child));
        return n;
    }

    static std::unique_ptr<QueryNode> makeFilter(const std::string& name,
                                                  const std::string& arg) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryNodeType::FILTER;
        n->filterName = name;
        n->filterArg = arg;
        return n;
    }

    /// Debug representation of the AST.
    std::string dump(int indent = 0) const {
        std::string pad(indent * 2, ' ');
        std::ostringstream os;
        switch (type) {
            case QueryNodeType::TERM:
                os << pad << "TERM(\"" << text << "\")";
                if (mode != MatchMode::SUBSTRING) {
                    os << " mode=" << static_cast<int>(mode);
                }
                if (caseSensitive) os << " case";
                if (matchPath) os << " path";
                if (nameOnly) os << " nameOnly";
                if (useNameKind) os << " nameKind=" << static_cast<int>(nameKind);
                break;
            case QueryNodeType::AND:
                os << pad << "AND(\n";
                for (auto& c : children) os << c->dump(indent + 1) << "\n";
                os << pad << ")";
                break;
            case QueryNodeType::OR:
                os << pad << "OR(\n";
                for (auto& c : children) os << c->dump(indent + 1) << "\n";
                os << pad << ")";
                break;
            case QueryNodeType::NOT:
                os << pad << "NOT(\n";
                if (!children.empty()) os << children[0]->dump(indent + 1) << "\n";
                os << pad << ")";
                break;
            case QueryNodeType::FILTER:
                os << pad << "FILTER(" << filterName << ":" << filterArg << ")";
                break;
        }
        return os.str();
    }
};

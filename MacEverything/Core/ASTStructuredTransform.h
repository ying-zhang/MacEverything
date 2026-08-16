#pragma once
#include "QueryAST.h"
#include "StructuredQueryParser.h"
#include <memory>

/// Recursively walk an AST and transform TERM nodes that contain '/'
/// into structured path-segment constraints.
///
/// A TERM like "/usr/local/test" becomes:
///   AND( FILTER("__pathseg", segments=[usr,local]), TERM("test") )
///
/// For DIR_EXACT mode ("/usr/local/"):
///   AND( FILTER("__pathseg", segments=[usr]), AND(TERM("local"), FILTER("folder:")) )
///
/// Glob patterns (containing *, ?) are left as-is (parseQuery returns PLAIN).
/// DIR_LIST mode is left as-is (not supported in advanced query context).
///
/// The original AST is consumed; a new root is returned.
inline std::unique_ptr<QueryNode> transformSlashTerms(std::unique_ptr<QueryNode> node) {
    if (!node) return nullptr;

    // Recurse into children first
    for (auto& child : node->children) {
        child = transformSlashTerms(std::move(child));
    }

    // Only transform SUBSTRING TERM nodes whose text contains '/'
    if (node->type != QueryNodeType::TERM) return node;
    if (node->mode != MatchMode::SUBSTRING) return node;
    if (node->quoted) return node; // quoted phrases are literal, not paths
    if (node->text.find('/') == std::string::npos) return node;

    // Parse the slash-containing text as a structured query (use pre-lowered text)
    ParsedQuery pq = parseQuery(node->text, node->textLower);

    // If parseQuery couldn't extract structure (glob, empty, etc.), keep original
    if (pq.mode == QueryMode::PLAIN) return node;

    // DIR_LIST not meaningful in advanced query context — keep original
    if (pq.mode == QueryMode::DIR_LIST) return node;

    // No segments and no meaningful name → keep original
    if (pq.pathSegments.empty() && pq.namePattern.empty()) return node;

    // Build the replacement AST subtree
    std::vector<std::unique_ptr<QueryNode>> andParts;

    // 1. Path segment constraint (if any segments exist)
    if (!pq.pathSegments.empty()) {
        auto pathFilter = QueryNode::makeFilter("__pathseg", "");
        pathFilter->pathSegments = std::move(pq.pathSegments);
        pathFilter->structuredMode = pq.mode;
        andParts.push_back(std::move(pathFilter));
    }

    // 2. Name term (the last component)
    if (!pq.namePattern.empty()) {
        // parseQuery lowercases the name pattern; for case-sensitive queries we
        // must keep the original-case name as `text` (textLower stays lowered).
        std::string nameText = pq.namePattern;
        if (node->caseSensitive) {
            std::string t = node->text;
            if (t.size() >= 2 && t.back() == '*' && t[t.size() - 2] == '/')
                t = t.substr(0, t.size() - 2);
            else if (!t.empty() && t.back() == '/')
                t = t.substr(0, t.size() - 1);
            if (!t.empty() && t.front() == '/') t = t.substr(1);
            size_t slash = t.rfind('/');
            nameText = (slash == std::string::npos) ? t : t.substr(slash + 1);
        }
        auto nameTerm = QueryNode::makeTerm(nameText, pq.namePattern, MatchMode::SUBSTRING);
        nameTerm->caseSensitive = node->caseSensitive;
        nameTerm->nameOnly = true; // match only name, not full path
        nameTerm->useNameKind = true;
        nameTerm->nameKind = (pq.mode == QueryMode::DIR_EXACT)
            ? PathSegmentKind::EXACT
            : pq.nameKind;

        // For DIR_EXACT, also require the record to be a directory
        if (pq.mode == QueryMode::DIR_EXACT) {
            auto folderFilter = QueryNode::makeFilter("folder", "");
            std::vector<std::unique_ptr<QueryNode>> dirParts;
            dirParts.push_back(std::move(nameTerm));
            dirParts.push_back(std::move(folderFilter));
            andParts.push_back(QueryNode::makeAnd(std::move(dirParts)));
        } else {
            andParts.push_back(std::move(nameTerm));
        }
    } else if (pq.mode == QueryMode::DIR_EXACT) {
        // No name pattern but DIR_EXACT (e.g. "/usr/") — the last segment IS the name
        // This case shouldn't normally happen since parseQuery puts last part as namePattern
        auto folderFilter = QueryNode::makeFilter("folder", "");
        andParts.push_back(std::move(folderFilter));
    }

    // If only one part, return it directly (no wrapping AND)
    if (andParts.size() == 1) {
        return std::move(andParts[0]);
    }

    return QueryNode::makeAnd(std::move(andParts));
}

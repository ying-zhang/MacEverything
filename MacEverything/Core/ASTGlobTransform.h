#pragma once
#include "QueryAST.h"
#include <memory>

/// Recursively walk an AST and transform TERM nodes that look like glob patterns
/// (containing '*' or '?') from SUBSTRING mode to GLOB mode.
///
/// This enables proper glob matching for queries like "*.cpp" or "test??.txt"
/// that would otherwise be treated as literal substring searches.
///
/// Only transforms SUBSTRING mode TERM nodes. REGEX, WHOLEWORD, etc. are left as-is.
inline std::unique_ptr<QueryNode> transformGlobTerms(std::unique_ptr<QueryNode> node) {
    if (!node) return nullptr;

    // Recurse into children first
    for (auto& child : node->children) {
        child = transformGlobTerms(std::move(child));
    }

    // Only transform SUBSTRING TERM nodes whose text contains '*' or '?'
    if (node->type != QueryNodeType::TERM) return node;
    if (node->mode != MatchMode::SUBSTRING) return node;
    if (node->quoted) return node; // quoted phrases are literal
    if (node->text.find('*') == std::string::npos &&
        node->text.find('?') == std::string::npos) return node;

    // Change mode to GLOB and pre-compile for zero-alloc matching
    node->mode = MatchMode::GLOB;
    node->compiledGlob = compileGlob(node->textLower);
    return node;
}

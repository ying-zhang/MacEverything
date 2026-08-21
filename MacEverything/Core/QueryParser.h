#pragma once
#include "QueryAST.h"
#include "QueryTokenizer.h"
#include <string>
#include <memory>

/// Recursive descent parser for Everything-style query syntax.
///
/// Grammar:
///   query      ::= or_expr
///   or_expr    ::= and_expr ('|' and_expr)*
///   and_expr   ::= not_expr (not_expr)*        // space = implicit AND
///   not_expr   ::= '!' atom | atom
///   atom       ::= '<' or_expr '>'             // grouping
///                | QUOTED                      // exact phrase
///                | FILTER                      // ext:cpp, size:>1mb
///                | WORD                        // substring/glob
class QueryParser {
public:
    /// Parse a query string into an AST. Returns nullptr on empty input.
    static std::unique_ptr<QueryNode> parse(const std::string& input);

private:
    QueryParser(std::vector<Token>&& tokens);

    std::unique_ptr<QueryNode> parseOrExpr();
    std::unique_ptr<QueryNode> parseAndExpr();
    std::unique_ptr<QueryNode> parseNotExpr();
    std::unique_ptr<QueryNode> parseAtom();

    const Token& peek() const;
    const Token& advance();
    bool match(TokenType type);
    bool atEnd() const;

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    size_t nestingDepth_ = 0;
    static constexpr size_t kMaxNestingDepth = 64;
};

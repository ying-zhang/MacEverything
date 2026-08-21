#include "QueryParser.h"
#include "QueryFilterParser.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::unique_ptr<QueryNode> QueryParser::parse(const std::string& input) {
    auto tokens = QueryTokenizer::tokenize(input);
    // If only END token, nothing to parse
    if (tokens.size() <= 1) return nullptr;

    QueryParser parser(std::move(tokens));
    auto root = parser.parseOrExpr();

    return root;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

QueryParser::QueryParser(std::vector<Token>&& tokens)
    : tokens_(std::move(tokens)) {}

// ---------------------------------------------------------------------------
// Recursive descent
// ---------------------------------------------------------------------------

// or_expr ::= and_expr ('|' and_expr)*
std::unique_ptr<QueryNode> QueryParser::parseOrExpr() {
    auto left = parseAndExpr();
    if (!left) return nullptr;

    std::vector<std::unique_ptr<QueryNode>> kids;
    bool hasOr = false;

    while (match(TokenType::PIPE)) {
        if (!hasOr) {
            hasOr = true;
            kids.push_back(std::move(left));
        }
        auto right = parseAndExpr();
        if (right) {
            kids.push_back(std::move(right));
        }
    }

    if (hasOr) {
        if (kids.size() == 1) return std::move(kids[0]);
        return QueryNode::makeOr(std::move(kids));
    }
    return left;
}

// and_expr ::= not_expr (not_expr)*
// Implicit AND: consecutive terms without an explicit operator.
std::unique_ptr<QueryNode> QueryParser::parseAndExpr() {
    auto first = parseNotExpr();
    if (!first) return nullptr;

    std::vector<std::unique_ptr<QueryNode>> kids;
    bool hasAnd = false;

    // Keep parsing while the next token can start a not_expr
    // (i.e., it's not PIPE, RANGLE, or END)
    while (!atEnd() && peek().type != TokenType::PIPE && peek().type != TokenType::RANGLE) {
        auto next = parseNotExpr();
        if (!next) break;
        if (!hasAnd) {
            hasAnd = true;
            kids.push_back(std::move(first));
        }
        kids.push_back(std::move(next));
    }

    if (hasAnd) {
        if (kids.size() == 1) return std::move(kids[0]);
        return QueryNode::makeAnd(std::move(kids));
    }
    return first;
}

// not_expr ::= '!' atom | atom
std::unique_ptr<QueryNode> QueryParser::parseNotExpr() {
    if (match(TokenType::BANG)) {
        auto child = parseAtom();
        if (!child) {
            // Bare '!' at end of input — treat as literal text
            return QueryNode::makeTerm("!");
        }
        return QueryNode::makeNot(std::move(child));
    }
    return parseAtom();
}

// atom ::= '<' or_expr '>'
//        | QUOTED
//        | FILTER
//        | WORD
std::unique_ptr<QueryNode> QueryParser::parseAtom() {
    if (atEnd()) return nullptr;

    const Token& tok = peek();

    // Grouping: < or_expr >
    if (tok.type == TokenType::LANGLE) {
        if (nestingDepth_ >= kMaxNestingDepth) {
            advance();
            return QueryNode::makeTerm("<");
        }
        advance();
        ++nestingDepth_;
        auto inner = parseOrExpr();
        --nestingDepth_;
        // Consume closing '>' if present (tolerant of missing)
        match(TokenType::RANGLE);
        return inner;
    }

    // Quoted phrase — literal text, must not be reinterpreted by the
    // slash/glob transforms.
    if (tok.type == TokenType::QUOTED) {
        advance();
        auto n = QueryNode::makeTerm(tok.value);
        n->quoted = true;
        return n;
    }

    // Filter: ext:cpp, size:>1mb, etc.
    if (tok.type == TokenType::FILTER) {
        advance();
        auto node = QueryNode::makeFilter(tok.filterName, tok.filterArg);
        QueryFilterParser::parse(*node);
        return node;
    }

    // Plain word
    if (tok.type == TokenType::WORD) {
        advance();
        return QueryNode::makeTerm(tok.value);
    }

    // Unexpected token — skip it
    advance();
    return nullptr;
}

// ---------------------------------------------------------------------------
// Token helpers
// ---------------------------------------------------------------------------

const Token& QueryParser::peek() const {
    return tokens_[pos_];
}

const Token& QueryParser::advance() {
    const Token& tok = tokens_[pos_];
    if (pos_ < tokens_.size() - 1) pos_++;
    return tok;
}

bool QueryParser::match(TokenType type) {
    if (peek().type == type) {
        advance();
        return true;
    }
    return false;
}

bool QueryParser::atEnd() const {
    return tokens_[pos_].type == TokenType::END;
}

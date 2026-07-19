#pragma once
#include <string>
#include <vector>
#include <cctype>
#include <string_view>
#include <unordered_set>
#include "StringUtils.h"

/// Token types produced by the query tokenizer.
enum class TokenType {
    WORD,       // plain text token
    QUOTED,     // text inside double quotes
    PIPE,       // | (OR operator)
    BANG,       // ! (NOT operator)
    LANGLE,     // < (group open)
    RANGLE,     // > (group close)
    FILTER,     // known_filter:value (e.g., ext:cpp)
    END,        // end of input
};

/// A single token from the query string.
struct Token {
    TokenType type;
    std::string value;       // text content (for WORD, QUOTED, FILTER)
    std::string filterName;  // filter name portion (for FILTER type only)
    std::string filterArg;   // filter argument portion (for FILTER type only)
};

/// Lexer for Everything-style query syntax.
/// Produces a flat list of tokens from a query string.
class QueryTokenizer {
public:
    static std::vector<Token> tokenize(const std::string& input) {
        std::vector<Token> tokens;
        size_t i = 0;
        size_t len = input.size();

        while (i < len) {
            char c = input[i];

            // Skip whitespace
            if (c == ' ' || c == '\t') {
                i++;
                continue;
            }

            // Single-character operators
            if (c == '|') {
                tokens.push_back({TokenType::PIPE, "|"});
                i++;
                continue;
            }
            if (c == '!') {
                tokens.push_back({TokenType::BANG, "!"});
                i++;
                continue;
            }
            if (c == '<') {
                tokens.push_back({TokenType::LANGLE, "<"});
                i++;
                continue;
            }
            if (c == '>') {
                tokens.push_back({TokenType::RANGLE, ">"});
                i++;
                continue;
            }

            // Quoted string
            if (c == '"') {
                i++; // skip opening quote
                std::string text;
                while (i < len && input[i] != '"') {
                    text += input[i];
                    i++;
                }
                if (i < len) i++; // skip closing quote
                tokens.push_back({TokenType::QUOTED, text});
                continue;
            }

            // Word or filter: collect until whitespace or operator.
            // First pass: collect the initial word segment (stop at operators).
            std::string word;
            size_t wordStart = i;
            while (i < len) {
                char ch = input[i];
                if (ch == ' ' || ch == '\t' || ch == '|' || ch == '!'
                    || ch == '<' || ch == '>' || ch == '"') {
                    break;
                }
                word += ch;
                i++;
            }

            if (!word.empty()) {
                // Check if this is a filter: name:value
                auto colonPos = word.find(':');
                if (colonPos != std::string::npos && colonPos > 0) {
                    std::string name = word.substr(0, colonPos);
                    std::string lowerName = me::toLower(name);

                    if (isKnownFilter(lowerName)) {
                        // For filters, re-collect arg allowing < > chars
                        // (e.g., size:>100kb, depth:<3)
                        size_t argStart = wordStart + colonPos + 1;
                        i = argStart;
                        std::string arg;

                        if (lowerName == "regex") {
                            arg = input.substr(argStart);
                            i = len;
                        } else {
                            while (i < len) {
                                char ch = input[i];
                                if (ch == ' ' || ch == '\t' || ch == '|' || ch == '!'
                                    || ch == '"') {
                                    break;
                                }
                                arg += ch;
                                i++;
                            }
                        }
                        Token t;
                        t.type = TokenType::FILTER;
                        t.value = lowerName + ":" + arg;
                        t.filterName = lowerName;
                        t.filterArg = arg;
                        tokens.push_back(std::move(t));
                        continue;
                    }
                }
                tokens.push_back({TokenType::WORD, word});
            }
        }

        tokens.push_back({TokenType::END, ""});
        return tokens;
    }

    /// Quick check: does the query string contain any advanced syntax?
    /// Used to decide whether to invoke the parser or use the fast path.
    static bool hasAdvancedSyntax(const std::string& q) {
        for (size_t i = 0; i < q.size(); i++) {
            char c = q[i];
            if (c == '|' || c == '!' || c == '<' || c == '>' || c == '"') {
                return true;
            }
            // Check for known filter prefix: word followed by colon
            if (c == ':' && i > 0) {
                // Extract the word before the colon
                size_t start = i;
                while (start > 0 && !std::isspace(static_cast<unsigned char>(q[start - 1]))
                       && q[start - 1] != '|' && q[start - 1] != '!'
                       && q[start - 1] != '<' && q[start - 1] != '>') {
                    start--;
                }
                std::string name;
                for (size_t j = start; j < i; j++) {
                    name += static_cast<char>(std::tolower(static_cast<unsigned char>(q[j])));
                }
                if (isKnownFilter(name)) return true;
            }
        }
        return false;
    }

private:
    static bool isKnownFilter(const std::string& name) {
        static const std::unordered_set<std::string> filters = {
            // Phase 2 filters
            "ext", "size", "file", "folder",
            "path", "nopath", "parent", "depth", "len",
            // Phase 3 filters
            "dm", "dc", "da",
            "datemodified", "datecreated", "dateaccessed",
            // Phase 4 modifiers (treated as filters by tokenizer)
            "case", "nocase", "regex", "ww", "wfn",
            "wholeword", "wholefilename",
            // Phase 4 macros
            "audio", "video", "pic", "doc", "exe", "zip",
            // Content
            "content", "infile",
            // Type shorthand
            "type",
        };
        return filters.count(name) > 0;
    }
};

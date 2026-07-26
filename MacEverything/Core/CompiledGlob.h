#pragma once
#include <string>
#include <vector>
#include <cstring>
#include "SIMDSearch.h"

/// Glob matching: '*' matches any sequence, '?' matches one char.
inline bool globMatchImpl(const std::string& pattern, const std::string& text) {
    size_t px = 0, tx = 0;
    size_t starPx = std::string::npos, starTx = 0;

    while (tx < text.size()) {
        if (px < pattern.size() && (pattern[px] == '?' || pattern[px] == text[tx])) {
            px++;
            tx++;
        } else if (px < pattern.size() && pattern[px] == '*') {
            starPx = px++;
            starTx = tx;
        } else if (starPx != std::string::npos) {
            px = starPx + 1;
            tx = ++starTx;
        } else {
            return false;
        }
    }

    while (px < pattern.size() && pattern[px] == '*') px++;
    return px == pattern.size();
}

/// Zero-allocation glob matching overload: operates on raw char* + length.
inline bool globMatchImpl(const std::string& pattern, const char* text, size_t textLen) {
    size_t px = 0, tx = 0;
    size_t starPx = std::string::npos, starTx = 0;

    while (tx < textLen) {
        if (px < pattern.size() && (pattern[px] == '?' || pattern[px] == text[tx])) {
            px++;
            tx++;
        } else if (px < pattern.size() && pattern[px] == '*') {
            starPx = px++;
            starTx = tx;
        } else if (starPx != std::string::npos) {
            px = starPx + 1;
            tx = ++starTx;
        } else {
            return false;
        }
    }

    while (px < pattern.size() && pattern[px] == '*') px++;
    return px == pattern.size();
}

/// Extract all literal (non-wildcard) segments from a glob pattern.
inline std::vector<std::string> extractLiteralSegments(const std::string& pattern) {
    std::vector<std::string> segments;
    std::string current;
    for (char c : pattern) {
        if (c == '*' || c == '?') {
            if (!current.empty()) {
                segments.push_back(std::move(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        segments.push_back(std::move(current));
    }
    return segments;
}

/// Pre-classified glob pattern for fast matching.
struct CompiledGlob {
    enum Type { SUFFIX, PREFIX, CONTAINS, EXACT, GENERIC };
    Type type;
    std::string fixed;                    // literal part for fast match (or best segment for GENERIC)
    std::string original;                 // original pattern for GENERIC fallback
    std::vector<std::string> segments;    // all literal segments >= 3 chars (for multi-segment trigram)
};

inline CompiledGlob compileGlob(const std::string& pattern) {
    // *.ext -> SUFFIX(".ext")
    if (pattern.size() >= 2 && pattern[0] == '*' &&
        pattern.find('*', 1) == std::string::npos &&
        pattern.find('?', 1) == std::string::npos) {
        return {CompiledGlob::SUFFIX, pattern.substr(1), pattern, {}};
    }
    // prefix* -> PREFIX("prefix")
    if (pattern.size() >= 2 && pattern.back() == '*' &&
        pattern.find('*') == pattern.size() - 1 &&
        pattern.find('?') == std::string::npos) {
        return {CompiledGlob::PREFIX, pattern.substr(0, pattern.size() - 1), pattern, {}};
    }
    // *keyword* -> CONTAINS("keyword")
    if (pattern.size() >= 3 && pattern.front() == '*' && pattern.back() == '*') {
        std::string mid = pattern.substr(1, pattern.size() - 2);
        if (mid.find('*') == std::string::npos &&
            mid.find('?') == std::string::npos) {
            return {CompiledGlob::CONTAINS, mid, pattern, {}};
        }
    }
    // No wildcard -> EXACT
    if (pattern.find('*') == std::string::npos &&
        pattern.find('?') == std::string::npos) {
        return {CompiledGlob::EXACT, pattern, pattern, {}};
    }
    // GENERIC: extract literal segments for trigram pre-filtering
    auto allSegs = extractLiteralSegments(pattern);
    std::string bestSeg;
    std::vector<std::string> qualifiedSegs;
    for (const auto& seg : allSegs) {
        if (seg.size() >= 3) {
            qualifiedSegs.push_back(seg);
            if (seg.size() > bestSeg.size()) {
                bestSeg = seg;
            }
        }
    }
    return {CompiledGlob::GENERIC, bestSeg, pattern, std::move(qualifiedSegs)};
}

inline bool compiledGlobMatch(const CompiledGlob& cg, const char* text, size_t len) {
    switch (cg.type) {
    case CompiledGlob::SUFFIX:
        return len >= cg.fixed.size() &&
               memcmp(text + len - cg.fixed.size(),
                      cg.fixed.data(), cg.fixed.size()) == 0;
    case CompiledGlob::PREFIX:
        return len >= cg.fixed.size() &&
               memcmp(text, cg.fixed.data(), cg.fixed.size()) == 0;
    case CompiledGlob::CONTAINS:
        return me::simdContains(text, static_cast<uint16_t>(std::min(len, size_t(65535))),
                                cg.fixed.data(), cg.fixed.size());
    case CompiledGlob::EXACT:
        return len == cg.fixed.size() &&
               memcmp(text, cg.fixed.data(), len) == 0;
    case CompiledGlob::GENERIC:
        return globMatchImpl(cg.original, text, len);
    }
    return false;
}

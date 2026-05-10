#pragma once
#include <string>
#include <vector>
#include "StringUtils.h"

enum class PathSegmentKind {
    SUBSTRING,
    PREFIX,
    SUFFIX,
    EXACT
};

/// A single path constraint segment (e.g. "usr" in /usr/local/bin).
struct PathSegment {
    std::string text;          // lowercased segment text
    bool adjacentToNext;       // true = next segment must be in the immediately following dir component
    PathSegmentKind kind = PathSegmentKind::SUBSTRING;
};

/// Query modes for structured (slash-containing) queries.
enum class QueryMode {
    PLAIN,      // No '/' — simple name substring search
    SEGMENTS,   // /abc/def — node name contains "def", path has constraint "abc"
    DIR_EXACT,  // /abc/def/ — find directory named "def" with path constraint "abc"
    DIR_LIST    // /abc/def/* — list direct children of directory "def"
};

/// Result of parsing a query string into structured components.
struct ParsedQuery {
    QueryMode mode = QueryMode::PLAIN;
    std::vector<PathSegment> pathSegments; // path constraints (may be empty)
    std::string namePattern;               // node name to search (lowercased)
    PathSegmentKind nameKind = PathSegmentKind::SUBSTRING;
};

/// Parse a raw query string into a structured query.
/// Handles slash-separated segments, adjacency via *, glob detection, and DIR modes.
/// Overload accepting pre-lowered text to avoid redundant me::toLower().
inline ParsedQuery parseQuery(const std::string& rawQuery, const std::string& lowered) {
    ParsedQuery result;
    if (rawQuery.empty()) return result;

    std::string q = lowered;

    // No slash → plain name search
    if (q.find('/') == std::string::npos) {
        result.mode = QueryMode::PLAIN;
        result.namePattern = q;
        return result;
    }

    // Glob detection: check if segments contain wildcard characters (*, ?)
    // that indicate a glob pattern rather than structured query syntax.
    // A standalone '*' between segments is an adjacency breaker, not a glob.
    // A trailing '/*' is DIR_LIST, not a glob.
    {
        auto tmp = q;
        if (!tmp.empty() && tmp.front() == '/') tmp = tmp.substr(1);
        // Strip trailing /* (DIR_LIST) and trailing / (DIR_EXACT) for glob check
        if (tmp.size() >= 2 && tmp.back() == '*' && tmp[tmp.size() - 2] == '/')
            tmp = tmp.substr(0, tmp.size() - 2);
        if (!tmp.empty() && tmp.back() == '/')
            tmp = tmp.substr(0, tmp.size() - 1);

        std::vector<std::string> segs;
        size_t s = 0;
        for (size_t i = 0; i <= tmp.size(); i++) {
            if (i == tmp.size() || tmp[i] == '/') {
                segs.push_back(tmp.substr(s, i - s));
                s = i + 1;
            }
        }
        bool isGlob = false;
        for (size_t i = 0; i < segs.size(); i++) {
            const auto& seg = segs[i];
            if (seg == "*") {
                // Leading * (first segment) is a glob, not adjacency breaker
                if (i == 0) { isGlob = true; break; }
                continue; // standalone * between segments = adjacency breaker
            }
            if (seg.find('*') != std::string::npos ||
                seg.find('?') != std::string::npos) {
                isGlob = true; break;
            }
        }
        if (isGlob) {
            result.mode = QueryMode::PLAIN;
            result.namePattern = q;
            return result;
        }
    }

    const bool leftAnchored = !q.empty() && q.front() == '/';
    bool rightAnchored = false;

    // Determine mode from trailing characters
    if (q.size() >= 2 && q.back() == '*' && q[q.size() - 2] == '/') {
        result.mode = QueryMode::DIR_LIST;
        q = q.substr(0, q.size() - 2); // strip trailing /*
    } else if (q.back() == '/') {
        result.mode = QueryMode::DIR_EXACT;
        rightAnchored = true;
        q = q.substr(0, q.size() - 1); // strip trailing /
    } else {
        result.mode = QueryMode::SEGMENTS;
    }

    // Strip leading /
    if (!q.empty() && q.front() == '/') q = q.substr(1);

    // Edge case: after stripping, empty query
    if (q.empty()) {
        result.mode = QueryMode::PLAIN;
        return result;
    }

    // Split into parts by '/'
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= q.size(); i++) {
        if (i == q.size() || q[i] == '/') {
            parts.push_back(q.substr(start, i - start));
            start = i + 1;
        }
    }

    // Last part is the node name pattern
    result.namePattern = parts.back();
    parts.pop_back();

    if (parts.empty()) {
        if (rightAnchored) {
            result.mode = QueryMode::SEGMENTS;
        }
        if (leftAnchored && rightAnchored) {
            result.nameKind = PathSegmentKind::EXACT;
        } else if (leftAnchored) {
            result.nameKind = PathSegmentKind::PREFIX;
        } else if (rightAnchored) {
            result.nameKind = PathSegmentKind::SUFFIX;
        }
    } else if (rightAnchored) {
        result.nameKind = PathSegmentKind::EXACT;
    } else {
        result.nameKind = PathSegmentKind::PREFIX;
    }

    // Remaining parts are path constraints
    for (size_t i = 0; i < parts.size(); i++) {
        if (parts[i] == "*") {
            // Standalone * breaks adjacency on the previous segment
            if (!result.pathSegments.empty())
                result.pathSegments.back().adjacentToNext = false;
            continue;
        }
        if (parts[i].empty()) continue;
        PathSegment seg;
        seg.text = parts[i];
        seg.adjacentToNext = true;
        if (i == 0 && leftAnchored) {
            seg.kind = PathSegmentKind::EXACT;
        } else if (i == 0) {
            seg.kind = PathSegmentKind::SUFFIX;
        } else {
            seg.kind = PathSegmentKind::EXACT;
        }
        result.pathSegments.push_back(std::move(seg));
    }

    return result;
}

/// Backward-compatible overload — computes lowercase internally.
inline ParsedQuery parseQuery(const std::string& rawQuery) {
    return parseQuery(rawQuery, me::toLower(rawQuery));
}

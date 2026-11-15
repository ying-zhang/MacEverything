#include "RegexHighlight.h"
#include <re2/re2.h>
#include <memory>
#include <string>

size_t MERegexHighlightRanges(const char* pattern, size_t patternLength,
                             const char* text, size_t textLength,
                             bool caseSensitive, MERegexRange* ranges, size_t capacity) {
    if (!pattern || !text || !ranges || !capacity || !patternLength ||
        patternLength > 1024 || textLength > 65536) return 0;

    // Reuse the last compiled expression across result rows without sharing a
    // mutable cache between threads. RE2 bounds both compilation memory and
    // matching complexity; UI highlighting never falls back to ICU.
    thread_local std::string cachedPattern;
    thread_local bool cachedCase = false;
    thread_local std::unique_ptr<re2::RE2> cachedRegex;
    std::string key(pattern, patternLength);
    if (!cachedRegex || cachedPattern != key || cachedCase != caseSensitive) {
        re2::RE2::Options options;
        options.set_case_sensitive(caseSensitive);
        options.set_log_errors(false);
        options.set_max_mem(2 * 1024 * 1024);
        cachedRegex = std::make_unique<re2::RE2>(key, options);
        cachedPattern = std::move(key);
        cachedCase = caseSensitive;
    }
    if (!cachedRegex->ok()) return 0;

    re2::StringPiece input(text, textLength);
    size_t count = 0, start = 0;
    while (start <= textLength && count < capacity) {
        re2::StringPiece match;
        if (!cachedRegex->Match(input, start, textLength, re2::RE2::UNANCHORED, &match, 1)) break;
        size_t offset = static_cast<size_t>(match.data() - text);
        size_t end = offset + match.size();
        if (!match.empty()) ranges[count++] = {offset, match.size()};
        if (end == textLength) break;
        if (end == start || match.empty()) {
            // Empty matches have no visible highlight. Advance by one UTF-8
            // scalar while keeping the original input for ^ and $ semantics.
            start = end + 1;
            while (start < textLength &&
                   (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) ++start;
        } else {
            start = end;
        }
    }
    return count;
}

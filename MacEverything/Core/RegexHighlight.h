#pragma once
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t offset;
    size_t length;
} MERegexRange;

// UTF-8 byte ranges from RE2, using the same case option as search. Invalid or
// oversized inputs return no highlights. At most capacity nonempty matches.
size_t MERegexHighlightRanges(const char* pattern, size_t patternLength,
                             const char* text, size_t textLength,
                             bool caseSensitive, MERegexRange* ranges, size_t capacity);
#ifdef __cplusplus
}
#endif

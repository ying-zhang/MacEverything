# 139 - Replace std::regex with Google RE2 for Linear-Time Regex Matching

## Problem

`regex:` queries on 5.5M+ records were extremely slow:
- `regex:a.*b.*c` ~17s
- `regex:a.b` ~7s
- Even simple patterns like `regex:test` took seconds

## Root Cause

`std::regex` (C++ standard library) uses a backtracking NFA/DFA engine that exhibits exponential worst-case behavior on patterns with nested quantifiers (e.g., `a.*b.*c`). Additionally, every record match required constructing a `std::string` from raw name data.

## Solution

Replaced `std::regex` with Google RE2 — a linear-time regex engine that guarantees O(n) matching regardless of pattern complexity. RE2 uses `StringPiece` (non-owning view) for zero-copy matching, eliminating per-record `std::string` allocations.

### Key Changes

1. **SearchEngineAdvancedQuery.cpp**:
   - `#include <regex>` → `#include <re2/re2.h>`
   - `RegexCache` type: `std::unordered_map<const QueryNode*, std::regex>` → `std::unordered_map<const QueryNode*, std::unique_ptr<re2::RE2>>`
   - Compilation: `std::regex(pattern, flags)` → `std::make_unique<re2::RE2>(pattern, opts)` with `set_case_sensitive()` for case control
   - Matching: `std::regex_search(std::string(...))` → `RE2::PartialMatch(re2::StringPiece(data, len), *re)` (zero-copy)

2. **Makefile**: Added `RE2_PREFIX`, `RE2_CFLAGS`, `RE2_LDFLAGS` to all build targets (test_all, benchmark, daemon, asan, tsan)

3. **MacEverything.xcodeproj/project.pbxproj**: Added RE2 header/library search paths and `-lre2` linker flag for both Debug and Release configurations

### Compatibility Notes

- RE2 uses POSIX-like syntax (not ECMAScript); `\d` → `[0-9]`, `\w` → `[a-zA-Z0-9_]`
- RE2 does not support backreferences (`\1`), lookahead (`(?=...)`), or lookbehind (`(?<=...)`)
- For file name search, these limitations have no practical impact

### Test Coverage (Part 75)

13 tests covering:
- Valid/invalid pattern compilation
- PartialMatch positive and negative cases
- Case-sensitive and case-insensitive matching
- Complex patterns (`a.*b.*c`)
- Character classes (`[^abc]de`)
- StringPiece zero-copy matching
- Empty string handling
- Escaped special characters
- unique_ptr ownership (mimics RegexCache usage)
- Performance sanity check (20KB string in <100ms)

## Expected Performance Impact (5.5M records)

| Query | Before (std::regex) | After (RE2) | Speedup |
|-------|-------------------|-------------|---------|
| regex:a.*b.*c | ~17s | <500ms | 30x+ |
| regex:a.b | ~7s | <300ms | 20x+ |
| regex:test | ~3s | <100ms | 30x+ |

All regex queries benefit uniformly — no pattern-specific optimization needed.

## Files Modified

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp`
- `MacEverything.xcodeproj/project.pbxproj`
- `Makefile`
- `tests/test_re2_integration.h` (new)
- `test_all.cpp`

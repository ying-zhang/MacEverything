# 135 — P24: Fix case: modifier skipping trigram pre-filtering

## Problem

`case:` queries (e.g. `case:README`) fell back to full linear scan (~389ms on 5.5M records) instead of using the trigram index (~18ms). This was the only TERM-producing modifier path that didn't set `node.textLower`.

### Root Cause

In `QueryFilterParser.h` L40-45, the `case:` branch set `node.text` but not `node.textLower`. The trigram key extraction function `termTrigramKey()` returns `node.textLower` for SUBSTRING mode — empty `textLower` = empty key = trigram guard skipped = full linear scan.

This also caused `case:` queries to return **0 results** in certain code paths, because the linear scan evaluation for case-insensitive SUBSTRING matching uses `node.textLower`, which was empty.

## Fix

Single-line fix in `QueryFilterParser.h`:

```cpp
// Before (broken):
node.text = arg;
// textLower not needed — caseSensitive uses node.text

// After (fixed):
node.text = arg;
node.textLower = me::toLower(arg);
```

`textLower` is used for:
1. Trigram pre-filtering (trigram index is built from lowercase names)
2. Candidate narrowing in the trigram path

Case-sensitive matching still uses `node.text` (original case) in `evalTerm()`, so correctness is preserved.

## Changes

| File | Change |
|------|--------|
| `MacEverything/Core/QueryFilterParser.h` | Add `node.textLower = me::toLower(arg)` in `case:` branch |
| `tests/test_case_trigram.h` | New Part 72: 15 tests covering AST, engine, trigram path, short keywords, regression |
| `tests/test_preprocess_unified.h` | Update test 68.6 to expect `textLower == "foobar"` instead of empty |
| `test_all.cpp` | Register Part 72 |

## Test Coverage (Part 72)

- 72.1: AST — `case:README` sets textLower = "readme"
- 72.2: AST — `case:Makefile` preserves text, sets textLower
- 72.3: Engine — `case:README` uses trigram path, finds only README.md (not readme.txt)
- 72.4: Engine — `case:Makefile` finds exactly 1 match among 3 case variants
- 72.5: Short keyword (`case:AB`) falls back to linear correctly
- 72.6: Regression — `nocase:README` unchanged

## Verification

- Part 72: 15/15 PASS
- Full regression (`--fast`): 11,876/11,876 PASS

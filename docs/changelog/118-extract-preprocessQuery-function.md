# 118 — Extract `preprocessQuery()` function

## Summary

Refactored the inline tilde expansion code in `SearchEngine::query()` into a
dedicated static function `preprocessQuery()`, centralising all query
preprocessing in one place.

## Motivation

After the tilde expansion fix (#117), the expansion logic lived inline inside
`query()`. As more preprocessing steps are added in the future (e.g.
environment variable expansion, alias resolution), a dedicated function keeps
the query entry point clean and provides a single, obvious location for all
input normalisation.

## Changes

### `MacEverything/Core/SearchEngineQuery.cpp`

- **Added** `static std::string preprocessQuery(const std::string& raw)` —
  performs all pre-routing query normalisation. Currently handles:
  1. Leading `~` expansion to `$HOME`
- **Modified** `SearchEngine::query()` — replaced inline tilde expansion with
  a call to `preprocessQuery(keyword)`, threading the result (`processed`)
  through to `hasAdvancedSyntax()`, `toLower()`, `parseQuery()`, and all
  downstream paths.

## Verification

- **Unit tests**: Part 65 (7 tilde expansion tests) — all pass.
- **Build**: `xcodebuild` Release build succeeded.
- **HTTP**: `curl "localhost:19860/api/search?q=~/*/*.txt"` returns expected
  results (e.g. `/Users/username/Downloads/f1.txt`).

## Risk

Zero — pure code extraction with no behavioural change. All existing tests
continue to pass.

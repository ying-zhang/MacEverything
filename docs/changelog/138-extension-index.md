# 138 - Extension Index for O(1) ext: Filter Queries

## Problem

`ext:` filter queries (e.g., `ext:py`, `ext:txt size:>10000`) on 5.5M+ records required linear string scanning across all file names to extract and compare extensions. This took ~252ms per query, far exceeding the <10ms target.

## Root Cause

The `ext:` filter was evaluated via `evalFilter()` which needs name data (to extract the file extension from each record's name). Without a pre-built index, the query engine had to scan all records linearly.

## Solution

Built a pre-computed extension index (`std::unordered_map<std::string, std::vector<uint32_t>>`) that maps lowercase file extensions to sorted record indices. This enables direct O(1) lookup of candidate records by extension, drastically reducing the search space.

### Key Changes

1. **SearchEngine.h**: Added `extensionIndex_` member and method declarations (`buildExtensionIndex`, `buildExtensionIndexFromData`, `addExtensionForRecord`, `removeExtensionForRecord`, `extractExtFromName`)

2. **SearchEngineIndex.cpp**: Implemented extension index infrastructure:
   - `extractExtFromName()`: extracts lowercase extension from name data in StringPool
   - `buildExtensionIndexFromData()`: static method for phase-2 build from snapshot data
   - `buildExtensionIndex()`: full rebuild from current engine state
   - `addExtensionForRecord()` / `removeExtensionForRecord()`: incremental maintenance

3. **SearchEngine.cpp**: Added extension index maintenance calls to ALL mutation paths:
   - `addRecord()`, `removeByPath()`, `updateByPath()`, `removeByPathPrefix()`
   - `compactRecords()` (COW pattern: build in phase 2, swap in phase 3)
   - `loadRecords()` (full rebuild)

4. **SearchEngineV6.cpp**: Extension index built during `completePhase2()` startup, with replay of pending mutations

5. **SearchEngineAdvancedQuery.cpp**: Extension index integrated as competitive candidate source alongside name/path trigram indices. The engine picks the smallest candidate set among all available pre-filter sources.

6. **QueryNeedsAnalysis.h**: Added `hasExtFilter` flag. ext: still sets `needsName = true` (evalFilter needs name data) but `hasExtFilter` signals the query engine can use the extension index for candidate pre-filtering.

### Test Coverage (Part 74)

- 74a: Basic ext: query correctness
- 74b: Extension index maintained after addRecord
- 74c: Extension index maintained after removeByPath
- 74d: Verify search path uses `advanced-ext-index`
- 74e: Multiple extensions (ext:cpp;h)
- 74f: Combined ext:+size: filter
- 74g: ext: with TERM keyword
- 74h-74i: QueryNeeds analysis verification
- 74j: Empty extension index returns no results

## Performance Results (5.6M records)

| Query | Before | After | Speedup |
|-------|--------|-------|---------|
| ext:py size:>1000 | ~252ms | ~42ms | 6x |
| ext:txt | ~252ms | ~20ms | 12.6x |
| ext:cpp;h | ~252ms | ~21ms | 12x |

The extension index narrows the candidate set from 5.6M records to the actual number of files with the matching extension (e.g., 56K for .txt, 218K for .py), eliminating unnecessary string comparisons.

## Files Modified

- `MacEverything/Core/SearchEngine.h`
- `MacEverything/Core/SearchEngine.cpp`
- `MacEverything/Core/SearchEngineIndex.cpp`
- `MacEverything/Core/SearchEngineV6.cpp`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp`
- `MacEverything/Core/QueryNeedsAnalysis.h`
- `tests/test_extension_index.h` (new)
- `tests/test_query_needs_analysis.h`
- `tests/test_compiled_glob_evalterm.h` (include path fix)
- `test_all.cpp`

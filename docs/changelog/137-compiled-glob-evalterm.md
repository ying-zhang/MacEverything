# 137: CompiledGlob Pre-compilation + Zero-alloc evalTerm GLOB Matching

## Summary

Optimized GLOB query evaluation by pre-compiling glob patterns during AST transformation and using zero-allocation matching in evalTerm. This eliminates 5.5M `std::string` allocations per GLOB query on a 5.5M record dataset.

## Problem

R33 benchmark (5.5M records) showed `*.h` GLOB query taking 272ms. Root cause analysis:

- `evalTerm` GLOB branch (`SearchEngineAdvancedQuery.cpp:290-304`) called `globMatchImpl(pattern, std::string(nameData, nameLen))` for every record
- Each call constructed a temporary `std::string` from raw `char*` data — 5.5M heap allocations per query
- `CompiledGlob.h` already had `compiledGlobMatch(cg, const char*, size_t)` with fast paths (memcmp for SUFFIX/PREFIX/EXACT, simdContains for CONTAINS), but it was unused in evalTerm

## Solution

### 1. Pre-compile glob patterns during AST transformation

**`QueryAST.h`**: Added `std::optional<CompiledGlob> compiledGlob` field to `QueryNode`. Set once during parsing, reused across all 5.5M record evaluations.

**`ASTGlobTransform.h`**: When transforming a TERM node to GLOB mode, pre-compile with `compileGlob(node->textLower)`:
```cpp
node->mode = MatchMode::GLOB;
node->compiledGlob = compileGlob(node->textLower);
```

### 2. Zero-allocation globMatchImpl overload

**`CompiledGlob.h`**: Added `globMatchImpl(const std::string& pattern, const char* text, size_t textLen)` overload that operates directly on raw `char*` without constructing `std::string`. Used by GENERIC type fallback.

### 3. Use compiledGlobMatch in evalTerm

**`SearchEngineAdvancedQuery.cpp`**: Updated GLOB case to use pre-compiled fast path:
- SUFFIX (e.g. `*.h`): `memcmp` on last N bytes — O(1)
- PREFIX (e.g. `test*`): `memcmp` on first N bytes — O(1)
- CONTAINS (e.g. `*config*`): SIMD substring search — ~4x faster than naive
- EXACT: `memcmp` full string — O(1)
- GENERIC (e.g. `*test*.cpp`): zero-alloc `globMatchImpl` overload

Path-containing patterns (with `/`) still construct a path buffer but use compiledGlobMatch on it.

## Performance Results

| Query | Before | After | Speedup |
|-------|--------|-------|---------|
| `*.h` (SUFFIX) | 272ms | 98ms | 2.8x |
| `*test*.cpp` (GENERIC) | ~300ms | 81ms | 3.7x |

## Files Changed

| File | Change |
|------|--------|
| `MacEverything/Core/QueryAST.h` | Added `compiledGlob` field |
| `MacEverything/Core/ASTGlobTransform.h` | Pre-compile on glob transform |
| `MacEverything/Core/CompiledGlob.h` | Added zero-alloc `globMatchImpl` overload, updated GENERIC case |
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | evalTerm GLOB uses `compiledGlobMatch` fast path |
| `test_all.cpp` | Added Part 73 dispatch |
| `tests/test_compiled_glob_evalterm.h` | 36 unit tests (Part 73) |

## Testing

- Part 73: 36 unit tests covering all CompiledGlob types (SUFFIX, PREFIX, CONTAINS, EXACT, GENERIC), AST transform correctness, nested AND/OR nodes, edge cases (empty strings, wildcard-only patterns)
- HTTP integration verified: `curl 'http://localhost:19860/api/search?q=*.h&limit=5'` returns results in ~98ms

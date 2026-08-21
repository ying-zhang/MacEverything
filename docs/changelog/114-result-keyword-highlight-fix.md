# 114 - Fix Search Result Keyword Highlighting for Advanced Queries

## Problem

搜索结果列表中的关键字高亮在使用高级查询（如 `ext:cpp hello`）时完全失效。

**根因**：`ContentView.swift` 将原始查询字符串（`ext:cpp hello`）直接传给 `ResultRow` 的 `keyword` 参数。`highlightMatches` 尝试在文件名中查找完整字符串 "ext:cpp hello"，自然无法匹配。

## Solution

### 1. 关键词提取 (`QueryHighlightTokenizer.extractSearchKeywords`)

在已有的 `QueryHighlightTokenizer` 中新增 `extractSearchKeywords()` 方法，复用 `tokenize()` 逻辑提取 `.word` 和 `.quoted` token 的文本内容，过滤掉 filter token 和 operator。

- `ext:cpp hello` → `["hello"]`
- `ext:cpp "hello world"` → `["hello world"]`
- `ext:cpp hello world` → `["hello", "world"]`

### 2. ViewModel 层 (`SearchViewModel.highlightKeyword`)

新增 `highlightKeyword` computed property，调用 `extractSearchKeywords()` 并将结果用空格拼接。

### 3. View 层 (`ContentView.swift`)

`ResultRow` 的 `keyword` 参数从 `viewModel.searchText` 改为 `viewModel.highlightKeyword`。

### 4. 多关键词高亮 (`TextHighlight.swift`)

修改 `highlightMatches` 和 `highlightCrossMatches`，当 keyword 包含空格时拆分为多个词，分别查找匹配范围并合并（复用已有的 `findAllLiteralRanges` 方法）。

## Files Changed

- **MODIFIED** `MacEverything/App/HighlightedSearchField.swift` — 新增 `extractSearchKeywords()` 方法
- **MODIFIED** `MacEverything/App/SearchViewModel.swift` — 新增 `highlightKeyword` computed property
- **MODIFIED** `MacEverything/App/ContentView.swift` — `searchText` → `highlightKeyword`
- **MODIFIED** `MacEverything/App/TextHighlight.swift` — 多关键词高亮支持

## Testing

- Build succeeded (Release configuration)
- HTTP API 验证搜索功能正常
- `ext:cpp hello` → 结果中 `hello` 被高亮，filter tokens 不参与高亮
- 纯文本搜索行为不变

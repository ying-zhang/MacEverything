# 127 - 高亮提示：基于 AST 的搜索结果高亮

## 问题

现有的搜索结果高亮系统存在根本性的架构缺陷：Swift 侧的 `QueryHighlightTokenizer.extractSearchKeywords()` 重新解析原始搜索文本来提取高亮关键词。它仅提取 `.word` 和 `.quoted` 类型的 token，丢失了关键信息：

- **修饰符过滤器**（case:Hello, regex:pattern, ww:test）— 参数被归类为 filterArg 而丢弃，导致无法高亮
- **波浪号查询**（~/Documents）— Swift 侧未将 `~` 展开为 `$HOME`
- **NOT 运算符**（!term）— 被否定的词仍然被高亮
- **path: 过滤器** — 参数未被高亮

## 方案

直接从 C++ 查询 AST 中提取结构化的高亮提示，AST 在经过 `preprocessQuery()` → `QueryParser::parse()` → `transformSlashTerms()` → `transformGlobTerms()` 处理后已包含正确高亮所需的全部信息。

### 数据流

```
searchText → bridge.parseHighlightHints(query)
  → C++: preprocessQuery → parse → transforms → collectHighlightHints(ast)
  → ObjC: [MEHighlightHint] (text, field, matchMode, caseSensitive)
  → Swift: [HighlightHint]
  → ResultRow → highlightCrossMatches(path:name:hints:...)
```

## 实现

### 第 1 步：C++ — HighlightHintExtractor.h（新文件）

- `HintField` 枚举：NAME, PATH, ANY
- `HighlightHint` 结构体：text + field + mode + caseSensitive
- `collectHighlightHints()`：AST 遍历器，跳过 NOT 子树，收集 TERM 节点，提取 path:/file: FILTER 参数
- `extractHighlightHints()`：便捷函数，复用完整的查询管道

### 第 2 步：Bridge — MEHighlightHint + parseHighlightHints:

- `MEHighlightHint` ObjC 类，包含只读属性（text, field, matchMode, caseSensitive 均为 uint8_t）
- MacSearchBridge 上的 `parseHighlightHints:` 方法调用 C++ 的 extractHighlightHints()

### 第 3 步：Swift — HighlightHint + ViewModel

- `HighlightHint` 结构体，使用 `#if !TESTING` 守卫以支持独立测试编译
- `highlightHints` 计算属性替换 SearchViewModel 中旧的 `highlightKeyword`

### 第 4 步：ResultRow + ContentView

- ResultRow：`keyword: String` → `hints: [HighlightHint]`
- ContentView：将 `viewModel.highlightHints` 传递给 ResultRow

### 第 5 步：TextHighlight — 基于提示的高亮

- `computeRangesForHint()`：模式感知的范围计算（substring、glob、regex、wholeWord、wholeFilename）
- `computeHighlightRanges()`：聚合多个提示的范围并进行合并去重
- `highlightMatches(in:hints:...)`：使用基于提示的范围构建 SwiftUI Text
- `highlightCrossMatches(path:name:hints:...)`：字段感知的分发（NAME→仅名称，PATH→仅路径，ANY→两者）
- `mapFullPathRanges()`：将 fullPath 范围映射回 path/name 组件

### 第 6 步：清理

- 从 SearchViewModel 中移除 `highlightKeyword`（由 `highlightHints` 替代）
- 保留 `QueryHighlightTokenizer.extractSearchKeywords()`（仍用于搜索输入语法着色）
- `contentKeyword` 不变（内容搜索不经过 AST 管道）

## 测试

### C++ 测试（Part 70 — tests/test_highlight_hints.h）

20+ 个测试用例，覆盖：
- 基本 TERM、case:、regex:、ww:、wfn: 修饰符
- NOT 运算符排除
- 多关键词、波浪号展开、glob 模式
- path:/file: 过滤器、复合查询、斜杠查询
- 空查询/空白查询、纯过滤器、引号短语、OR 运算符
- size: 过滤器、关键词+过滤器混合、nopath: 排除

### Swift 测试（tests/test_highlight_ranges.swift）

22 个纯函数测试，覆盖：
- 子串匹配（大小写不敏感和大小写敏感）
- glob 字面量提取与匹配
- 正则表达式模式匹配
- 全词边界匹配
- 完整文件名精确匹配
- 多提示范围合并
- 空/无匹配边界情况

### 集成验证

- 应用构建并启动成功
- 通过 HTTP API 验证了基本查询、case:、regex:、path:、ext:、glob、NOT 查询
- 全部 11805 个 C++ 测试通过

## 修改的文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/HighlightHintExtractor.h` | 新增 — C++ 提示提取 |
| `MacEverything/Bridge/MacSearchBridge.h` | 新增 MEHighlightHint + parseHighlightHints: |
| `MacEverything/Bridge/MacSearchBridge.mm` | 实现桥接方法 |
| `MacEverything/App/HighlightHint.swift` | 新增 — Swift HighlightHint 结构体 |
| `MacEverything/App/SearchViewModel.swift` | highlightKeyword → highlightHints |
| `MacEverything/App/ResultRow.swift` | keyword → hints 参数 |
| `MacEverything/App/ContentView.swift` | 传递 hints 给 ResultRow |
| `MacEverything/App/TextHighlight.swift` | 基于提示的高亮重载 |
| `tests/test_highlight_hints.h` | C++ 单元测试（Part 70） |
| `tests/test_highlight_ranges.swift` | Swift 纯函数测试 |
| `test_all.cpp` | 注册 Part 70 |
| `Makefile` | 添加 -IMacEverything/Core 编译标志 |

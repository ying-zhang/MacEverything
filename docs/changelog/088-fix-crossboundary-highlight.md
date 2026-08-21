# 088 — Fix Cross-Boundary Keyword Highlighting

## 问题

搜索关键词包含 `/` 且跨越 path/name 边界时（如 `test/goodprice`），搜索结果中的高亮完全失效。

根因：`ResultRow` 分别对 `item.name` 和 `item.path` 独立调用 `highlightMatches`：
- `item.name` = `goodprice`，关键词 `test/goodprice` 在其中找不到匹配
- `item.path` 以 `/test` 结尾，也不包含完整的 `test/goodprice`

因此两个组件都返回无高亮的纯文本。

而 path 中包含完整 `test/goodprice` 子串的记录（如 `.../origin/test/goodprice/sale`），path 部分能正确高亮，但 name 部分仍然没有高亮。

## 修复方案

### 变更 1：TextHighlight.swift — 新增 `highlightCrossMatches`

新函数 `highlightCrossMatches(path:name:keyword:...)` 的逻辑：

1. **快速路径**：keyword 不含 `/` 时，回退到原有的独立匹配（不含 `/` 的关键词不可能跨越 path/name 边界）
2. 构造 `fullPath = path + "/" + name`
3. 在 fullPath 上查找所有 case-insensitive 匹配
4. 将每个匹配范围映射回 path 和 name 各自的子区间：
   - 完全在 path 内 → 直接映射到 path
   - 完全在 name 内 → 偏移映射到 name
   - 跨边界 → 拆分为 path 尾部和 name 头部两个范围
5. 分别用映射后的范围构建 path 和 name 的高亮 Text

同时新增辅助函数 `buildHighlightedText`，从 `highlightMatches` 中提取公共的 Text 构建逻辑。

### 变更 2：ResultRow.swift — 使用新函数

将两次独立的 `highlightMatches` 调用替换为一次 `highlightCrossMatches` 调用，返回的 `(nameText, pathText)` 元组直接用于显示。

## 边界情况处理

- keyword 不含 `/`：走快速路径，行为与修复前完全一致
- glob 模式（含 `*` 或 `?`）：跳过高亮，与修复前一致
- keyword 为空：返回纯文本，与修复前一致
- 匹配完全在 path 或 name 内：正确映射，与修复前一致

## 变更文件

| 文件 | 变更 |
|------|------|
| `MacEverything/App/TextHighlight.swift` | 新增 `highlightCrossMatches` + `buildHighlightedText` |
| `MacEverything/App/ResultRow.swift` | L70-75 改用 `highlightCrossMatches` |

## 测试结果

- `./test_all --fast`：全部 PASS（pre-commit hook 自动执行）
- xcodebuild Release 构建通过
- HTTP API 验证 `test/goodprice` 搜索返回 4 条结果

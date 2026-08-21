# 107 - Glob 查询结果高亮非通配符部分

## 问题

当用户使用含通配符的搜索（如 `*.bash`、`test*`、`*config*`），搜索结果中的文件名和路径没有任何高亮。这是因为 `TextHighlight.swift` 中遇到含 `*` 或 `?` 的 keyword 时直接跳过了高亮逻辑。

## 方案

在 Swift UI 层对 glob pattern 提取 literal（非通配符）片段，并在搜索结果中高亮这些片段。

### 核心逻辑

1. `extractGlobLiterals(_:)` — 按 `*` 和 `?` 分割 pattern，提取所有非空 literal 片段
2. `findAllLiteralRanges(in:literals:)` — 在文本中找所有 literal 片段的大小写不敏感匹配，合并重叠 range
3. 修改 `highlightMatches` 的 glob 分支，用提取的 literal 做高亮而非直接跳过

### 示例

| 查询 | 提取 literal | 高亮效果 |
|------|-------------|---------|
| `*.bash` | `[".bash"]` | 文件名中 `.bash` 加粗高亮 |
| `test*` | `["test"]` | 文件名中 `test` 加粗高亮 |
| `*config*` | `["config"]` | 文件名中 `config` 加粗高亮 |
| `a*b` | `["a", "b"]` | `a` 和 `b` 分别高亮 |

## 修改文件

- `MacEverything/App/TextHighlight.swift` — 新增 `extractGlobLiterals`、`findAllLiteralRanges` 两个私有函数，修改 `highlightMatches` 中的 glob 分支

## 测试

- 全量测试通过（11413 passed, 0 failed）
- UI 验证：通过 HTTP 接口验证搜索功能正常

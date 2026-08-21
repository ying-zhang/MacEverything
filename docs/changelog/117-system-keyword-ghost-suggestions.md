# 117 - 系统关键词幽灵文本建议

## 背景

搜索栏的幽灵文本自动补全此前仅根据用户搜索历史进行推荐。对于正在学习过滤语法（如 `ext:`、`size:`、`path:`）的用户来说，缺乏可发现性机制——必须事先知道关键词才能使用。

## 方案

将系统过滤关键词作为幽灵文本建议的备选来源。当用户输入的部分文字与某个已知过滤关键词前缀匹配时，幽灵文本会显示完整的关键词。

### 优先级顺序：
1. **搜索历史匹配** — 最高优先级（保留已有行为）
2. **系统关键词匹配** — 在没有历史匹配时作为备选

### 匹配规则：
- 仅在输入单个不完整单词时触发（不含空格、不含冒号）
- 当多个关键词匹配时，优先选择最短的（例如输入 "d" → 显示 "dc:" 而非 "datemodified:"）
- 长度相同时按字母顺序排序

### 示例：
- 输入 "ex" → 幽灵文本显示 "ext:"
- 输入 "si" → 幽灵文本显示 "size:"
- 输入 "au" → 幽灵文本显示 "audio:"
- 输入 "dat" → 幽灵文本显示 "datemodified:"（在 "da:" 和 "dc:" 之后最短的日期过滤器）
- 输入 "ext:" → 不显示关键词幽灵文本（冒号已存在）
- 输入历史中的 "ext:cpp" → 历史匹配优先于关键词

### 支持的关键词（共 28 个）：
`ext:`, `size:`, `file:`, `folder:`, `path:`, `nopath:`, `parent:`, `depth:`, `len:`, `dm:`, `dc:`, `da:`, `datemodified:`, `datecreated:`, `dateaccessed:`, `case:`, `nocase:`, `regex:`, `ww:`, `wfn:`, `wholeword:`, `wholefilename:`, `audio:`, `video:`, `pic:`, `doc:`, `exe:`, `zip:`, `content:`, `type:`

## 变更内容

- **`MacEverything/App/SearchViewModel.swift`**
  - 新增 `systemKeywords` 静态数组，包含所有已知的过滤关键词
  - 新增 `systemKeywordMatch(for:)` 静态方法，实现前缀匹配并按最短优先排序
  - 更新 `updateGhostSuggestion()`，在没有历史匹配时回退到系统关键词匹配

## 验证

- 在 master 分支上构建成功
- 应用正常启动并通过 HTTP API 响应搜索请求
- 幽灵文本建议现已包含系统关键词作为备选

# 141: 使用 RE2 FilteredRE2 替代手写正则 literal 提取器

## 问题

`extractRegexLiteralsImpl()` 是手写的字符级解析器，把正则表达式中的 `|`（alternation）当作普通元字符断开 literal。所有分支的 literal 被收集到同一个 vector，下游 `intersectPostingListsMulti` 对所有 trigram 做 AND 交集。

对于 `(foo|bar|baz)` 这样的模式：
- 提取出 `["foo", "bar", "baz"]`
- 要求文件名同时包含 foo AND bar AND baz 的 trigram
- 但正则语义是 foo OR bar OR baz
- 导致搜索结果丢失

## 根因

手写字符级解析器无法区分 AND 语义（`config.*\.json` 的 config 和 json 需同时出现）和 OR 语义（`(foo|bar)` 只需出现一个）。所有 literal 被统一做 AND 交集。

## 修复方案

### 1. 用 RE2 FilteredRE2 替代手写解析器

RE2 提供了 `FilteredRE2` 类（`<re2/filtered_re2.h>`），专为 regex 预过滤设计：
- 利用 RE2 自己的正则 AST 解析器提取 atoms（literal 子串）
- 内部维护 AND-OR 树，正确处理交替、括号分组、字符类等所有语义
- `Compile()` 返回小写去重的 atom 列表（min length 3，匹配 trigram 最小长度）

替换后代码从 ~40 行手写字符级解析变为 ~10 行 FilteredRE2 调用。

### 2. UNION 取代 AND 交集

新增 `unionPostingListsMulti()` 方法：
- 对每个 atom 内部做 trigram 交集（单个 atom 的 trigram 是 AND 关系）
- 跨 atom 取并集（不同 atom 可能是 OR 关系）
- 比纯 AND 交集候选集更大，但永远不会丢失结果

Stage 2 regex trigram 预过滤从 `intersectPostingListsMulti`（AND）改为 `unionPostingListsMulti`（UNION）。

## 修改文件

| 文件 | 修改内容 |
|------|---------|
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | 添加 `#include <re2/filtered_re2.h>`；用 FilteredRE2 替换 `extractRegexLiteralsImpl`；Stage 2 改用 UNION |
| `MacEverything/Core/SearchEngineIndex.cpp` | 新增 `unionPostingListsMulti` |
| `MacEverything/Core/SearchEngine.h` | 声明 `unionPostingListsMulti` |
| `tests/test_regex_trigram.h` | 新增 60.13-60.18 测试覆盖 FilteredRE2 atom 提取和 UNION 语义 |

## 测试结果

- Part 60: 21/21 通过
- 全量 fast tests: 11979/11979 通过

## 性能影响

- 对于 AND 模式（`config.*\.json`）：UNION 候选集略大于 INTERSECT，但每个 atom 的 trigram 交集已很小，性能几乎无影响
- 对于 OR 模式（`(foo|bar)`）：UNION 是唯一正确的方式
- FilteredRE2 的 Compile 开销：每次 regex 查询额外一次编译（微秒级），可忽略

## 注意事项

当前查询语法的 tokenizer 将 `|` 作为 PIPE 操作符在 query 层面消费，因此 regex alternation 无法通过 `regex:` filter 传递到 regex 引擎。此修复对 `extractRegexLiteralsImpl` 函数内部的正确性进行了根本性修复，为未来可能支持 quoted regex 参数做好准备。

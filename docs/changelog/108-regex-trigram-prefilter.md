# 108 — Regex Trigram Pre-filtering (P0)

## 问题

正则查询 (`regex:test.*\.py`) 在 ~5M 条记录上耗时 7385ms，因为 `bestTrigramTerm()` 对 REGEX 模式返回空字符串，跳过了 trigram 预过滤，导致全量线性扫描。

## 根因

`queryAdvanced()` 中的 trigram 预过滤只针对 SUBSTRING 模式的 `trigramKey` 进行查找。REGEX 模式的查询节点没有提取 `trigramKey`，因此 `trigramKey.empty()` 为 true，直接跳过 trigram 块进入线性扫描。

## 修复方案

从正则模式中提取连续字面子串（长度 >= 3），利用现有 `intersectPostingListsMulti()` API 做 trigram 预过滤，将候选集从 5M 缩小到数千条，再对候选执行正则匹配。

### 新增函数

1. **`extractRegexLiteralsImpl()`** — 遍历正则模式字符串，提取连续字面子串：
   - 处理转义字符（`\.` → 字面 `.`）
   - `\d`, `\w`, `\s` 等简写中断当前字面串
   - 字符类 `[...]` 内部不提取
   - 元字符 `.`, `*`, `+`, `?`, `|` 等中断字面串
   - 仅保留长度 >= 3 的子串
   - 输出统一小写化

2. **`extractRegexLiteralsFromAST()`** — 递归遍历 QueryAST，收集所有 REGEX 节点的字面子串

3. **Regex trigram 预过滤块** — 在现有 SUBSTRING trigram 块之后，检查 `!useTrigramIndex` 时尝试 regex trigram 路径

4. **`searchPath` 标签** — 区分 `advanced-regex-trigram` / `advanced-trigram` / `advanced-linear`

5. **`me_test::extractRegexLiterals()`** — 暴露给测试的包装函数

### 变更文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | +90 行：新增提取/过滤函数、trigram 块、searchPath 标签 |
| `tests/test_regex_trigram.h` | 新建，Part 60，14 个测试用例 |
| `test_all.cpp` | 注册 Part 60 |

## 测试

- Part 60：14 个测试全部 PASS
  - Tests 1-8：`extractRegexLiterals()` 单元测试（基础提取、转义、`\d`、字符类、短字面串过滤、多字面串、alternation、大小写）
  - Test 9-10：SearchEngine 集成测试（regex trigram 查询、结果正确性）
  - Test 11：无可提取字面串时回退到线性扫描
  - Test 12：空模式边界
- `--fast` 全量回归：11,484 测试 PASS，0 失败

## 预期效果

对于包含 >= 3 字符字面子串的正则查询，候选集从 ~5M 缩小到数千条，查询延迟从 ~7s 降至 < 100ms。

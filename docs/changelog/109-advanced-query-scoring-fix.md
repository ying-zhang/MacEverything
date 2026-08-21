# 109 — 高级查询评分修复

## 背景

`queryAdvanced()` 中的结果评分逻辑存在两个 bug：

1. **评分使用原始输入而非 TERM 文本**：`me::toLower(input)` 使用完整输入字符串（如 `"ext:cpp Hello"`）做 `namePriority` 评分。当查询包含过滤器前缀时，评分基于错误的字符串，导致结果排序不准确。例如 `ext:cpp Hello` 查询中，`Hello_World.cpp` 本应排在首位（name starts with "hello"），但因为用 `"ext:cpp hello"` 做评分，无法正确匹配文件名。

2. **`me::toLower()` 在循环内重复计算**：每次 `evalRecord()` 调用都执行 `me::toLower(input)`，对百万条记录造成不必要的开销。

## 根因分析

Phase 1-4 实现 `queryAdvanced()` 时直接复用了普通 `query()` 的评分逻辑模板，但普通查询的 `input` 就是搜索词本身，而高级查询的 `input` 包含过滤器前缀、修饰符等语法结构，不能直接用于文件名匹配评分。

## 修复方案

### 1. 新增 `extractScoringTerm()` 辅助函数

遍历 AST 树，找到第一个 SUBSTRING 模式的 TERM 节点，返回其小写文本。纯过滤器查询（如 `audio:`）返回空字符串。

### 2. 评分逻辑修复

- 将 scoring term 提取到 `evalRecord` lambda 之前（循环外），消除重复计算
- 当 `scoringTerm` 非空时，使用它做 `namePriority` 评分
- 当 `scoringTerm` 为空时（纯过滤器查询），所有结果使用默认 priority=2，按路径长度排序

## 测试

新增 4 个测试（C16-C17，Part 59 总计 84 tests）：

- **C16**: `ext:cpp Hello` — 验证 Hello_World.cpp 排在第一位（评分使用 "hello" 而非 "ext:cpp hello"）
- **C17**: `audio:` — 纯宏查询正常返回，不崩溃

## 修改文件

| 文件 | 修改 |
|------|------|
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | 添加 `extractScoringTerm()`，修复 `evalRecord` 评分逻辑 |
| `tests/test_query_modifiers.h` | 添加 C16-C17 评分测试 |

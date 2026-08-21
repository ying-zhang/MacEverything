# 128 - Path trigram early-exit 优化

## 背景

R22 性能基准报告中，`/Library/Application` 查询耗时 30-60ms，尽管 name trigram 仅返回 ~3,872 个候选。

## 根因分析

`/Library/Application` 经 `transformSlashTerms()` 转换为：
- `AND(FILTER("__pathseg", segments=["library"]), TERM("application", nameOnly=true))`

trigram 竞争选择中：
- **Name trigram**: `bestTrigramTerm()` 提取 "application"，`intersectPostingLists()` 返回 ~3,872 候选 → 通过阈值检查
- **Path trigram**: `bestPathSegTerm()` 提取 "library"，`intersectPostingLists()` 返回大量路径索引（macOS 有大量 `/Library`、`/System/Library`、`~/Library` 路径）

问题在于 Stage 3 **无论 name trigram 是否已经成功**，都会执行完整的路径索引展开循环：
1. 遍历所有匹配路径索引 `pathIdxCands`
2. 对每个路径索引通过 `pathIdxToRecords_[pi]` 展开为记录 ID
3. 排序 + 去重

这个展开过程消耗 25-50ms，但最终 Stage 4 竞争时 path 候选数远多于 name 候选数，展开完全是浪费。

## 修复方案

在 `intersectPostingLists()` 返回 `pathIdxCands` 后、展开循环前，增加 early-exit 判断：

```cpp
bool skipExpansion = nameOk && pathIdxCands.size() >= nameCands.size();
```

**原理**：每个路径索引至少映射 1 条记录，所以 `pathIdxCands.size()` 是展开后候选数的下界。如果路径索引数已经 >= name 候选数，展开后只会更多，不可能在 Stage 4 竞争中获胜。

同时修复 timing 测量：`afterTrigram` 现在始终在 path trigram 阶段结束后更新，而非仅在 `!nameOk` 时更新。

### 修改位置

`MacEverything/Core/SearchEngineAdvancedQuery.cpp`，Stage 3:

```cpp
// Early-exit: if name trigram already succeeded and path index count
// is >= nameCands (each path index expands to >=1 record), expansion
// cannot produce fewer candidates — skip the expensive expansion.
bool skipExpansion = nameOk && pathIdxCands.size() >= nameCands.size();
if (!skipExpansion) {
    // ... existing expansion loop ...
}
```

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | Stage 3 增加 early-exit 判断 + 修复 timing |
| `tests/test_trigram_competition.h` | 新增 3 个测试用例（69.10-69.12） |
| `tests/test_highlight_hints.h` | 修复 include 路径 |

## 测试

### 新增测试用例

- **69.10**: 模拟 `/Library/Application`，"library" 路径索引多但 "application" name 候选少 → 验证 name trigram 获胜，跳过 path 展开
- **69.11**: `raresegment` 路径索引极少（1-2个）→ 验证 path trigram 仍然获胜（不被误 skip）
- **69.12**: 验证 trigram timing 被正确捕获

### 测试结果

- Part 69: 22/22 通过
- 全量 fast 测试: 11,781/11,781 通过

## 预期性能改善

| 查询 | 修复前 | 修复后（预期） |
|------|--------|----------------|
| `/Library/Application` | 30-60ms | < 10ms（跳过 25-50ms 的无用 path 展开） |
| `/raresegment/xxx` | 正常 | 不受影响（path 索引少，仍展开） |

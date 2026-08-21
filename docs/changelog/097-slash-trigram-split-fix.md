# 097 - 修复 Slash 查询始终走 linear scan 问题

## 背景

所有含 `/` 的搜索查询（如 `usr/local`、`/Library/Application Support`、`src/main`）始终走 `linear` 全量扫描（200-320ms），从未使用设计好的 `trigram-split` 快速路径（预期 <10ms）。

## 根因分析

`querySlashSplit()` 嵌套在 `if (useTrigramIndex)` 分支内，但 `useTrigramIndex` 的判定逻辑会将**完整查询字符串**（含 `/`）交给 `intersectPostingLists(nameTrigramIndex_, lowerKey, ...)`。

由于文件名中不含 `/`，跨斜杠的 trigram（如 `r/l`、`/lo`）在 `nameTrigramIndex_` 中不存在：
- `nameAllFound = false` → `useTrigramIndex = false` → `querySlashSplit` 永远不可达

这是一个门控逻辑错误：slash 查询需要同时使用 `nameTrigramIndex_` 和 `pathTrigramIndex_`（各自处理斜杠分割后的子串），但门控条件用整个查询串检查了 name 索引。

## 修复方案

在 `query()` 方法的 `else` 分支（非 trigram 路径）中，增加 slash 查询检测：

```cpp
} else if (hasSlash && useTrigram && !pathTrigramIndex_.empty()) {
    // 绕过 name-trigram 门控，直接调用 querySlashSplit
    std::vector<uint32_t> emptyPhase1;
    useSlashSplit = querySlashSplit(lowerKey, totalSize, myGen, emptyPhase1, merged);
    if (useSlashSplit) {
        useTrigramIndex = true;  // 确保 searchPath 标签正确
    } else {
        // 两部分都 < 3 字符，回退到 linear scan
        queryLinearScan(lowerKey, false, totalSize, myGen, merged);
    }
} else {
    queryLinearScan(lowerKey, useGlob, totalSize, myGen, merged);
}
```

**关键点**：
- `emptyPhase1` 传空 vector：Phase 1 未产出任何名称匹配，dedup bitmap 全零，不跳过任何结果
- `querySlashSplit` 返回 false 时（两个子串都 < 3 字符），安全回退到 linear scan
- `/usr` 类查询通过 `querySlashSplit` 内部的特殊处理（L592：空 pathPart 时拼接 "/" + namePart）正确工作

## 变更文件

| 文件 | 变更内容 |
|------|----------|
| `MacEverything/Core/SearchEngine.cpp` | 在 else 分支中插入 slash 查询 → `querySlashSplit` 分支（15 行） |
| `tests/test_slash_query.h` | 新增 Test 11：验证 slash 查询走 `trigram-split` 路径，覆盖相对路径、绝对路径、短路径回退 |

## 测试结果

### 单元测试
- 11022 tests passed, 0 failed

### Test 11 覆盖场景
| 查询 | 预期路径 | 结果 |
|------|----------|------|
| `local/bin` | trigram-split | PASS |
| `/usr/local` | trigram-split | PASS |
| `/usr` | trigram-split | PASS |
| `a/b` | linear (回退) | PASS |

## 预期性能改善

| 查询类型 | 修复前 | 修复后 |
|----------|--------|--------|
| `usr/local` | ~200-320ms (linear) | <10ms (trigram-split) |
| `/Library/Application` | ~200-320ms (linear) | <10ms (trigram-split) |
| `src/main` | ~200-320ms (linear) | <10ms (trigram-split) |
| `a/b` (短路径) | ~200-320ms (linear) | ~200-320ms (linear, 正确回退) |

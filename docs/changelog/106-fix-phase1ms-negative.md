# 106 - Fix phase1Ms negative values in trigram-degrade and glob-trigram paths

## 问题描述

基准测试发现 11 个查询的 `phase1Ms` 持续为负值，影响性能分析准确性。

### 根因

`phase1Ms` 计算公式为 `afterPhase1 - afterTrigram`（SearchEngineQuery.cpp L877）。在以下两条路径中，`afterTrigram` 被更新为较晚时间戳，但 `afterPhase1` 保持 `afterLock` 初始值，导致差值为负：

1. **Trigram 降级路径**（L709-720）：查询进入 trigram 分支后，候选数超阈值（>totalSize/67），`useTrigramIndex` 置为 false，但 `afterTrigram` 已在 L719 被更新。后续 else 分支不再设置 `afterPhase1`。
   - 受影响查询：`bin`(-0.1ms)、`test`(-0.7ms)、`python`(-1.5ms)

2. **Glob-trigram 路径**（L780-812）：glob 查询使用 trigram 预过滤时，`afterTrigram` 在 L786 被更新，但 `afterPhase1` 从未被设置。
   - 受影响查询：`*.py`(-4.8ms)、`*.cpp`(-0.5ms)、`*.json`(-1.8ms)、`*.swift`(-1.1ms)、`*.md`(-0.1ms)

### 修复思路

遵循 097 修复的先例（L826 `afterPhase1 = afterTrigram`），在两条路径中均设置 `afterPhase1 = afterTrigram`，语义为"这些路径不执行 Phase 1 名称扫描，phase1Ms 应为 0"。

## 变更内容

### SearchEngineQuery.cpp

1. **L719-720（trigram 降级路径）**：在 `afterTrigram = now()` 之后增加条件判断，当 trigram 降级时设置 `afterPhase1 = afterTrigram`。
2. **L786（glob-trigram 路径）**：在 `afterTrigram = now()` 之后直接设置 `afterPhase1 = afterTrigram`。

### tests/test_slash_query.h

新增两个测试：
- **Test 13**：构造 200 条同名记录触发 trigram 降级，验证 `phase1Ms >= 0`
- **Test 14**：使用 `*.cpp` glob 查询，验证 glob-trigram 路径的 `phase1Ms >= 0`

## 验证结果

- 单元测试：11384 tests passed, 0 failed
- HTTP API 验证：所有 7 个受影响查询 `phase1Ms` 从负值变为 `0.0`

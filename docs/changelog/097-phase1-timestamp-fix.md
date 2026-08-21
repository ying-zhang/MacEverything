# 097 - 修复 Slash 查询 phase1 时间戳为负数

## 背景

在 096 修复 slash 查询走 `trigram-split` 路径后，日志中出现 `phase1=-1796ms` 等负值时间戳，影响性能分析和监控的准确性。

## 根因分析

`query()` 方法中，时间戳变量在 L1036-1038 初始化为 `afterLock`：

```cpp
auto beforeTrigram = afterLock, afterTrigram = afterLock;
auto afterPhase1 = afterLock;
auto beforePhase2 = afterLock, afterPhase2 = afterLock;
```

在 else 分支的 slash 查询路径（L1144-1158），`beforeTrigram` 和 `afterTrigram` 被正确设置为 `querySlashSplit()` 前后的 `now()`，但 `afterPhase1` **从未更新**，保持为 `afterLock`。

Timing 计算公式（L1203）：
```cpp
timing.phase1Ms = toMs(afterPhase1 - afterTrigram);
```

由于 `afterPhase1 = afterLock`（较早时刻），`afterTrigram = now()`（较晚时刻），差值为负数。

## 修复方案

在 `afterTrigram` 赋值后立即设置 `afterPhase1 = afterTrigram`：

```cpp
afterTrigram = std::chrono::steady_clock::now();
afterPhase1 = afterTrigram;  // No Phase 1 in this path; zero out phase1Ms
```

这使 `phase1Ms = afterPhase1 - afterTrigram = 0`，语义正确：slash 查询从 else 分支进入时不经过 Phase 1 名称扫描，phase1 时间应为 0。

## 变更文件

| 文件 | 变更内容 |
|------|----------|
| `MacEverything/Core/SearchEngine.cpp` | L1153: 添加 `afterPhase1 = afterTrigram;`（1 行） |
| `tests/test_slash_query.h` | 新增 Test 12：验证 slash 查询的 phase1Ms/trigramMs/phase2Ms 均为非负 |

## 测试结果

### 单元测试
- 11039 tests passed, 0 failed

### Test 12 验证
```
timing: trigram=0.0ms phase1=0.0ms phase2=0.0ms
```

### HTTP API 验证
| 查询 | phase1Ms (修复前) | phase1Ms (修复后) |
|------|-------------------|-------------------|
| `usr/local` | -1796ms | 0.0ms |
| `/Library/Application` | -1637ms | 0.0ms |

# 090 — Trigram 候选数过多时回退到 Linear Scan

## 问题

搜索 `"test"` 时走 trigram 路径，产生 88,634 个候选，Phase 1 逐个做 `simdContains` 验证耗时 541ms。而 linear scan 扫描全量 473 万记录仅 ~200ms。

根因：trigram Phase 1 是**单线程顺序遍历 posting list**，通过离散索引跳跃访问 `records_` 和 `namePool_`，导致大量 cache miss（每个候选 ~6.1us）。而 linear scan 使用 `dispatch_apply` 多线程 + 连续内存顺序扫描（每个记录 ~0.042us），即使记录数多 53 倍，总耗时仍更低。

## 阈值计算

| 路径 | 元素数 | 耗时 | 单元素成本 |
|------|--------|------|-----------|
| Trigram Phase 1 | 88,634 | 541ms | 6.1us |
| Linear scan | 4,734,000 | 200ms | 0.042us |

交叉点：`candidates * 6.1us = 200ms` -> `candidates ~ 32,800` -> `32,800 / 4,734,000 ~ 0.69%`

保守取 **1.5%**（`totalSize / 67`），确保只在 trigram 明显更慢时才回退。

验证：`"test"` 的 88,634 / 4,734,000 = 1.87% > 1.5% -> 回退到 linear。

## 修复方案

### 变更 1：SearchEngine.cpp — 插入阈值检查

在 trigram 候选交集完成后（L436），检查候选数是否超过阈值：

```cpp
// Trigram candidates exceed threshold -- linear scan is faster due to
// cache-friendly sequential access vs random posting-list traversal
if (trigramCandidates.size() > totalSize / 67) { // ~1.5%
    trigramCandidates.clear();
    useTrigramIndex = false;
}
```

使用整数除法 `totalSize / 67` 避免浮点运算。`useTrigramIndex = false` 后自然走 linear scan 分支，日志中 `path=linear` 也会正确反映。

### 变更 2：test_trigram_index.h — 新增阈值回退测试

新增 Test 11，构造 10,000 条记录的数据集：
- 200 条含 "test"（2% > 1.5%）-> 验证回退到 linear
- 5 条含 "unique_xyz"（0.05% < 1.5%）-> 验证保持 trigram

## 变更文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngine.cpp` | L437-442 插入阈值检查（5 行） |
| `tests/test_trigram_index.h` | 新增 Test 11：阈值回退测试 |

## 测试结果

- `./test_all --fast`：10918/10918 PASS
- xcodebuild Release 构建通过
- HTTP API 验证：
  - `"test"` -> path=linear, ~323ms（之前 541ms trigram）
  - `"searchengine"` -> path=trigram, 10ms, 672 candidates

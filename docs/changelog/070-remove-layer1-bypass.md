# 070 - 去掉 Layer1 Adaptive Trigram Bypass

## 背景

Layer1 bypass 的原始设计意图：当 trigram 候选集超过总量 1% 且记录数 >1M 时，认为随机访问的 cache miss 成本高于顺序扫描，因此回退到线性扫描。

10M benchmark 数据证明这个假设是错误的：
- bypass 后走线性扫描：306ms（与 limit 无关）
- 不 bypass 走 trigram：32ms（limit=100）

bypass 反而让高频查询 "test" 慢了 9.5 倍。

## 根因分析

bypass 的前提假设"随机访问比顺序扫描慢"在以下条件下不成立：
1. trigram 候选集虽大（116 万），但仍远小于全量（497 万），减少了 76% 的扫描量
2. 有 limit 时 trigram 路径可以在找到足够结果后高效停止排序
3. 线性扫描必须遍历全部记录才能保证排序正确性，limit 参数完全无效

## 改动

- **删除** `SearchEngine.cpp` L298-306：Layer1 bypass 判断逻辑（6 行）
- **删除** L317 过时注释

## 测试验证

### 10M Benchmark (Part 46)

| 查询 | Before | After | 提速 |
|------|--------|-------|------|
| "test" L=100 | 306ms | **32ms** | **9.5x** |
| "test" L=10 | 362ms | **33ms** | **11x** |
| "test" L=1 | 314ms | **35ms** | **9x** |
| "test" unlimited | 331ms | 405ms | 0.8x（预期） |

unlimited 变慢是因为 trigram 路径需要遍历全部候选并排序，但这是合理的——无限结果查询本身就是重操作。

### 生产环境验证 (400 万 records)

- 之前日志：`Query "test" total=355ms path=linear`
- 现在实测：`queryTimeMs: 76ms`（提速 4.7x）
- 结果排序正确：精确匹配 `/bin/test` 排第一

### 回归测试

- `--fast` 全量测试：10,807 tests all passed
- 10M benchmark 7 个正确性检查全部通过

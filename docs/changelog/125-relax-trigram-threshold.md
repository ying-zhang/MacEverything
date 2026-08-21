# 125 - 放宽 name trigram 候选阈值

## 背景

R21 性能基准报告中，部分查询本应使用 trigram 预过滤但退化到线性扫描：

| 查询 | 预期路径 | 实际路径 | 延迟 |
|------|----------|----------|------|
| `test` | advanced-trigram | advanced-linear-gcd | 214ms |
| `*.py` | advanced-trigram | advanced-linear-gcd | 126ms |

## 根因分析

`intersectPostingLists()` 返回的候选数需通过阈值检查 `candidates <= totalSize / 67`（约 1.5%）才能使用 trigram。对于 5.35M 记录：

- `test`: 92K 候选 = 1.7%，超过 1.5% 阈值 → 退化线性
- `*.py`: 365K 候选 = 6.8%，超过 1.5% 阈值 → 退化线性

阈值 `totalSize/67` 过于严格，导致许多常见关键词无法利用 trigram 加速。

## 修复方案

将 name trigram 候选阈值从 `totalSize / 67`（~1.5%）放宽至 `totalSize / 10`（10%）。

### 修改位置

`MacEverything/Core/SearchEngineAdvancedQuery.cpp`，两处阈值判断：

1. **Stage 1 name trigram**（L641）：
   ```cpp
   // Before:
   nameOk = allFound && nameCands.size() <= totalSize / 67;
   // After:
   nameOk = allFound && nameCands.size() <= totalSize / 10;
   ```

2. **Stage 2 regex trigram**（L653）：
   ```cpp
   // Before:
   nameOk = allFound && nameCands.size() <= totalSize / 67;
   // After:
   nameOk = allFound && nameCands.size() <= totalSize / 10;
   ```

path trigram 阈值 `totalSize / 4`（25%）未改动。

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | 两处阈值 `/67` → `/10` |
| `tests/test_trigram_competition.h` | 新增 3 个测试用例（69.7-69.9） |

## 测试

### 新增测试用例

- **69.7**: 1000 条记录，50 条 (5%) 含 "test" → 验证 trigram 生效
- **69.8**: 100 条记录，15 条 (15%) 含 "test" → 验证超过 10% 退化线性
- **69.9**: 1000 条记录，30 条 (3%) `.py` 文件 → 验证 glob trigram 在放宽阈值下生效

### 测试结果

- Part 69: 17/17 通过
- 全量 fast 测试: 11,754/11,754 通过

## 性能验证 (HTTP, 5.37M 记录)

| 查询 | 修复前 | 修复后 |
|------|--------|--------|
| `test` | advanced-linear-gcd, 214ms | advanced-trigram, 92K 候选, ~54ms |
| `*.py` | advanced-linear-gcd, 126ms | advanced-trigram, 365K 候选, ~50ms |
| `*test*.cpp` | advanced-trigram, ~41ms | advanced-trigram, 92K 候选, ~29ms |

## 未修复项

- `桌面` (326ms, 0 结果): macOS 文件系统使用英文名 "Desktop" 而非中文 "桌面"，属于预期行为
- P21 (`config`/`readme` 等走 linear): 这些来自 `linear_forced` 控制组（`trigram=0`），不是真实问题

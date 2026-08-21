# 071 — Path Trigram Index

## 概述

为 SearchEngine 新增 **路径 trigram 索引**（`pathTrigramIndex_` + `pathIdxToRecords_`），将路径搜索从线性扫描全部记录（O(N)）优化为 trigram 交集 + 路径展开（O(候选路径数)）。在 10M 记录基准测试中，路径搜索加速 **23x–120x**，无任何场景出现性能退化。

## 动机

此前 `query()` 的 Phase 2（路径匹配）必须线性扫描全部 ~500 万条记录。即使关键词在路径中高度唯一（如 "DerivedData" 仅匹配极少路径），仍需遍历全部记录，耗时 300–700ms。

`PathTable` 已将 4.5M 记录的目录路径去重为 ~80K 唯一路径。方案 D 利用这一特性建立两级查找：
1. **pathTrigramIndex_**：trigram → sorted pathIdx（~80K 级别）
2. **pathIdxToRecords_**：pathIdx → sorted record indices

## 数据结构

```cpp
// SearchEngine.h 新增成员
std::unordered_map<Trigram, std::vector<uint32_t>> pathTrigramIndex_; // trigram -> sorted pathIdx
std::vector<std::vector<uint32_t>> pathIdxToRecords_; // pathIdx -> sorted record indices
```

新增 5 个实例方法和 3 个静态方法用于构建和增量维护。

## 实现要点

### 构建（buildPathTrigramIndex / rebuildPathIdxToRecords）
- 遍历 PathTable 所有路径，提取小写化后的 trigram，映射到 pathIdx
- 遍历所有活记录，按 pathIdx 分组到 pathIdxToRecords_

### query() Phase 2 改造
- **使用条件**：关键词 ≥ 3 字符、不含 `/`、非 glob 模式
- 提取关键词 trigram，在 pathTrigramIndex_ 中交集得到候选 pathIdx
- 对候选路径验证 substring 匹配（过滤 trigram false positive）
- 展开为 record indices，排除 Phase 1 已匹配的文件名候选
- **回退条件**：短关键词 / glob / 含 `/` → 保留原有线性扫描

### 15+ 变异点维护
所有增删改路径（`addRecord`, `removeByPath`, `updateByPath`, `removeByPathPrefix`, `batchRescanPrefix`, `compactRecords`, `replayWALEntries`）均同步维护 pathIdxToRecords_ 的增量更新，并在 PathTable 新增路径时更新 pathTrigramIndex_。

COW compaction 使用静态方法在锁外构建新索引，然后在 swap 阶段原子替换。

## 基准测试（10M 记录）

| 场景 | 查询 | 优化前 | 优化后 | 变化 |
|------|------|--------|--------|------|
| S1 | "test" L=100 | 35.8ms | 39.4ms | 噪声范围 |
| S1 | "SearchEngine" L=100 | 9.7ms | 5.4ms | 1.8x 加速 |
| S1 | "unique_xyz" L=100 | 3.0ms | 2.2ms | 1.4x 加速 |
| S5 | **"homebrew"** | **674.5ms** | **29.3ms** | **23x 加速** |
| S5 | **"DerivedData"** | **512.2ms** | **14.6ms** | **35x 加速** |
| S5 | **"level4"** | **445.5ms** | **3.7ms** | **120x 加速** |
| S6 | "README.md" L=100 | 5.2ms | 5.6ms | 噪声范围 |
| S7 | "EXACT_MATCH" L=100 | 2.9ms | 3.1ms | 噪声范围 |

所有场景无退化，路径搜索场景大幅提速。

## 内存估算

- 4M 记录：~32MB 额外
- 10M 记录：~142MB 额外

## 测试

新增 `tests/test_path_trigram.h`（Part 47），8 个测试、19 个断言：
1. buildPathTrigramIndex 正确性
2. pathIdxToRecords 映射正确性
3. 增量 add/remove 一致性
4. 仅路径匹配（DerivedData 不在文件名中）
5. 含 `/` 关键词回退线性扫描
6. compaction 后索引一致性
7. 短关键词（< 3 字符）回退
8. batchRescanPrefix 维护索引

全部 10,822 项 `--fast` 测试通过，0 失败。

## 变更文件

| 类型 | 文件 |
|------|------|
| 修改 | `MacEverything/Core/SearchEngine.h` |
| 修改 | `MacEverything/Core/SearchEngine.cpp` |
| 新增 | `tests/test_path_trigram.h` |
| 修改 | `test_all.cpp` |

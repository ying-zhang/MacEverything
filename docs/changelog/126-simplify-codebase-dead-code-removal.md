# 126 — 简化代码库：删除死代码、重复逻辑和冗余抽象

## 背景

对项目进行审计后发现以下可安全删除的代码，不影响功能和性能：

1. 死 SIMD 函数（从未在生产代码中调用）
2. evalFilter/evalNode 的 AoS/SoA 重复逻辑（~68 行）
3. v4 持久化读取路径（v5 已是唯一写入格式，v4 为死代码）
4. CompactionTimer 独立类（仅一个消费者 IndexPersistence）

## 实施

### Step 1: 删除死 SIMD 函数及测试

**文件变更**:
- `MacEverything/Core/SIMDSearch.h` — 删除 `simdTypeEq16`（从未调用）和 5 个 `simdCompareU64x2*` 函数，共 ~102 行
- `tests/test_simd_batch_filter.h` — 删除引用这些函数的测试（63d-63n），保留存活函数 `simdTypeLive16` 的测试

### Step 2: 统一 evalFilter/evalNode — 消除 SoA 重复

**文件变更**: `MacEverything/Core/SearchEngineAdvancedQuery.cpp`

**方案**: 删除 `evalFilterSoA` 和 `evalNodeSoA`，在 `evalFilter`/`evalTerm` 中增加 null-pointer 分支：
- `nameData == nullptr` 时字符串类过滤器（ext/path/nopath/parent/depth/len/__pathseg）提前返回 true
- `evalTerm` 中 `nameData == nullptr` 时提前返回 false

pure-filter 快速路径改为传 `nameData=nullptr, pathData=nullptr` 调用统一的 `evalNode`，消除 ~68 行重复代码。

### Step 3: 删除 v4 持久化读取路径

**文件变更**:
- `MacEverything/Core/PagedIndexWriter.cpp` — 删除 `readString` helper、v4 反序列化分支、`deserializePage` 函数，~150 行
- `MacEverything/Core/PagedIndexWriter.h` — 删除 `deserializePage` 声明
- `MacEverything/Core/ServiceEngine.cpp` — `v4_paged` → `v5_paged`
- `tests/test_paged_persistence_v5.h` — P53-6 改为验证 v4 被正确拒绝

**策略**: `load()` 遇到 v4 版本号时返回 false 并输出警告日志，应用启动后触发全量扫描重建索引。

### Step 4: 内联 CompactionTimer 到 IndexPersistence

**文件变更**:
- 删除 `MacEverything/Core/CompactionTimer.h` 和 `CompactionTimer.cpp`（~90 行）
- `MacEverything/Core/IndexPersistence.h` — 将 `dispatch_source_t`、`dispatch_queue_t` 作为私有成员
- `MacEverything/Core/IndexPersistence.cpp` — `startAutoCompaction`/`stopTimer`/`rescheduleTimer` 内联为私有方法
- 删除 `tests/test_compaction_timer.h`，从 `test_all.cpp` 移除 part 33
- `MacEverything.xcodeproj/project.pbxproj` — 移除 CompactionTimer 引用

## 变更统计

| 指标 | 数值 |
|------|------|
| 净删除行数 | ~552 行 |
| 删除文件 | 3 个（CompactionTimer.h/.cpp, test_compaction_timer.h）|
| 修改文件 | 11 个 |
| 测试结果 | 11748 passed, 0 failed |

## 性能验证

基准测试无回归：
- trigram 查询平均中位数: 7.3ms
- 线性扫描平均中位数: 218.0ms
- trigram vs linear 加速比: 68x - 522x

## 验收

- Xcode Release 构建通过
- 11748 测试全部通过
- DMG 打包成功
- HTTP API 搜索验证通过（文本搜索、过滤器查询、路径查询）
- bench_search.py 性能无回归

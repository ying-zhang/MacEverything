# 113: SoA 列式存储 + SIMD 批量过滤 + GCD 并行化

## 背景

`queryAdvanced()` 对纯过滤查询（如 `size:>1mb`、`dm:today`、`file:`）存在两个性能瓶颈：

1. **缓存利用率低**：FileRecord ~80-104 字节/条，但纯过滤查询只需 type(1B) + size(8B) + modTime(8B) = 17 字节。AoS 布局导致每条记录加载整个 cache line 但只用 ~20%。
2. **单线程线性扫描**：纯过滤查询无 trigram 候选集，必须全表扫描，但单线程执行且未利用 SIMD 批量比较。

## 实施内容

### P0a: SoA 列式数组 + Helper 方法

**修改文件**：`SearchEngine.h`, `SearchEngine.cpp`

- 在 SearchEngine 中添加三个列式数组：`types_`(uint8_t)、`sizes_`(uint64_t)、`modTimes_`(int64_t)，与 `records_` 等长。
- 新增三个 helper 方法统一维护 SoA 一致性：
  - `tombstoneAt(idx)`：标记删除 + SoA 清零
  - `pushRecord(rec)`：追加记录 + SoA 追加
  - `rebuildSoA()`：从 records_ 全量重建 SoA
- 替换全部 22 个 mutation 站点（loadRecords/loadRecordsV5/addRecord/removeByPathUnlocked/removeByPathPrefix/batchRescanPrefix/updateByPathUnlocked/compactRecords/replayWALEntries）使用 helper 方法，防止 SoA 与 records_ 不同步。

### P0b: AST 需求分析

**新建文件**：`QueryNeedsAnalysis.h`

- 实现 `analyzeQueryNeeds()` 递归遍历查询 AST，确定哪些字段被访问。
- `QueryNeeds::isPureFilter()` 返回 true 当查询不需要 name/path 字段（纯过滤查询）。
- 纯过滤查询可完全跳过 StringPool 访问。

### P1a: SIMD 批量过滤

**修改文件**：`SIMDSearch.h`

新增 NEON SIMD 批量过滤函数（含 scalar fallback）：
- `simdTypeLive16()`：一次检查 16 个 type 字节的活跃性（!= 0）
- `simdTypeEq16()`：一次比较 16 个 type 字节是否等于目标值
- `simdCompareU64x2GT/GE/LT/LE/EQ()`：一次比较 2 个 uint64 值

### P1b: GCD 并行扫描

**修改文件**：`SearchEngineAdvancedQuery.cpp`

- 引入 `dispatch_apply` 对线性扫描进行并行化，按 chunk 分配到多线程。
- 纯过滤路径使用 `evalNodeSoA()`/`evalFilterSoA()` 直接从 SoA 数组读取，跳过 StringPool。
- SIMD 快速路径：16 字节对齐的 preamble + 主循环（`simdTypeLive16` 批量跳过 tombstone）+ scalar tail。
- 非纯过滤路径使用完整的 `evalNode()` 含字符串匹配。
- 每个线程使用独立的 `threadResults` 和 `localPathBuf`，无锁并行。

## 测试

### 单元测试

- **Part 62**（QueryNeedsAnalysis）：16 个测试用例，覆盖 TERM/size:/file:/folder:/dm:/ext:/path:/len:/type:/datemodified:/datecreated:/dateaccessed: 各种组合和 AND/OR/NOT 嵌套。
- **Part 63**（SIMD Batch Filter）：15 个测试用例，覆盖 simdTypeLive16/simdTypeEq16/simdCompareU64x2 的各种场景、边界值（零值、UINT64_MAX）。

### 集成验证

通过 HTTP API 验证纯过滤查询和混合查询的正确性。

## 关键设计决策

1. **SoA 仅包含过滤字段**：只提取 type/size/modTime 三个字段，name/path 仍通过 StringPool 访问，避免数据膨胀。
2. **Helper 方法强制一致性**：所有 mutation 站点必须通过 `tombstoneAt()`/`pushRecord()`/`rebuildSoA()` 操作，防止 SoA 与 records_ 不同步。
3. **SIMD 主要用于 tombstone 批量跳过**：`simdTypeLive16()` 一次跳过 16 个 tombstone，减少分支预测失败，而非试图用 SIMD 实现完整的 AST 求值。
4. **GCD dispatch_apply 自动负载均衡**：利用系统线程池，避免手动线程管理。

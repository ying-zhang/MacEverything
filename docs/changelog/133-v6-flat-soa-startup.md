# #133 v6 Flat SoA 持久化格式 — 亚秒级启动

## 概述

将持久化格式从 v5 AoS 分页格式升级为 v6 Flat SoA（Structure-of-Arrays）单文件格式，配合两阶段启动策略，实现亚秒级可搜索状态。

## 动机

MacEverything 启动加载索引需要 12-17 秒（4.5M 条记录）。瓶颈是 v5 AoS 分页格式的逐条反序列化（~25%）和 trigram 索引构建（~40%）。用户体验上，启动后需要长时间等待才能开始搜索。

## 方案设计

### v6 文件格式 (`index.v6`)

单文件替代 `index.pages` + `index.ptable`：

```
Header (64 bytes):  magic("MEV6") + version + recordCount + liveCount + timestamp
                    + lastEventId + sectionCount + headerCRC + reserved
Section Table:      sectionCount × 24 bytes (sectionId + offset + byteLength + crc32)
Sections:           NAMES_ORIG | NAMES_LOWER | PATH_POOL | LOWER_PATH_POOL
                    | PATH_INDICES | TYPES | SIZES | MOD_TIMES
                    | INODES | DEV_IDS | METADATA_KV
```

- StringPool sections：直接 `loadRaw()` 加载
- 数值列：`fread()` + `memcpy` 直接灌入 vector
- 每个 section 有独立 CRC32 校验

### 两阶段启动

**Phase 1（同步，~200ms）**— `loadRecordsV6()`：
- fread 所有 section 到内存
- 安装 SoA 列和 StringPool
- 从 SoA + origNamePool 构建 records_
- 重建 pathLookup_ / lowerPathLookup_
- 重建 pathIndex_
- 搜索立即可用（trigram 为空时 linear scan 回退）

**Phase 2（后台 GCD，~6.7s）**— `completePhase2()`：
- buildTrigramIndexFromData()
- buildPathTrigramIndexFromData()
- buildPathIdxToRecordsFromData()
- buildRecentCacheFromData()
- unique_lock 下 swap + 重放增量变更
- Phase 2 期间的增删记录通过 mutation replay 保持一致性

### v5 → v6 自动迁移

```
IndexPersistence::load():
  1. 尝试 v6 → 成功则 Phase 1 完成
  2. 尝试 v5 → 成功后立即 fullRewrite 写出 v6
  3. 尝试 v3 → 同上
  4. 均无 → return 0（触发全盘扫描）
```

## 实现文件

### 新建
- `MacEverything/Core/FlatIndexWriter.h/cpp` — v6 读写器（load / fullRewrite / flushDirtyPages）

### 修改
- `SearchEngine.h/cpp` — 新增 `loadRecordsV6()`, `completePhase2()`, `snapshotForV6()`, Phase 2 基础设施
- `SearchEngineIndex.cpp` — compactRecords 完成后清除 phase2Pending
- `IndexPersistence.h/cpp` — v6 级联加载 + 迁移逻辑
- `ServiceEngine.cpp` — Phase 2 后台调度
- `tests/test_flat_persistence_v6.h` — 12 个测试用例
- `tests/test_dirty_compaction.h` — 更新为检查 v6 文件
- `tests/test_compact_threshold.h` — 更新为检查 v6 文件
- `tests/test_paged_persistence.h` — 迁移测试改为验证 v6 输出
- `tests/test_wal_batch_replay.h` — 清理 lambda 增加 v6 文件移除

## 关键修复

- `FlatIndexWriter::fullRewrite()` 需要 `"w+b"` 模式而非 `"wb"`，因为 CRC 计算需要 seek-back + fread
- WAL 批量重放测试需要在 cleanup 中移除 `.v6` 文件，避免自动迁移产生的 stale 文件影响后续测试

## 性能结果

| 指标 | v5 (before) | v6 (after) | 改善 |
|------|-------------|------------|------|
| 启动总时间 | ~17s | ~6.3s | 2.7x |
| 搜索可用时间 | ~17s | ~200ms | 85x |
| Phase 2 完成 | N/A | ~6.7s | — |
| 查询耗时 | ~3.5ms | ~3.5ms | 持平 |
| 记录数 | 4.5M | 5.5M | — |

Phase 2 期间搜索走 linear scan 回退，查询仍在 50-100ms 内完成。Phase 2 完成后 trigram 加速生效，查询恢复到 ~3.5ms。

## 测试

- 12 个新增 v6 专项测试 + 既有测试全部更新
- 全部 11,890 个测试通过，0 失败
- 生产环境验证：4,510,559 条记录，Phase 2 重放 5,086 条增量变更

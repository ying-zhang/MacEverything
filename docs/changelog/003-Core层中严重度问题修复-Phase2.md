# 003 - Core 层中严重度问题修复 (Phase 2)

- **类型**: bugfix
- **严重级别**: P2-MEDIUM
- **日期**: 2026-04-14
- **Commit**: `85998d9`

## 问题描述

Core 层存在 10 个中严重度问题，覆盖性能、并发和正确性三个维度。

## 根因分析与修复方案

### 性能 (P1-P6)
- **P1**: WAL 每次 append 都 fsync → 改为批量 sync + 显式 `flush()`
- **P2**: DirectoryScanner 缓冲区 4MB 过大 → 降至 1MB，reserve 200K→50K
- **P3**: Trigram 提取使用 `unordered_set` 开销大 → 改用 bitmap
- **P4**: 内容索引两次 I/O → 合并为单次 I/O + 内联二进制检测
- **P5**: posting list 无序插入 → `lower_bound` 有序插入 + `binary_search`
- **P6**: Trigram 查询跳过逻辑存在无效恒等比较 → 修复

### 并发 (T1-T2)
- **T1**: Compact 原子性验证（Phase 1 修复后已正确）
- **T2**: `hasAllowedExtension` 拆分为带锁/无锁两个变体

### 正确性 (L1-L2)
- **L1**: macOS APFS 大小写不敏感 → pathIndex key 改为全小写
- **L2**: compact 中 `rename()` 未检查错误 → 添加错误检查

## 影响的文件

- `MacEverything/Core/SearchEngine.cpp`
- `MacEverything/Core/IndexPersistence.cpp`
- `MacEverything/Core/ContentIndex.cpp`
- `MacEverything/Core/DirectoryScanner.cpp`

## 测试覆盖

165 个测试通过（48 个新增 Phase 2 测试 + 117 个已有测试）。

# 002 - Core 层高严重度 Bug 修复 (C1-C5)

- **类型**: bugfix
- **严重级别**: P1-HIGH
- **日期**: 2026-04-14
- **Commit**: `dfe2651`

## 问题描述

Core 层存在 5 个高严重度 bug，涉及 compaction 后索引失效、析构期间数据竞争、WAL 并发写入、文件写入静默失败、扫描器状态残留。

## 根因分析

- **C1**: `compactRecords()` 未返回旧→新索引映射，ContentIndex 的 `fileIndices` 在 compaction 后指向错误记录
- **C2**: `IndexPersistence` / `ContentIndexPersistence` 析构时未等待正在执行的 compaction 完成
- **C3**: `ContentIndexPersistence::walAppendAdd/Remove` 未加锁，并发 WAL 写入存在数据竞争
- **C4**: `SearchEngine::saveToFile()` 未检查 `writeRecord()` 返回值，写入失败时静默生成损坏文件
- **C5**: `DirectoryScanner::scan()` 未重置内部状态（`done_`, `activeTasks_`, `visitedDirs_` 等），导致复用时残留旧数据

## 修复方案

- C1: `compactRecords()` 返回 old→new 索引重映射表
- C2: 析构器通过 `dispatch_sync` 等待 compaction 队列排空
- C3: `walAppendAdd/Remove` 加 `walMutex_` 锁
- C4: 检查 `writeRecord()` 返回值，失败时中止写入
- C5: `scan()` 入口重置所有状态字段

## 影响的文件

- `MacEverything/Core/SearchEngine.cpp`
- `MacEverything/Core/IndexPersistence.cpp`
- `MacEverything/Core/ContentIndexPersistence.cpp`
- `MacEverything/Core/DirectoryScanner.cpp`

## 测试覆盖

117 个测试通过，包含 26 个新增 Phase 1 回归测试。

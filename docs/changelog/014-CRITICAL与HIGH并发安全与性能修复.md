# 014 - CRITICAL/HIGH 并发安全与性能修复

- **类型**: bugfix
- **严重级别**: P0-CRITICAL / P1-HIGH
- **日期**: 2026-04-14
- **Commit**: `80eb805`

## 问题描述

多项并发安全缺陷和性能瓶颈在高负载场景下暴露，包括 use-after-free、WAL 删除顺序错误、撕裂读、终止竞态，以及多处性能问题。

## 根因分析

**CRITICAL 级别：**

- **C-1: query() use-after-free** — `shared_lock` 作用域过短，查询期间底层数据可能被 compact 释放。
- **C-2: compact() WAL deletion order** — 先删除 WAL 再写 base 文件，崩溃窗口内数据丢失。
- **C-3: _contentIndex torn read** — 多线程并发读写 `_contentIndex` 缺少同步，导致撕裂读。
- **C-4: prepareForTermination race** — 终止流程与后台任务存在竞态，信号量未正确等待。

**HIGH 级别：**

- **P-2: trigram dedup** — trigram 列表存在重复条目，浪费内存和搜索时间。
- **P-3: recordsAtIndices N+1 lock** — 每条记录单独获取锁，产生 N+1 锁竞争。
- **P-4: NSNumber boxing** — 频繁创建 NSNumber 对象产生不必要的堆分配。
- **P-5: spin-wait** — 忙等待消耗 CPU 时间片。
- **P-6: @Published on isLoadingMore** — 不必要的 SwiftUI 发布导致频繁重绘。

## 修复/实现方案

- **C-1**: 扩展 `shared_lock` 作用域，覆盖整个查询生命周期。
- **C-2**: 调整为先写 base 文件再删除 WAL，保证崩溃一致性。
- **C-3**: 引入 `shared_mutex` + `safeContentIndex` 包装，消除撕裂读。
- **C-4**: 使用 `dispatch_semaphore` 等待后台任务完成后再终止。
- **P-2**: 对 trigram 列表执行 `sort + unique` 去重。
- **P-3**: 批量获取索引记录，单次加锁。
- **P-4**: 消除 NSNumber 装箱，直接使用原生类型。
- **P-5**: 将 spin-wait 替换为 `dispatch_semaphore`。
- **P-6**: 移除 `isLoadingMore` 的 `@Published` 属性。

## 影响的文件

- `MacEverything/Core/FileDatabase.h` / `.cpp`
- `MacEverything/Core/ContentIndexPersistence.h` / `.cpp`
- `MacEverything/Bridge/SearchBridge.mm`
- `MacEverything/App/SearchViewModel.swift`

## 测试覆盖

- 并发查询 + 压缩同时执行的竞态测试
- WAL 崩溃恢复一致性测试
- trigram 去重正确性验证
- 批量索引访问性能基准测试

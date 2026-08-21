# 032 - PathTable 线程安全与 WAL 互斥锁

- **类型**: bugfix
- **严重级别**: P0-CRITICAL
- **日期**: 2026-04-15
- **Commits**: `9f5a669`, `5cdfd76` (merge)

## 问题描述

两个严重的并发安全问题可能导致崩溃或未定义行为：

1. **P0-1**: `resolveRecordPath()` 返回引用类型，在并发 compaction 期间可能产生悬空引用（dangling reference）
2. **P0-2**: `wal_` shared_ptr 被 `compact()` (GCD 线程) 和 `attachWAL()` (主线程) 并发访问，缺少同步保护

## 根因分析

**P0-1**: `resolveRecordPath()` 返回 `const std::string&`，调用方持有引用期间，compaction 线程可能替换底层 PathTable，导致引用指向已释放内存。此外，`pathTable()` 方法为 public，外部可直接获取内部状态引用，绕过同步机制。`PathTable::resolve()` 对越界索引缺乏防护。

**P0-2**: `wal_` 是 `shared_ptr`，在无互斥保护的情况下被多线程并发读写。`shared_ptr` 的引用计数操作虽是原子的，但对象本身的替换（赋值）不是线程安全的。

## 修复/实现方案

**P0-1**:
- `resolveRecordPath()` 改为返回 `std::string`（按值返回），内部持有 `shared_lock` 确保拷贝期间数据有效
- `pathTable()` 从 public 移至 private，新增 `pathTableSnapshot()` 方法返回快照
- `PathTable::resolve()` 增加越界检查，越界时返回空字符串

**P0-2**:
- 新增 `walMutex_` 互斥锁保护 `wal_` 的并发访问
- 模式与 `ContentIndexPersistence` 中已有的 WAL 互斥保护一致

## 影响的文件

- PathTable 相关头文件和实现文件
- `resolveRecordPath()` 所在文件
- WAL 管理相关文件

## 测试覆盖

- 并发场景下 resolveRecordPath 的正确性验证
- PathTable 越界访问返回空字符串的验证
- WAL 并发 attach/compact 不崩溃的验证

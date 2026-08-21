# 044 - WAL Rename 失败链修复

- **类型**: bugfix
- **严重级别**: P0-CRITICAL
- **日期**: 2026-04-15
- **Commit**: `24f135f`

## 问题描述

WAL 文件 rename 操作首次失败后，后续所有 compact 周期的 rename 都会永久失败（ENOENT），导致 WAL 无法压缩，持续膨胀，最终影响启动性能和磁盘空间。

## 根因分析

rename 失败后的清理逻辑存在致命缺陷：

1. 首次 rename 失败（任何原因）
2. 下一个 compact 周期开始时，对旧 WAL 对象调用 `closeAndDelete()`
3. 但旧 WAL 对象的 `path_` 指向 `.wal.new` 文件 — 而这个文件正是当前活跃 WAL 正在使用的文件
4. `closeAndDelete()` 删除了 `.wal.new`，导致当前 WAL 的底层文件被删除
5. 此后所有 rename 调用都因源文件不存在（ENOENT）而永久失败

这是一个级联故障：单次 rename 失败会触发不可逆的文件删除，使系统进入永久错误状态。

## 修复/实现方案

1. **替换 closeAndDelete() 为 close()**：对旧 WAL 仅关闭文件描述符，不删除文件。旧 WAL 的 inode 在 fd 关闭后自然变为无引用状态，由文件系统回收。
2. **使用 POSIX rename() 原子替换**：确保替换操作的原子性，避免中间状态。
3. **增强错误日志**：rename 失败时记录 errno 和 strerror，便于诊断。

## 影响的文件

- `MacEverything/Core/IndexWAL.cpp` — compact() 中的 WAL 替换逻辑
- `MacEverything/Core/IndexWAL.h` — 接口调整

## 测试覆盖

- rename 失败后连续 compact 周期的行为验证
- 确认不再出现 ENOENT 级联失败
- WAL 文件完整性在错误恢复后的验证

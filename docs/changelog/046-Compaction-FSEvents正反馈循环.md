# 046 - Compaction-FSEvents 正反馈循环

- **类型**: bugfix
- **严重级别**: P0-CRITICAL
- **日期**: 2026-04-15
- **Commit**: `aa773bf` (merge), branch commit: `62303cd`

## 问题描述

应用运行约 1 小时后，索引记录数从 4.3M 暴涨至 26.3M，最终因内存耗尽（OOM）崩溃。系统处于持续的高 CPU 和 I/O 负载状态。

## 根因分析

存在三层正反馈循环：

1. **Compaction 写入** → 产生大量文件系统变更事件
2. **FSEvents 接收变更** → 触发 `MustScanSubDirs` 事件
3. **MustScanSubDirs** → 触发全盘重扫 → 产生大量新记录
4. 新记录增加 → 触发更多 compaction → 回到步骤 1

每轮循环都会放大记录数量，形成指数级增长。Compaction 自身的文件写入操作成为了触发下一轮重扫的源头，构成不可收敛的正反馈。

## 修复/实现方案

三个层面切断反馈循环：

**Fix 1 — kFSEventStreamCreateFlagIgnoreSelf**：
- 使用内核级标志过滤本进程产生的文件系统事件
- 从源头消除 compaction 写入触发 FSEvents 的问题

**Fix 2 — 排除目录祖先过滤**：
- 过滤 `MustScanSubDirs` 事件中路径为已排除目录的祖先目录的情况
- 避免不必要的大范围重扫

**Fix 3 — 内联 compactRecords()**：
- 当 tombstone 比例超过 30% 时执行内联压缩
- 减少不必要的文件写入操作

## 影响的文件

- `MacEverything/Bridge/MacSearchBridge.mm` — FSEvents 流创建标志
- `MacEverything/Core/SearchEngine.cpp` — 事件过滤与内联压缩逻辑

## 测试覆盖

- 验证 IgnoreSelf 标志正确设置
- 排除目录祖先路径过滤逻辑测试
- 内联压缩触发条件与行为验证
- 长时间运行记录数稳定性验证

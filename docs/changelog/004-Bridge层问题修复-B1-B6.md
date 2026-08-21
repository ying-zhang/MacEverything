# 004 - Bridge 层问题修复 (B1-B6)

- **类型**: bugfix
- **严重级别**: P2-MEDIUM
- **日期**: 2026-04-14
- **Commit**: `ac1fb6d`

## 问题描述

Bridge 层（Objective-C++ 桥接）存在 6 个问题，涉及内存泄漏、数据一致性、线程安全等。

## 根因分析与修复方案

- **B1**: Scanner 进度定时器的 dispatch block 强引用 scanner → 改用 `weak_ptr` 防止延长 scanner 生命周期
- **B2**: `applyFSEvents:toEngine:` 未更新内容索引 → 添加内容索引增量更新以保持一致性
- **B3**: `saveIndexToFile` 使用裸 `saveToFile` 未保存 `IndexMetadata`（lastEventId、版本信息）→ 保留元数据
- **B4**: Bridge 层的 `recentIndices:` 方法逐条加锁 → 新增 `SearchEngine::recentIndices()` 批量方法减少锁获取
- **B5**: `setupContentPersistence` 中 `createDirectoryAtPath` 未检查错误 → 添加错误检查，失败时提前返回
- **B6**: BOOL 实例变量（`_isScanning`, `_isMonitoring`, `_isContentIndexing`, `_shuttingDown`）跨队列访问不安全 → 改为 `std::atomic<bool>`

## 影响的文件

- `MacEverything/Bridge/MacSearchBridge.mm`

## 测试覆盖

新增 Bridge 层回归测试，总测试数持续通过。

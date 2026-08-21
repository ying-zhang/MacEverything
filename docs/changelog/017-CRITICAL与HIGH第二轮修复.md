# 017 - CRITICAL/HIGH 第二轮修复

- **类型**: bugfix
- **严重级别**: P0-CRITICAL / P1-HIGH
- **日期**: 2026-04-14
- **Commit**: `ef9a15a` (merge)

## 问题描述

第一轮并发修复后，审计发现仍存在持久化层 WAL 删除顺序、线程安全指针管理、短关键词内容搜索等 CRITICAL 问题，以及多项 HIGH 级别的性能和健壮性缺陷。

## 根因分析

**CRITICAL：**

- **C-1**: `ContentIndexPersistence::compact()` WAL 删除顺序仍然不安全，崩溃窗口内可能丢失数据。
- **C-2/C-3**: 持久化层指针在多线程间以裸指针传递，缺乏线程安全的生命周期管理。
- **C-4**: 短关键词（1-2 字符）的内容搜索逻辑不正确，无法命中有效结果。

**HIGH：**

- **H-1/H-2**: Posting list 操作和 `rebuildRecentCache` 性能不佳。
- **H-3/H-4**: WAL 文件缺少 magic header，CRC 校验失败时日志信息不足。
- **H-6**: 测试缺少默认数据集。
- **H-7**: FSEvents 未处理卷卸载事件，导致监听悬挂。
- **H-8**: `onSearchTextChanged` 重复触发。
- **H-9**: `contentKeyword` 未缓存，重复计算。

## 修复/实现方案

- **C-1**: 修正 WAL 删除顺序为：写 base → fsync → 删 WAL。
- **C-2/C-3**: 使用线程安全的 `shared_ptr` 管理持久化对象。
- **C-4**: 修复短关键词内容搜索的 trigram 生成和匹配逻辑。
- **H-1/H-2**: 优化 posting list 合并和 recentCache 重建算法。
- **H-3/H-4**: 为 WAL 文件添加 magic header 校验，增强 CRC 错误日志。
- **H-6**: 添加默认测试数据集。
- **H-7**: 注册 FSEvents unmount 事件回调，安全释放资源。
- **H-8**: 去除重复的 `onSearchTextChanged` 调用。
- **H-9**: 缓存 `contentKeyword` 计算结果。

## 影响的文件

- `MacEverything/Core/ContentIndexPersistence.h` / `.cpp`
- `MacEverything/Core/FileDatabase.h` / `.cpp`
- `MacEverything/Core/TrigramIndex.h` / `.cpp`
- `MacEverything/Bridge/SearchBridge.mm`
- `MacEverything/App/SearchViewModel.swift`

## 测试覆盖

- WAL 崩溃恢复场景测试
- 短关键词内容搜索正确性测试
- posting list 合并性能基准
- FSEvents unmount 回调验证
- 默认测试数据集集成

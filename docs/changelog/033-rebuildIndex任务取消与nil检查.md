# 033 - rebuildIndex 任务取消与 nil 安全检查

- **类型**: bugfix
- **严重级别**: P1-HIGH
- **日期**: 2026-04-15
- **Commits**: `5afeaf1` (merge), `085cb62` (merge)

## 问题描述

1. **P1-1**: `rebuildIndex()` 未取消正在执行的异步任务，旧回调覆盖新状态
2. **P1-2**: 包含非法 UTF-8 字节的路径导致 `stringWithUTF8String` 返回 nil，后续操作崩溃

## 根因分析

**P1-1**: `rebuildIndex()` 重建索引时，`searchTask`、`recentTask`、`indexChangeTask` 三个异步任务仍在执行。这些任务的回调持有旧的上下文，完成后会覆盖刚刚重建的新状态，导致数据不一致。

**P1-2**: macOS 文件系统可能包含非 UTF-8 编码的文件名。`[NSString stringWithUTF8String:]` 在输入包含非法 UTF-8 序列时返回 nil。`[NSMutableArray addObject:nil]` 会抛出异常导致崩溃；`rescanSubtree:nil` 导致 `std::string(NULL)` 构造，属于未定义行为（UB）。

## 修复/实现方案

**P1-1**:
- 在 `rebuildIndex()` 中显式取消 `searchTask`、`recentTask`、`indexChangeTask`
- 同时递增 `searchGeneration`，确保旧回调即使执行也会因 generation 不匹配而被丢弃

**P1-2**:
- 在所有 `stringWithUTF8String` 调用后增加 nil 检查
- `addObject:` 前验证对象非 nil
- `rescanSubtree:` 调用前验证参数非 nil

## 影响的文件

- `rebuildIndex()` 所在文件
- Bridge 层中使用 `stringWithUTF8String` 的文件

## 测试覆盖

- 验证 rebuildIndex 后旧任务回调不影响新状态
- 验证非法 UTF-8 路径不导致崩溃

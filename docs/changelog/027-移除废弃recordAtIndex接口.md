# 027 - 移除废弃的 recordAtIndex 接口

- **类型**: refactor
- **日期**: 2026-04-14
- **Commit**: `50e8701`

## 问题描述

`loadRecords` 执行后，`FileRecord.path` 被清空，路径信息仅通过 `PathTable` 存储。`recordAtIndex:` 方法始终返回空路径，且当前无任何调用方（Swift 层统一使用批量 API）。该方法已成为死代码，增加维护负担。

## 修复/实现方案

直接移除 `recordAtIndex:` 方法。由于 Swift 层已全面迁移至批量 API（batch APIs），不存在任何调用者，移除操作安全无副作用。

## 影响的文件

- Bridge 层中包含 `recordAtIndex:` 实现的文件

## 测试覆盖

- 确认编译通过，无调用方引用该方法
- 验证 Swift 批量 API 路径不受影响

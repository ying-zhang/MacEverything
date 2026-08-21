# 034 - 焦点变更冷却修复与 safeEngine 一致性

- **类型**: bugfix
- **严重级别**: P2-MEDIUM
- **日期**: 2026-04-15
- **Commits**: `c3676d3`, `726b0cd`

## 问题描述

1. 窗口获得焦点时触发刷新但未正确启动冷却机制，可能导致频繁刷新
2. `saveToFile` 绕过 `safeEngine` 访问器直接操作引擎，与其他调用点不一致，存在线程安全隐患

## 根因分析

**焦点冷却**: `focusChanged(true)` 直接调用 `doRefresh()` 但未设置 `isCooldownActive` 标志，也未调度冷却定时器（`scheduleCooldown()`）。这意味着在冷却窗口期内的后续事件不会被正确抑制，可能导致重复刷新。

**safeEngine**: `saveToFile` 是唯一一个绕过 `safeEngine` 访问器直接使用引擎指针的调用点。`safeEngine` 封装了线程安全的引擎访问逻辑，绕过它可能在并发场景下产生数据竞争。

## 修复/实现方案

**焦点冷却**:
- 在 `focusChanged(true)` 路径中设置 `isCooldownActive = true`
- 调用 `scheduleCooldown()` 启动冷却定时器

**safeEngine**:
- 将 `saveToFile` 中的直接引擎访问改为通过 `safeEngine` 访问器，与所有其他调用点保持一致

## 影响的文件

- 焦点变更处理相关文件
- `saveToFile` 所在文件

## 测试覆盖

- 验证窗口聚焦后冷却机制正确激活
- 验证冷却期内的重复事件被正确抑制
- 验证 saveToFile 通过 safeEngine 访问时行为正确

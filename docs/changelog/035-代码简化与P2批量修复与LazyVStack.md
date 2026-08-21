# 035 - 代码简化、P2 批量修复与 LazyVStack 布局修复

- **类型**: bugfix
- **严重级别**: P2-MEDIUM
- **日期**: 2026-04-15
- **Commits**: `9fc5407` (merge), `ff6595b` (merge), `9748559` (merge)

## 问题描述

三组独立问题的合并修复：

1. **代码简化**: 审查发现多处可简化的代码模式
2. **P2 批量修复**: 多个 P2 级别的安全和健壮性问题
3. **LazyVStack 布局异常**: 窗口最小化后恢复时，搜索结果列表出现行高错乱/间隙

## 根因分析

**代码简化**:
- `applySettings()` 中脏检查逻辑位置不合理
- Combine Timer 可用更简洁的 async `.task` 替代
- `isBinaryFile()` / `readFileContent()` 已无调用方，属于死代码

**P2 批量修复**:
- 文件计数缺乏上限约束（潜在的 50M 限制问题）
- `.app` 检测使用大小写敏感匹配，遗漏大写扩展名
- FSEvents 回调中使用原始指针捕获，存在悬空指针风险
- `NSWorkspace.open` 返回值未检查

**LazyVStack 布局**:
- 窗口最小化/恢复后，SwiftUI 的 LazyVStack 内部缓存的行高度数据被破坏。使用缓存的错误高度进行布局，导致可见行之间出现异常间隙。

## 修复/实现方案

**代码简化**:
- 将脏检查移至 `applySettings()` 顶部，提前返回
- 用 async `.task` 修饰符替换 Combine Timer
- 删除废弃的 `isBinaryFile()` 和 `readFileContent()`

**P2 批量修复**:
- 增加 50M 上限的计数边界检查
- `.app` 检测改为大小写不敏感
- FSEvents 回调中原始指针改为 `weakSelf` 弱引用
- 检查 `NSWorkspace.open` 返回值

**LazyVStack 布局**:
- 为行视图添加 `.fixedSize(horizontal: false, vertical: true)`，强制使用实际的内在高度而非缓存高度

## 影响的文件

- `ContentSettingsView.swift` 或相关设置视图
- FSEvents 回调相关文件
- 搜索结果列表视图文件
- `ContentIndex` 相关文件

## 测试覆盖

- 验证 applySettings 在配置未变时不触发重建
- 验证大小写不敏感的 .app 检测
- 验证窗口最小化恢复后列表布局正常

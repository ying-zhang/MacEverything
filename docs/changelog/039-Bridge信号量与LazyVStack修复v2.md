# 039 - Bridge 信号量修复与 LazyVStack v2 修复

- **类型**: bugfix
- **严重级别**: P1-HIGH
- **日期**: 2026-04-15
- **Commits**: `9833a8d` (merge), `0327ca0` (merge)

## 问题描述

1. **Bridge 信号量**: Bridge 层的信号量管理存在多个并发安全问题，可能导致死锁或资源泄漏
2. **LazyVStack v2**: 035 中的 `.fixedSize` 修复仅对可见行生效，屏幕外行的缓存高度仍然是损坏的，最小化恢复后滚动时仍会出现布局异常

## 根因分析

**Bridge 信号量**:
- 使用替换信号量（replacing semaphore）的方式管理并发。旧信号量被替换后，等待在旧信号量上的线程永远无法被唤醒
- 部分退出路径未调用 signal，导致等待线程永久阻塞
- `rebuildContentIndex` 在主线程执行，阻塞 UI
- 回调中使用原始指针捕获 self，存在悬空指针风险

**LazyVStack v2**:
- 035 中的 `.fixedSize(horizontal: false, vertical: true)` 修复仅影响当前可见的行。SwiftUI 的 LazyVStack 为离屏行维护独立的高度缓存，这些缓存在窗口最小化期间被损坏。当用户滚动到这些行时，仍然使用损坏的缓存高度进行布局。无法通过修改单个行的修饰符来清除 LazyVStack 的内部缓存。

## 修复/实现方案

**Bridge 信号量**:
- 用 generation 计数器替代信号量替换机制。每次操作递增 generation，回调通过比较 generation 判断是否过期
- 在所有退出路径上无条件调用 signal，消除死锁可能
- 将 `rebuildContentIndex` 移至后台队列执行，避免阻塞主线程
- 回调中原始指针捕获改为 `weakSelf`，防止悬空指针

**LazyVStack v2**:
- 为 ScrollView 分配 `scrollViewID`
- 监听 `didDeminiaturizeNotification`（窗口恢复通知），在收到通知时递增 `scrollViewID`
- ScrollView 的 identity 变化强制 SwiftUI 销毁并重建整个 ScrollView 实例，彻底清除所有缓存的行高度

## 影响的文件

- Bridge 层并发管理相关文件
- 搜索结果列表视图（ScrollView/LazyVStack）相关文件

## 测试覆盖

- 验证并发搜索请求不产生死锁
- 验证 rebuildContentIndex 不阻塞 UI
- 验证窗口最小化/恢复后滚动列表布局正确
- 验证 generation 过期的回调被正确丢弃

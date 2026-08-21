# 115 - 修复搜索栏幽灵文本对齐偏移

## 问题

搜索栏的幽灵文本（来自搜索历史的自动补全建议）与用户输入的文本在视觉上存在错位。幽灵文本显示在实际字符渲染位置的略偏左处，产生"重影"/重叠效果，导致 UI 看起来不正常。

## 根因

幽灵文本作为 SwiftUI `Text` 视图在 `ZStack` 中与 `HighlightedSearchField`（内部封装 `NSTextView` + `NSScrollView`）并列渲染。两套渲染系统的文本布局位置不同：

- `NSTextView` 会应用 `textContainerInset` 和 `lineFragmentPadding`（默认 5pt），使文本相对视图前缘产生偏移
- SwiftUI `Text` 从 ZStack 的精确前缘开始，没有相应的偏移量

这种固有的内边距不匹配意味着幽灵文本与实际文本永远无法完美对齐。

## 解决方案

将幽灵文本渲染从 SwiftUI 层移入 `NSTextView` 的 `draw(_:)` 方法中，使用与真实文本相同的 `textContainerInset` + `lineFragmentPadding` 偏移量。由于两者共享同一坐标系，因此保证了像素级精确对齐。

### 变更内容

1. **`HighlightedSearchField.swift`**
   - 为 `HighlightedSearchField` 添加 `ghostSuggestion: String?` 参数
   - 为 `HighlightedNSTextView` 添加 `ghostSuggestion` 属性并触发 `needsDisplay`
   - 在 `draw(_:)` 中添加幽灵文本绘制，使用与现有占位符相同的内边距计算
   - 在 `updateNSView()` 中将幽灵建议从 SwiftUI 传递给 NSTextView

2. **`ContentView.swift`**
   - 移除 ZStack 中的 SwiftUI `Text` 幽灵覆盖层
   - 移除不再需要的 ZStack 包装器
   - 将 `ghostSuggestion` 参数传递给 `HighlightedSearchField`

## 验证

- 在 master 上构建成功
- 应用启动后通过 HTTP API 响应搜索查询正常
- 幽灵文本现在渲染在与输入文本完全相同的位置

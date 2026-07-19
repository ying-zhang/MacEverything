# MacEverything 外观、行高与缩略图开发计划（plan-ui-v1）

## Context

MacEverything 是一个 macOS 文件搜索应用（纯 SwiftUI + AppKit 互操作）。当前 UI 无主题系统——完全依赖系统语义色（`Color.primary`/`.secondary`/`.accentColor`、`NSColor.labelColor`/`.controlBackgroundColor` 等）；搜索结果行高由 `ResultDensity` 枚举控制（compact 28pt / comfortable 38pt）；文件图标由 `FileIconCache`（NSCache，500 项）通过 `NSWorkspace` 提供扩展名级图标，无内容缩略图。

本计划为应用增加三大 UI 功能，所有设置使用 `UserDefaults` 持久化并即时生效，不触发索引重建。

---

## 概要

1. **浅色/深色主题 + 自定义颜色/字体** — 跟随系统 / 浅色 / 深色三种模式；浅色与深色各自保存独立的背景色和正文色；全局可选字体族和字号。
2. **数值行高** — 将 `ResultDensity` 替换为 `24–80 pt` 连续行高，常规与内容搜索结果统一。
3. **缩略图显示** — 使用 `QLThumbnailGenerator` 为可见行异步获取文件内容缩略图，默认关闭，失败时回退系统图标。

---

## 第一步：AppSettings 扩展与数据迁移

**文件**：`AppSettings.swift`

### 新增枚举

```swift
enum AppearanceMode: String, CaseIterable, Codable, Identifiable {
    case system, light, dark
    var id: String { rawValue }
    var title: String { ... } // L10n 中英文
}
```

### 新增 UserDefaults Key 与 @Published 属性

| 属性 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `appearanceMode` | AppearanceMode | `.system` | 主题模式 |
| `lightBackgroundColor` | String | `""` | 浅色背景 sRGB RGBA，空=系统默认 |
| `darkBackgroundColor` | String | `""` | 深色背景 sRGB RGBA |
| `lightTextColor` | String | `""` | 浅色正文色 |
| `darkTextColor` | String | `""` | 深色正文色 |
| `fontFamily` | String | `""` | 字体族名称，空=系统默认 |
| `fontSize` | CGFloat | `13.0` | 正文字号，`clamped(10, 24)` |
| `resultRowHeight` | CGFloat | `38.0` | 行高，`clamped(24, 80)` |
| `showThumbnails` | Bool | `false` | 缩略图开关 |

遵循现有 `didSet { save(...) }` 模式。`fontSize`/`resultRowHeight` 用现有 `clamped()` 验证。复用 `save(_ value: Double, _ key: String)` 存储 CGFloat。

### Schema Version 5 迁移

在 `migrateSettingsIfNeeded()` 中新增 `version < 5` 分支：将旧 `resultDensity` 映射为 `resultRowHeight`（compact→28, comfortable→38），保留旧 key 不删以便降级。最终 `defaults.set(5, ...)`.

### 辅助方法

- `resetAppearanceDefaults()` — 重置所有外观设置到默认值。
- `Color.init?(rgbaString:)` / `Color.rgbaString` — sRGB RGBA 字符串（`"r,g,b,a"` 逗号分隔 0–1 浮点）与 Color 互转。空串返回 nil。

---

## 第二步：ThemeManager

**新建文件**：`MacEverything/App/ThemeManager.swift`

`@MainActor ObservableObject`，单例 `static let shared`，发布以下解析值：

- `resolvedBackgroundColor: Color` — 用户自定义背景色，或系统默认 `Color(nsColor: .windowBackgroundColor)`
- `resolvedTextColor: Color` — 用户自定义正文色，或 `.primary`
- `bodyFont: Font` — 自定义字体+字号，或 `.system(size: fontSize)`
- `bodyNSFont: NSFont` — AppKit 版本
- `searchFieldNSFont: NSFont` — 自定义字体族 + 固定 26pt

### 响应机制

1. 监听 `AppSettings.shared.objectWillChange` → 调用 `resolve()`
2. KVO 观察 `NSApp.effectiveAppearance`（当 `appearanceMode == .system` 时需要跟随系统切换）

### resolve() 核心逻辑

1. 根据 `appearanceMode` 设置 `NSApp.appearance`（nil / .aqua / .darkAqua）
2. 通过 `NSApp.effectiveAppearance.bestMatch(from:)` 判断当前实际深浅
3. 选取对应的 bg/text 颜色字符串，解析为 Color，缺失时用系统默认
4. 构建字体：检查 `fontFamily` 是否在已安装列表中，缺失时回退系统字体

### 注入

在 `MacEverythingApp.body`、`AppDelegate.createSearchWindow()`、设置窗口中通过 `.environmentObject(ThemeManager.shared)` 注入。

---

## 第三步：视图层应用主题与行高

### ContentView.swift

- 引入 `@EnvironmentObject var theme: ThemeManager`
- 最外层 VStack 添加 `.background(theme.resolvedBackgroundColor)` 替换默认窗口背景
- 状态栏 `Color(nsColor: .controlBackgroundColor)` 保持不变（语义色不受自定义影响）
- 提示文字（"No results found" 等）保留 `.secondary`

### ResultRow.swift

引入 `@EnvironmentObject private var theme: ThemeManager`。

| 当前 | 替换为 |
|---|---|
| `let dense = settings.resultDensity == .compact` | 删除 |
| `.frame(height: dense ? 28 : 38)` | `.frame(height: settings.resultRowHeight)` |
| `dense ? 18 : 22`（图标尺寸） | `min(settings.resultRowHeight - 8, 32)` |
| `dense ? 2 : 5`（垂直 padding） | `max(2, (settings.resultRowHeight - 28) / 2)` |
| `nameFont: .subheadline` | `nameFont: theme.bodyFont` |
| `nameColor: .primary` | `nameColor: theme.resolvedTextColor` |
| `.font(.subheadline)` (ext/size/date) | `.font(theme.bodyFont)` |

路径色保留 `.secondary`（路径为次要信息）。选中/悬停/警告/强调色继续使用语义色。

### ContentResultRow.swift

同样引入 ThemeManager，替换：

| 当前 | 替换为 |
|---|---|
| `let dense = settings.resultDensity == .compact` | 删除 |
| `.font(.title3)`（文件名） | `.font(theme.bodyFont)` |
| `.foregroundColor(.primary)` | `.foregroundColor(theme.resolvedTextColor)` |
| `dense ? 20 : 24`（图标） | `min(settings.resultRowHeight - 8, 32)` |
| `dense ? 2 : 5`（padding） | 按行高计算 |
| `dense ? 1 : 2`（snippet行数） | `settings.resultRowHeight < 40 ? 1 : 2` |

ContentResultRow 当前无固定 height，需添加外层 `.frame(height:)` 或通过 padding 约束。当行高 < 40 且空间不足时隐藏 snippet。

### HighlightedSearchField.swift

搜索框保持 26pt 但使用自定义字体族。三处 `NSFont.systemFont(ofSize: 26)` 替换为 `ThemeManager.shared.searchFieldNSFont`：

- `makeNSView` 中 `textView.font = ...`
- `applyHighlighting` 中 `let font = ...`
- `draw` 中 ghost/placeholder 的 `.font:` 属性

由于 `NSViewRepresentable` 无法使用 `@EnvironmentObject`，直接引用 `ThemeManager.shared`。在 `updateNSView` 中检测字体变化并刷新。

### TextHighlight.swift

不直接修改——`highlightCrossMatches` 和 `highlightMatches` 通过参数接收 font/color，变更全部在调用方（ResultRow/ContentResultRow）传参时完成。

---

## 第四步：缩略图服务

**新建文件**：`MacEverything/App/ThumbnailService.swift`

### ThumbnailService 类

`@MainActor ObservableObject`，单例模式，包含：

- `NSCache<NSString, NSImage>`：`countLimit = 300`, `totalCostLimit = 50MB`
- `QLThumbnailGenerator.shared` 实例
- `inFlightKeys: Set<String>` 并发去重
- `@Published revision: UInt64` 触发 SwiftUI 重绘

### 缓存键

`"标准化路径|modTime|pixelSize|screenScale"` — 文件修改后自动失效。

### thumbnail(for:modTime:pixelSize:) → NSImage?

1. 缓存命中 → 返回
2. 文件夹或 .app → 返回 nil（调用方用系统图标）
3. 并发去重检查
4. 发起 `QLThumbnailGenerator.Request(.thumbnail)` 异步请求
5. 回调中存入缓存、增加 revision
6. 返回 nil 表示缩略图尚未就绪

### cancelAll()

关闭缩略图时调用：清空 inFlightKeys + cache，触发 revision 刷新。

### ResultRow / ContentResultRow 集成

在 `fileIcon(for:)` 方法中，当 `settings.showThumbnails && item.type != 2 && !item.name.hasSuffix(".app")` 时，先尝试 `ThumbnailService.thumbnail(...)`，有结果则用，否则 fallback 到 `FileIconCache.icon(...)`。

ContentResultRow 的 `ContentFileItem` 缺少 `modTime`，可用 `FileManager.attributesOfItem` 获取，或暂缓 ContentResultRow 的缩略图支持，优先实现 ResultRow。

### showThumbnails didSet

关闭时调用 `ThumbnailService.shared.cancelAll()` 释放资源。

---

## 第五步：设置界面更新

**文件**：`GeneralSettingsView.swift`

### 新增 "外观" 标签页

在 TabView 中添加（位于 "Search & Results" 之后）：

- **主题模式**：三段 Picker（跟随系统 / 浅色 / 深色）
- **浅色配色 GroupBox**：背景色 ColorPicker + 正文色 ColorPicker
- **深色配色 GroupBox**：同上
- **字体族选择器**：`NSFontManager.shared.availableFontFamilies` 下拉，首项"系统默认"（空串）
- **字号滑块**：10–24 pt
- **实时预览文字**：使用当前主题设置渲染示例文本
- **恢复默认外观按钮**

ColorPicker 需要 `String ↔ Color` 双向 Binding（通过 rgbaString 转换）。

### 替换 "结果密度" 选择器

在 `resultsSection` 中：删除 `ResultDensity` Picker，替换为 `BoundedIntegerControl`（复用现有组件，CGFloat→Int 双向绑定），范围 24–80，步进 2。

### 缩略图开关

在 `resultsSection` 行高控制之后添加 Toggle + 资源警告提示文字。

### 窗口尺寸

当前 `.frame(width: 720, height: 620)`，增加标签页后可能需调整。

---

## 第六步：本地化

在 `Localizable.strings` (en / zh-Hans) 中新增：Appearance / Theme / Follow System / Light / Dark / Light Mode Colors / Dark Mode Colors / Background / Text / Font Family / System Default / Font Size / Reset Appearance Defaults / Result Row Height / Show file thumbnails / 缩略图资源警告 / 预览示例文字。

---

## 文件变更清单

| 文件 | 操作 | 说明 |
|---|---|---|
| `AppSettings.swift` | 修改 | 新增属性、Key、迁移 v5、重置、颜色编解码 |
| `ThemeManager.swift` | **新建** | 主题解析器，系统外观监听 |
| `ThumbnailService.swift` | **新建** | QL 缩略图服务，NSCache + 并发去重 |
| `ContentView.swift` | 修改 | 注入 ThemeManager，应用自定义背景色 |
| `ResultRow.swift` | 修改 | 行高/字体/颜色替换，缩略图集成 |
| `ContentResultRow.swift` | 修改 | 行高/字体/颜色替换 |
| `HighlightedSearchField.swift` | 修改 | 搜索框字体随主题更新 |
| `GeneralSettingsView.swift` | 修改 | 外观标签页，行高控制，缩略图开关 |
| `MacEverythingApp.swift` | 修改 | 注入 ThemeManager |
| `AppDelegate.swift` | 修改 | createSearchWindow 注入 ThemeManager |
| `Localizable.strings` (en/zh) | 修改 | 新增本地化字符串 |

## 实施顺序

1. AppSettings 扩展（数据基础）
2. ThemeManager 创建
3. 行高替换（ResultRow + ContentResultRow）— 可与步骤 2 并行
4. 主题应用到视图层（ContentView + ResultRow + ContentResultRow + HighlightedSearchField）
5. 缩略图服务 — 可与步骤 4 并行
6. 设置界面更新
7. 本地化
8. 测试验证

## 测试计划

- **单元测试**：颜色编解码往返一致性、字号/行高边界夹取、旧密度迁移、缺失字体回退、缩略图缓存键唯一性、并发去重、resetAppearanceDefaults。
- **UI 测试**：三种主题即时切换、跟随系统、明暗独立配色、字体/字号变更、行高非法输入修正、缩略图开关。
- **手工验证**：快速滚动缩略图无卡顿无串图；关闭缩略图后内存回落；损坏/无权限/网络卷文件回退；多窗口同步更新；多屏不同缩放倍率。
- **CI**：运行现有 Swift 测试 + XCUITest + Release xcodebuild。

## 验收标准

- 默认：跟随系统主题，系统背景色/正文色/字体，13 pt，38 pt 行高，缩略图关闭。
- 修改任何设置后所有窗口即时更新，重启保持。
- 浅色/深色自定义颜色互不覆盖。
- 行高 24–80 pt、字号 10–24 pt，无效输入不写入持久化。
- 缩略图未就绪/失败/不支持时始终显示系统图标，无空白占位。
- 不改变搜索引擎、HTTP API、MCP 接口或索引格式。

---

**首个实施步骤**：将本计划保存到仓库根目录 `plan-ui-v1.md`。

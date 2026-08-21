# 055 - XCUITest UI 测试目标

## 变更概述

为 MacEverything 添加 XCUITest UI 测试目标，实现自动化 UI 测试基础设施。

## 规划

### 目标
- 在 SwiftUI 视图上添加 accessibility identifiers，使 XCUITest 能够定位 UI 元素
- 创建 `MacEverythingUITests` 测试目标，包含 7 个基础 UI 测试用例
- 修改 `project.pbxproj` 添加完整的 UI 测试 target 配置

### 风险评估
- 仅添加 `.accessibilityIdentifier()` 修饰符，不影响现有 UI 行为和布局
- 新增独立 test target，不影响主 target 的编译和运行

## 实施内容

### Step 1: 添加 Accessibility Identifiers

在以下 SwiftUI 视图中添加了 `.accessibilityIdentifier()` 修饰符：

**ContentView.swift:**
- `searchField` — 搜索输入框
- `clearButton` — 清除按钮
- `statusBar` — 状态栏 HStack
- `indexedCount` — "files indexed" 文本
- `matchCount` — "matches" 文本
- `contentResultsList` — 内容搜索结果 ScrollView
- `noResultsLabel` — "No results found" 文本
- `fileResultsList` — 文件搜索结果 ScrollView

**ResultRow.swift:**
- `resultRow` — 文件结果行外层容器

**ContentResultRow.swift:**
- `contentResultRow` — 内容结果行外层容器

### Step 2: 创建 UI 测试文件

新建 `MacEverythingUITests/MacEverythingUITests.swift`，包含 7 个测试用例：

| 测试方法 | 验证内容 |
|---------|---------|
| `testAppLaunchesSuccessfully` | 主窗口和搜索框存在 |
| `testSearchProducesResults` | 输入搜索词后出现结果行 |
| `testClearSearchField` | 点击清除按钮后搜索框清空 |
| `testContentSearch` | `infile:` 前缀触发内容搜索 |
| `testRapidTyping` | 快速连续输入不导致崩溃 |
| `testResultCountDisplayed` | 搜索后显示匹配数量 |
| `testStatusBarShowsIndexedCount` | 扫描完成后显示索引文件数 |

### Step 3: 更新 project.pbxproj

添加完整的 `MacEverythingUITests` native target 配置：
- PBXBuildFile, PBXContainerItemProxy, PBXFileReference
- PBXFrameworksBuildPhase, PBXResourcesBuildPhase, PBXSourcesBuildPhase
- PBXGroup, PBXNativeTarget, PBXTargetDependency
- XCBuildConfiguration (Debug + Release), XCConfigurationList
- 产品类型: `com.apple.product-type.bundle.ui-testing`
- 使用 B-prefix UUID 系列

## 构建验证

- 主 target `MacEverything` Release 构建: **BUILD SUCCEEDED**
- UI 测试 target `MacEverythingUITests` Debug 构建: **BUILD SUCCEEDED**

## 影响范围

- 新增文件: `MacEverythingUITests/MacEverythingUITests.swift`
- 修改文件: `MacEverything/App/ContentView.swift`, `MacEverything/App/ResultRow.swift`, `MacEverything/App/ContentResultRow.swift`, `MacEverything.xcodeproj/project.pbxproj`
- 对现有功能无任何影响，仅增加测试基础设施

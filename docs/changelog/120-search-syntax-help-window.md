# 120 - Search Syntax Help Window

## 概述

新增"搜索语法帮助"窗口，用户可通过帮助菜单或状态栏菜单访问，展示所有支持的搜索语法文档。

## 变更内容

### 新建文件

- **`MacEverything/App/SearchSyntaxHelpView.swift`**
  - 纯 SwiftUI 视图，使用 `ScrollView` + `VStack` 布局
  - 15 个语法分区：基础搜索、布尔运算、通配符、正则、扩展名过滤、大小过滤、类型过滤、路径过滤、日期过滤、名称长度、匹配修饰符、文件类型宏、结构化路径、内容搜索、波浪号展开
  - 三个私有辅助组件：`SyntaxSection`（分组容器）、`SyntaxRow`（语法+说明行）、`SyntaxNote`（提示文本）

### 修改文件

- **`MacEverything/App/MacEverythingApp.swift`**
  - 添加 `CommandGroup(replacing: .help)` 替换默认帮助菜单，放入 "Search Syntax Help" 按钮
  - 快捷键 `Cmd+Shift+/`（即 `Cmd+?`）
  - 新增 `SearchSyntaxHelpWindowController` 单例，复用现有窗口控制器模式
  - 窗口尺寸 580×720，可调整大小，最小 450×400

- **`MacEverything/App/AppDelegate.swift`**
  - 状态栏菜单添加 "Search Syntax Help..." 入口
  - 新增 `openSearchSyntaxHelp()` 操作方法

- **`MacEverything.xcodeproj/project.pbxproj`**
  - 添加 `SearchSyntaxHelpView.swift` 的 PBXBuildFile、PBXFileReference、PBXGroup 和 Sources build phase 引用

## 访问方式

1. 菜单栏 → Help → Search Syntax Help（快捷键 `Cmd+?`）
2. 状态栏图标 → Search Syntax Help...

## 验证

- 构建成功（BUILD SUCCEEDED）
- 应用启动正常，HTTP 搜索服务无回归
- UI 功能需手动验证：帮助窗口弹出、内容可滚动、窗口可调整大小

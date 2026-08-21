# 064 - 支持启动后最小化

## 背景

MacEverything 启动时总是显示主窗口。在以下场景中，这不是期望的行为：
1. **Login Item 自动启动**：开机自启时弹出窗口会打扰用户
2. **脚本/自动化启动**：如 CI 测试、Claude 启动 app 进行功能验证时，不希望窗口弹到前台

## 方案设计

在 `AppDelegate.applicationDidFinishLaunching` 中增加启动模式检测，满足以下任一条件时，启动后隐藏主窗口：

1. **命令行参数 `--minimized`**：通过 `CommandLine.arguments.contains("--minimized")` 检测
2. **Login Item 启动**：通过 `NSAppleEventManager.shared().currentAppleEvent` 检测 `keyAEPropData == "com.apple.loginwindow"`

隐藏方式：在获取到 SwiftUI 创建的主窗口后，调用 `window.orderOut(nil)` + `NSApp.hide(nil)`。

## 修改文件

- `MacEverything/App/AppDelegate.swift`
  - `applicationDidFinishLaunching`：增加 `shouldStartMinimized` 判断，条件满足时隐藏窗口
  - 新增 `shouldStartMinimized()` 静态方法：封装命令行参数和 Login Item 两种检测逻辑

## 使用方式

```bash
# 正常启动（显示窗口）
open MacEverything.app

# 最小化启动（隐藏到菜单栏）
open MacEverything.app --args --minimized
```

用户可通过全局快捷键（默认 Option+Space）或菜单栏图标随时唤起窗口。

## 验收结果

- 构建成功
- `--minimized` 启动后窗口未显示，菜单栏图标正常
- HTTP 服务（localhost:19860）正常响应搜索请求

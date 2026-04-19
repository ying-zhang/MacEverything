# MacEverything

> macOS 上的极速文件搜索工具 — 灵感源自 Windows 上的 [Everything](https://www.voidtools.com/)

MacEverything 可以在数秒内索引整个 macOS 磁盘上的数百万文件，并在毫秒级响应时间内返回搜索结果。它常驻菜单栏，通过全局快捷键（默认 `Option+Space`）随时召唤搜索窗口，像 Alfred/Spotlight 一样即搜即得，但更快、更专注。

## 功能特性

**文件名搜索**
- Trigram 索引加速，3 字符以上查询避免全量扫描
- 4 级相关性排序：精确匹配 > 前缀匹配 > 包含匹配 > 仅路径匹配
- GCD 多核并行查询，通常 < 100ms 返回结果
- 支持最多 10,000 条结果分页懒加载

**文件内容搜索**
- 搜索框输入 `infile:` 前缀触发全文检索
- 基于 Trigram 的倒排索引 + FNV-1a 内容哈希做增量更新
- 可配置索引的文件类型和最大文件大小
- 搜索结果附带关键词高亮上下文片段

**实时监控**
- 基于 FSEvents 的文件级事件监听，索引自动保持最新
- WAL (Write-Ahead Log) + CRC32 校验，崩溃安全持久化
- 启动时从上次事件 ID 增量追赶，无需重建索引

**界面与交互**
- 全局快捷键唤出搜索窗口（可自定义）
- 搜索历史 Ghost Text 自动补全（Tab 接受）
- 右键菜单：打开、在 Finder 中显示、复制路径
- 拖放支持、开机自启、Full Disk Access 引导

## 技术架构

```
┌─────────────────────────────────┐
│       SwiftUI App Layer         │  界面、ViewModel、交互
├─────────────────────────────────┤
│    Objective-C++ Bridge Layer   │  零开销互操作
├─────────────────────────────────┤
│       C++20 Core Engine         │  扫盘、索引、搜索、持久化
└─────────────────────────────────┘
```

- **Core** (`MacEverything/Core/`)：纯 C++20 引擎。使用 `getattrlistbulk` 批量读取文件属性，多线程工作窃取式扫描，Trigram 倒排索引，PathTable 路径去重，WAL + 原子 rename 持久化。
- **Bridge** (`MacEverything/Bridge/`)：Objective-C++ 桥接层，避免 NSNumber/NSDictionary 装箱开销。
- **App** (`MacEverything/App/`)：SwiftUI 界面层，MVVM 架构，80ms/300ms 分级防抖。

## 构建

**环境要求**：macOS 13+、Xcode 15+

```bash
# 构建 Release 版本
xcodebuild -project MacEverything.xcodeproj -scheme MacEverything \
  -configuration Release build SYMROOT=build

# 打包 DMG
hdiutil create -volname MacEverything \
  -srcfolder build/Release/MacEverything.app \
  -ov -format UDZO MacEverything.dmg
```

## 测试

```bash
# 运行快速单元测试
make test

# 运行慢速集成测试
make test-slow
```

测试文件位于 `tests/` 目录，每个测试模块为独立的 `.h` 文件，通过 `test_all.cpp` 统一引入。

## 使用方式

1. 首次启动时，按照提示授予 **Full Disk Access** 权限
2. 等待初始扫描完成（进度会在状态栏显示）
3. 按 `Option+Space`（或自定义快捷键）唤出搜索窗口
4. 输入文件名关键词即时搜索；输入 `infile:关键词` 搜索文件内容
5. 双击打开文件，右键查看更多操作

## 项目结构

```
MacEverything/
├── Core/                  # C++20 核心引擎
│   ├── SearchEngine       # 搜索引擎（Trigram 索引 + 并行查询）
│   ├── DirectoryScanner   # 多线程文件扫描器
│   ├── ContentIndex       # 文件内容倒排索引
│   ├── IndexPersistence   # WAL + 自动压缩持久化
│   ├── FileSystemWatcher  # FSEvents 实时监控
│   ├── PathTable          # 路径字符串去重表
│   └── ...
├── Bridge/                # Objective-C++ 桥接层
│   └── MacSearchBridge    # C++ → Swift 互操作
├── App/                   # SwiftUI 应用层
│   ├── ContentView        # 主界面
│   ├── SearchViewModel    # 搜索视图模型
│   ├── HotkeyManager     # 全局快捷键
│   └── ...
└── tests/                 # 40+ 测试模块
```

## 性能设计亮点

- **`getattrlistbulk`**：单次系统调用批量获取文件属性，避免逐文件 stat
- **Trigram 倒排索引**：亚线性搜索复杂度，百万级文件毫秒级响应
- **Generation 计数器**：快速输入时自动取消过时查询，每 1024 次迭代检查
- **PathTable 路径去重**：目录路径 intern 化，记录仅存 uint32 索引，大幅降低内存占用
- **APFS Firmlink 去重**：正确处理 macOS Data/System 卷合并产生的目录环路
- **Thread-local Bitmap**：内容索引 trigram 提取使用 2^24 位图，O(1) 去重

## 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE) 文件。

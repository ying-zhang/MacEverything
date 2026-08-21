# 060 — ServiceEngine 提取 + CLI Daemon

## 概述

将 ~850 行编排逻辑从 ObjC++ Bridge (`MacSearchBridge.mm`) 提取为纯 C++ `ServiceEngine` 类，Bridge 瘦身为类型转换层，新增 CLI daemon 目标。

## 动机

1. CLI 版本无法复用 Bridge 层中的编排逻辑（启动流程、FSEvents 处理、rescan 防抖、内容索引管理、关闭流程）
2. Bridge 层过于厚重 — 80% 是核心逻辑而非 ObjC/Swift 类型转换

## 架构

```
┌─────────────┐     ┌──────────────┐
│  GUI (Swift) │     │  CLI daemon  │
│  + 瘦Bridge  │     │  (纯C++)     │
└──────┬───────┘     └──────┬───────┘
       │                     │
       └──────┬──────────────┘
              │
    ┌─────────▼──────────┐
    │  ServiceEngine(C++) │
    │  + SearchEngine     │
    │  + DirectoryScanner │
    │  + FileSystemWatcher│
    │  + ContentIndex     │
    │  + Persistence      │
    │  + HttpServer       │
    └────────────────────┘
```

## 实施阶段

### Phase 0: HttpServer 引擎间接引用

**问题**：HttpServer 持有 `shared_ptr<SearchEngine>` 拷贝，engine 重建后引用过期。

**方案**：改为持有 `std::function` getter，每次请求时调用获取最新 engine。

- 修改 `HttpServer.h/.cpp` — `start()` 参数改为 getter functions
- 修改 `MacSearchBridge.mm` — 适配新接口
- 新增 `tests/test_http_engine_swap.h` (Part 37) — 验证 engine 替换后 HttpServer 使用新 engine

### Phase 1-3: ServiceEngine 核心

创建纯 C++ `ServiceEngine` 类，从 Bridge 迁移全部编排逻辑：

- **Phase 1**: 状态管理与生命周期 — 构造/析构、startFullScan、startIncremental、shutdown
- **Phase 2**: FSEvents 处理与 rescan 防抖 — applyFSEvents、startMonitoring、scheduleRescanForPaths
- **Phase 3**: 内容索引管理 — startContentIndexing、rebuildContentIndex、setupContentPersistence

新增文件：
- `MacEverything/Core/ServiceEngine.h` — 类声明、ServiceConfig、回调类型
- `MacEverything/Core/ServiceEngine.cpp` — 核心实现
- `MacEverything/Core/PathUtils.h` — 平台工具函数（getDefaultCachePath 等）
- `tests/test_service_engine.h` (Part 38) — 构造、扫描、查询、关闭测试

### Phase 4: Bridge 瘦身

将 MacSearchBridge 从直接操作 C++ 对象改为转发到 ServiceEngine：

- `MacSearchBridge_Internal.h` — 移除所有 C++ ivar，改为持有 `shared_ptr<ServiceEngine>`
- `MacSearchBridge.mm` — 所有编排方法改为转发
- `MacSearchBridge+Content.mm` — 内容相关方法改为转发

瘦身后 Bridge 职责仅为：ObjC singleton、NSString/NSArray 类型转换、MEFileResult 对象构造、main queue 回调包装。

### Phase 5: CLI Daemon

新增 `MacEverything/CLI/daemon_main.cpp` (~195 行)：

- 解析命令行参数 (`--port`, `--root`, `--cache-dir`, `--log-dir`)
- 创建 ServiceEngine，设置 AdminCallbacks
- GCD dispatch source 处理 SIGTERM/SIGINT 信号
- `dispatch_main()` 进入 GCD 主事件循环

构建：`make daemon` 生成 `maceverything-daemon` 二进制。

新增 `tests/test_daemon_startup.h` (Part 39)：
- fork+exec 启动 daemon
- 验证 `/api/health` 返回 200
- 验证 `/api/status` 返回 200
- 发送 SIGTERM 验证优雅关闭
- 扩展超时以兼容 EDR/杀毒软件对新编译二进制的云扫描延迟

## 测试

所有 10,705 个测试通过，包括新增的：
- Part 37: HttpServer engine swap (4 tests)
- Part 38: ServiceEngine lifecycle (7 tests)
- Part 39: CLI daemon startup (4 tests)

lint-bridge 通过（Bridge ObjC++ 语法检查）。

## 修改的文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/HttpServer.h` | start() 参数改为 getter functions |
| `MacEverything/Core/HttpServer.cpp` | 9 处引用改为 getter 调用 |
| `MacEverything/Core/ServiceEngine.h` | 新增 |
| `MacEverything/Core/ServiceEngine.cpp` | 新增 |
| `MacEverything/Core/PathUtils.h` | 新增 |
| `MacEverything/CLI/daemon_main.cpp` | 新增 |
| `MacEverything/Bridge/MacSearchBridge.h` | 接口不变 |
| `MacEverything/Bridge/MacSearchBridge_Internal.h` | ivar 改为 ServiceEngine |
| `MacEverything/Bridge/MacSearchBridge.mm` | 瘦身为转发层 |
| `MacEverything/Bridge/MacSearchBridge+Content.mm` | 瘦身为转发层 |
| `Makefile` | 新增 daemon 目标 |
| `test_all.cpp` | 新增 Part 37-39 |
| `.gitignore` | 新增 maceverything-daemon |
| `tests/test_http_engine_swap.h` | 新增 |
| `tests/test_service_engine.h` | 新增 |
| `tests/test_daemon_startup.h` | 新增 |

## 风险与缓解

- **EDR 延迟**: Alibaba EDR 安全软件对新编译二进制有 5-10 秒云扫描延迟，测试超时已扩展到 20 秒
- **多实例冲突**: InstanceLock 确保 GUI 和 CLI 不同时写入同一索引
- **GCD 生命周期**: ServiceEngine 使用 shared_ptr + dispatch_group 管理异步操作生命周期

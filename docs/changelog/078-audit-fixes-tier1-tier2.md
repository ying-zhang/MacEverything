# 078 - 项目审计修复（Tier 1 + Tier 2）

## 概述

对 MacEverything 全项目进行深度审计后，实施了 11 项修复，涵盖安全、正确性和性能三个维度。

## 审计流程

1. 全面审计产出 28+ 发现项
2. 二次评审（subagent review）剔除 10 项过度审计、降级 6 项严重度被夸大的项
3. 最终保留 14 项，分三个 Tier 实施（Tier 3 为重构项，本次不单独排期）

## Tier 1 修复（8 项，每项 1-4 行改动）

### M1: ServiceEngine 锁类型修复
- **文件**: `ServiceEngine.cpp`
- **问题**: `startHttpServer()`/`stopHttpServer()` 使用 `shared_lock` 保护写操作（创建/停止 httpServer_），可导致数据竞争
- **修复**: `std::shared_lock` → `std::unique_lock`

### H4: 移除 CORS 通配符
- **文件**: `HttpServer.cpp`
- **问题**: `Access-Control-Allow-Origin: *` 允许任意网页通过 JS 读取本地文件列表，构成隐私泄露
- **修复**: 删除该 header（SwiftUI 通过 C++ bridge 调用，不需要 CORS）

### H5: 添加编译警告标志
- **文件**: `Makefile`
- **问题**: `CXXFLAGS` 无任何警告标志
- **修复**: 添加 `-Wall -Wextra`

### H6: 默认测试集补全
- **文件**: `test_all.cpp`
- **问题**: 默认 `./test_all` 和 `--fast` 缺少 Parts 47, 48, 49
- **修复**: 在默认集合和 fast 集合中加入

### M4: walMutex_ mutable 修复
- **文件**: `IndexPersistence.h`, `IndexPersistence.cpp`
- **问题**: `const_cast<std::mutex&>(walMutex_)` 绕过 const — 技术上 UB
- **修复**: 声明为 `mutable std::mutex walMutex_`，删除 const_cast

### M17: 删除未使用的 weakEngine
- **文件**: `ServiceEngine.cpp`
- **问题**: `auto weakEngine = std::weak_ptr<SearchEngine>()` 声明后未使用
- **修复**: 删除

### M10: 添加 Sanitizer 构建目标
- **文件**: `Makefile`
- **问题**: 无 ASan/TSan 构建目标，项目有大量并发代码
- **修复**: 新增 `test-asan` 和 `test-tsan` 目标

### H1: HTTP Server recv 超时
- **文件**: `HttpServer.cpp`
- **问题**: `recv()` 无超时，慢连接可阻塞所有后续请求
- **修复**: 在 `handleConnection` 开头为 clientFd 设置 `SO_RCVTIMEO`（5 秒）

## Tier 2 修复（3 项，中等工作量）

### C1: ServiceEngine shutdown 生命周期保护
- **文件**: `ServiceEngine.cpp`
- **问题**: `dispatch_group_wait` 15 秒超时后析构继续，GCD block 可能持有悬空 this
- **修复**: 改为 `DISPATCH_TIME_FOREVER`（ServiceEngine 仅在进程退出时销毁）

### atoi → strtol 验证
- **文件**: `HttpServer.cpp`（3 处）
- **问题**: `std::atoi()` 对非数字输入返回 0，无法区分错误
- **修复**: 改用 `strtol` + `endptr` 验证，非法输入使用默认值

### H2: 查询热路径 bitmap 优化
- **文件**: `SearchEngine.cpp`
- **问题**: `vector<bool> isCandidate(totalSize, false)` 每次查询分配最多 3 次，totalSize 数百万时约 1.8MB 分配+清零
- **修复**: 引入 `thread_local ReusableBitmap` + dirty tracking，3 个分配点均替换为复用 bitmap

## 验证

- 10838 项测试全部通过
- Xcode Release 构建成功
- HTTP 功能验证通过（搜索正常，CORS header 已移除，非法参数正确处理）

## 未实施项（Tier 3，重构时一并处理）

- `computePriority()` 辅助函数提取
- `tombstoneRecord()` 提取
- SwiftUI 主线程调用调查

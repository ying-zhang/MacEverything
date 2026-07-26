# MacEverything 项目说明文档

> 从零开始理解 MacEverything 的完整指南

## 目录

1. [项目概述](#1-项目概述)
2. [架构总览](#2-架构总览)
3. [核心模块详解](#3-核心模块详解)
4. [性能分析](#4-性能分析)
5. [数据流与生命周期](#5-数据流与生命周期)
6. [持久化系统](#6-持久化系统)
7. [并发模型](#7-并发模型)
8. [HTTP API 与 MCP](#8-http-api-与-mcp)
9. [UI 层](#9-ui-层)
10. [构建与测试](#10-构建与测试)
11. [文件清单](#11-文件清单)

---

## 1. 项目概述

MacEverything 是 macOS 平台上的超快文件搜索工具，灵感来自 Windows 上的 [Everything](https://www.voidtools.com/)。它通过直接调用 macOS 底层 API（`getattrlistbulk`）进行全盘扫描，在内存中维护文件名和内容的倒排索引，提供毫秒级的搜索响应。

**核心目标**：
- 全盘扫描 < 10 秒（数百万文件）
- 搜索延迟 < 100ms
- FSEvents 实时监控，文件变更 < 1 秒感知
- 支持文件名搜索、路径搜索、glob 模式、正则匹配
- 支持文件内容全文搜索（`infile:` 前缀）

**技术栈**：C++20 核心引擎 + Objective-C++ 桥接层 + SwiftUI 界面层

---

## 2. 架构总览

```
┌─────────────────────────────────────────────────────┐
│                  SwiftUI App Layer                   │
│   ContentView · SearchViewModel · HotkeyManager     │
│   ResultRow · PermissionView · SettingsViews         │
├─────────────────────────────────────────────────────┤
│              Objective-C++ Bridge Layer              │
│         MacSearchBridge (singleton + categories)     │
├─────────────────────────────────────────────────────┤
│               C++20 Core Engine Layer                │
│  ┌──────────────────────────────────────────────┐   │
│  │           ServiceEngine (编排中枢)            │   │
│  │  ┌──────────┐  ┌──────────┐  ┌───────────┐  │   │
│  │  │Directory │  │ Search   │  │ Content   │  │   │
│  │  │Scanner   │  │ Engine   │  │ Index     │  │   │
│  │  └──────────┘  └──────────┘  └───────────┘  │   │
│  │  ┌──────────┐  ┌──────────┐  ┌───────────┐  │   │
│  │  │Index     │  │FileSystem│  │ Http      │  │   │
│  │  │Persist.  │  │Watcher   │  │ Server    │  │   │
│  │  └──────────┘  └──────────┘  └───────────┘  │   │
│  └──────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────┤
│            macOS APIs & System Layer                 │
│  getattrlistbulk · FSEvents · GCD · CoreFoundation  │
└─────────────────────────────────────────────────────┘
```

**层间通信**：
- App → Bridge：Objective-C 方法调用（`MacSearchBridge` 单例）
- Bridge → Core：C++ 直接调用（`ServiceEngine` 的 `shared_ptr`）
- Core 内部：GCD 队列调度 + `shared_mutex` 保护共享状态

**另有三个独立可执行程序**：
- **daemon**（`MacEverything/CLI/daemon_main.cpp`）：无头后台服务，用于服务器部署
- **mace**（`MacEverything/CLI/mace_main.cpp`）：短命令客户端，通过本机 HTTP API 查询正在运行的 GUI 应用
- **MCP Server**（`MacEverything/CLI/mcp_main.mm`）：Model Context Protocol 服务，使用 Foundation 结构化解析 JSON-RPC，供 Codex、Claude Code、Cursor 等工具调用

---

## 3. 核心模块详解

### 3.1 DirectoryScanner — 文件系统扫描器

**文件**：`MacEverything/Core/DirectoryScanner.h/.cpp`

**职责**：多线程遍历文件系统，收集所有文件/目录的元数据。

**扫描流程**：
1. 从根目录（默认 `/`）开始，将根目录推入工作队列
2. 4-32 个工作线程（按 `hardware_concurrency()` 自适应）从队列中抢任务
3. 每个线程用 `getattrlistbulk()` 批量读取目录条目，每次使用 1MB 缓冲区
4. 发现的子目录批量推回工作队列（单次加锁，降低锁争用）
5. 每个线程将结果存入独立的 `vector<FileRecord>`（预分配 50,000 条，无跨线程争用）
6. 扫描完成后，`takeResults()` 通过 move 迭代器合并所有线程结果

**关键优化**：
- **`getattrlistbulk` vs `readdir`+`stat`**：前者在单次系统调用中返回目录下所有条目的全部属性（名称、类型、大小、修改时间、inode、设备号），避免了 per-entry 的两次系统调用
- **APFS 固件链接去重**：通过 `InodeKey`（`dev_t` + `ino_t`）的 HashSet 防止重复遍历同一 inode
- **跨卷过滤**：记录根路径的 `dev_t`，跳过不同设备的挂载点（避免遍历网络挂载、autofs 等）
- **App Bundle 剪枝**：`.app` 目录标记为 `type=5`，不递归其内部文件（一个 .app 可包含数千文件）
- **autofs 安全**：检测 `MNTTYPE_AUTOFS` 挂载点，跳过可能阻塞的网络路径

**时间复杂度**：O(N)，N 为文件系统条目总数。受限于 I/O 而非 CPU。

### 3.2 SearchEngine — 搜索引擎

**文件**：`MacEverything/Core/SearchEngine.h/.cpp`（约 1527 行，项目最大文件）

**职责**：管理所有文件记录，提供高速搜索。

#### 3.2.1 核心数据结构

```
FileRecord (14 bytes + strings)
├── name: string       // 文件名（如 "main.cpp"）
├── path: string       // 父目录路径（加载后被 PathTable 替代，string 被释放）
├── type: uint8_t      // 0=墓碑, 1=文件, 2=目录, 3=符号链接, 5=App Bundle
├── size: uint64_t     // 文件大小（字节）
├── modTime: time_t    // 最后修改时间（秒级时间戳）
├── inode: uint64_t    // ATTR_CMN_FILEID
└── devId: int32_t     // ATTR_CMN_DEVID
```

**记录存储**：
- `records_`：`vector<FileRecord>`，平坦数组，下标即为 recordIndex
- `lowerNames_`：`vector<string>`，并行数组，每个元素是对应记录的小写文件名
- `pathIndices_`：`vector<uint32_t>`，并行数组，每个元素是对应记录在 PathTable 中的路径索引
- `pathIndex_`：`unordered_map<string, uint32_t>`，全路径 → recordIndex 的映射表，提供 O(1) 的路径查找

**PathTable（路径字符串驻留表）**：
```
┌─────────────────────────────────┐
│ PathTable                       │
│ paths_: ["/usr/bin", "/usr/lib",│
│          "/home/user/Desktop"]  │
│ lookup_: {"/usr/bin"→0,         │
│           "/usr/lib"→1, ...}    │
│                                 │
│ 数百万文件共享约数万个目录路径   │
│ 每条记录仅存储 4 字节的索引     │
└─────────────────────────────────┘
```

#### 3.2.2 Trigram 倒排索引

**核心思想**：将每个文件名拆分为所有连续 3 字符子串（trigram），建立从 trigram 到记录索引列表的倒排映射。

```
例：文件名 "readme.txt"
→ 小写: "readme.txt"
→ trigrams: "rea", "ead", "adm", "dme", "me.", "e.t", ".tx", "txt"
→ 每个 trigram 打包为 uint32_t（3 字节 → 低 24 位）
→ nameTrigramIndex_["rea"] → [idx1, idx2, ...]
   nameTrigramIndex_["ead"] → [idx1, idx3, ...]
```

**数据结构**：`unordered_map<Trigram, vector<uint32_t>>`，posting list 保持有序。

提取由 `TrigramExtraction.h` 的滚动三字节窗口完成。相邻 trigram 复用前两个
字节，每个输入字节只做一次 ASCII 小写转换；短输入采用局部排序去重，避免为
常见文件名随机访问 2 MiB bitmap。

**路径 Trigram 索引**（两级间接）：
```
pathTrigramIndex_: trigram → sorted vector<pathIdx>
pathIdxToRecords_: pathIdx → sorted vector<recordIdx>
```
路径索引利用 PathTable 的去重特性——约 80,000 个唯一路径远少于数百万条记录，避免了路径 trigram 的大量重复。

#### 3.2.3 搜索算法

`query()` 方法采用两阶段搜索：

**Phase 1 — Trigram 加速（关键词 ≥ 3 字符，非 glob）**：
1. 从关键词提取所有 trigram
2. 在 `nameTrigramIndex_` 中查找每个 trigram 的 posting list
3. 按 posting list 长度排序（最短优先）
4. 逐一求交集，得到候选集
5. 对每个候选做子串验证（消除 trigram 误匹配）
6. 使用 `__builtin_prefetch` 预取记录数据（提前 4 条，隐藏内存延迟）
7. 每 1024 次迭代检查 `queryGeneration_` 原子计数器，支持取消

posting-list 求交统一由 `PostingListIntersection.h` 处理：尺寸接近时使用低开销
双指针标量合并，列表长度比达到 32:1 时切换到 libc++ 的不均衡列表快速路径。

**Phase 2 — 路径补充扫描**：
1. 通过 `dispatch_apply`（GCD）并行线性扫描全部记录
2. 检查关键词是否出现在路径中（而非文件名）
3. 合并 Phase 1 和 Phase 2 结果

**结果排序**（4 级相关度）：
| 优先级 | 匹配类型 | 示例（搜索 "readme"） |
|--------|----------|----------------------|
| 0（最高） | 文件名精确匹配 | `readme` |
| 1 | 文件名前缀匹配 | `readme.md` |
| 2 | 文件名包含匹配 | `my-readme-file.txt` |
| 3（最低） | 仅路径匹配 | `/readme-project/config.json` |

同级内按完整路径长度升序（短路径优先）。当 `maxResults < 总匹配数` 时，使用 `partial_sort`（O(N·log K)）而非全排序（O(N·log N)）。

**短关键词和 glob 模式**：关键词 < 3 字符或包含 `*`/`?` 时，无法使用 trigram 索引，退化为并行线性扫描。

#### 3.2.4 增量变更

| 操作 | 方法 | 复杂度 |
|------|------|--------|
| 新增记录 | `addRecord()` | O(L)，L = 文件名长度（trigram 更新） |
| 删除记录 | `removeByPath()` | O(1) 查找（通过 `pathIndex_`） + O(L) trigram 移除 |
| 更新记录 | `updateByPath()` | 删除旧 + 添加新 |
| 批量重扫 | `batchRescanPrefix()` | O(K) 删除 + O(M) 添加 + 1 次 trigram 重建 |
| WAL 重放 | `replayWALEntries()` | O(E)，E = WAL 条目数，每条 O(1) 查找 |

#### 3.2.5 Compaction（压缩整理）

`compactRecords()` 使用 COW（Copy-on-Write）三阶段算法：

```
Phase 1 [shared_lock]：快照当前活跃记录（仅复制非墓碑记录）
                        ↓
Phase 2 [无锁]        ：在副本上构建新的记录数组、trigram 索引、
                        PathTable、pathIndex_（耗时最长，不阻塞查询）
                        ↓
Phase 3 [unique_lock] ：原子交换新旧数据，重放 Phase 2 期间的变更
```

**触发条件**：墓碑记录占比 > 25%（`kTombstoneCompactRatio = 0.25`）。

### 3.3 ContentIndex — 文件内容索引

**文件**：`MacEverything/Core/ContentIndex.h/.cpp`

**职责**：对文件内容建立 trigram 倒排索引，支持全文搜索。

**工作流程**：
1. 按配置的扩展名列表（默认：常见代码/文本文件）和最大文件大小（默认 1MB）过滤文件
2. 读取文件前 8KB 检测二进制文件（有 NUL 字节则跳过）
3. 提取文件内容的所有 trigram，使用 `thread_local` 位图（2^24 位 = 2MB）去重
4. 计算 FNV-1a 64位哈希，用于变更检测（增量重索引时跳过未修改文件）
5. 搜索时：trigram 交集 → 候选文件 → 重读文件验证 → 生成片段（64KB 分块读取，80 字符上下文窗口）

**位图去重优化**：
```
传统做法：每次提取前 memset 清零整个位图 → O(16M)
优化做法：维护 dirty 列表，仅清理实际使用的位 → O(unique_trigrams)
         典型文件 unique_trigrams ≈ 几千，远小于 16M
```

### 3.4 FileSystemWatcher — 文件系统监控

**文件**：`MacEverything/Core/FileSystemWatcher.h/.cpp`

**职责**：通过 macOS FSEvents API 实时监控文件系统变更。

**关键参数**：
- `kFSEventStreamCreateFlagFileEvents`：文件级粒度（而非目录级）
- 300ms 合并延迟：平衡响应速度与批处理效率
- 串行 dispatch queue：保证事件处理顺序

**事件处理**（在 `ServiceEngine+FSEvents.cpp` 中）：
- 事件分类为 create / modify / remove
- 应用到 SearchEngine + WAL
- `kFSEventStreamEventFlagMustScanSubDirs`（日志截断）触发子树全量重扫

**Rescan 防抖**：
- 5 秒防抖延迟（`kRescanDebounceDelaySec`）
- 300 秒节流间隔（`kRescanThrottleIntervalSec`）
- 路径包含合并：如 `/a` 和 `/a/b` 同时需要重扫，只保留 `/a`

### 3.5 ServiceEngine — 服务编排

**文件**：`MacEverything/Core/ServiceEngine.h/.cpp`（+ `ServiceEngine+FSEvents.cpp`、`ServiceEngine+Content.cpp`）

**职责**：整个应用的 C++ 中枢，管理所有核心对象的生命周期和协调。

**启动流程（增量模式）**：
```
startIncremental()
  ├── 加载缓存索引（paged 格式 + WAL 重放）
  ├── 启动 FSEvents 监控
  ├── FSEvents 历史回放（从 lastEventId 开始）
  ├── 后台同步引擎（background sync）
  ├── 启动内容索引（如已配置）
  └── 启动 HTTP 服务器
```

**启动流程（全量扫描模式）**：
```
startFullScan()
  ├── DirectoryScanner 全盘扫描
  ├── SearchEngine.loadRecords()
  ├── 保存索引到磁盘
  ├── 启动 FSEvents 监控
  ├── 启动内容索引
  └── 启动 HTTP 服务器
```

**关机流程**：
```
shutdown()
  ├── 设置 shuttingDown_ = true
  ├── 取消内容索引
  ├── 停止 FSEvents 监控
  ├── 停止 HTTP 服务器
  ├── 停止自动 compaction
  ├── dispatch_group_wait(DISPATCH_TIME_FOREVER)  // 等待所有后台任务
  └── 最终 flush 持久化
```

---

## 4. 性能分析

### 4.1 扫描性能

| 指标 | 说明 |
|------|------|
| **API** | `getattrlistbulk()`：单次系统调用返回整个目录的全部条目和属性 |
| **线程数** | 4-32（按 CPU 核心数自适应） |
| **缓冲区** | 每线程 1MB |
| **结果容器** | 每线程独立 `vector`（预分配 50K 条），无跨线程争用 |
| **子目录入队** | 批量入队（一次加锁推入所有新发现的子目录） |
| **跳过策略** | 跨卷目录、autofs 挂载点、.app 内部文件、已访问 inode |
| **时间复杂度** | O(N)，N = 文件系统条目总数 |
| **典型耗时** | 数百万文件约 3-8 秒（取决于磁盘速度和文件数量） |

### 4.2 索引构建性能

`loadRecords()` 完成以下工作：

| 步骤 | 操作 | 复杂度 |
|------|------|--------|
| 1 | 并行小写化（`dispatch_apply`） | O(N·L)，L = 平均文件名长度 |
| 2 | PathTable 构建（路径驻留） | O(N)，每条记录一次哈希查找 |
| 3 | pathIndex_ 构建（全路径映射） | O(N)，每条记录一次哈希插入 |
| 4 | Trigram 倒排索引构建 | O(N·L)，提取每个文件名的所有 trigram |
| 5 | 路径 Trigram 索引构建 | O(P·Lp)，P = 唯一路径数（约数万），Lp = 平均路径长度 |
| 6 | 记录去重（基于 pathIndex_） | O(N) |
| 7 | RecentCache 构建 | O(N·log K)，K = 200 |

**总复杂度**：O(N·L)，瓶颈在 trigram 提取。

### 4.3 查询性能

#### 4.3.1 Trigram 路径（关键词 ≥ 3 字符）

| 阶段 | 操作 | 复杂度 |
|------|------|--------|
| Trigram 提取 | 从关键词生成 trigram | O(Q)，Q = 关键词长度 |
| Posting list 查找 | 按最短列表优先排序 | O(T·log T)，T = trigram 数（≤ Q-2） |
| 列表交集 | 迭代最短列表 | O(min(k1, k2, ...))，ki = posting list 长度 |
| 子串验证 | 每个候选做子串匹配 | O(C·L)，C = 候选数，L = 文件名长度 |
| Phase 2 路径扫描 | 并行线性扫描 | O(N/P)，P = 并行度 |
| 排序 | partial_sort | O(M·log K)，M = 匹配数，K = maxResults |

**关键路径优化**：
- `__builtin_prefetch`：提前 4 条预取 `records_[]` 数据，隐藏随机访问延迟
- `ReusableBitmap`：线程局部位图，O(dirty_count) 重置而非 O(N) 的 memset
- `queryGeneration_`：原子计数器，每 1024 次迭代检查，支持快速用户输入取消旧查询

#### 4.3.2 线性扫描路径（关键词 < 3 字符或 glob 模式）

| 操作 | 复杂度 |
|------|--------|
| 全量扫描 | O(N/P)，P = GCD 并行度 |
| 匹配检查 | 每条记录一次 `find()` 或 `globMatch()` |

#### 4.3.3 典型查询延迟

以下数据基于约 500 万条记录的实测基准（来自 `test_query_perf.h` 和 benchmark）：

| 查询类型 | 典型延迟 | 说明 |
|----------|----------|------|
| 高选择性 trigram（如 "readme"） | < 5ms | posting list 短，候选少 |
| 中等选择性（如 "index", "test"） | 5-50ms | 候选较多但 partial_sort 限制输出 |
| 低选择性/高频词 | 50-200ms | 大量候选需要验证 |
| 单字符搜索（如 "a"） | 100-500ms | 退化为全量线性扫描 |
| 无匹配查询 | < 5ms | trigram 交集为空，立即返回 |
| 路径查询（含 `/`） | < 50ms | 使用路径 trigram 索引（优化后从 674ms → 29ms） |
| 绝对路径查询（如 "/usr/local"） | < 50ms | trigram 加速（优化后从 844ms → <50ms） |

**慢查询诊断**：超过 100ms 的查询会自动记录详细耗时分解：
```
SLOW_QUERY keyword="xxx" lock_wait=0.1ms trigram=0.5ms phase1=45ms 
           phase2=12ms lock_held=58ms sort=3ms total=61ms
```

### 4.4 持久化性能

#### 4.4.1 索引加载

| 步骤 | 操作 | 复杂度/特点 |
|------|------|------------|
| 页面加载 | 读取 `.pages` + `.ptable` 文件 | O(N/1024) 次页面 CRC32 校验 |
| WAL 重放 | 逐条应用 WAL 变更 | O(E)，E = WAL 条目数，每条 O(1) 查找 |
| 自动迁移 | v3 → v4 格式（仅首次） | O(N) 一次性 |

#### 4.4.2 增量写入

| 操作 | 复杂度/特点 |
|------|------------|
| WAL 追加 | O(1) 每条变更（CRC32 计算 + 追加写入） |
| WAL fsync | 每 64 条批量同步（分摊 fsync 开销） |
| 脏页刷新 | O(D·1024)，D = 脏页数（仅写修改过的页面） |
| 页表重写 | 原子写入（tmp + fsync + rename）|
| WAL 交换 | 无缝切换：打开新 WAL → 原子交换 → 刷页面 → 重命名 |

#### 4.4.3 Full Compaction

| 阶段 | 操作 | 阻塞查询？ |
|------|------|-----------|
| 快照活跃记录 | shared_lock 下复制 | 是（短暂） |
| 构建压缩数据 | 无锁重建所有结构 | 否 |
| 原子交换 | unique_lock 下替换 | 是（短暂） |
| 重写页面文件 | tmp + rename | 否 |

**自适应定时器**：30s - 600s 动态调整，基于脏页比例和 WAL 大小：
- WAL 超过 2MB → 缩短到最小 30s
- 无脏页 → 延长到最大 600s

### 4.5 内存优化

| 优化手段 | 效果 |
|----------|------|
| **PathTable 路径驻留** | 数百万条记录共享约数万个路径字符串，每条记录仅存 4 字节索引 |
| **FileRecord.path 释放** | `loadRecords()` 后每条记录的 `path` 字段被 `clear()` + `shrink_to_fit()`，释放堆内存 |
| **type 字段** | `uint8_t`（1 字节）而非 `int`（4 字节） |
| **thread_local 位图** | 避免每次 trigram 提取分配 2MB 位图，且仅清理脏位（O(dirty) vs O(16M)） |
| **每线程扫描缓冲区** | 每线程固定 1MB，避免重复分配 |
| **结果预分配** | 扫描线程预分配 50K 条，合并后 `shrink_to_fit()` |
| **RecentCache 固定上限** | `std::set<RecentEntry>` 限制 200 条，增量维护 |
| **墓碑压缩** | 墓碑占比 > 25% 时物理删除，回收内存 |

**典型内存占用**：通过 benchmark 输出的 per-entry 指标监控（`resident_size / recordCount`）。

### 4.6 Benchmark 工具

项目提供了 4 阶段基准测试（`benchmark.cpp`）：

```bash
# 编译
clang++ -std=c++20 -O2 MacEverything/Core/*.cpp benchmark.cpp -o benchmark

# 运行（默认扫描 /）
./benchmark [root_path]
```

**输出指标**：
- Phase 1：扫描吞吐量（entries/s）
- Phase 2：索引构建时间
- Phase 3：14 种查询场景的延迟（取 3 次最优值）
- Phase 4：记录批量获取延迟（µs/record）
- 汇总：总启动时间、内存使用、per-entry 内存、平均查询时间

---

## 5. 数据流与生命周期

### 5.1 首次启动（全量扫描）

```
App 启动
  → MacSearchBridge.init
    → ServiceEngine(config)
      → startFullScan()
        → DirectoryScanner.scan("/")        // 多线程扫描
        → scanner.takeResults()             // 合并结果
        → engine.loadRecords(records)       // 构建索引
        → IndexPersistence.flush()          // 持久化到磁盘
        → startMonitoring()                 // 开启 FSEvents
        → startContentIndexing()            // 后台内容索引
        → startHttpServer(19860)            // 启动 HTTP API
  → UI 显示搜索框
```

### 5.2 后续启动（增量加载）

```
App 启动
  → ServiceEngine.startIncremental()
    → IndexPersistence.load()               // 加载页面索引 + WAL 重放
    → engine.loadRecords(cached_records)    // 从缓存恢复
    → startMonitoring()                     // 开启 FSEvents
    → FSEvents 历史回放（lastEventId → now） // 追赶离线期间的变更
    → backgroundSyncEngine()                // 后台验证与同步
    → startContentIndexing()
    → startHttpServer(19860)
```

### 5.3 实时文件变更

```
文件系统变更
  → FSEvents 回调（300ms 合并延迟）
    → applyFSEvents()
      → engine.addRecord() / removeByPath() / updateByPath()
      → WAL.append()
      → contentIndex.updateContentForPath()
    → scheduleRescanForPaths()（如需子树重扫）
      → 防抖 5s → flushPendingRescans()
        → DirectoryScanner 局部扫描
        → engine.batchRescanPrefix()
```

### 5.4 搜索请求

```
用户输入
  → SearchViewModel（80ms 去抖动，内容搜索 300ms）
    → MacSearchBridge.queryResults()
      → engine.query(keyword, maxResults)
        → Phase 1: trigram 交集 + 验证
        → Phase 2: 路径补充扫描
        → 4 级排序 + partial_sort
      → forEachRecordWithPath()（单次 shared_lock 批量读取）
    → UI 更新结果列表
```

---

## 6. 持久化系统

### 6.1 文件格式

```
~/Library/Caches/com.maceverything.app/
├── index.pages          # 页面数据文件（magic "MEPG"，追加写入）
├── index.ptable         # 页表文件（magic "MEPT"，原子重写）
├── index.wal            # 写前日志（magic "WAL1"，CRC32 校验）
├── index.bin            # v3 遗留格式（仅用于迁移）
├── content_index.bin    # 内容索引（magic "MECI" v2）
├── content_index.wal    # 内容 WAL（magic "CWL1"）
└── maceverything.lock   # 单实例锁（flock）
```

### 6.2 页面格式（v4）

```
index.pages 文件结构:
┌──────────────────────────────────┐
│ Magic: "MEPG" (4 bytes)         │
│ Version: 1 (4 bytes)            │
├──────────────────────────────────┤
│ Page 0 数据 (≤1024 条记录序列化) │
│ Page 1 数据                      │
│ ...                              │
│ Page N 数据（追加写入，旧版本     │
│           成为"死空间"）          │
└──────────────────────────────────┘

index.ptable 文件结构:
┌──────────────────────────────────┐
│ Magic: "MEPT" (4 bytes)         │
│ Version: 1 (4 bytes)            │
│ PageCount: uint32_t             │
│ 元数据块 (IndexMetadata)         │
├──────────────────────────────────┤
│ PageEntry[0]: offset(8) +       │
│   byteLength(4) + recordCount(2)│
│   + CRC32(4) = 18 bytes         │
│ PageEntry[1]: ...               │
│ ...                              │
└──────────────────────────────────┘
```

**写入策略**：
- 正常刷新：仅追加脏页到 `.pages`，原子重写 `.ptable`
- 死空间超过 50%（`kDeadSpaceRewriteRatio`）时，全量重写两个文件
- 使用 `F_NOCACHE`（`fcntl`）旁路内核页缓存，避免大量顺序写入污染缓存

### 6.3 WAL（写前日志）

```
WAL 条目格式:
┌────────────────────────────────┐
│ Op: uint8_t (Add/Remove/Update)│
│ PathLen: uint32_t              │
│ Path: [PathLen bytes]          │
│ Record 字段 (name, type, etc.) │
│ CRC32: uint32_t                │
└────────────────────────────────┘
```

- **批量 fsync**：每 64 条（`syncInterval_`）做一次 fsync，分摊开销
- **大小限制**：50MB 硬上限
- **CRC32**：ISO 3309 多项式（`0xEDB88320`），函数局部静态查找表（线程安全）

### 6.4 WAL 交换协议

```
1. 创建新 WAL 文件（walPath_.new）
2. 原子切换引擎到新 WAL（detachWAL → attachWAL(new)）
3. 刷新脏页到 pages 文件
4. 将新 WAL 重命名覆盖旧 WAL
→ 确保切换窗口中的变更不丢失
```

---

## 7. 并发模型

### 7.1 锁策略

| 资源 | 锁类型 | 读操作 | 写操作 |
|------|--------|--------|--------|
| SearchEngine records/indices | `shared_mutex` | `shared_lock`（查询并发） | `unique_lock`（变更/compaction） |
| ContentIndex | `shared_mutex` | `shared_lock` | `unique_lock` |
| ServiceEngine 核心对象 | 4 个 `shared_mutex` | `shared_lock` | `unique_lock` |
| WAL | `std::mutex` | — | 独占 |
| DirectoryScanner 工作队列 | `mutex` + `condition_variable` | — | 独占 |
| 扫描器 inode 去重集 | `mutex` | — | 独占 |

### 7.2 GCD 使用

| 组件 | GCD 原语 | 用途 |
|------|----------|------|
| 并行查询 | `dispatch_apply` | Phase 2 路径扫描、trigram 索引构建 |
| 并行内容索引 | `dispatch_apply` | 多文件并行索引 |
| FSEvents 回调 | serial dispatch queue | 保证事件顺序 |
| 自动 compaction | `dispatch_source_t` timer | 定期刷新 |
| 后台任务 | `dispatch_group_t` | shutdown 等待所有后台工作 |
| 内容 compaction | `dispatch_after` | 60 秒延迟合并 |
| 变更队列 | `dispatch_queue_t` (serial) | FSEvents 变更序列化 |

### 7.3 原子操作

| 变量 | 类型 | 用途 |
|------|------|------|
| `queryGeneration_` | `atomic<uint64_t>` | 查询取消：新查询递增，旧查询每 1024 次检查 |
| `liveCount_` | `atomic<uint32_t>` | 活跃记录计数 |
| `cancelled_`/`done_`/`activeTasks_` | DirectoryScanner 原子量 | 扫描取消与完成协调 |
| `isScanning_`/`isMonitoring_`/... | ServiceEngine 状态标志 | 无锁状态查询 |
| `contentIndexGeneration_` | `atomic<uint64_t>` | 内容索引取消 |

### 7.4 COW Compaction 时序

```
时间线:
t0 ────── t1 ────────────────── t2 ──── t3
│ Phase1  │      Phase 2       │Phase3 │
│shared_  │    无锁构建新数据   │unique_│
│lock     │                    │lock   │
│         │                    │       │
│ 查询 ✓  │  查询 ✓ 变更 ✓     │阻塞   │
│ 变更 ✗  │  旧数据上查询/变更  │交换   │
```

Phase 2 是最耗时的部分（可能数秒），但不持有任何锁，查询和变更均不受影响。Phase 1 和 Phase 3 仅持锁极短时间。

---

## 8. HTTP API 与 MCP

### 8.1 HTTP Server

**绑定**：`127.0.0.1:19860`（仅本地访问）

**线程模型**：单线程 `poll()` 循环，每次处理一个连接。查询内部通过 GCD 并行化。

| 端点 | 方法 | 参数 | 说明 |
|------|------|------|------|
| `/search` | GET | `q`(关键词), `max`(数量) | 文件名搜索 |
| `/search/content` | GET | `q`(关键词), `max`(数量) | 内容搜索 |
| `/recent` | GET | `count`(数量) | 最近修改的文件 |
| `/status` | GET | — | 扫描/同步/监控状态 |
| `/health` | GET | — | 健康检查 |
| `/admin/rebuild-index` | POST | — | 重建文件名索引 |
| `/admin/rebuild-content-index` | POST | — | 重建内容索引 |
| `/admin/content-config` | GET/POST | JSON body | 内容索引配置 |

**响应格式**：JSON，包含 `queryTimeMs` 字段显示查询耗时。

**安全**：
- 仅绑定 127.0.0.1
- 5 秒 `SO_RCVTIMEO` 超时
- 64KB 请求体上限
- 10,000 结果上限
- 端口冲突自动重试（最多 5 次）

### 8.2 MCP Server

**协议**：JSON-RPC 2.0 over stdio

**工具**：
| 工具名 | 功能 |
|--------|------|
| `search_files` | 文件名搜索 |
| `search_content` | 内容搜索 |
| `recent_files` | 最近修改文件 |
| `index_status` | 索引状态 |

**实现**：作为 HTTP 代理，将 MCP 请求转换为 HTTP 调用发送到 `localhost:19860`。

---

## 9. UI 层

### 9.1 搜索界面

- **Alfred 风格**搜索框，全局热键唤起（默认 Option+Space）
- **Ghost text 自动补全**：基于搜索历史的前缀匹配（最频繁 > 最近使用）
- **去抖动**：文件名搜索 80ms，内容搜索 300ms
- **分页**：`pageSize=100`，`maxResults=10,000`，`LazyVStack` + `onAppear` 触发加载更多
- **搜索取消**：`searchGeneration` 计数器，新搜索自动取消旧搜索

### 9.2 结果展示

- **文件图标缓存**：`NSCache`，500 图标上限，按扩展名/路径/类型分级缓存
- **关键词高亮**：大小写不敏感匹配，命中片段加粗 + 强调色
- **交互**：双击打开、Cmd+Click 在 Finder 中显示、右键菜单（打开/显示/复制路径）、拖放支持

### 9.3 刷新节流

`IndexRefreshThrottle` 状态机：
- 窗口聚焦时：索引变更后触发刷新，冷却期内合并事件
- 窗口失焦时：延迟所有刷新事件
- 重新聚焦时：如有积压事件，立即刷新

---

## 10. 构建与测试

### 10.1 构建

```bash
# 构建 Release
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcodebuild -project MacEverything.xcodeproj \
  -scheme MacEverything -configuration Release build SYMROOT=build

# 打包 DMG
hdiutil create -volname MacEverything \
  -srcfolder build/Release/MacEverything.app \
  -ov -format UDZO MacEverything.dmg
```

**环境要求**：macOS 15+, Xcode 16+

### 10.2 测试

```bash
make test          # 快速测试（<5s）
make test-slow     # 慢速测试（含全盘扫描）
make test-asan     # AddressSanitizer
make test-tsan     # ThreadSanitizer
make benchmark     # 性能基准测试
```

**测试架构**：
- 49 个测试部分（Part），分布在 57 个 `.h` 头文件中
- `test_all.cpp` 仅负责 `#include` 和 `main()` 调度，不包含测试实现
- 支持 `--fast`/`--slow`/`--bench`/`--part N` 参数选择
- 另有 1 个 Swift 测试文件（`test_index_refresh_throttle.swift`），独立编译

**测试覆盖**：
- 单元测试：各核心模块（SearchEngine、ContentIndex、WAL、PagedWriter、PathTable 等）
- 并发测试：4 读者 + 2 写者，压力测试 2 秒
- 集成测试：端到端扫描+搜索、FSEvents、ServiceEngine 生命周期
- 性能测试：500K/10M 记录查询基准、WAL 批量 fsync、并行 snippet 生成
- 协议测试：MCP JSON-RPC 2.0（进程外子进程测试）
- 快速/慢速分类，快速测试保持 < 5 秒

---

## 11. 文件清单

### Core 层（17 组 .h + .cpp）

| 文件 | 行数 | 职责 |
|------|------|------|
| `FileRecord.h` | 14 | 核心数据结构定义 |
| `SearchEngine.h/.cpp` | 288/1527 | 搜索引擎（记录管理、trigram 索引、查询、compaction） |
| `SearchEnginePersistence.cpp` | 216 | 遗留 v1/v2/v3 格式读写 |
| `DirectoryScanner.h/.cpp` | 64/324 | 多线程文件系统扫描器 |
| `ContentIndex.h/.cpp` | 150/747 | 文件内容 trigram 倒排索引 |
| `IndexPersistence.h/.cpp` | 85/294 | 页面索引 + WAL 编排 |
| `IndexWAL.h/.cpp` | 89/272 | 写前日志（CRC32 校验） |
| `PagedIndexWriter.h/.cpp` | 62/459 | 页面格式读写 |
| `ContentIndexPersistence.h/.cpp` | 131/439 | 内容索引持久化 + WAL |
| `FileSystemWatcher.h/.cpp` | 88/214 | FSEvents 封装 |
| `HttpServer.h/.cpp` | 78/626 | HTTP REST API |
| `ServiceEngine.h/.cpp` | 154/506 | 服务编排中枢 |
| `ServiceEngine+FSEvents.cpp` | 315 | FSEvents 事件处理 |
| `ServiceEngine+Content.cpp` | 252 | 内容索引管理 |
| `CompactionTimer.h/.cpp` | 35/56 | GCD 定时器封装 |
| `InstanceLock.h/.cpp` | 27/40 | 单实例 flock 锁 |
| `RescanDebounce.h` | 75 | 路径包含合并（header-only） |
| `PathUtils.h` | 42 | 缓存/日志路径工具 |
| `Logger.h/.cpp` | 149/155 | 线程安全日志（5MB 轮转，3 备份） |
| `StringUtils.h/.cpp` | 7/46 | ASCII 快速小写化 + Unicode 回退 |
| `TrigramExtraction.h` | header-only | 滚动 byte-trigram 提取与自适应去重 |
| `PostingListIntersection.h` | header-only | 自适应有序 posting-list 求交 |

### Bridge 层

| 文件 | 行数 | 职责 |
|------|------|------|
| `MacSearchBridge.h/.mm` | 136/380 | ObjC 单例桥接 |
| `MacSearchBridge+Content.h/.mm` | 32/129 | 内容搜索分类 |
| `MacSearchBridge_Internal.h` | 13 | 内部类扩展 |

### App 层（14 个 Swift 文件）

| 文件 | 行数 | 职责 |
|------|------|------|
| `MacEverythingApp.swift` | 78 | @main 入口 |
| `SearchViewModel.swift` | 466 | 搜索逻辑（去抖动、分页、历史） |
| `ContentView.swift` | 286 | 主界面 |
| `AppDelegate.swift` | 127 | 状态栏、启动参数、登录项 |
| `ResultRow.swift` | 159 | 文件结果行（图标缓存、交互） |
| `ContentResultRow.swift` | 97 | 内容结果行 |
| `PermissionView.swift` | 44 | Full Disk Access 引导 |
| `HotkeyManager.swift` | 81 | 全局热键（Carbon API） |
| `IndexRefreshThrottle.swift` | 79 | 刷新节流状态机 |
| `AppLogger.swift` | 19 | Swift → C++ 日志桥接 |
| `TextHighlight.swift` | 58 | 关键词高亮 |
| `ShortcutSettingsView.swift` | 234 | 热键设置面板 |
| `ContentSettingsView.swift` | 152 | 内容索引设置面板 |
| `SearchHistoryStore.swift` | 71 | 搜索历史持久化 |

### CLI 层

| 文件 | 行数 | 职责 |
|------|------|------|
| `daemon_main.cpp` | 197 | 无头守护进程 |
| `mace_main.cpp` | 184 | 短命令查询客户端 |
| `MaceClient.h` | 234 | HTTP、URL 和 JSON 响应处理 |
| `mcp_main.mm` | 481 | MCP 服务（JSON-RPC 2.0 stdio） |

### 测试

| 文件 | 测试内容 |
|------|----------|
| `test_helpers.h` | 共享工具（内存监控、CPU 计时、断言函数） |
| `test_scan_query.h` (Part 1) | 全盘扫描 + 查询基准 |
| `test_mutation.h` (Part 3) | 增量变更正确性 + 10K 操作性能 |
| `test_thread_safety.h` (Part 5) | 4R+2W 并发压力测试 |
| `test_trigram_index.h` (Part 8) | Trigram 倒排索引 |
| `test_query_perf.h` (Part 44) | 500K 记录查询性能（7 场景） |
| `test_query_perf_10m.h` (Part 46) | 1000 万记录查询性能 |
| `test_mcp_protocol.h` (Part 49) | MCP 协议（进程外测试） |
| 另有约 50 个测试文件... | 覆盖 WAL、持久化、compaction、FSEvents 等 |

---

## 附录：关键常量速查

| 常量 | 值 | 说明 |
|------|------|------|
| `kRecordsPerPage` | 1024 | 每页记录数 |
| `kCompactThreshold` | 100 | WAL 条目数最小刷新阈值 |
| `kTombstoneCompactRatio` | 0.25 | 墓碑占比 > 25% 触发全量压缩 |
| `kDeadSpaceRewriteRatio` | 0.5 | 死空间 > 50% 触发文件重写 |
| `kWALSizeFlushThreshold` | 2MB | WAL 大小超此值缩短刷新间隔 |
| `kBaseIntervalSec` | 300s | 自动 compaction 基础间隔 |
| `kMinIntervalSec` | 30s | 自动 compaction 最短间隔 |
| `kMaxIntervalSec` | 600s | 自动 compaction 最长间隔 |
| `kRecentCacheSize` | 200 | 最近文件缓存上限 |
| WAL 最大大小 | 50MB | 超出后拒绝追加 |
| WAL fsync 间隔 | 64 条 | 批量同步 |
| FSEvents 合并延迟 | 300ms | 事件批处理窗口 |
| 搜索去抖动 | 80ms/300ms | 文件名/内容搜索 |
| HTTP 端口 | 19860 | 默认监听端口 |
| 内容索引最大文件 | 1MB | 默认值，可配置 |
| 日志文件大小 | 5MB | 超出后轮转，保留 3 个备份 |
| 图标缓存上限 | 500 | NSCache 限制 |
| 搜索历史上限 | 200 条 | UserDefaults JSON 编码 |
| 查询取消检查频率 | 每 1024 次 | `queryGeneration_` 检查间隔 |

# MacEverything 项目说明文档

> 当前源码架构与开发指南。可下载安装包以 [GitHub Releases](https://github.com/ying-zhang/MacEverything/releases) 为准；本文的源码能力不代表已发布版本包含全部功能。

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

当前源码架构由 GUI 进程持有唯一的 `ServiceEngine`。关闭搜索窗口不会退出应用；应用继续在菜单栏运行并维护索引、FSEvents、HTTP API、CLI 和 MCP 服务。只有用户明确退出应用时才停止引擎。

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

**另有两个客户端可执行程序**：
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

**文件**：`MacEverything/Core/SearchEngine*.cpp` 及相关查询模块；核心查询逻辑已拆分为多个职责明确的文件。

**职责**：管理所有文件记录，提供高速搜索。

#### 3.2.1 核心数据结构

```
FileRecord（输入/输出结构，内部不逐条存放此对象）
├── name: string       // 文件名（如 "main.cpp"）
├── path: string       // 父目录路径（导入后驻留到 pathPool_）
├── type: uint8_t      // 0=墓碑, 1=文件, 2=目录, 3=符号链接, 5=App Bundle
├── size: uint64_t     // 文件大小（字节）
├── modTime: time_t    // 最后修改时间（秒级时间戳）
├── inode: uint64_t    // ATTR_CMN_FILEID
└── devId: int32_t     // ATTR_CMN_DEVID
```

**内部记录存储（SoA）**：
- `origNamePool_` / `namePool_`：`StringPool`，原始文件名和小写文件名。
- `pathPool_`：去重的父目录字符串池；`pathIndices_` 为每条记录保存一个 4 字节路径索引。
- `types_`、`sizes_`、`modTimes_`、`inodes_`、`devIds_`：并行的定长数组，共用 recordIndex；`type=0` 表示墓碑。
- `pathLookup_` / `lowerPathLookup_`：目录路径到字符串池索引的查找表，小写路径在需要时计算。
- `pathIndex_`：小写完整路径的 64 位哈希到 recordIndex 的映射；`pathIndexCollisions_` 单独处理碰撞。

`FileRecord` 用于扫描、导入和结果导出；`PathTable` 保留兼容快照接口，当前存储以 `StringPool` 和列数组为准。多个文件共用驻留后的目录路径，避免每条记录重复存储长路径。

#### 3.2.2 Trigram 倒排索引

**核心思想**：将每个文件名拆分为所有连续 3 字节子串（trigram），建立从 trigram 到记录索引列表的倒排映射。

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
路径索引利用 pathPool_ 的去重特性——约 80,000 个唯一路径远少于数百万条记录，避免了路径 trigram 的大量重复。

#### 3.2.3 搜索算法

`query()` 根据查询类型、候选规模和索引可用性选择倒排索引或扫描：

**文件名 Trigram 候选（关键词至少 3 字节）**：
1. 从关键词提取所有 trigram
2. 在 `nameTrigramIndex_` 中查找每个 trigram 的 posting list
3. 按 posting list 长度排序（最短优先）
4. 逐一求交集，得到候选集
5. 对每个候选做子串验证（消除 trigram 误匹配）
6. 使用 `__builtin_prefetch` 预取记录数据（提前 4 条，隐藏内存延迟）
7. 每 1024 次迭代检查 `queryGeneration_` 原子计数器，支持取消

posting-list 求交统一由 `PostingListIntersection.h` 处理：尺寸接近时使用低开销
双指针标量合并，列表长度比达到 32:1 时切换到 libc++ 的不均衡列表快速路径。

**路径匹配与扫描回退**：
1. 普通词同时尝试文件名和路径 trigram；路径候选通过 `pathIdxToRecords_` 展开并与文件名候选合并，不能遗漏仅路径命中的记录。
2. 多词查询可选择更小的候选集，再验证其余条件；路径候选过大或索引不适用时，使用自适应并行扫描。
3. 不改变匹配语义：每个普通词都可以在文件名或完整路径中命中。

**结果排序**（4 级相关度）：
| 优先级 | 匹配类型 | 示例（搜索 "readme"） |
|--------|----------|----------------------|
| 0（最高） | 文件名精确匹配 | `readme` |
| 1 | 文件名前缀匹配 | `readme.md` |
| 2 | 文件名包含匹配 | `my-readme-file.txt` |
| 3（最低） | 仅路径匹配 | `/readme-project/config.json` |

正则查询及结果正则高亮均使用 RE2；高亮通过 `RegexHighlight.h/.cpp` 的 C 接口返回 UTF-8 字节区间，再映射为 Swift 字符串区间。高亮有模式长度、文本长度、编译内存和匹配条数上限；超限只停用高亮，不改变搜索结果。

同级内按完整路径长度升序（短路径优先），路径长度也相同则按记录号升序。扫描时每个线程只保留最好的 `maxResults` 条（缓冲区达到 2K 后用 `nth_element` 截断），合并后再 `partial_sort`。普通子串查询会另存全部命中记录号；下一次能证明是收窄的输入只重验这批记录。OR、NOT、通配符、正则、过滤器和索引变更都不复用。

**短关键词和 Glob**：不足三字节的关键词通常使用扫描；Glob 会尝试从字面片段中提取 trigram，无可用候选或选择性不足时再扫描。

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
                        StringPool、pathIndex_（耗时最长，不持有引擎锁）
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
- 普通文件创建、修改、删除按文件更新 SearchEngine、WAL 和内容索引，不因一次文件保存重扫整目录。
- 单边重命名在生命周期锁外核对文件身份与新旧路径；仍存在的硬链接保留。目录删除、移动与重命名需要协调后代记录。
- `kFSEventStreamEventFlagMustScanSubDirs` 触发事件目录自身的子树重扫，不扩大到父目录。
- 重扫保留目录自身的已有记录，并遵循隐藏目录、排除规则、App 包和显式嵌套根的扫描规则。
- 路径映射统一配置路径与真实路径拼写；根边界校验拒绝 `../` 越界。EACCES/EIO 等临时错误保留原索引，不误判为删除。

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
| 2 | StringPool 构建（名称和路径驻留） | O(N)，每条记录一次哈希查找 |
| 3 | pathIndex_ 构建（全路径映射） | O(N)，每条记录一次哈希插入 |
| 4 | Trigram 倒排索引构建 | O(N·L)，提取每个文件名的所有 trigram |
| 5 | 路径 Trigram 索引构建 | O(P·Lp)，P = 唯一路径数（约数万），Lp = 平均路径长度 |
| 6 | 记录去重（基于 pathIndex_） | O(N) |
| 7 | RecentCache 构建 | O(N·log K)，K = 200 |

**总复杂度**：O(N·L)，瓶颈在 trigram 提取。

### 4.3 查询性能

#### 4.3.1 Trigram 路径（关键词 ≥ 3 字节）

| 阶段 | 操作 | 复杂度 |
|------|------|--------|
| Trigram 提取 | 从关键词生成 trigram | O(Q)，Q = 关键词长度 |
| Posting list 查找 | 按最短列表优先排序 | O(T·log T)，T = trigram 数（≤ Q-2） |
| 列表交集 | 根据列表长度比分别合并或跳跃查找 | 耗时取决于各 posting list 长度及分布 |
| 子串验证 | 每个候选做子串匹配 | O(C·L)，C = 候选数，L = 文件名长度 |
| 路径匹配 | 路径候选展开与验证，必要时扫描 | 取决于候选规模；最坏遍历全部记录 |
| 排序 | 线程内有界保留 + partial_sort | 输出最多 K 条；匹配和筛选仍需处理全部候选 |

**关键路径优化**：
- `__builtin_prefetch`：提前预取名称池与记录列数据，隐藏随机访问延迟
- `ReusableBitmap`：线程局部位图，O(dirty_count) 重置而非 O(N) 的 memset
- `queryGeneration_`：原子计数器，每 1024 次迭代检查，支持快速用户输入取消旧查询

#### 4.3.2 扫描回退（短词、低选择性或索引不适用）

| 操作 | 复杂度 |
|------|--------|
| 全量扫描 | O(N/P)，P = GCD 并行度 |
| 匹配检查 | 每条记录一次 `find()` 或 `globMatch()` |

#### 4.3.3 典型查询延迟

以下为历史基准中约 500 万条记录的参考数据（来自 `test_query_perf.h` 和 benchmark），不是本轮修复后的重新测量，也不作为所有设备的延迟保证：

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

| 操作 | 当前实现与开销 |
|------|----------------|
| 基础索引加载 | 读取 `index.v6` 中的 SoA 列与字符串池，校验头和各节 CRC；随后回放 WAL |
| 延迟构建 | 基础记录先可搜索，trigram 等辅助索引在 Phase 2 构建 |
| WAL 追加 | 每条变更序列化并追加；默认每 64 条 fsync；单段上限 50MB |
| 基础快照刷新 | 全量序列化快照到临时文件，检查写入、定位、flush、fsync 和 close，然后原子替换 |
| 旧格式迁移 | 尝试读取兼容的旧格式并写为 v6；不支持或损坏的格式需要重建 |
| 压缩整理 | 快照活跃记录、在副本构建、短暂独占交换，再重写 v6 文件 |

当前基础索引不是“只写脏页”的分页持久化。脏页统计仍参与刷新间隔计算，但每次基础快照刷新会重写完整 v6 文件；WAL 负责承接两次快照之间的增量。定时器按变更规模和 WAL 大小自适应调整。

### 4.5 内存优化

| 优化手段 | 效果 |
|----------|------|
| **StringPool 路径驻留** | 数百万条记录共享约数万个路径字符串，每条记录仅存 4 字节索引 |
| **SoA 列存储** | 输入的 FileRecord 转换为字符串池和定长列，避免长期保留逐条 string 对象 |
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
make benchmark  # 使用 Makefile 中的 RE2、Abseil 和系统框架配置

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
    → IndexPersistence.load()               // 优先 v6 Flat SoA，回放 WAL
    → 缓存记录先可搜索，后台完成 Phase 2 索引
    → backgroundSyncEngine()                // 回放本地历史事件；网络目录轮询协调
    → startMonitoring(replayResumeId)        // 成功回放后沿用已交付水位
      （回放失败则后台全扫，完成后从扫描起点继续监听）
    → startContentIndexing()
    → startHttpServer(19860)
```

### 5.3 实时文件变更

```
文件系统变更
  → FSEvents 回调（300ms 合并延迟）
    → applyOrDeferEvents()                   // 核对生命周期与快照编号
      → applyFSEvents()                     // 普通文件走单条变更
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
        → 文件名与路径候选合并、验证；必要时回退扫描
        → 有界 Top-K + 记录号同分；普通子串可复用上一轮完整候选
      → forEachRecordWithPath()（单次 shared_lock 批量读取）
    → UI 更新结果列表
```

---

## 6. 持久化系统

### 6.1 当前文件与兼容格式

```
~/Library/Caches/com.maceverything.app/
├── index.v6             # 当前基础索引（magic "MEV6"，SoA + 字符串池）
├── index.wal            # WAL 基础名称，另有轮转段；恢复时按顺序回放
├── index.pages          # 旧分页数据，仅用于兼容读取/迁移
├── index.ptable         # 旧分页页表，仅用于兼容读取/迁移
├── index.bin            # 更早的索引格式，仅用于兼容读取/迁移
├── content_index.bin    # 内容索引（magic "MECI" v2）
├── content_index.wal    # 内容 WAL（magic "CWL1"）
└── maceverything.lock   # 单实例锁（flock）
```

### 6.2 v6 基础快照

`FlatIndexWriter` 写入一个文件：64 字节头、每项 24 字节的节表，以及 10 个数据节。数据节分别存放原始名称、小写名称、目录路径池、路径索引、类型、大小、修改时间、inode、设备号和元数据。头与各节均有 CRC；读取时校验大小、偏移和记录列之间的一致性。早期 v6 文件多出的 lowerPathPool 节保留读取兼容，当前不再写出。

写入先生成 `.tmp`，检查每次写入和文件定位，完成 `fflush`、`fsync`、`fclose` 后再 rename，并同步父目录。遇错不继续处理后面的数据节。替换前的写入失败会删除临时文件并保留原快照；若替换后的父目录同步失败，则返回失败并保留 WAL 供恢复，不能把它等同于“旧快照仍在”。

### 6.3 WAL（写前日志）

WAL 由带版本的头和连续条目组成。条目保存操作类型、路径、记录字段及 CRC32，正常追加先 flush，默认每 64 条 fsync，单段硬上限为 50MB。恢复时按段顺序读取，只回放完整且校验有效的条目，遇到损坏或截断停止该段。

### 6.4 快照与 WAL 轮转

1. 创建新的 WAL 段，并将引擎后续变更切换到新段。
2. 从引擎取得快照，完整重写 v6 基础索引。
3. 只有基础索引成功写入后，才清理旧 WAL 段。
4. 基础写入失败时保留各段；下次先重试基础写入，避免每次定时器触发都再新建一段。按路径幂等回放允许恢复时重新应用已包含在基础快照中的变更。


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

**线程模型**：一个 `poll()` 接收线程将连接分发给 8 个工作线程，等待队列最多保留 64 个连接；查询内部继续通过 GCD 并行化。

| 端点 | 方法 | 参数 | 说明 |
|------|------|------|------|
| `/api/search` | GET | `q`(关键词), `limit`(数量) | 文件名搜索 |
| `/api/search/content` | GET | `q`(关键词), `limit`(数量) | 内容搜索 |
| `/api/recent` | GET | `limit`(数量) | 最近修改的文件 |
| `/api/status` | GET | — | 扫描/同步/监控状态 |
| `/api/memory` | GET | — | 内存占用拆分 |
| `/api/health` | GET | — | 粗粒度健康状态，免认证 |
| `/api/index/rebuild` | POST | — | 重建文件名索引 |
| `/api/content/rebuild` | POST | — | 重建内容索引 |
| `/api/content/config` | GET/POST | JSON body | 内容索引配置 |

**响应格式**：JSON，包含 `queryTimeMs` 字段显示查询耗时。

**安全**：
- 仅绑定 127.0.0.1
- Bearer token 鉴权可在设置中启用，默认关闭；启用时除 `/api/health` 外均要求 token，文件权限为 `0600`
- 严格校验 Host，并拒绝所有带 Origin 的浏览器请求
- 5 秒 `SO_RCVTIMEO` 超时
- 64KB 请求体上限
- API 单次请求上限 10,000；GUI 文件名查询上限默认为 10,000、可配置到 100,000；内容查询默认 200，并有独立上限设置
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

- **首次索引模式**：快速开始仅使用所选目录；全盘搜索在明确授权 Full Disk Access 后切换到 `/` 并重建
- **Alfred 风格**搜索框，全局热键唤起（默认 Option+Space）
- **Ghost text 自动补全**：基于搜索历史的前缀匹配（最频繁 > 最近使用）
- **去抖动**：文件名搜索 80ms，内容搜索 300ms
- **分页**：文件名结果每页 100 条、内容结果每页 200 条；文件名查询上限为 100–100,000（默认 10,000），内容查询上限为 50–200（默认 200），导出使用各自上限内的完整缓存
- **搜索取消**：`searchGeneration` 计数器，新搜索自动取消旧搜索

### 9.2 结果展示

- **文件图标缓存**：`NSCache`，500 图标上限，按扩展名/路径/类型分级缓存
- **关键词高亮**：大小写不敏感匹配，命中片段加粗 + 强调色
- **交互**：双击打开、Cmd+Click 在 Finder 中显示、右键菜单（打开/显示/复制完整路径）、拖放支持

### 9.3 刷新节流

`IndexRefreshThrottle` 状态机：
- 窗口聚焦时：索引变更后触发刷新，冷却期内合并事件
- 窗口失焦时：延迟所有刷新事件
- 重新聚焦时：如有积压事件，立即刷新

---

## 10. 构建与测试

### 10.1 构建

```bash
# 安装构建工具并准备固定版本静态依赖
brew install cmake ripgrep
scripts/prepare-re2-deps.sh

# 构建 Release
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcodebuild -project MacEverything.xcodeproj \
  -scheme MacEverything -configuration Release build SYMROOT=build

scripts/verify-release-app.sh build/Release/MacEverything.app

# 完整双架构构建、签名检查与 DMG 打包，最终产物放入 artifacts/
scripts/build-release-dmgs.sh
```

**环境要求**：macOS 15+、Xcode 16+、CMake 3.22+、ripgrep。RE2/Abseil 从固定版本并校验 SHA-256 的源码静态编译，不复用 Homebrew 或旧应用中的 dylib。

### 10.2 测试

```bash
make test          # 快速回归，包含 C++、Swift、Bridge、签名和文档检查
make test-slow     # 慢速测试（含全盘扫描）
make test-asan     # AddressSanitizer
make test-tsan     # ThreadSanitizer（默认 Part 5）
make benchmark     # 性能基准测试
```

**测试架构**：
- 测试实现分布在 `tests/` 中的 C++ 头文件、Swift 和 Python 测试文件中，数量以当前工作树为准
- `test_all.cpp` 仅负责 `#include` 和 `main()` 调度，不包含测试实现
- 支持 `--fast`/`--slow`/`--bench`/`--part N` 参数选择
- Swift 测试由 Makefile 中对应目标独立编译

**测试覆盖**：
- 单元测试：各核心模块（SearchEngine、ContentIndex、WAL、PagedWriter、PathTable 等）
- 并发测试：4 读者 + 2 写者，压力测试 2 秒
- 集成测试：端到端扫描+搜索、FSEvents、ServiceEngine 生命周期
- 性能测试：500K/10M 记录查询基准、WAL 批量 fsync、并行 snippet 生成
- 协议测试：MCP JSON-RPC 2.0（进程外子进程测试）
- 快速/慢速分类；快速集跳过基准和压力测试，耗时随设备与模块数量变化

---

## 11. 文件清单

文件数量和行数变化频繁，不在文档中维护容易失真的静态统计。以目录职责和 Xcode/Makefile 构建清单为准：

- `MacEverything/Core/`：搜索、扫描、持久化、FSEvents、HTTP 和服务生命周期。
- `MacEverything/Bridge/`：Objective-C++ 桥接与内容搜索分类。
- `MacEverything/App/`：SwiftUI 界面、设置、结果交互和导出。
- `MacEverything/CLI/`：`mace` 客户端与 MCP stdio 代理。
- `tests/`：C++ 模块测试和可独立编译的 Swift 测试。
- `benchmarks/`：可复现的微基准、查询集和历史结果。

查看当前源码规模可运行：

```bash
find MacEverything -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' -o -name '*.swift' \) -print0 | xargs -0 wc -l | sort -nr
```

---

## 附录：关键常量速查

| 常量 | 值 | 说明 |
|------|------|------|
| `kRecordsPerPage` | 1024 | 每页记录数 |
| `kCompactThreshold` | 100 | WAL 条目数最小刷新阈值 |
| `kTombstoneCompactRatio` | 0.25 | 墓碑占比 > 25% 触发全量压缩 |
| `kDeadSpaceRewriteRatio` | 0.5 | 旧分页写入器的历史阈值，非当前 v6 刷新策略 |
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
| 搜索历史上限 | 默认 50、最多 200 条 | UserDefaults JSON 编码 |
| 查询取消检查频率 | 每 1024 次 | `queryGeneration_` 检查间隔 |

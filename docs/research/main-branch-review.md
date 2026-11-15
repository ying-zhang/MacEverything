# MacEverything 源码审查

**更新时间**：2026-11-15。本页记录 1.11.15 发布源码的审查与验证；正式下载以 [GitHub Releases](https://github.com/ying-zhang/MacEverything/releases) 为准。本次正式版本为 1.11.15。

## 已修复的问题

### 增量索引与目录范围

- 普通文件事件直接按文件维护索引，不因一次文件保存重扫整个父目录；目录变化和 `MustScanSubDirs` 才做子树协调。
- 单边重命名通过文件身份寻找旧路径，身份查找及文件系统核对在生命周期锁外完成；仍存在的硬链接不会被误删。
- 增量路径与全扫描使用一致的隐藏目录、排除模式和 App 包规则；核对祖先目录，并兼容显式嵌套根和 `/tmp`、`/private/tmp` 等路径别名。
- 根事件不会扩大到根外，拒绝 `..` 越界。子树重扫保留已索引目录自身的记录，临时权限或 I/O 错误不直接当作删除。
- 目录移出索引范围、只收到旧路径事件时，清理旧目录及后代。陈旧事件回归加入实际应用计数，避免测试因磁盘文件仍存在而产生假通过。

### 快照与 WAL 错误处理

- `FlatIndexWriter` 检查文件位置、定位及节长度；一个节失败后立即结束，不再继续查询位置或写后续节。
- 检查字符串池、数组和元数据长度，避免写出前发生 32 位截断；快照仍经临时文件、flush、fsync、close、rename 和父目录同步完成。
- WAL 打开和恢复读取也检查定位失败。替换前的写入失败保留旧快照；基础写入失败保留 WAL 段供恢复。
- 新增实际写入故障回归：用 `RLIMIT_FSIZE` 在五个写入位置限制文件大小，验证失败返回、临时文件清理、旧快照与事件水位可恢复，以及解除限制后可正常写入。该测试模拟 EFBIG，不等于已覆盖实际磁盘耗尽、断电或文件系统故障。
- 清除 DirectoryScanner、IndexPersistence、SearchEngineAdvancedQuery 中三处无效赋值。

### 正则结果高亮

用户正则的搜索与高亮均使用 RE2，替换原先 ICU 高亮加语法守卫的方案。C 接口返回 UTF-8 字节区间，Swift 映射回原字符串；缓存最后一个模式，并限制模式长度、输入长度、编译内存和匹配数量。空匹配按 UTF-8 字符前进，锚点始终相对于原输入。超限只跳过高亮，不改变搜索结果。

回归覆盖中文、emoji、空匹配、锚点、大小写、RE2 不支持的反向引用，以及曾容易产生回溯问题的表达式。固定的整词字面量高亮仍可使用转义后的 ICU 表达式，不接受用户正则结构。

### 文档与帮助

中英文 README、应用内中英文帮助与官网统一查询语法：OR 用 `|`，扩展名用分号，区间用 `..`；`parent:` 匹配完整父路径，`wfn:` 包含扩展名，`len:` 按 UTF-8 字节计数；`dc:`、`da:` 当前按修改时间处理。`regex:` 消费剩余查询，须放末尾。官网直接列出完整语法、宏和 RE2 基础说明。

项目指南改为实际使用的 SoA / StringPool、v6 基础快照和 WAL 段恢复，不再把旧分页写入、逐条 FileRecord 存储或 ICU 语法守卫描述为当前实现。历史研究和基准保留时间边界。

## 既有修复与设计约束

- 初次扫描根不可达时报告失败，不用空索引替换已有数据；重建先预检根目录。
- 本地目录按 watcher 水位恢复 FSEvents；NAS 通过轮询同步，避免网络根触发本地全量重扫。水位为零时从 `SinceNow` 开始。
- HTTP 仅绑定回环地址，统一校验认证、Host、Origin 和请求体；内容查询响应断开、服务停止和 30 秒截止时间。
- RE2 / Abseil 静态链接，保留 Hardened Runtime 与库验证；应用与 MCP 版本来自同一 Bundle。
- 单实例锁的元数据写入限制在缓冲区有效长度内；文档版本与链接有自动检查。
- `regex:` 消费剩余文本是设计约束。带转义引号或 `!` 的复杂路径表达式仍有解析边界，扩展语法前需统一转义规则。

## 验证与边界

2026-11-15 本轮快速核心测试 **1977 通过、0 失败**，正则高亮 Swift 测试 **89 通过、0 失败**；Bridge、本地化、文档版本护栏及签名回归通过。最终源码的 ASan 与 TSan 快速全集各 **1977 通过、0 失败**，无 sanitizer 报告。逐次日志、安装包复核与源码指纹保存在本地 `build/review/`。

arm64 Release 构建及静态分析成功。分析器原 15 条提示中的写入位置、rewind 和无效赋值问题已处理；剩余 1 条为 `HttpToken.h` 跨进程文件锁内读取，锁用于避免多个进程同时初始化 token，本轮保留同步语义。Xcode 另有本机 CoreSimulator 版本不匹配及 AppIntents 元数据跳过的环境提示。

此前本地真实 FSEvents 集成、端到端与延迟测试共 22 项通过；100 个文件全部找到，记录的中位延迟 301.4ms、P99 464.5ms。此为单机一次测量，不是延迟保证。性能断言应在空闲机器执行，不能用并行构建时的墙钟耗时判断算法回归。

1.11.15 的 arm64 与 x86_64 Release 构建均通过。两种 DMG 只读挂载复制后完成签名、架构、静态依赖及三份可执行文件哈希复核；GUI 各运行 8 秒未退出，CLI 帮助正常，包内 MCP 各 45 项通过，并确认版本号为 1.11.15 / 构建 15。Intel 通过 Rosetta 运行，未重做原生 Intel、其他 macOS 版本和真实 NAS 的长期验收。历史 Windows SMB 基础扫描与同步反馈不替代断线重连、权限变化、挂载点切换或长时间吞吐测试；历史全量套件和双架构构建数字也不作为本轮结果。自动化检查不替代异机安装及 GUI 人工测试。

## 验证入口

```bash
scripts/prepare-re2-deps.sh
make test-fast
make test-asan
make test-tsan
# test-tsan 目标默认只跑 Part 5；复核整个快速集：
./build/tests/test_all_tsan --fast --quiet
xcodebuild -scheme MacEverything -configuration Release build analyze
scripts/verify-release-app.sh /path/to/MacEverything.app arm64
scripts/create-dmg.sh /path/to/MacEverything.app /path/to/test.dmg
```

只做本地验证不会创建 GitHub Release 或部署官网；发布版本与时机须由维护者明确指定。

## 相关文档

- [研究与审查文档索引](README.md)
- [项目说明文档](../project-guide.md)
- [基准测试索引](../benchmark/README.md)
- [变更日志索引](../changelog/README.md)

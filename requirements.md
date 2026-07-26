# MacEverything 产品与工程约束

本文档描述当前版本的稳定产品边界。历史阶段计划、已完成审计和实现细节分别保存在 `docs/changelog/` 与 `docs/project-guide.md`，不在这里重复维护。

## 产品目标

- 在 macOS 15+ 上提供低延迟文件名、路径和内容搜索。
- GUI/菜单栏进程是唯一生产索引引擎；关闭搜索窗口不停止索引，明确退出应用才停止。
- 支持所选目录快速开始和需要 Full Disk Access 的全盘搜索。
- 通过 FSEvents 增量维护索引，并正确处理挂载、卸载、事件丢失和局部重扫。
- 提供原生 GUI、`mace` 命令行客户端、MCP stdio 代理和仅监听回环地址的 HTTP API。

## 当前架构

- SwiftUI：界面、设置、搜索会话、结果分页与文件操作。
- Objective-C++：Swift/C++ 类型转换与生命周期桥接。
- C++20：扫描、查询、内容索引、持久化、FSEvents 和 HTTP 服务。
- 索引持久化采用当前 v6 flat/SoA 格式，同时保留必要的旧版本迁移读取能力。
- GUI、CLI 和 MCP 不各自启动完整索引引擎；CLI/MCP 查询正在运行的 GUI 服务。

## 必须保持的系统约束

1. 目录遍历优先使用 `getattrlistbulk`，不得以递归 `std::filesystem` 或逐条 `stat` 替换生产扫描路径。
2. 扫描必须处理权限拒绝、firmlink、autofs、网络卷和跨卷边界，任何单个路径失败不得导致进程崩溃。
3. 共享索引目录必须先获得 instance lock；获取失败时不得加载、回放或修改该目录的索引状态。
4. 持久化写入必须检查完整写入、校验数据并通过原子替换或 WAL 保证崩溃恢复。
5. 查询、FSEvents、压缩和关闭流程必须保持明确的锁顺序与可取消性，不得在主线程等待无界后台任务。
6. HTTP 服务只绑定回环地址，校验 Host 并拒绝浏览器 Origin；启用访问令牌时，除健康检查外均要求 Bearer token。
7. 用户可见文本必须同时提供英文和简体中文 key，并通过 `make lint-localizations` 校验。
8. 发布包必须内嵌目标架构所需的 RE2/Abseil 动态库，不依赖开发机 Homebrew 路径。

## 搜索与结果边界

- 文件名和路径查询支持普通子串、多词 AND、布尔表达式、glob、正则和结构化过滤器。
- 内容查询使用 `infile:` 或 `content:`，结果包含匹配摘要。
- 文件名查询结果上限由设置控制，范围为 100–100,000，默认 10,000；GUI 每页显示 100 条。
- 内容查询结果上限独立设置，范围为 50–200，默认且最多 200；GUI 每批显示 200 条。
- 分页只影响显示，不影响各自上限内结果导出；导出不受 200 条 GUI 分页限制。
- 搜索历史默认保留 50 条，最多 200 条。
- CSV/TXT 导出覆盖当前过滤和排序后的完整查询缓存，并防止 CSV 公式注入。

## 质量门槛

- 每项行为修复必须包含能复现原问题的自动化测试。
- 提交前至少运行 `make test-fast` 和 Release Xcode 构建。
- 涉及持久化、线程或生命周期的改动应运行对应慢速测试，并在适用时运行 ASan/TSan。
- 性能声明必须注明数据规模、硬件、查询类型以及冷/热缓存条件；不得用单一最快值概括所有查询。
- Release notes 使用中文，覆盖 fork 基线以来的功能与重要修复。

## 发布产物

- 本地最终产物仅放在 `artifacts/`，目录中只保留 `.app` 和 `.dmg`。
- 默认使用 `xcodebuild -scheme MacEverything -configuration Release build` 构建应用。
- 使用 `scripts/create-dmg.sh` 或 `scripts/build-release-dmgs.sh` 生成发布 DMG。

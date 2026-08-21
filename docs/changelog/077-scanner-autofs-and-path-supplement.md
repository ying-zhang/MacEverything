# 077 — Scanner autofs 挂起修复 + 路径查询补全

## 问题

### 问题 1：应用启动挂起
应用首次启动（无缓存索引）时，DirectoryScanner 扫描到 `/System/Volumes/Data/home`（autofs 挂载点）会导致所有工作线程阻塞在 `open()` 系统调用上（因为没有配置 NFS 服务器，`open()` 会无限期等待），扫描永远无法完成，HTTP 服务也无法启动。

### 问题 2：`/usr/local/bin` 查询只返回目录本身
查询 `/usr/local/bin` 时，slash-split 逻辑将其拆为 `pathPart="/usr/local"` + `namePart="bin"`。双索引交集只能找到文件名包含 "bin" 的记录（即 `bin` 目录本身），但该目录下的文件（如 `brew`、`git`、`wget`）的文件名中不包含 "bin"，因此被遗漏。

## 根因分析

### 问题 1 根因
`getattrlistbulk` 返回每个条目的 `devid`（设备 ID），autofs 挂载点的 devid 与根文件系统不同。但 scanner 之前没有利用这个信息来跳过跨设备挂载点。

### 问题 2 根因
slash-split 的双索引交集分支在找到任意数量的结果后就不再触发 path-only 回退，导致仅靠文件名交集无法覆盖路径下所有文件的场景被忽略。

## 修复方案

### Scanner 跨设备过滤（DirectoryScanner.cpp/h）
- 在 `scan()` 开始时用 `stat()` 获取根路径的 `st_dev`（设备 ID）
- 在 `scanDirectory()` 处理每个目录条目时，比较其 `devid` 与根设备 ID
- 不同设备 ID 的目录直接跳过，不再 `open()` 或递归
- APFS firmlink（如 `/usr/local`、`/Users`）共享相同的 devid，不受影响

### 路径查询补全（SearchEngine.cpp）
- 双索引交集完成后，始终执行 path-only 补充匹配
- 将交集已找到的记录标记为 `isCandidate` 以防重复
- path-only 分支按 `pathPart` 过滤路径，按 `lowerKey` 验证全路径匹配
- 移除了调试阶段添加的冗余日志

## 验证结果

- 全部 10871 项测试通过（包括 Test 10：`/usr/local/bin` 匹配场景）
- 应用在 ~110 秒内完成 4,427,034 条记录的全盘扫描（之前无限挂起）
- HTTP 查询 `/usr/local/bin` 返回 20 条结果（目录 + 其下文件）
- HTTP 查询 `/usr/local` 返回 5 条结果
- HTTP 查询 `tests/test_query_perf` 返回 5 条结果（普通 slash 查询不受影响）

## 变更文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/DirectoryScanner.h` | 新增 `rootDevId_` 成员 |
| `MacEverything/Core/DirectoryScanner.cpp` | 根设备 ID 检测 + 跨设备目录跳过 |
| `MacEverything/Core/SearchEngine.cpp` | 双索引交集后始终补充 path-only 结果 |

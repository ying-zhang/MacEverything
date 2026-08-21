# 051 - 压缩阈值与 F_NOCACHE

- **类型**: bugfix
- **严重级别**: P1-HIGH
- **日期**: 2026-04-15
- **Commit**: `7aa385b` (merge), branch commit: `eea0b77`

## 问题描述

两个独立问题：

**P1 — 过度压缩**：WAL 仅有少量条目时（远低于合理阈值）仍触发压缩操作，产生不必要的 40-70 秒 I/O 开销。

**P2 — 压缩后缓存污染**：`saveToFile` 写入的临时文件数据进入系统页缓存（page cache），导致热搜索数据被驱逐，压缩完成后短期内搜索性能下降。

## 根因分析

**P1**：缺乏压缩前的 WAL 条目数量检查，任何时候只要满足定时条件就执行压缩，即使变更量极少也进行全量重写。

**P2**：macOS 的统一缓冲区缓存（UBC）将临时文件的写入数据缓存在页缓存中，大量索引文件写入（112MB+）污染缓存，挤出活跃的搜索数据页。

## 修复/实现方案

**P1**：增加 WAL 条目数量阈值（100 条），低于阈值时跳过压缩。`prepareForTermination` 使用 `force=true` 参数强制压缩，确保退出前数据完整持久化。

**P2**：对 `saveToFile` 的临时文件设置 `F_NOCACHE` 标志，绕过页缓存直接写入磁盘，避免驱逐热搜索数据。

## 影响的文件

- `MacEverything/Core/IndexWAL.cpp` — 条目数量阈值检查
- `MacEverything/Core/SearchEngine.cpp` — saveToFile 中设置 F_NOCACHE
- `MacEverything/Core/SearchEngine.h` — prepareForTermination force 参数

## 测试覆盖

- WAL 条目低于阈值时 compact 被跳过的验证
- force=true 时无条件执行压缩
- F_NOCACHE 标志正确设置的验证
- 压缩后搜索性能无退化的回归测试

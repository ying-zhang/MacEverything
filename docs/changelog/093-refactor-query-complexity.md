# 093 — 重构 SearchEngine::query() 复杂度

## 概述

将 `SearchEngine::query()` 从一个约 640 行、7 层嵌套的巨型方法，重构为一个精简的调度器（约 120 行）加 4 个聚焦的策略方法和 3 个静态辅助函数。纯结构性重构，行为零变更。

## 动机

原始 `query()` 方法极难理解和修改：
- 单个函数约 640 行
- 7 层嵌套控制流
- 重复代码模式（优先级计算 x8、posting list 交集 x4、路径拼接 x6）
- 职责混杂：trigram 查找、名称匹配、路径匹配、斜杠分割处理、线性扫描、排序

## 变更内容

### 提取静态辅助函数

1. **`namePriority()`** — 根据名称数据计算匹配优先级（0=精确匹配、1=前缀匹配、2=包含匹配），消除 8 处重复的优先级计算代码块。

2. **`intersectPostingLists()`** — 对关键词的 trigram posting list 取交集，消除名称和路径 trigram 查找中 4 处重复的交集代码块。

3. **`buildFullPathBuf()`** — 将 `path + '/' + name` 拼接到可复用缓冲区，采用 2 倍增长策略，消除 6 处重复的路径拼接代码块。使用 `lowerPathPool_` 中预转换的小写路径数据（无需运行时调用 `simdToLowerAscii`）。

### 提取策略方法

4. **`querySlashSplit()`** — 处理包含 `/` 的查询，将其拆分为路径和名称两部分，同时使用两个 trigram 索引进行查找。

5. **`queryPathTrigram()`** — 处理仅路径的 trigram 匹配，用于名称 trigram 未命中的查询，使用 `pathTrigramIndex_` 进行查找。

6. **`queryLinearScanPath()`** — 当 trigram 不可用或不完整时的线性扫描回退方案，用于路径匹配。包含基于 `lowerPathPool_` 的路径去重优化。

7. **`queryLinearScan()`** — 用于 glob 模式和短关键词（< 3 字符）的全量线性扫描。使用 GCD `dispatch_apply` 并行执行，并应用路径去重优化。

### query() 变为精简调度器

重构后的 `query()` 方法：
1. 验证输入、获取锁、递增 generation 计数器
2. 第一阶段：trigram 名称匹配（内联，约 20 行）
3. 第二阶段：根据查询特征委托给 4 个策略方法之一
4. 排序并截断结果

### 与 lowerPathPool_ 的集成

所有策略方法均使用 `lowerPathPool_` 实现零成本小写路径比较，与 changelog 090 引入的优化保持一致。路径去重（预扫描唯一路径到 `pathMatchCache` 向量，实现 O(1) 的逐记录查找）同时应用于 `queryLinearScanPath` 和 `queryLinearScan`。

## 变更文件

- `MacEverything/Core/SearchEngine.h` — 添加 `Match` 结构体、4 个策略方法声明、3 个静态辅助函数声明
- `MacEverything/Core/SearchEngine.cpp` — 提取辅助函数和策略方法，将 `query()` 重写为调度器

## 测试

- 全部 10,969 个快速单元测试通过（0 失败）
- 搜索相关测试通过：路径搜索、trigram、查询取消、SIMD、lowerPathPool、排名、路径表
- HTTP API 验证通过：trigram 搜索、斜杠分割搜索、线性扫描均返回正确结果
- master 分支上构建和打包成功

## 风险

零 — 纯结构性重构，行为无变更。所有原始逻辑完整保留在提取的方法中。

# 098 - SearchEngine.cpp 拆分为 3 个文件

## 背景

`SearchEngine.cpp` 已增长到 1845 行，超出项目编码规范中 1000 行上限。需要拆分以提升可维护性，也为后续 Everything 搜索语法支持（Phase 1+）做准备。

## 变更内容

将 `SearchEngine.cpp` 拆分为 3 个文件，纯机械移动，不改变任何行为：

| 文件 | 行数 | 职责 |
|------|------|------|
| `SearchEngine.cpp` | 829 | 记录管理（loadRecords, addRecord, removeByPath, compactRecords）、WAL、最近文件缓存 |
| `SearchEngineQuery.cpp` | 797 | 查询策略（trigram/linear/slash-split/path-trigram）、glob 匹配、ReusableBitmap |
| `SearchEngineIndex.cpp` | 258 | trigram 索引构建/增删、路径 trigram 索引、dirty page tracking |

## 修改文件列表

- `MacEverything/Core/SearchEngine.cpp` — 移除已迁移的代码
- `MacEverything/Core/SearchEngineQuery.cpp` — 新增，包含所有查询相关代码
- `MacEverything/Core/SearchEngineIndex.cpp` — 新增，包含所有索引构建/维护代码
- `MacEverything.xcodeproj/project.pbxproj` — 添加两个新 .cpp 文件引用

## 验证

- 编译通过（xcodebuild Release）
- 全部 11068 个测试通过，无回归
- 所有文件均在 1000 行限制以内

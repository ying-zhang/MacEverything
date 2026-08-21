# 116: 整合结构化查询与高级查询路径

## 背景

此前，查询引擎有两条互斥的执行路径：

1. **结构化查询**（`queryStructured`）：处理含 `/` 的路径查询，利用 `pathSegmentsMatch()` 进行组件级右向左匹配，并通过 path trigram 加速候选集筛选。
2. **高级查询**（`queryAdvanced`）：处理含过滤器（`ext:`, `size:` 等）、布尔运算（`|`, `!`, `<>` 分组）或引号的查询，通过 AST 解析后逐记录求值。

当查询同时包含路径和高级语法（如 `/usr/local ext:cpp`）时，`hasAdvancedSyntax()` 会将其路由到高级查询路径，导致路径部分仅做简单的全路径子串匹配，丢失了：
- 路径组件级匹配（右向左、邻接控制）
- path trigram 加速
- name/path 分离评分

## 设计方案

**AST 后处理**：在 `queryAdvanced()` 内部，AST 解析完成后，通过 `transformSlashTerms()` 将含 `/` 的 TERM 节点转化为结构化约束：

```
TERM("/usr/local/test") → AND(FILTER("__pathseg", segments=[usr,local]), TERM("test"))
```

`__pathseg` 是一个内部合成 filter，在 `evalFilter()` 中通过调用 `pathSegmentsMatch()` 实现组件级匹配。

## 实施

### 新增文件
- `MacEverything/Core/ASTStructuredTransform.h`：AST 后处理逻辑，递归遍历 AST 将含 `/` 的 SUBSTRING TERM 转化为 `__pathseg` filter + name TERM
- `tests/test_ast_structured_transform.h`：60 个单元测试覆盖基础拆分、DIR_EXACT、嵌套 AND/OR/NOT、大小写保留等场景

### 修改文件
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp`：
  - 集成 `transformSlashTerms()` 调用
  - 在 `evalFilter()` 中实现 `__pathseg` filter 评估
  - 添加 path trigram 预过滤逻辑（利用 `pathTrigramIndex_`）
  - 更新 `extractScoringTerm()` 和 `bestTrigramTerm()` 以跳过 `__pathseg` filter
- `MacEverything/Core/QueryAST.h`：添加 `pathSegments` 和 `structuredMode` 字段
- `MacEverything/Core/QueryNeedsAnalysis.h`：识别 `__pathseg` filter，设置 `needsPath=true`
- `MacEverything/Core/SearchEngine.h`：将 `pathSegmentsMatch` 设为 public
- `test_all.cpp`：注册 Part 64 测试

## 验证结果

### 单元测试
- Part 64（AST Structured Transform）：60/60 通过
- Part 55（Query Parser）、58（Structured Query）、62（QueryNeedsAnalysis）均无回归

### HTTP 功能验证
| 查询 | searchPath | 结果 |
|------|-----------|------|
| `/usr/local ext:h` | advanced-trigram | 正确返回 /usr/local 下的 .h 文件 |
| `/usr/bin size:>100kb` | advanced-trigram | 正确返回 /usr/bin 下的大文件 |
| `/src/components \| /lib/utils` | advanced-linear-gcd | OR 路径段正常工作 |
| `/usr/local/bin` (纯结构化) | structured | 仍走快速结构化路径（2ms） |

## 关键决策
- 不改变分发逻辑（`hasAdvancedSyntax()` 门控不变），通过 AST 后处理在高级查询内部增强
- `__pathseg` 设计为内部 filter，对用户透明
- Glob 模式（`/usr/*/test`）由 `parseQuery()` 返回非 PLAIN 模式时正常处理，返回 PLAIN 时保持原样

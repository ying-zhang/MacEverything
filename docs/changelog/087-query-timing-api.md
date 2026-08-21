# 087 - Add Detailed Query Timing Breakdown to HTTP API

## 变更内容

在 `/api/search` HTTP 响应中新增 `timing` 对象，暴露搜索每个步骤的精确耗时和统计信息。

## 新增字段

```json
{
  "timing": {
    "totalMs": 3.14,
    "lockWaitMs": 0.00,
    "trigramMs": 2.16,
    "phase1Ms": 0.00,
    "phase2Ms": 0.98,
    "sortMs": 0.00,
    "lockHeldMs": 3.14,
    "totalRecords": 4734331,
    "candidates": 11,
    "nameMatches": 11,
    "pathMatches": 0,
    "resultCount": 11,
    "usedTrigram": true,
    "searchPath": "trigram"
  }
}
```

### 字段说明

| 字段 | 含义 |
|------|------|
| `totalMs` | 搜索引擎内部总耗时（不含 JSON 序列化） |
| `lockWaitMs` | 等待 shared_lock 的时间 |
| `trigramMs` | Trigram 索引查询 + 交集计算 |
| `phase1Ms` | 名称验证（trigram 候选的 SIMD 确认） |
| `phase2Ms` | 路径搜索 / NEON 全表扫描 |
| `sortMs` | 结果排序 |
| `lockHeldMs` | 持锁总时间 |
| `totalRecords` | 索引中的总记录数 |
| `candidates` | Trigram 候选数量 |
| `nameMatches` | 文件名匹配数 |
| `pathMatches` | 路径匹配数 |
| `resultCount` | 最终返回结果数 |
| `usedTrigram` | 是否使用了 trigram 索引 |
| `searchPath` | 搜索路径：`trigram` / `trigram-split` / `linear` |

## 实现方式

1. 新增 `QueryTimingInfo` 结构体（`SearchEngine.h`）
2. 新增 `query()` 重载，接受 `QueryTimingInfo&` 引用参数
3. 原 `query()` 委托给新重载
4. HTTP handler 使用新重载并在 JSON 中序列化 timing 对象

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngine.h` | 新增 `QueryTimingInfo` 结构体和 `query()` 重载 |
| `MacEverything/Core/SearchEngine.cpp` | 实现带 timing 的 `query()` 重载 |
| `MacEverything/Core/HttpServer.cpp` | 在 JSON 响应中输出 timing 对象 |

## 性能测试结果（~473 万文件索引）

### Trigram ON vs OFF 对比

| Query | Trigram ON | OFF (NEON) | 加速比 | 搜索路径 |
|-------|-----------|------------|--------|---------|
| hello | 0.12ms | 41.79ms | 348x | trigram |
| .cpp | 0.62ms | 43.36ms | 70x | trigram |
| test | 3.50ms | 46.60ms | 13x | trigram |
| main | 1.33ms | 42.69ms | 32x | trigram |
| index | 2.65ms | 39.36ms | 15x | trigram |
| README.md | 1.28ms | 37.89ms | 30x | trigram |
| package.json | 2.14ms | 36.62ms | 17x | trigram |
| .swift | 0.45ms | 50.27ms | 112x | trigram |
| config | 2.47ms | 35.11ms | 14x | trigram |
| CMakeLists.txt | 0.72ms | 35.45ms | 49x | trigram |
| .gitignore | 0.55ms | 48.76ms | 89x | trigram |
| SearchEngine.cpp | 3.14ms | 35.84ms | 11x | trigram |

### 路径搜索

| Query | Trigram ON | OFF (NEON) | 搜索路径 |
|-------|-----------|------------|---------|
| /usr/local | 43.53ms | 35.25ms | trigram-split |
| /etc | 2.05ms | 47.44ms | trigram-split |
| tests/test | 23.62ms | 44.75ms | trigram-split |
| /opt/homebrew/bin | 38.16ms | 46.13ms | trigram-split |

### 短关键词（< 3 字符，无法使用 trigram）

| Query | ON/OFF 均为 | 搜索路径 |
|-------|-------------|---------|
| py | ~46ms | linear |
| .h | ~50ms | linear |
| a | ~38ms | linear |

### 关键发现

1. **Trigram 文件名搜索**：11x-348x 加速，取决于候选集大小
2. **Trigram 耗时构成**：trigram 索引查询约 0.05-2.5ms，phase1 验证约 0.05-2.9ms
3. **NEON 全表扫描**：稳定在 35-50ms（约 473 万记录），无崩溃
4. **路径搜索**：trigram-split 对长路径效果有限（仍需全路径验证），短路径如 `/etc` 有 23x 加速
5. **排序开销**：大结果集 0.5-2.3ms，小结果集 <0.05ms，可忽略
6. **锁等待**：始终为 0ms，无竞争

## 验证

1. Release 构建通过
2. DMG 打包 + app 启动正常
3. 20 种 query x 2 模式（trigram ON/OFF）= 40 次请求全部成功，零崩溃
4. timing 对象在所有响应中正确返回

# 085 — 同类文件搜索产品竞品分析

## 变更类型
研究文档

## 规划
对 MacEverything 的同类产品进行全面竞品分析，覆盖 Windows、Linux、macOS 三大平台以及跨平台工具，重点关注：
- 核心算法实现（索引策略、搜索算法、增量更新机制）
- 速度优化技巧（SIMD、内存布局、并发模型）
- Mac App Store 商业产品与 GitHub 开源项目的市场格局

## 实施情况

### 产出文件
- `docs/research/competitive_analysis.md` — 完整竞品分析报告（9 个章节）

### 分析覆盖范围

| 平台 | 产品 |
|------|------|
| Windows | voidtools Everything (MFT 直接解析、USN Journal、线性扫描) |
| Linux | FSearch (delta 压缩索引)、plocate (trigram + TurboPFor) |
| macOS 原生 | Spotlight (mdworker)、Find Any File (searchfs)、EasyFind、HoudahSpot、Alfred、Raycast、LaunchBar |
| 跨平台 | fd (并行 DFS)、ripgrep (Teddy/SIMD)、fzf (Smith-Waterman 模糊匹配) |
| GitHub 开源 | Cardinal (Tauri/Rust ~1000 stars)、macfind、searchfs CLI、KatSearch、plocate-macos 等 9 个项目 |

### 核心发现
1. **MacEverything 架构已与行业最佳实践对齐**：getattrlistbulk 批量扫描、trigram 倒排索引、SoA 内存布局、FSEvents+WAL 增量更新
2. **主要市场缺口**：Mac 平台尚无产品能实现 sub-100ms 文件名搜索 + 轻量持久化索引 + 实时增量更新的完整组合
3. **优化方向**：短查询 SIMD 直扫、模糊匹配、零拷贝索引加载、delta 压缩持久化

### 关键竞品对比

| 维度 | Everything (Win) | FSearch (Linux) | Cardinal (Mac) | MacEverything |
|------|------------------|-----------------|----------------|---------------|
| 扫盘 | MFT 直读 | readdir 多线程 | Rust 并行 walkdir | getattrlistbulk |
| 索引 | 内存线性表 | delta 压缩 | mmap slab | trigram 倒排 |
| 搜索 | 线性扫描 | 线性 + 排序 | Rabin-Karp | trigram 交集 |
| 增量 | USN Journal | inotify | FSEvents | FSEvents + WAL |

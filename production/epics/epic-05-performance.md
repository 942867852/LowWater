# Epic 05 · 性能预算（Core 验收 5）

- **指向**：`game-concept.md §四-CORE 验收 5` ｜ `control-manifest.md §5`（5a–5j）｜ `architecture.md §8`（tick 分级预算）｜ `02-npc-simulation.md §8`（取舍声明）｜ `ADR-005`（定点）· `ADR-003`（LOD）
- **一句话验收**：GTX 1060 6GB / 16GB RAM / SATA SSD / 1080p，场内模拟实体 **≤120**（full-behavior **≤32**，其余 coarse tick），**仿真（非渲染）耗时 ≤6ms/帧**。
- **⚠️ 口径冲突（须主理人裁决）**：概念文档写 `full-behavior ≤40`；任务硬约束 + `S2 §8` 取舍声明 + `ADR-000 F-8` 均为 **≤32**。**本 Epic 按 32 执行**，回传时列为待裁决项。
- **双口径（取严）**：① 单帧硬上限 `max_per_frame ≤ 6.0ms`（架构最坏 **4.880ms**，余 18.7%）；② 摊销 **≈0.057ms/帧**（对 6ms 有 105× 余量）。**二者都满足才算过**。
- **涉及系统**：S2（NPC）主 · S1（生理/容器）· S6（传闻批处理）· S0·B（时钟/批处理/快进）· S0·C（RNG/worldHash）
- **涉及契约**：**无新结构**；只消费 `ViewSnapshot`（实体计数）· `DebugSnapshot`（LOD 计数）。**不改任何契约。**
- **依赖**：Epic 00（E0-S3/S7）· Epic 01（S1/S2/S5 实体规模）· Epic 02（E2-S1/S7 渲染侧）
- **放行闸门**：**本 Epic 全部为仿真侧，不需 1060**；渲染侧（不属于本验收但必须测）→ **E2-S7**（闸门 A/B 本体）

---

## 目标与验收（`control-manifest §5`，5a–5j 全部成立）

| # | 指标 | 阈值 | 测量点 | 归属 Story |
|---|---|---|---|---|
| 5a | `max_per_frame`（单帧仿真墙钟） | **≤ 6.0 ms** | 每帧 | E5-S1 · E5-S5 |
| 5b | S2 `p95_per_tick` | **≤ 3.20 ms** | 每 `SIM_TICK` | E5-S1 · E5-S5 |
| 5c | S1 `p95_per_tick` | **≤ 1.20 ms** | 每 tick | E5-S1 · E5-S5 |
| 5d | S6 单日批处理 `p95` | **≤ 1.5 ms** | 每 `DAILY_CUTOVER` | E5-S1 |
| 5e | 快进 8 游戏小时总墙钟 | **≤ 800 ms** | 每次 SLEEP | E5-S4 · E5-S5 |
| 5f | 场内实体数 | **≤ 120**（actor ≤77 + 容器/信号 ≤43） | 每 tick 断言 | E5-S3 |
| 5g | `FULL` 并发数（含 PROTECTED） | **≤ 32** | 每 tick 断言 | E5-S3 |
| 5h | `PROTECTED` 数 | **≤ 3** | 启动期断言 | E5-S3 |
| 5i | 仿真侧内存 | **≤ 4 MB**（记账户上界 3.7MB） | 每 60 tick 采样 | E5-S4 |
| 5j | 存档写入 | **≤ 200 ms** | 每次 save | E5-S4 |

> **失败动作**（硬纪律）：构建失败 + 输出 per-system 分解（定位超支系统）→ **走预算复议，不得就地放宽**（S2 §8 原话："宁明示取舍，不假装装得下"）。

---

## Story 清单（按实现顺序）

### E5-S1 · `--perf` 计时探针（per-tick / per-frame p50/p95/max）
- **判据（跑 Y 得 Z）**：`simcore_cli perf --scenario core-stress --seed 20250910 --days 3` 打印 `max_per_frame / s2.p95 / s1.p95 / s3.p95 / s4.p95 / s6.daily.p95 / fastforward.8h / mem.sim / entities / lod.full`；**计数类**（entities/lod.full）同 seed 逐位复现。
- **涉及契约**：无（计时探针）；消费 `DebugSnapshot`（实体/LOD 计数）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E0-S7 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E5-S2）
- **备注**：**墙钟量不入 golden**（见 `tests/STORY_CASE_TEMPLATE.md`）；CI 只断**上限**不断绝对值，计数类走 golden。

### E5-S2 · `core-stress` 压力场景脚本（固定、可重放）
- **判据（跑 Y 得 Z）**：`simcore_cli scenario --name core-stress` 脚本化固定执行：**两社区往返 + 强制 32 个 FULL + 触发一次日切 + 一次 DISPATCH + 一次 8h 快进 + 一次 80% 清空 zone**；同 seed 逐位复现。
- **涉及契约**：复用 `drain-zone-x`（E1-S9）的清空段 · `--force-*` 钩子仅场景内可用
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E1-S9 ｜ **工作量**：2 人日 ｜ **可并行**：✓（与 E5-S1）
- **备注**：场景是 5a–5e 的共同输入，**必须固定**（否则数字不可比）。

### E5-S3 · 实体 / LOD 上限断言（5f / 5g / 5h + 门禁 B14）
- **判据（跑 Y 得 Z）**：每 tick 断言 `entities ≤ 120`（actor ≤77 + 容器/信号 ≤43）、`lod.full ≤ 32`（含 PROTECTED）、`protected ≤ 3`；超限**自动报警**（不静默）。`--dump-lod` 打印每 actor `tickLevel`。
- **涉及契约**：`simLOD` 三档（`FULL/PROTECTED/COARSE`）· `PROTECTED` 名单唯一定义处（S0 §D，C-27，**禁止两份**）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E1-S7 · E1-S8 ｜ **工作量**：2 人日 ｜ **可并行**：✓
- **备注**：`LOD_FULL_MAX` 取 **32**（ADR-000 F-8）；取 40 会破 S2 自留 3.20ms（architecture §8 注 2）。

### E5-S4 · 内存核算 + 存档写入 + 快进墙钟（5e / 5i / 5j）
- **判据（跑 Y 得 Z）**：`--perf` 每 60 tick 采样 `mem.sim ≤ 4MB`；`fastforward.8h ≤ 800ms`；save 写入 `≤ 200ms`；`--dump-memsize` 打印分项（actor 常驻 / 写队列 / AttributionLog / DaySnapshot / 容器表）。
- **涉及契约**：`SaveGame v1` 字段集（写入耗时）· `AttributionLog` 环形缓冲大小
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E0-S3 ｜ **工作量**：2 人日 ｜ **可并行**：✓

### E5-S5 · CI 接线（headless 仿真侧门禁）
- **判据（跑 Y 得 Z）**：`ci.sh` 增加 `perf` 步骤：headless `core-stress` 断言 `max_per_frame≤6.0 / s2.p95≤3.20 / s1.p95≤1.20 / fastforward.8h≤800`；超支退出码 1，并输出 **per-system 分解**（定位超支系统）。
- **涉及契约**：无
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E5-S1/S2 ｜ **工作量**：1 人日 ｜ **可并行**：✗（收口）

> **渲染侧（不属本验收，但必须测）**：`tools/render_smoke.py --target 1060 --res 1080p` 记录总帧时间与渲染分项；GTX 1060 上 ≥30fps。**由 E2-S7 承担**（闸门本体）——**诚实标注：开发机 GT 730 为 FAIL，1060 数据至今未实测**。

---

## 依赖图（Epic 内）

```
E0-S7 ─▶ E5-S1 ─┐
E1-S9 ─▶ E5-S2 ─┼─▶ E5-S5（CI）
E1-S7/S8 ─▶ E5-S3 ┤
E0-S3 ─▶ E5-S4 ──┘
（渲染侧）E2-S1 ─▶ E2-S7（闸门，独立）
```

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| **1060 未实测 → 预算全是"架构分配值非实测"**（architecture §8 注 4） | 首个可玩版本各系统用 `--perf` 实测回填；任一超支走预算复议 |
| 口径 `40 vs 32` 未裁决 | 本 Epic 按 **32**；回传列待裁决；两值都不改契约 |
| CI 墙钟门禁受机器负载影响（flaky） | 门禁**留大余量**（4.88 vs 6.0 = 18.7%）；只断上限不断绝对值；计数类走 golden |
| 视锥内匿名细节需求膨胀 | 匿名 FULL 上限 **16**（总 FULL=32）为默认策略；要更多须**重新申请配额**（S2 §8） |
| `PROTECTED` 被实现成两份 | 唯一定义处 S0 §D（C-27）；E5-S3 每启动断言 `≤3` |

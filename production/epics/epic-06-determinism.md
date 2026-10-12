# Epic 06 · 可确定性（Core 验收 6）

- **指向**：`game-concept.md §四-CORE 验收 6` ｜ `control-manifest.md §6`（6a–6f）｜ `S0 §C3/C4` ｜ `ADR-002`（可分流 RNG）· `ADR-003`（离屏 LOD，R-B1/R-B2）· `ADR-005`（定点）
- **一句话验收**：同一存档 save/load 后续跑 10 日，安全天数曲线与原固定种子回放**差异 ≤5%**；`worldHash` **逐位相同**；**LOD 近似是唯一合法差异来源**且必须在调试面板可见。
- **公式**（S0 §C3）：`max_d |S_orig[d] − S_reload[d]| / max(S_orig[d],1) ≤ 5%`，`d ∈ [d0, d0+10]`。**理想值 0%。**
- **涉及系统**：S0·C（确定性）主 · S0·B（时钟/存档）· S2（LOD 分级）· **全系统**（`worldHash` 量 = 生理 + 库存 + 位置量化 0.1m + 关系值）
- **涉及契约**：`SaveGame v1` · `worldHash` · `ReplayRecord` · `StreamId` 注册表 · `SimcoreVersion` 三元组（`gameVersion / simSchemaVersion / streamIdRegistryVersion`）
- **依赖**：Epic 00（E0-S3 replay）· Epic 01（S1/S2 状态量）
- **放行闸门**：**无**（纯仿真侧）；「差异来源」面板为调试侧（`DebugSnapshot`），**不需 1060**

---

## 目标与验收（`control-manifest §6`，6a–6f 全部成立）

| # | 指标 | 阈值 | 归属 Story |
|---|---|---|---|
| 6a | `max_d |ΔSafeDays| / max(S_orig[d],1)` | **≤ 5%** | E6-S3 |
| 6b | `worldHash` 逐位比对（每 60 tick） | **逐位相同** | E6-S2 |
| 6c | 全 COARSE 10 日 vs 全 FULL 10 日 | 差异 **≤5%** 且差异来源面板可见 | E6-S4 |
| 6d | `fastForward(8h)` vs 连续 `simTick × 480` | **逐位相同** | E6-S6 |
| 6e | 恢复后首 tick | **必须是 `savedTick + 1`**（不跳号、不补跑） | E6-S1 |
| 6f | 版本不一致（三元组任一） | **拒绝重放 + 明确提示，不静默** | E6-S5 |

> **RNG 纪律（红线，S0 §C2）**：禁裸 `rand`/系统时间/未初始化内存；禁跨 `streamId` 共享计数器；**判定必须在固定 tick 上发起，禁止在渲染帧上发起**。`makeSeedCtx` 第二参永为 `e.absTick`（门禁 B5）。

---

## Story 清单（按实现顺序）

### E6-S1 · save/load 恢复语义 + 存档字段集（6e）
- **判据（跑 Y 得 Z）**：`simcore_cli replay --days 10 --save-at 3 --load-and-continue` 恢复后**首 tick == `savedTick + 1`**（不跳号、不补跑）；存档含 `worldSeed / dayKey / minuteOfDay / 每 streamId counter / 写队列全部条目 / 每 actor LOD 状态 / DaySnapshot[D-1..D] / AttributionLog 尾部`；**不依赖运行时缓存**（路径/导航须可由 seed 重建）。
- **涉及契约**：`SaveGame v1` 字段集 · `StreamId` 注册表 · `worldHash`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E0-S3 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E6-S2）

### E6-S2 · `worldHash` 每 60 tick + 逐位比对 + 首次不一致 dump（6b）
- **判据（跑 Y 得 Z）**：每 60 tick 生成 `worldHash = FNV1a(生理 + 库存 + 位置量化 0.1m + 关系值)`；两次运行**逐位相同**；**首次不一致即中断并 dump 该 tick 全量状态**（S0 §C4）。
- **涉及契约**：`worldHash` 规范子集（≈3.76 KB）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E0-S3 ｜ **工作量**：2 人日 ｜ **可并行**：✓
- **备注**：**禁把指针/时间/迭代序入 hash**（否则假不确定）。

### E6-S3 · `determinism_diff`（safeDays ≤5%，6a）
- **判据（跑 Y 得 Z）**：`tools/determinism_diff.py --a runs/orig.jsonl --b runs/reload.jsonl --metric safeDays --threshold 0.05` 输出 `max_d|Δ|/max(S,1)` 并断言 ≤5%。
- **涉及契约**：`DaySnapshot[D].safeDays` 序列
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E6-S1 ｜ **工作量**：2 人日 ｜ **可并行**：✓

### E6-S4 · 全 COARSE vs 全 FULL 差异 ≤5%（6c）+ 差异来源可见
- **判据（跑 Y 得 Z）**：`replay --lod all-coarse` vs `--lod all-full` 10 日差异 ≤5%；差异来源写入 `DebugSnapshot`（面板「差异来源」展示）；连续量在 boundary 量化（ADR-003 **R-B1**）+ 整数余数累加器（**R-B2**），**禁整段平均**。
- **涉及契约**：`simLOD` 三档 · `DebugSnapshot`（`ViewSnapshot` 超集）
- **触碰 Godot 表现层**：否（断言走 headless dump；面板仅展示）｜ **需 1060 冒烟**：否
- **前置**：E1-S7 · E0-S7 ｜ **工作量**：3 人日 ｜ **可并行**：✓
- **备注**：`renderLOD` **不含视锥**、不进存档/worldHash（ADR-000 F-7，裁决 28）。

### E6-S5 · 版本一致性守卫（6f）
- **判据（跑 Y 得 Z）**：`gameVersion / simSchemaVersion / streamIdRegistryVersion` 任一不一致 → **拒绝重放 + 明确提示，不静默**；启动时校验 `streamId` 枚举与存档一致，缺失即报错（S0 §C7-2）。
- **涉及契约**：`SimcoreVersion` 三元组 · `StreamId` 注册表
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E6-S1 ｜ **工作量**：2 人日 ｜ **可并行**：✓

### E6-S6 · `--ff-8h-vs-tick` 逐位（6d）+ CI 接线
- **判据（跑 Y 得 Z）**：`replay --ff-8h-vs-tick` 逐位相同（门禁 B7）；`ci.sh` 增加 `determinism` 步骤（**3 seed**）：任一 `worldHash` 不一致或 `ΔsafeDays >5%` → 退出码 1；长跑（30 日）每周。
- **涉及契约**：无（CI）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E6-S1/S2/S3/S4/S5 ｜ **工作量**：1 人日 ｜ **可并行**：✗（收口）
- **备注**：E0-S3 已把"快进 ≡ 逐 tick"覆盖到 `verify`；本 Story 把 `--save-at / --lod` 两模式补齐到 CLI 并接 CI。

---

## 依赖图（Epic 内）

```
E0-S3 ─┬─▶ E6-S1 ─▶ E6-S3 ─┐
       └─▶ E6-S2 ──────────┼─▶ E6-S6（CI）
E0-S7 ─▶ E6-S4 ───────────┤
E1-S7 ─▶ E6-S4            │
E6-S1 ─▶ E6-S5 ───────────┘
```

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| **R1：FULL/COARSE 连续量分叉 → 10 日差异 >5%** | boundary 量化（R-B1）+ 整数余数累加器（R-B2）+ **禁整段平均**；CI 常跑 6c 并把差异来源在面板可见 |
| `worldHash` 被非确定量污染 | 规范子集**限定**（生理+库存+位置 0.1m+关系值）；**禁**指针/时间/迭代序入 hash |
| 恢复后跳号/补跑 | 首 tick 断言 `== savedTick+1`；HUD 显示绝对 tick 供复核 |
| 新增 `streamId` 未进存档 → counter 归零 → 重放偏移 | 启动校验 `streamId` 枚举与存档一致（S0 §C7-2） |
| 浮点悄悄混入 | 门禁 B9（`double/float/sin/exp/sqrt` == 空）+ `mulM/divM` 唯一实现（ADR-005） |

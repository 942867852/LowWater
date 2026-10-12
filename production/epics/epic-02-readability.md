# Epic 02 · 可读性达标（Core 验收 2）

- **指向**：`game-concept.md §四-CORE 验收 2` ｜ `control-manifest.md §2`（盲测协议）｜ `09-presentation-ux.md §8 A1/A2/A13` ｜ `accessibility.md`（三级分级）
- **一句话验收**：不给 UI 文字提示，测试者仅看画面游玩 2 个游戏日后，能预测某指定 NPC 在次日 **06:00 / 12:00 / 20:00** 的位置，**三选三命中率 ≥70%**（样本 ≥12 受试 × 3 NPC = 36）。
- **这条"为什么不能自动化"**：它测的是**人类从画面提取规律**的能力，本质是可用性测试（`control-manifest §2`）。本 Epic 只负责"把测量误差压到最小"的工具与通道，**判据由人工执行**。
- **涉及系统**：S9（呈现，四条通道 C-1–C-4）· S2（作息/信号的可读性来源）· S0·B（昼夜时钟）
- **涉及契约（只读）**：`ViewSnapshot`（`SimViewActor`/`SimViewCamera`/`SimViewHud`）· `activityId` · `needSeverity[6]` · `carryingSilhouette` · `renderHint` · `WorldSignal`（`SimWorldSignal`）· **不新增任何字段**（`view-contract` 命名冻结）
- **依赖**：Epic 01（作息/信号可复现）· Epic 00（E0-S7 钩子）
- **放行闸门**：**本 Epic 全部 Story 依赖 GTX 1060 渲染冒烟（闸门 A 入口 / 闸门 B 出口，见 `production/vertical-slice.md §5`）**

---

## 目标与验收（`09-presentation-ux §8`）

| 通道 | 内容 | 判据 |
|---|---|---|
| **C-1 时间锚** | 昼夜色温四档 + 三个世界标点（06:00 灯灭→聚集→散开，方向各异） | 关 HUD 时钟组命中率仍 ≥70%（A13） |
| **C-2 岗位可辨** | 无底盘=人 + 岗位 glyph 名签（10 glyph × 3 底盘）；同 NPC 形状两日一致 | 关闭色相/静音/关闭运动下仍可判读（A2） |
| **C-3 路线可读** | 固定通勤路线 + 踩实旧径 + 岔口脚印；偏离**只**在变化预告时发生 | 同上 |
| **C-4 携带物剪影** | 8m 完整 / 25m 剪影+步频 / 60m 静止·移动·有灯 | 同上 |

> **HUD 纪律**（`09-presentation-ux §6.3`）：**凡是关于世界的答案，HUD 一律不给**；不给 urgency 数字（裁决 21）、不给全图列表/箭头、不给甘特/需求数值/归因链。
> **表现层零判定**：`src/render`/`src/ui` 内 `rollCheck|makeSeedCtx` 命中 == 0（`control-manifest B4`）。

---

## Story 清单

### ★ E2-S1 · Godot 4 表现层骨架（消费 `ViewSnapshot`）　【需 1060 冒烟：闸门 A/B】
- **判据（跑 Y 得 Z）**：Godot 4.5 加载 GDExtension，`LowWaterSim.new()` 可调 `simcore`；一帧内 `advance(dtReal)` → 读 `simcore_view_snapshot()` → 渲染 ≤120 实体简模；输入入队 → 下一 `SIM_TICK` 消费。**桥接已跑通**（见 `godot-cpp-bridge.md` §4 期望输出）。
- **涉及契约**：`ViewSnapshot`（只读值拷贝）· `InputCommand` · `SimViewCamera`
- **触碰 Godot 表现层**：**是** ｜ **需 1060 冒烟**：**是（闸门 A 入口）**
- **前置**：E0-S7 ｜ **工作量**：3 人日 ｜ **可并行**：✗（表现层串行起点）
- **备注**：**开工前必须先跑闸门 A**（1060 基线）；`ViewSnapshot` 只读、发布只在 tick 边界（双缓冲）。

### ★ E2-S2 · C-1 时间锚（昼夜色温四档 + 三个世界标点）　【需 1060 冒烟：闸门 B】
- **判据（跑 Y 得 Z）**：画面在 06:00/12:00/20:00 恰落三档色温；06:00 出现"灯灭→聚集→散开"三连标点；`--assert-legibility` 截帧校验 ΔL\* ≥ 18 且对比 ≥ 4.5:1（正午/黄昏/夜各一次）。
- **涉及契约**：`SimViewHud{minuteOfDay}` · 世界信号 `WATCH_ADDED`（灯）
- **触碰 Godot 表现层**：**是** ｜ **需 1060 冒烟**：**是（闸门 B）**
- **前置**：E2-S1 · E1-S7 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E2-S3/S4）
- **备注**：C-1 权重受**关钟对照组**校准（S9 A13）——若关钟组 <70%，回本 Story 加强色温曲线/标点。

### ★ E2-S3 · C-2 岗位可辨（glyph 名签，10 glyph × 3 底盘）　【需 1060 冒烟：闸门 B】
- **判据（跑 Y 得 Z）**：同一 NPC 两日形状完全一致；名签 = `homePost` glyph + 名字 + 固定侧位；`--assert-glyph-alphabet` 断言除 10 glyph × 3 底盘外自建图标数 == 0；关闭色相仍可判读（三重编码）。
- **涉及契约**：`SimViewActor{activityId,isNamed}` · 形状字母表（`accessibility §2.1`）
- **触碰 Godot 表现层**：**是** ｜ **需 1060 冒烟**：**是（闸门 B）**
- **前置**：E2-S1 · E1-S6 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E2-S2/S4）
- **备注**：色相仅作冗余；**禁止系统自建第二套图标**。

### ★ E2-S4 · C-3 路线可读（通勤路线 + 脚印 + 偏离信号）　【需 1060 冒烟：闸门 B】
- **判据（跑 Y 得 Z）**：NPC 走固定通勤路线；踩实旧径/岔口脚印在 `observeRadius` 内可见；`REROUTE` 的 `FOOTPRINT_DEVIATION` 信号先于行为改变（≥6 游戏小时）出现；`--assert-line-minpx` 绊线屏幕宽度 ≥1.5px。
- **涉及契约**：`Block.route` · `WorldSignal{kind:REROUTE}` · `SimWorldSignal.installTick/applyTick`
- **触碰 Godot 表现层**：**是** ｜ **需 1060 冒烟**：**是（闸门 B）**
- **前置**：E2-S1 · E1-S7 ｜ **工作量**：2 人日 ｜ **可并行**：✓
- **备注**：`renderLOD` 切换前后 `activity` 语义一致（`--assert-lod-invariance`）。

### ★ E2-S5 · C-4 携带物剪影（8m/25m/60m 三档）　【需 1060 冒烟：闸门 B】
- **判据（跑 Y 得 Z）**：8m 出完整桶/灯/工具/袋；25m 降为剪影+步频；60m 只余 静止·移动·有灯；随 `carryingSilhouette` 值切换。
- **涉及契约**：`SimViewActor{carryingSilhouette,renderHint}` · `carryingSilhouette` 闭集 5 值
- **触碰 Godot 表现层**：**是** ｜ **需 1060 冒烟**：**是（闸门 B）**
- **前置**：E2-S1 ｜ **工作量**：2 人日 ｜ **可并行**：✓

### ★ E2-S6 · 盲测协议工具（`dump-actor-pos` + `readability_score`）
- **判据（跑 Y 得 Z）**：`simcore_cli dump-actor-pos --at 360,720,1200 --out actual.json` 导出实际位置；`tools/readability_score.py --guess guesses.csv --actual actual.json` 输出命中矩阵与三选三命中率；`--clock-off` 输出关钟对照组命中率。
- **涉及契约**：`SimViewActor.posMm`（**判定区域** = `OBSERVE_RADIUS` 与 20m 取大；或同 `postId`/同 `block.intentTag`）
- **触碰 Godot 表现层**：否（headless）｜ **需 1060 冒烟**：否
- **前置**：E0-S7 · E1-S5 ｜ **工作量**：2 人日 ｜ **可并行**：✓
- **备注**：埋点仅作**诊断**（定位"作息不可读"vs"信号没被看到"），不作判据。

### ★ E2-S7 · 1060 渲染冒烟执行（放行闸门 G4）　【需 1060 冒烟：**闸门本体**】
- **判据（跑 Y 得 Z）**：在 ≥4GB VRAM / Vulkan 或 D3D12 / 1080p 机器跑 `tools/gpu-smoke` → `result.json` 判 `PASS`（≥30fps）且 `report.md` §1 确认后端非 OpenGL、分辨率 1080p；回填 `engine-evaluation §E4.2` 三行 🟨 与 `information_channel_ms`（<1.20ms）、`worst_frame_p95_ms`（≤16.7ms）。
- **涉及契约**：无（渲染侧）
- **触碰 Godot 表现层**：**是** ｜ **需 1060 冒烟**：**是（闸门 A/B 本体）**
- **前置**：E2-S1 ｜ **工作量**：1 人日（换机，非开发）｜ **可并行**：✗
- **备注**：**诚实标注**：开发机（GT 730）为 FAIL；**1060 数据至今未实测**。`FAIL` → **走预算复议**，不得就地放宽（S2 §8）。

### E2-S8 · 验收 2 盲测执行（**人工**，执行方：测试 严守真）
- **判据（跑 Y 得 Z）**：≥12 受试、release 构建、关全部调试 UI、自由游玩 2 游戏日 → 填纸质/独立表单预测 3 名 NPC×3 时刻 → **三选三命中率 ≥70%**（且样本 ≥36）；并跑关钟对照组（A13）。
- **涉及契约**：无（人工协议）
- **触碰 Godot 表现层**：**是（被测对象）** ｜ **需 1060 冒烟**：**是（闸门 B 出口，未过则测试无效）**
- **前置**：E2-S2/S3/S4/S5/S6 · E2-S7 ｜ **工作量**：人工（≥1 日）｜ **可并行**：✗
- **备注**：Core 里程碑建议 3 轮（首个可玩 / 中段 / 放行前）；失败 → 用埋点定位后回对应通道 Story。

---

## 依赖图（Epic 内）

```
E0-S7 ─▶ E2-S6
E1-S5 ─▶ E2-S6
E1-S6 ─▶ E2-S3
E1-S7 ─▶ E2-S2 ─┐
E2-S1 ─┬─▶ E2-S2 ─┼─▶ E2-S8（人工）
       ├─▶ E2-S3 ─┤
       ├─▶ E2-S4 ─┤
       ├─▶ E2-S5 ─┘
       └─▶ E2-S7（闸门 A/B）
```

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| **1060 数据未实测**（当前最大知识缺口） | 闸门 A 阻断开工、闸门 B 阻断盲测；未过则预算复议 |
| 表现层污染仿真 | `src/render`/`src/ui` 零判定（B4）；`renderLOD` 不含视锥、不进存档（裁决 28） |
| HUD 泄题（把答案画在 UI 上） | `--assert-urgency-number` / `--assert-board-no-ordinal` / 15 条不显示清单；盲测用 release 关调试 UI |
| 关钟组 <70%（C-1 权重不足） | A13 校准回路：上调色温 ΔL\* 与世界标点非文字载体，重测 |
| 样本偏差 | 固定 seed + 固定存档；≥12 受试 × 3 NPC；双盲表单 |

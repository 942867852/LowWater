# Epic 01 · 三跳连锁可复现（Core 验收 1）

- **指向**：`game-concept.md §四-CORE 验收 1`（唯一放行标准）· `control-manifest.md §1`（1a–1e）· `§8 B10/B11/B12/B13` · `ADR-004`（归因链）· `ADR-003`（离屏 LOD）
- **一句话验收**：固定种子 + 脚本清空某区 80% 物资 → **3 游戏日内**必然观测到
  `S1.zone.*.remaining →(1) S2.actor.*.need →(2) S2.actor.*.schedule →(3) S5.community.*.safeDays`，
  且 `ΔsafeDays ≤ −10%`、`UNATTRIBUTED==0`、`maxStep≤3`、`visibleSignal` 非空率 100%；**写不出归因链的链条视为未实现。**
- **涉及系统**：S1（稀缺）主 · S2（NPC 三层）主 · S5（社区产能/`safeDays`）· S8（区域/路程）· S0·B/C（时钟/归因）
- **涉及契约**：`Quantity` · `ZoneSnapshot` · `ZoneMutation` · `ContainerDef` · `Claimant` · `PhysioState` · `LoadState` · `Injury` · `NeedVector` · `Post`/`PostRoster` · `ScheduleDay`/`Block`·`Waypoint` · `LossStreak` · `WorldSignal` · `GapSignal` · `ActorDied`/`ActorIncapacitated` · `AttributionLink`
- **依赖**：Epic 00（E0-S2/S5/S6）
- **可并行说明**：区/容器（S1）与 NPC 三层（S2）**在两社区配置冻结后可并行**；S5 产能依赖 `PostRoster`，排在 S2-S8 之后

---

## 目标与验收

| 子判据 | 内容（`control-manifest §1`） |
|---|---|
| 1a | 闭合集存在 ≥1 条 `step==3` 且 `effectRef` 以 `S5.community.*.safeDays` 结尾的链 |
| 1b | 链终点 `ΔsafeDays ≤ −10%`（相对触发前一日 `DaySnapshot`） |
| 1c | 全链 `UNATTRIBUTED == 0` |
| 1d | 全链 `step ≤ 3`（越界即设计违规，报警 + 记评审，**不静默截断**） |
| 1e | 中间节点 `ScheduleChanged.visibleSignal` **非空率 = 100%** 且可反查 `WorldSignalRegistry`（违反时 `assert` 中断，**不降级**） |

**硬约束**（来自 `game-concept` / S1 / S2）：
- 任何宏观后果 **≤3 跳**可归因（P1）；`safeDays` 取 **木桶 min**，**不加总**（S1 验收 3）。
- 静态守恒：任一 zone `remainingByCluster` **逐日单调不增**（S1 验收 1，B11）。
- gossip 有界：链长 ≤3、单 actor 单日入站 ≤2、`reachedSet` ≤7（B13）。

---

## Story 清单（按实现顺序；★ = 在垂直切片内）

### ★ E1-S1 · S8 区域内容最小集（1 zone + 播种表 + 路程表）
- **判据（跑 Y 得 Z）**：`--dump-zone ruins_x` 打印容器 `initialItems` 求和 == `ZoneSnapshot` 首日快照；`--dump-route` 所有 O-D 的 `pathLenMeters` **非 0**（`S8 A3`）；同 seed 播种逐位复现。
- **涉及契约**：`ContainerDef`（含 `claimantSlots[]`，以 S8 §2.4 为准）· `Claimant` · `UniqueManifest` · `LocationId` 命名
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：— ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E1-S4）
- **备注**：切片只做 `ruins_x` 1 个 zone（~6 容器），非 Core 的 21 容器。

### ★ E1-S2 · S1 容器与 zone 状态（`ZoneSnapshot` + `remainingByCluster`）
- **判据（跑 Y 得 Z）**：`--assert-static-conservation` 通过（逐 zone 逐日单调不增）；`--dump-container <id>` 打印 tier/claimants/items。
- **涉及契约**：`ZoneSnapshot` · `ZoneMutation` · `ContainerDef` · `Quantity`（`cluster→unit` 固定映射）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E1-S1 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E1-S4）

### ★ E1-S3 · S1 三段 `rollCheck` 取用 + `claimToken` 仲裁
- **判据（跑 Y 得 Z）**：脚本取用 → 发现`MIND/scavenge_eye` → 拆取`HAND/dismantle` → 不留痕`VIGOR/stealth` 三段各有 `CheckResult` 记录；同刻两名 actor 争同一容器 → 按 `sortKey=(priority, plannedDay, actorId 字典序)` 仲裁，落选者当日不重试。
- **涉及契约**：`Claimant.priority`（档）· `CheckResult` · `ZoneMutation{TAKE|LEAVE|DISTURB}`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E0-S6 · E1-S2 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E1-S4）
- **备注**：DC 一律取 S0 §D 四档常量；**第二套骰子已废止**（裁决 C-02）。

### ★ E1-S4 · S1 生理 / 负重 / 伤病最小集（辐射 + MINOR）
- **判据（跑 Y 得 Z）**：水 `−8/h`、食 `−4/h`、睡 `−5/h` 逐时结算（整数 milli，无漂移）；`speedMul = clamp(1.15−0.5·(cur/cap)²,0.45,1.15)`；进入 `hazardLevel≥1` zone → `rad` 累积 → 达阈发 `ActorIncapacitated{RAD_OVERDOSE}`。
- **涉及契约**：`PhysioState` · `LoadState` · `Injury{MINOR,RAD_SICKNESS}` · `ActorIncapacitated`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E0-S6 · E1-S1 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E1-S2/S3）
- **备注**：切片只做 `RAD_SICKNESS`+`MINOR`（D1 死因必需），四类伤病留 Extended。

### ★ E1-S5 · S2 L1 需求（`NeedVector` + `NeedUnmet`）
- **判据（跑 Y 得 Z）**：每 `HOURLY_TICK` 结算 `safety/social/creed`（`clamp 0..100`）；`v<40→1, v<20→2, v<5→3`；每条 `(actorId,need,dayKey)` 每日**只发一次** `NeedUnmet`。
- **涉及契约**：`NeedVector{safety,social,creed,physio:只读}` · `MemoryEvent` · `NeedUnmet`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E1-S4 ｜ **工作量**：2 人日 ｜ **可并行**：✓（与 E1-S8）
- **备注**：**L1 不读写水/食/睡**（只转发 `PHYSIO_*`）；需求气泡只画三条（裁决 1/36）。

### ★ E1-S6 · S2 L2 调度（`ScheduleDay` 24h blocks）
- **判据（跑 Y 得 Z）**：`--dump-schedule <actor> <day>` 打印 blocks（`startMin/endMin/postId/locationId/route/intentTag`）；每人 ≤8 块、每块路点 ≤6；睡眠块落在 `[curfewMin, curfewMin+SLEEP_BLOCK_MIN]`；**无随机迟到**（`startMin` 纯算术）。
- **涉及契约**：`ScheduleDay`/`Block`/`Waypoint` · `intentTag` 闭集 · `Post`/`PostRoster` · `travelMin` 路程常量
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E1-S5 · E1-S8 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E1-S7 的前半）
- **备注**：默认 `homePost` 世袭（S2 R-A），只四类硬理由可改派。

### ★ E1-S7 · S2 L3 行为树扰动 + D2 pressure + 四类 `changeKind`
- **判据（跑 Y 得 Z）**：脚本连续 2 日搬空同一目标 → 3 游戏日内观测到 `pressure` 增量 → `ScheduleChanged{changeKind∈{REROUTE,ADD_WATCH,DEEPEN_STASH,SET_TRAP}}` → `visibleSignal` 指向的世界对象**真实存在**且 `installTick ≤ applyTick`（`S2 A1`）。
- **涉及契约**：`LossStreak` · `WorldSignalRegistry`/`WorldSignal{sigId,refId,kind,installTick,expireTick}` · `ScheduleChanged.visibleSignal` · `MemoryEvent`
- **触碰 Godot 表现层**：否（信号是仿真侧写路径）｜ **需 1060 冒烟**：否
- **前置**：E1-S6 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E1-S8）
- **备注**：`leadTicks = applyTick − installTick ≥ 360`（G-SIG-LEAD）；**本系统 raw rng == 0**。

### ★ E1-S8 · S5 岗位产能 + `safeDays` + `GapSignal`
- **判据（跑 Y 得 Z）**：`--dump-posts <cid>` 打印 `rated/onDuty/effHead/实际产出`；`实际产出 = 额定 × (effHead/rated)`；`staffingRatio<0.60 → SCARCE`（退出需连续 2 日 ≥0.60）；`--dump-safedays` 取**木桶 min**（脚本灌满水清空食 → `safeDays==foodPersonDays`，反之亦然；**取到加总值即失败**）。
- **涉及契约**：`Post`/`PostRoster` · `SafeDays` · `GapSignal{sourceKind∈5值}` · `SCARCE` 与 `TIGHT` **两灯不合并**
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E1-S4 ｜ **工作量**：2 人日 ｜ **可并行**：✓（与 E1-S5/S7）
- **备注**：切片只做 `STOCK`/`POST` 两路 `sourceKind`；五路全量留 Core 后段。

### ★ E1-S9 · 闭合集断言 + `drain-zone-x` 场景脚本
- **判据（跑 Y 得 Z）**：`simcore_cli scenario --name drain-zone-x --seed 20250910 --zone ruins_x --ratio 0.80 --days 3` → 退出码 0 且 `assert-attribution --closed-set core_chain.h` 命中 1a–1e；`export-attribution` 产出举证 `.md`。
- **涉及契约**：`AttributionLink` · 闭合集 `core_chain.h`（1a–1e 机器判据）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E0-S5 · E1-S2/S3/S7/S8 ｜ **工作量**：2 人日 ｜ **可并行**：✗（收口项）
- **备注**：`core_chain.h` 为**闭合集唯一定义处**；若需新增链式节点 → 走裁决，不自行扩契约。

---

## 依赖图（Epic 内）

```
E1-S1 ─▶ E1-S2 ─▶ E1-S3
   └────▶ E1-S4 ─┬─▶ E1-S5 ─▶ E1-S6 ─▶ E1-S7
                 └─▶ E1-S8 ─┘             │
E0-S6 ──▶ (S3)                            ▼
E0-S5 ────────────────────────────▶ E1-S9 ◀── E1-S8
```

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| **R3 日切批处理中断链 → `UNATTRIBUTED>0`** | `WriteQueueItem` 强制 `causeRef`（`enqueue` 断言）；派生效果继承上游 `effectRef`；多因一果取 `step` 最小者为主链（`architecture §10 R3`） |
| **`visibleSignal` 为空 → 1e 挂** | S2 A6：空即 `assert` 中断（**不降级**）；每类 `changeKind` 必须有非空信号 |
| 级联终点放错社区 → 永不触发 | **终点须为水绑定社区 `jing_cell`**（见 `production/vertical-slice.md §2.2`） |
| 静态守恒被再生打破 | `--assert-static-conservation`；只允许 S1 §5.3 两类 stub 例外 |
| 木桶被写成加总 | `--dump-safedays` 双向断言（水满食空 / 水空食满），取加总即失败 |

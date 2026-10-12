# 表现层只读投影契约 · ViewSnapshot + WorldSignalRegistry（字段级唯一定义处）

- **文档 ID**：PHASE4-000A ｜ **阶段**：Phase 4 · 第 0 步（三路并行的前置）
- **作者**：engineering-lead（程基岩）｜ **调度**：主理人（游承峰）｜ **状态**：**冻结（Frozen）**
- **实现载体**：`src/simcore/view.h`（**字段必须与本文件逐字一致**，任一先改 = 契约破坏）
- **上游**：`architecture.md`（§2 / §3.2 / §6 / §8 / §10 R5）· `accessibility.md §10.1` · `09-presentation-ux.md §0/§6.1` · `adr/ADR-003`（simLOD/renderLOD 分离）· `adr/ADR-004`（RefId）· `adr/ADR-005`（定点）· `02-npc-simulation.md`（WorldSignal / 四 changeKind）· `07-work-board.md`
- **闭合**：`architecture.md §11.3` 的 **R7**（`ViewSnapshot` / `WorldSignalRegistry` 字段级契约缺位）。

> **命名纪律（最重要的一条）**：本文件的字段名 / 枚举值与实现逐字一致，**冻结后不得改名**。UX 规格、资产规格、Epic 拆分三路并行产出**只准引用，不准各自发明**。确需变更 → 走 §9。

---

## 0. 为什么必须先冻结

`accessibility.md §10.1` 与 `09-presentation-ux.md §0` 各自列了 `ViewSnapshot` 需求字段，但架构文档里它**只是概念**。三路并行若不先钉死名字，会各自发明一套（重演并行写 GDD 的返工）。本契约是**唯一定义处**：字段级、带来源与更新时机、带硬边界与拒绝理由。

---

## 1. 结构总览

```
ViewSnapshot (src/simcore/view.h · SimViewSnapshot)
├─ actors[]  : SimViewActor    ×≤120   per actor 投影（§2.1）
├─ signals[] : SimWorldSignal  ×≤48    WorldSignal 只读镜像（§2.2 / §4）
├─ boards[]  : SimViewBoard    ×≤2     工作板投影（§2.3）
├─ hud       : SimViewHud              玩家自身状态 + 时间（§2.4）
└─ camera    : SimViewCamera            【相机派生】只在 renderLOD 侧消费（§2.5 / §5.3）

WorldSignalRegistry (SimWorldSignalRegistry)  ← §4，仿真侧持有；快照只读镜像其 entries[]
```

- **一次发布 = 一个值拷贝**：快照是 POD 缓冲（无指针、无堆分配），表现层拿到的是副本。
- **发布时机**：**只在 tick 边界发布**（架构 §10-R5 双缓冲）；`publishedAbsTick` 记录对应 `absTick`。

---

## 2. `ViewSnapshot` 逐字段定义

> 类型 = C ABI 定长类型；`Milli` = `int32`（真值×1000，ADR-005）；mm = 真值×1000；`turn` = 1/65536 圈（ADR-005）。

### 2.1 `per actor`（`SimViewActor`）

| 字段 | 类型 | 单位 / 范围 | 来源系统 | 更新时机 |
|---|---|---|---|---|
| `actorId` | `uint32` | ActorId 内部编码（S0 §D；匿名最高位置 1） | S0 §D | 每 `SIM_TICK` |
| `isNamed` | `uint8` | 0 / 1（有名 ≤16） | S0 §D | 同上 |
| `activityId` | `uint8` | 闭集 14 值（§3.1） | **S2 §2.2** | `SIM_TICK`（当前占位，TBD-1） |
| `needSeverity[6]` | `uint8[6]` | 0..3（§3.2 顺序冻结） | **S2 §3.1**（生理三项镜像 S1） | `HOURLY_TICK` |
| `carryingSilhouette` | `uint8` | 闭集 5 值（§3.3） | S2（`carrying:ItemStack[]` 投影） | `SIM_TICK`（TBD-2） |
| `posMm[3]` | `int32[3]` | mm，±2.147e6 | S1/S2（`world.h pos*`） | `SIM_TICK` |
| `facingTurn` | `uint16` | turn，0..65535 | S2 | `SIM_TICK`（TBD-3） |
| `renderHint` | `uint8` | 位域 `{MOVING, CARRIES_LIGHT}`（§3.4） | 派生（sim 状态纯函数） | 同上 |

> **不进快照**：`tickLevel`（simLOD 镜像）→ 归 **`DebugSnapshot`**（见 §7-① 与 §5.1）。

### 2.2 `per signal`（`SimWorldSignal`）

| 字段 | 类型 | 单位 / 范围 | 来源系统 | 更新时机 |
|---|---|---|---|---|
| `signalId` | `uint32` | 单调递增（同 seed 逐位同） | S2 §2.2 | 安装时 |
| `kind` | `uint8` | 闭集 4 值（§3.5） | **S2 §3.5** | 安装时 |
| `state` | `uint8` | `ACTIVE / EXPIRED / REVOKED` | S2 | 撤销 / 降级时 |
| `refId` | `char[64]` | `"<signalKind>:<refId>"`（S2 §5.1） | S2 | 安装时 |
| `installTick` | `uint64` | absTick；**信号装进世界** | S2 §4.1 | 安装时 |
| `applyTick` | `uint64` | absTick；**行为变更生效**（次日 `DISPATCH`） | S2 §4.1 | 注册时 |
| `expireTick` | `uint64` | absTick；0 = 不急 | S2 | 降级时 |

- **`expired` 字段**：由只读访问器 `simview_signal_expired(s)`（`:= state != ACTIVE`）派生，不另设字段（防两份真相）。
- **契约 `G-SIG-LEAD`**：`applyTick − installTick ≥ 360`（6 游戏小时）。可查询：`simview_signal_lead_ticks()` / `simview_signal_lead_ok()`。详见 §4.3。

### 2.3 `per board`（`SimViewBoard` / `SimViewWorkOrder`）

| 字段 | 类型 | 单位 / 范围 | 来源系统 |
|---|---|---|---|
| `communityId` | `uint8` | 0=`he_valley` / 1=`jing_cell` | S0 §D |
| `entryCount` | `uint8` | ≤3 | S7 |
| `safeDaysMilli` | `Milli` | 木桶 `min(water/…, food/…)`，不美化不缩水 | S3 / S5 |
| `trendDelta7dMilli` | `Milli` | 7 日趋势（板头箭头） | S3 |
| `entries[]` | `SimViewWorkOrder` | ≤3，按 S7 §3.2 `sortKey` 序 | S7 / S5 |
| `sedimentLine` | `char[64]` | 沉降小字（不占配额） | S7 §2.2 |

`SimViewWorkOrder`：`orderId[64]` · `communityId` · `kind`(闭集 4：`POST/CREED/PERSON/STOCK`) · `needCluster`(6 键) · `urgency`(1..5，**仅选介质，数字不渲染**) · `qtyMilli`(≡`deficitUnits` 合并后 Σ) · `deadlineDay`(≡`expiresDayKey`) · `haulFit`(`FITS/TIGHT/OVER`) · `state`(`OPEN/NOTED/RESOLVED/EXPIRED`) · `travelMinQuantized`(5 分钟档) · `headlineCode[64]`(那句话的键) · `targetLocationId[64]`(供画八方位，**不给坐标**)。

> **不进快照**：`benefitSpec`（行动前不得渲染回报，S7 §2.1-①）与 `resolvedBy`（"没有验收的人"，S9 §6.3-#10）→ 归 `DebugSnapshot`（§7-②③）。

### 2.4 `HUD`（`SimViewHud`）—— 只有"玩家自身状态 + 时间"

| 字段 | 类型 | 单位 / 范围 | 来源系统 |
|---|---|---|---|
| `dayKey` / `minuteOfDay` | `int32` | 1 起 / 0..1439 | S0 §B2 |
| `physio[4]` | `int32[4]` | `water / food / sleep / rad`（Milli） | S1 `PhysioState` |
| `loadCurrentKgMilli` | `Milli` | 当前负重 | S1 `getLoad` |
| `speedMulMilli` | `Milli` | `SPEED_MUL`（0.45..1.15） | S0 §D |
| `staminaMilli` | `Milli` | 体力 | S0 §D `STAMINA_POOL` |
| `ledgerCount` / `ledgerHeadline[≤3][64]` | `uint32` / `char` | 欠条页 ≤3 条短句 | S4 §6-3 |

### 2.5 `camera`（`SimViewCamera`）—— **相机派生字段**

| 字段 | 类型 | 单位 | 用途 |
|---|---|---|---|
| `centerDirTurn` | `uint16` | turn | 屏幕中心方向 = 可读性保持锥（±25°）判定轴 |
| `fovHalfTurnH` / `fovHalfTurnV` | `uint16` | turn | 水平 / 垂直半视锥角（默认 10922 ≈ 60°） |
| `playerYawTurn` | `uint16` | turn | 相机水平朝向（与 `centerDir` 同源） |

> **本组是唯一"相机派生"字段**。它们**由表现层每帧写入**（相机是表现层自有输入，`CameraSettings` `InputCommand`），`projectView` 只做原样回读。**只允许 renderLOD / 可读性保持锥消费**，绝不回喂 `simLOD`（证明见 §5.3）。

---

## 3. 枚举闭集（值域冻结，禁止重排）

| # | 枚举 | 取值（顺序即值） |
|---|---|---|
| 3.1 | `activityId` | `SLEEP RITUAL COMMUTE WORK HAUL SOCIAL WATCH PATROL TREAT EAT SEEK ALERT DEAD FLED`（14；S2 §2.2） |
| 3.2 | `needSeverity[6]` 下标 | `0 WATER 1 FOOD 2 SLEEP 3 SAFETY 4 SOCIAL 5 CREED`；档 `NONE/LOW/MID/HIGH = 0/1/2/3`（S2 §3.1：`v<40→1, v<20→2, v<5→3`） |
| 3.3 | `carryingSilhouette` | `NONE BUCKET LAMP TOOL BAG`（5；桶/灯/工具/袋/无） |
| 3.4 | `renderHint` 位 | `bit0 MOVING` · `bit1 CARRIES_LIGHT`（**非 renderLOD**；其余位 TBD-4） |
| 3.5 | `changeKind` | `REROUTE ADD_WATCH DEEPEN_STASH SET_TRAP`（4；**没有第五种**） |
| 3.6 | `signalState` | `ACTIVE=0 EXPIRED=1 REVOKED=2` |
| 3.7 | `cluster`（`needCluster`） | `WATER FUEL AMMO MEDS SEED FOOD`（S0 §D 6 键） |

---

## 4. `WorldSignalRegistry`

### 4.1 结构

```text
WorldSignalRegistry { count, nextSignalId, entries[≤48]: WorldSignal }
```

### 4.2 四种 `changeKind` ↔ 可见信号（S2 §3.5，`visibleSignal` 格式 `"<signalKind>:<refId>"`）

| `kind` | 升级源（actor `pressure`） | `refId` 形态 | 降级（反向安装） |
|---|---|---|---|
| `REROUTE` | ≥2（近 3 日） | `FOOTPRINT_DEVIATION:<loc>` | 脚印回踏旧径 |
| `ADD_WATCH` | ≥4 或单次丢 `CRITICAL` | `WATCH_ADDED:<loc>` | 灯灭 / 坐具搬走 / 人重新赶路 |
| `DEEPEN_STASH` | ≥6 或同容器连续 2 日被清空 | `STASH_MOVED:<ctrOld>→<ctrNew>` | 柜子搬回门边（前后两处绳印） |
| `SET_TRAP` | ≥8 或同社区 ≥2 人达 L2 | `TRIPWIRE:<ctr>` | 铃铛被摘、残桩留存 |

### 4.3 `installTick` 语义与 `≥6 游戏小时` 的数据保证（S2 A6）

| 事件 | absTick | 规则 |
|---|---|---|
| **决策** | 达阈值的 `DISPATCH`(06:00) | 发 `ScheduleChanged{changeKind, visibleSignal}` |
| **安装** `installTick` | 决策后的**下一个 `HOURLY_TICK`**（写队列整点应用） | 信号（脚印/灯/绳印/绊线）装进世界 |
| **生效** `applyTick` | **次日 `DISPATCH`**（日程变更次日生效） | 行为改变 |
| **撤销** `expireTick` | 降级回路触发时（连续 3 干净日且低于阈值−2） | 反向安装 |

- **可查询字段**：`installTick` 与 `applyTick` 都在条目上 → `leadTicks = applyTick − installTick` 直接可查。
- **保证方式**：因为**行为变更被延后到次日 `DISPATCH`**（S2 §4.1），而信号在决策后 ≤1 小时内装入，恒有 `leadTicks ≥ 360`（典型 ≈23h）。
- **强制**：`ScheduleChanged` 的 `visibleSignal` 非空率 = 100%，且可反查本表活条目（S2 A6 / `assert-visible-signal`，**违反即 assert 中断，不降级**）。注册时校验 `simview_signal_lead_ok()`，不满足即 `assert`。
- **安装 / 撤销是仿真侧写路径**：`view.h` **不提供** install/revoke（只读契约）。

---

## 5. 硬边界声明（逐条落到结构上）

### 5.1 只读：表现层不得回写仿真状态

| 结构性保证 | 落点 |
|---|---|
| 投影形参为 `const World&` | `simcore::projectView(const World&, …)` —— 编译期不可改 |
| 快照是**值拷贝**（POD 缓冲，无指针指向仿真内存） | `SimViewSnapshot` 全为定长标量/数组 |
| `view.h` **无写入口** | 无 setter；Registry 安装/撤销在仿真侧 |
| **无 sim 函数消费快照** | 没有任何 L1/L2 函数把 `ViewSnapshot`/`SimViewCamera` 当**输入** |

> 校验：用例 11（§7-⑥）——投影与仿真**锁步交替**运行，`worldHash` 与 `tickLevel` 逐位不变。

### 5.2 不进存档、不参与 `worldHash`

- `worldHash`（S0 §C3）只含：生理 + 库存 + 位置量化 0.1m + 关系值。**本契约全部字段不在其中**（含 `camera`）。
- `SaveGame v1`（架构 §7.3）**不含** `ViewSnapshot` / `renderLOD` / `SimViewCamera`。快照每次由仿真状态**重新投影**，无需持久化。

### 5.3 相机朝向不得进入 `simLOD`（ADR-003 D2 / 裁决 28）

| 层 | 决定因素 | 是否含相机 |
|---|---|---|
| **`simLOD`**（`tickLevel`） | 距离 + `PROTECTED` + 名额抢占 | **❌** |
| `renderLOD`（表现层私有，不进契约） | 视锥 + 距离 + 遮挡 | ✅ |

**相机派生字段为何无害（三条结构性理由）**：
1. 它们**由表现层写入**（相机是表现层输入），不是仿真产出；`projectView` 仅回读。
2. **没有任何仿真代码路径读取它们**（§5.1 第四条），故不可能改变 `tickLevel` / 判定 / RNG。
3. **不在 `worldHash`、不在存档**（§5.2）。

> 校验：既有用例 5（改 yaw → `worldHash`/`tickLevel` 不变）+ 用例 11（整个投影通道不变）。

### 5.4 渲染成本与仿真成本归属（确认 accessibility「表现层仿真成本恒为 0」）

**成立。** 理由：本契约的所有消费发生在表现层；投影是**只读 O(n)、零分配、零 RNG、零写**。
- **仿真成本 = 0**：不产生任何 `SIM_TICK`、不改任何仿真状态（用例 11 举证）。
- 构建快照本身的 O(n)（≤120 实体 + ≤48 信号）**归渲染帧预算**，不归仿真 6ms 预算；量级 < 0.05ms，被 `accessibility §7.1` 的信息通道预算（< 1.20ms）覆盖。

---

## 6. 命名规范与数值精度判据

- **命名**：与 `S0 §D` 的 ID 命名、`ADR-004 D7` 的 `RefId` 语法、`ADR-005` 的定点表示一致。字段名沿用设计文档原词（`activityId` / `needSeverity` / `carryingSilhouette` / `installTick` / `safeDays` …），**不得起别名**。
- **跨界一律整数定点**（ABI 稳定 + 投影可逐位复现）：`Milli`（×1000）· mm（×1000）· `turn`（1/65536 圈）。**禁止浮点跨 C ABI 边界**（ADR-005 D4）。
- **渲染侧可局部用浮点**（ADR-001 R1「不跨界」）：表现层把整数投影转成自己的观测格式，**不得回写**。

| 量 | 契约表示 | 渲染容差判据 |
|---|---|---|
| 位置 | mm（`int32`） | 插值误差由表现层承担；不设回写容差（≈±1mm 可接受） |
| 数量 / 比率 / `safeDays` | `Milli` | 显示 3 位小数；**比对用整数精确等值**（无容差） |
| 角度 | `turn`（`uint16`） | 容差 ≤ 1 turn ≈ 0.0055° |
| 枚举 / 档位 | `uint8` | 无容差，整数精确 |

---

## 7. 与字段请求的逐条对应（`accessibility §10.1` / `09-presentation-ux §0`）

| 请求字段 | 归属 | 满足 |
|---|---|---|
| `activityId` `isNamed` `needSeverity[6]` `carryingSilhouette` | §2.1 | ✅ |
| `posMm` `facing` `renderHint`（S9 §0） | §2.1 | ✅ |
| `tickLevel`（§10.1，**明示仅供调试面板**） | **移至 `DebugSnapshot`** | ✅（改由 §5.1/§7-① 承载，正是其声明用途） |
| `signalId` `kind` `refId` `installTick` `expired` | §2.2 | ✅（`expired` 见 §2.2；另加 `applyTick` 供 `G-SIG-LEAD`） |
| `WorkOrder[]` + `urgency` · `safeDays` · `trendDelta7d` | §2.3 | ✅ |
| `HUD{dayKey, minuteOfDay, physio[4], loadCurrentKg, speedMul, stamina, ledger[≤3]}` | §2.4 | ✅ |
| `camera{frustum, centerDir}` | §2.5 | ✅（标注"相机派生"，§5.3） |

### 拒绝放进快照的字段及理由（+替代通道）

| # | 字段 | 拒绝理由 | 替代通道 |
|---|---|---|---|
| ① | `tickLevel`（simLOD 镜像） | 一旦进正式快照就有被渲染的风险，击穿 S9 §6.2-A 的"调试 UI 硬隔离"与 S9 §6.3-#4 | **`DebugSnapshot`**（超集，仅开发工具） |
| ② | `benefitSpec`（回报） | 行动前不得给回报，否则板变回任务列表（S7 §2.1-①） | 事后一句世界侧短句（`WorkOrderResolved.effectCode`） |
| ③ | `resolvedBy` | "没有验收的人"是 S7 的最后拼图；显示即变回"任务完成者"（S9 §6.3-#10） | **`DebugSnapshot`**；正式 UI 只在条目转 `RESOLVED` 时不解释 |
| ④ | `urgency` **数字的渲染**（非字段） | 裁决 21：数字=任务等级（S7 §3.1 / accessibility §5） | `urgency` 字段**保留在快照**（**仅用于选介质**），渲染用介质 + 那句话 |
| ⑤ | 世界信号 `expireTick` 的玩家可见性 | 过期不应成为"任务失败"提示（S7 §3.3） | 字段保留（供调试面板 ⑪），正式 UI 不渲染 |

> ⑤ 的 `expireTick` 仍留在结构体内（它服务于 §4 的注册表语义），只是**正式 UI 不消费**；`① ② ③` 是**结构性移除**（不在 `SimViewSnapshot` 里）。

---

## 8. 留待后续确定（`TBD`，必须定时的硬约束）

| # | 事项 | 何时必须定 |
|---|---|---|
| TBD-1 | `activityId` 真实来源（当前为占位：`DEAD`/`SEEK`/`COMMUTE`/`WORK`） | S2 落地 `getActorState().activity` 时 |
| TBD-2 | `carryingSilhouette` 由 `carrying:ItemStack[]` 的映射规则 | S1/S2 给出 `ItemStack → 5 枚举` 映射时 |
| TBD-3 | `facingTurn` 精确朝向（当前按速度四象限占位） | S2 日程路点给出 `facing` 时 |
| TBD-4 | `renderHint` 其余位（当前仅 `MOVING`/`CARRIES_LIGHT`） | 当距离分档（§3.2-D）需要新载体时；**新增位须回到本契约** |
| TBD-5 | `boards[]` 的真实来源（当前占位为空） | S7 `getBoard(cid, dayKey)` 接入时 |
| TBD-6 | `HUD.loadCurrentKg / stamina / ledger` 的真实来源 | S1 `getLoad` / S4 账本接入时 |

---

## 9. 变更流程

1. 任何字段增删改 → **先改本文件**（唯一定义处），再改 `src/simcore/view.h`，二者同步提交。
2. 变更 = 契约破坏：三路并行产出（UX / 资产 / Epic）如已引用旧名，**必须同步改名并重跑用例 11**。
3. 枚举**只准追加**（追加到末尾 + 更新上限常量），**禁止重排**（重排 = 静默改变已冻结值域）。

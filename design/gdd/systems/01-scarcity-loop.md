# S1 · Scarcity-Loop（稀缺主循环）· 系统设计文档

- **Task ID**：GDD-001｜**阶段**：Phase 2 · 批次 B1｜**优先级**：P0
- **主笔**：design-strategist（文策渊）｜**调度**：主理人（游承峰）｜**状态**：待拍板
- **依赖已读**：`design/gdd/game-concept.md` · `design/gdd/systems/00-foundation.md`
- **依赖方向**：**严格单向**。S1 依赖 S0·A/S0·B/S0·C 与区域内容；**不依赖**经济（S3）与社区（S5）。对外只经 `tryTransfer` 与三个事件流出口。
- **边界（严格）**：区域物资池与容器（含 claimant）、拾荒判定、负重与体力、生理衰减、辐照、伤病、`claimToken` 单点仲裁、`tryTransfer` 单点入库。
  **不写**：制作、经济、NPC 行为树、UI 布局。

---

## 0.0 规范系统编号对照表（PHASE2-REVIEW · 裁决 C-13，全文已按此表替换）

| 规范编号 | 系统 | 文件 | 本文档原用临时编号 |
|---|---|---|---|
| **S0** | 底座（`S0·A` 角色判定 / `S0·B` 世界时钟 / `S0·C` 确定性 / `S0·D` 常量表 / `S0·E` 死亡清算） | `00-foundation.md` | S1 / S2 / S12 |
| **S1** | **稀缺主循环（本文档）** | `01-scarcity-loop.md` | S5（自称） |
| **S2** | NPC 三层模拟 | `02-npc-simulation.md` | S?（未编号） |
| **S3** | 易货经济与社区仪表 | `03-economy-barter.md` | S6（"经济"） |
| **S4** | 记忆驱动对话与契约账本 | `04-dialogue-contract.md` | —（未引用） |
| **S5** | 社区、信条与声望 | `05-community-creed.md` | S7（"社区"） |
| **S6** | 传闻传播（待写） | — | S9（"gossip"） |
| **S7** | 工作板（待写） | — | C9（概念文档 scope ID，保留原写法） |
| **S8** | 区域内容（待写） | — | "区域内容 GDD" |
| **S9** | 呈现 / UI（待写） | — | "UI GDD" |

---

## 0. 全局契约（逐字继承，不改字段名）

```text
ZoneSnapshot(zoneId) -> { remainingByCluster, depletionDay, hazardLevel }
ContainerView(containerId) -> { tier, items[], claimants:Claimant[], lastLootedDay, isDisturbed }
Claimant{ actorId, containerId, plannedDay, priority, needReason }
LoadState{ currentKg, capKg, speedMul, stamina, staminaMax }
PhysioState{ water, food, sleep, rad }        // C-01 已裁决：唯一字段名 rad；radAccum 仅为序列化别名
Injury{ type:"MINOR"|"INFECTION"|"FRACTURE"|"RAD_SICKNESS", severity, requiredItemId, treatedDay? }
claimToken(containerId, actorId, tick) -> { granted, reason }     // 取用唯一仲裁点
tryTransfer(fromActor, toOwner, items[]) -> bool                   // 入库唯一入口
事件: ContainerEmptied{containerId, byActorId, dayKey, items[], displacedClaimants[]}
     ClaimDisplaced{actorId, containerId, dayKey, needImpact}
     ActorIncapacitated{actorId, causeCode, dayKey}
speedMul = clamp(1.15 − 0.5 × (currentKg/capKg)², 0.45, 1.15)
```

> **C-01 已裁决（主理人拍板）**：**统一为底座 §D/§A2 的 `rad`** —— §D 是唯一定义处；`radAccum` **降为序列化别名**，仅允许出现在跨进程 / 存档载荷中，运行时字段、公式与接口一律写 `rad`。本份契约与全文已改写完毕。

---

## 1. 系统概览与目标（对应支柱/动词）

### 1.1 定位

S1 是《枯水期》的**约束层**：它不生产目标，它决定"任何目标都得付什么"。八个动词里 **取 SCAVENGE** 与 **负 HAUL** 落在本系统，其余六个动词在与本层的资源摩擦中才有意义（想"守望"就得先把水搬到；想"易"就得先扛得动）。

- **支柱**：**P4（主）**——拿走的每样东西原本流向某个人的喉咙；**P2**——容器状态必须一眼可读（tier / claimant / isDisturbed 三个可见量）；**P3**——你不来，NPC 也会把它取走；**P1**——`ContainerEmptied.displacedClaimants` 是 D1 抽血式级联的**第一跳**数据源，链必须 ≤3 跳。
- **反目标**：不做负重上限外的"背包格子类比"；不做动态困难的隐形下调；不做任何形式的资源局刷（除了 §5.3 的两类 stub）。

### 1.2 一句话职责

> 把"捡垃圾"从掉落表演译成**一次有声有姓的剥夺**：这个柜子是谁的、他明天几点来、为什么非它不可、你搬走之后他今晚怎么过。

### 1.3 【必答】R2：为什么第 90 分钟不塌成无意义搬运

"重复感"的成因不是动作重复，而是**重复的动作不产生新的世界状态**。S1 用四条机制级回答，前两条为硬性要求：

**① 归属先于物品暴露（claimant 是叙事单位，不是标签）**
`ContainerView` 的读取顺序被强制为：**先解析 claimant，再解析 items**。即"看得见是谁的"独立门槛低于"看得见里面有什么"。一次提取因而在语义上永远不是 +3个零件，而是**对未来某段他人日程的一次写入**。更关键的是 `Claimant.plannedDay` 给每个容器装了倒计时：抢在他来之前 / 让他拿到并赊一个人情，是**同一动作的两个相反意图**——玩家每次蹲同一口柜子都在重做这道选择题，而答案随 NPC 需求状态变化。

**② 竞争拾荒（容器是被多人同时逼近的稀缺点，不是资源节点）**
容器并非静置等玩家：同一容器可挂多个 `Claimant`，且 NPC 侧由 `ScavengeAttempt`（依赖 S2 NPC 模拟）驱动、同样必须经 `claimToken` 取号。由此产生三种持续张力：（a）**先手悬念**——你抵达时的第一个问题永远是"有没有人比我先到"；（b）**拖延有价**——今天不取，明天大概率没了，这是“世界不等人”的最小可玩单元；（c）**抢赢要付票**——抢赢意味着你要在超重/高压/夜色修正下完成 haul，收益兑换成另一次负担见 ③。

**③ 留痕会被读走，然后失效（isDisturbed → D2 的供料点）**
每个 `rollCheck`（VIGOR/潜行）失败都会置 `isDisturbed = true`。这个痕迹次日被 NPC 读出 → 改道/加岗/把东西搬进更深的屋子 → **玩家旧地图知识失效，必须重新"读"**。这让"多跑一趟"在世界侧真的改变 future accessibility，而不是重复同一份静态数据。这是 P3 从惩罚变博弈的唯一开关。

**④ 同一柜子对不同技能水平给出不同内容（再读有收益）**
容器带隐藏层，只有大成功或达到 `scavenge_eye`/`dismantle` 等级门槛才揭示。属性闸门（底座 §A3.3，属性 8 → L5）把长期成长挂在"同一个柜子第三次来还能不能更深一层"，而非挂在更好的掉落表上。**成就感由 extracting depth 提供，不由 loot density 提供**——这从根本上消掉了"必须开出更多东西"的数值通胀需求。

---

## 2. 核心概念与数据模型

### 2.1 层级

`Zone`（区域，永不刷新的物资池计量单位） → `Cluster`（若有其为产地簇分类） → `Container`（唯一可交互实体） → `ItemSlot`（静态槽位 + 数量）。

> **`remainingByCluster` 的 key 严格复用底座 §D 的 `ClusterId`**，单位遵循 §D 固定映射（`WATER→"L"` 等），S1 **不新增任何簇**。
> **裁决 11（C-17 / C-26）已拍板**：`ClusterId` 为 **6 键** = `WATER | FUEL | AMMO | MEDS | SEED | FOOD`（`FOOD → "portion"`）。对本系统的影响仅一处：`WILD_FOOD` 再生（§5.3）与容器内的口粮一律计入 **`FOOD`** 键。`FOOD.tradable = false` 只约束经济侧（不进 `PriceTable/quote/execTrade`），**`tryTransfer`、容器与 `remainingByCluster` 对 FOOD 无任何特例分支**（C-19 关闭）。

### 2.2 容器 tier（本题必答项）

| tier | 判定依据（生成期静态确定，**运行时不可迁移**） | Claimant 约束 | 玩家侧信号 |
|---|---|---|---|
| `COMMON` 普通 | 内容仅含当季可得的粗料；同 zone 内同型槽位 ≥4 | 可为 `NULL`；有则 `priority` 最低档 | 无标记 |
| `SCARCE` 稀缺 | 含 ≥1 单位 MEDS/FUEL/AMMO，或含"错季"物资 | **必须 ≥1 名 claimant** | 柜体有缠绳/刻痕标记 |
| `CRITICAL` 关键 | 承载 `UniqueManifest` 条目，**或**是某 `PostId` 的唯一补给锚点 | **必须 ≥1 名 claimant 且 priority ≥ 高档**，且该 claimant 的 `needReason` 必须指向某个 Post | 该社区区域内可被" gossip/工作板"预告（stub，见 §5.3） |

> tier 一旦生成即冻结；清空后容器**不降级不刷新**，只把状态置为 `DEPLETED`。

### 2.3 全图唯一件（某些关键件整张地图只有一件）：播种与追踪

**播种（worldgen 期一次性，之后只读）**

1. 每件 U 类物品定义（`uniqueItemDefId`）在 `ContainerDef[]` 中以 `isUniqueAnchor = true` 声明候选容器**集合 K**（|K| ≥ 3，分布在**不同 zone**）。
2. `containerId_anchor = K[ rng(makeSeedCtx("gen.unique." + uniqueItemDefId, 0)).next() % |K| ]` —— `worldSeed` 固定则该归属固定，可在迭代期用不同 seed 反复抽签而不需要手改数据。
3. 结果写入**只读表 `UniqueManifest`**：`Map<uniqueItemDefId, containerId>`，全图唯一，启动时校验"每个 uniqueItemDefId 恰好 1 条"；违反即报错（这是 R5 无菌荒土的硬保险）。

**追踪（三态 + 一步可归因）**

```text
SEALED    -> (所在容器被开启且该槽位被取走) -> IN_TRANSIT -> CONSUMED | DESTROYED | LOST
```

- 状态迁移**不新增事件**，全部经既有事件的负载与 `AttributionLink`（`RefId = "S1.unique.<uniqueItemDefId>.state"`）表达，避免与并行 GDD 的事件总线打架。
- **`IN_TRANSIT` 期间必须与一个 `LoadState` 绑定的 actor 同在**；该 actor 死亡时，底座 Part E `EstateSettlement` 负责释放，S1 只保证**前一刻的 `lastContainerId` 已落盘**，使遗物可被寻回（这是"股东的后来者能否捡到关键件"的唯一救济，也防止关键件无声消失违反 P1）。
- `LOST` 仅在三类情况下成立：所在的 `ZoneMutation` 已被应用的 actor 死亡且无 heir / 被丢弃在 `Zone` 且该 zone 进入不可达 / 玩家销毁。**每进入 `LOST` 必须写 warn 级 `AttributionLink`**，缺失即视为未实现。

### 2.4 ZoneMutation（提斑排队实体）

```text
ZoneMutation{ mutationId, containerId, actorId, kind: TAKE|LEAVE|DISTURB, items[], enqueueTick, applyTick }
```

> **时基契约（已拍板）**：容器变更一律入队，**下一个 `HOURLY_TICK` 应用**。
> **玩家侧例外不是例外**：玩家出手后**自身 inventory 立即乐观更新**（保证手感），但**区域侧**（`remainingByCluster` / `isDisturbed` / `lastLootedDay`）仍在整点生效。这是"世界察觉你"的延迟，也是 P3 的最小刻度。

### 2.5 `ZoneSnapshot` 三字段的定义

- `remainingByCluster`：截至该快照的剩余量；**不含**已入队未应用的 mutation。
- `depletionDay`：以最近 3 个 `dayKey` 的平均取用速率外推，`zone` 归零的预测日；无取用史 → `null`。**它是给 UI 与 NPC 粗嫌层（COARSE）用的安装读数，不是实体。**
- `hazardLevel`：`0..3` 整数，来自**区域静态配置**（`CALM/TINTED/HOT/SEARING`），**不做动态扩散**（已拍板 stub，见 §5.3）。

---

## 3. 规则与公式

**所有随机经 `rng(seedCtx)`；禁止裸 rand；禁止在渲染帧上发起判定（底座 附录B-6）。**

### 3.1 发现率 —— 必须落成 rollCheck，禁止自写概率式

> **C-02 已裁决（主理人拍板 · 概念文档已回填修订）**：概念文档 §3.1-C 的概率式 `P(发现高价值件) = clamp(0.05 + 0.06×(拾荒辨识 − 区域难度) + 0.02×心智, 0.01, 0.60)` **正式废止** —— 它是一条不经过 `rollCheck` 的**第二套骰子**，违反 S0 附录 B-1，且不可归因、不可复现。`design/gdd/game-concept.md` §3.1-C 该行已被替换为对本节的引用，**杜绝其它 GDD 从概念文档回流抄错**。一切发现判定以下列三段 `rollCheck` 为唯一实现。

每次对一个容器的完整提取，按固定顺序发起三次判定，**每次一个独立静态 `streamId`**（保证同 tick 内多次取值不撞车，见 §3.5）：

| # | 意图 | 调用 | DC 来源 | 成功 | 大成功 | 大失败必有后果（S0·A 硬约束） |
|---|---|---|---|---|---|---|
| 1 | 看见暗藏内容 | `rollCheck(actorId, MIND, scavenge_eye, discoveryDC, seedCtx)` | 见下表 | 揭示主槽位 | 另揭示**隐藏层** | `isDisturbed=true` + 发出声响 → 邻近 scan around NPC 进入 `ALERT` |
| 2 | 拆下被锈死/嵌住的件 | `rollCheck(actorId, HAND, dismantle, extractDC, seedCtx)` | 下表 | 取得该件 | 额外产出 1 个拆解副产物 | 巧手系：**必定** 造成 `Injury{type:"MINOR"}` |
| 3 | 不留痕 | `rollCheck(actorId, VIGOR, stealth, noticeDC, seedCtx)` | 下表 | `isDisturbed` 保持 false | 抹去已有痕迹（清 `lastLootedDay` 的可见性） | `isDisturbed=true` **且** leave footprints of NPC readable 明日痕迹 |

**DC 一律取底座 §D 四档常量**，不新增数值：

| tier / 区域难度 | 平凡 | 苛刻 | 危险 | 致命 |
|---|---|---|---|---|
| `discoveryDC` | `DC_TRIVIAL` | `DC_DEMANDING` | `DC_PERILOUS` | `DC_DEADLY` |
| `extractDC` | `DC_TRIVIAL` | `DC_DEMANDING` | `DC_PERILOUS` | — |
| `noticeDC` | `DC_TRIVIAL` | `DC_DEMANDING` | `DC_PERILOUS` | `DC_DEADLY` |

- `modTotal` 由 S0·A 内部按底座 §A3.1 修正表累加；S1 **不传修正、不改 DC**，只负责把 actor 状态写对（夜间无光、负重 weariness、tool quality 由 S0·A 读取）。
- **判定必须在 `SIM_TICK` / `HOURLY_TICK` 上发起**。玩家按键 → 入队 → **下一个 `SIM_TICK` 执行判定**，绝不在渲染帧调用。

### 3.2 负重与体力

```text
currentKg = Σ(item.massKg × item.count)
speedMul  = clamp(1.15 − 0.5 × (currentKg / capKg)², 0.45, 1.15)   // 契约给定，唯一实现处
实际速度  = BASE_WALK_SPEED × speedMul                              // m/s，BASE_WALK_SPEED = 1.4
```

- `capKg = CARRY_CAP = 25 + 5 × VIGOR`；`staminaMax = STAMINA_POOL = 60 + 10 × VIGOR`。**二者只读自 S0·A `DerivedStats`，S1 不重算。**
- 行走体力消耗（每 **`HOURLY_TICK`** 结算一次，非逐帧）：`Δstamina = −STAMINA_DRAIN_MOVE × (currentKg / capKg) / speedMul`（候选常量，见附录 A-2）。
- `speedMul < 0.70` → 触发底座 §A3.1 的负重修正 −1（体魄·潜行）。**这条耦合是刻意的**：贪心超重会同时被 metabolize 到搬运、潜行、耐力三条线上，"贪一次"因此永远不是无代价的最优解。

### 3.3 生理衰减与辐照

```text
每 HOURLY_TICK:
  water = clamp(water + PHYSIO_DECAY_WATER, 0, 100)      // −8 点/h
  food  = clamp(food  + PHYSIO_DECAY_FOOD,  0, 100)      // −4 点/h
  sleep = clamp(sleep + PHYSIO_DECAY_SLEEP, 0, 100)      // −5 点/h
  rad   = clamp(rad   + HAZARD_RAD_PER_HOUR[hazardLevel], 0, 100)   // 仅在 actor 位于该 zone 时
```

- 睡眠恢复 `SLEEP_RECOVER_PER_HOUR = +12.5`（经 S0·B `requestSleep`）。
- **摄入**：消耗 1 L 水 → `water += PHYSIO_POINTS_PER_LITER (64)`；1 portion 食物 → `food += PHYSIO_POINTS_PER_FOOD_UNIT (96)`。反向换算严格走底座 §D，**禁止出现"S1 定义 1L = 60 点"之类的就地重建**。
- `water < 20` / `food < 20` → 自动座上底座 §A3.1 的脱水/饥饿修正。

### 3.4 伤病与辐照病

`Injury{ type, severity, requiredItemId, treatedDay? }`

- **施加**：来源三类——拆解大失败（`MINOR`）／负重击穿 + 跌落（候选：`FRACTURE`，见附录 A-3）／`rad ≥ 60`（`RAD_SICKNESS`）／`MINOR` 未处理且未清创逐日推进（`INFECTION`）。
- **治疗**：必须消耗 **`requiredItemId` 指向的具体物资**（禁止通用血包），且 `rollCheck(actorId, VIGOR, fieldmedic, DC_DEMANDING, seedCtx)` 成功才置 `treatedDay`。失败消耗物资但伤病保留。
- **单向扣除**：伤病只写 S0·A 的 actor 状态，由 S0·A 的 `modTotal` 表普遍读取；**S1 不得直接让 UI 或对话申请修正值**。
- 归零/超越阈值 → 发 `ActorIncapacitated{actorId, causeCode, dayKey}`，`causeCode ∈ {DEHYDRATION, STARVATION, EXHAUSTION, RAD_OVERDOSE, INJURY}`。

### 3.5 RNG 纪律（本系统实例化）

一个 tick 内多次取值会撞同一 counter，故本系统**每个逻辑抽取一个静态 streamId**：

```text
loot.zone.<zoneId>.<containerId>.eye      // 判定 1 发现
loot.zone.<zoneId>.<containerId>.hand     // 判定 2 拆解
loot.zone.<zoneId>.<containerId>.stealth  // 判定 3 潜行
loot.zone.<zoneId>.<containerId>.q<slot>  // 槽位数量抖动（静态槽位索引，worldgen 可枚举）
claim.<zoneId>.<containerId>              // 仲裁失败时的 deterministic fallback
gen.unique.<uniqueItemDefId>              // 唯一件播种（仅 tick 0 一次）
rad.<actorId>                             // 辐照边缘豁免判定（可选，未定）
```

全部 **静态可枚举**：`zoneId`/`containerId`/`slot` 均来自 worldgen，启动即可完整列出 → 满足底座 §C3存档校验要求。

---

## 4. 状态与流程

### 4.1 容器状态机

```text
SEALED --claimToken(granted)+t1成功--> OPENING --t2/t3--> LOOTING --ZoneMutation@HOURLY_TICK--> DEPLETED
   ^                                       |
   +---------------- t1 失败 --------------+  （保持 SEALED，isDisturbed=true）
```

### 4.2 F1 · 提取流程（玩家与 NPC 共用同一条）

1. 到达容器 → 只读 `ContainerView`（先 claimants，后 items）。
2. 请求取号 `claimToken(containerId, actorId, tick)`。Rule query **唯一**：所有消费者（含 NPC `ScavengeAttempt`）都必须经此点，**不存在绕过**。
3. 拒绝 → 返回 reason 码 → 结束（见 §7）。
4. 授权 → 三次 `rollCheck`（§3.1）→ 生成 `ZoneMutation{kind:TAKE}` → **入队**。
5. **下一 `HOURLY_TICK`**：按 `(mutationId)` 升序应用 → 更新 `remainingByCluster` / `lastLootedDay` / `isDisturbed` → 若容器净空：**发 `ContainerEmptied`**。
6. **失主清算**：容器内仍有未被取走物品的 `Claimant`（且其 `plannedDay > dayKey` 尚未抵达）→ 逐个发 `ClaimDisplaced`；已抵达过或已失效的 claimant 不计入，避免重复冲销同一条需求链。

### 4.3 F2 · Claimant 抵达与 displacement（D1 第一跳）

1. NPC 在 `plannedDay` 的时间窗内到达并执行 `ScavengeAttempt`（依赖 S2）。此处 **S1 不写行为树，只提供失败与否的判定结果与书签**。
2. 若容器已 `DEPLETED` 或目标件不在：该次 attempt 失败 → S1 发 `ClaimDisplaced{actorId, containerId, dayKey, needImpact}`。
3. **`ContainerEmptied.displacedClaimants[]` 的逐个 `ClaimDisplaced` 是 D1 的第 1 跳。** 下游消费方式见 §5.1。

### 4.4 F3 · 单点入库（唯一入口）

`tryTransfer(fromActor, toOwner, items[]) -> bool`

- `toOwner` 为**不透明 OwnerRef 字符串**（`"actor.npc.xxx"` / `"stock.<community>.public"`）。**S1 不解析其语义**——这是保持"不依赖经济/社区"的关键。
- 返回 `false` 的条件（**全有或全无，禁止部分成功**）：① 目标容量为负 ② `items` 中存在 item 不在 `fromActor` 库存 ③ 违反 tier 锁定规则（如 `CRITICAL` 件不可入无主地堆，防洗入妹妹腐败）。**无论成功失败必记归因。**
- 本函数**同时是玩家入库、NPC 入库、缴 arom 与充公的唯一通道**；经济与制作系统**只能通过它写入存储**，不得直接改库存。

### 4.5 F4 · 日切钩子（挂载 S0·B 批处理第 3 步之后）

重算全部 `ZoneSnapshot`；应用 §5.3 的两类再生；衰减季节性 ` retaining_probability` 的警告对口袋水质无影响（保持 stub）。

---

## 5. 对外接口（暴露 / 依赖 / stub）

### 5.1 暴露（**并说明下游怎么消费**）

| 接口 | 消费方 | 用法与注意 |
|---|---|---|
| `ZoneSnapshot(zoneId)` | 区域 GDD / UI / NPC-COARSE | 只读；`depletionDay` 是预测值不是承诺 |
| `ContainerView(containerId)` | UI / NPC 感知 | `claimants` **先于** `items` 被感知（§1.3①） |
| `claimToken(containerId, actorId, tick)` | **所有取用方**（玩家输入、NPC `ScavengeAttempt`） | 唯一仲裁点；`tick` 必须是 `absTick` |
| `tryTransfer(fromActor, toOwner, items[])` | 经济 / 社区 / 制作 | 唯一入库点；全有或全无 |
| `getLoad(actorId) -> LoadState` | UI / S0·A / NPC | `speedMul` 由此只读导出 |
| `getPhysio(actorId) / getInjuries(actorId)` | UI / NPC L1（**只读引用**） | NPC 侧由 S2 读写，`L1` 只读 |
| **事件 `ContainerEmptied`** | **S2 NPC（D1 第一跳）**、S5、日志 | 见下方字段消费说明 |
| **事件 `ClaimDisplaced`** | S2 NPC 需求、S6 gossip、工作板 C9 | 见下方字段消费说明 |
| **事件 `ActorIncapacitated`** | S0·B 中断规则、S5 岗位、Part E | `causeCode` 决定 EstateSettlement 是否启动 |

**`ContainerEmptied.displacedClaimants[]` 的消费契约（D1 第一跳，必须逐字段可解释）**

```text
displacedClaimants[] : Claimant[]   // 元素结构与 Claimant 完全一致，不另建结构
  ├─ actorId     : 下游直接作为 "谁被抽血了" 的主体；也是 step 1 AttributionLink 的 actorIds[]
  ├─ containerId : 与事件顶层 containerId 冗余但必须保留（跨 system 边界时更易校验）
  ├─ plannedDay  : 下游判定"他原本今天/明天来"；plannedDay == dayKey → 当天临:'，为最锋利叙事时刻
  ├─ priority    : 下游决定 Scheduler 抬高搜索半径的强度（priority 越高，改道越激进）
  └─ needReason  : 必须是 enum，不得为自由文本。取值见下
```

**`needReason` 闭集（裁决 C-04 已拍板 · 5 值，禁止扩充、禁止自由文本）**：`POST_SUPPLY`（岗位补给）· `TREAT_SELF`（自用伤病）· `TREAT_OTHER`（救治他人）· `DEPENDENT`（家中有老弱）· `STOCK`（吃进公共仓）。
> 实现期静态断言：`needReason` 为 enum，取值为上列 5 值之一；出现自由文本或第 6 值 → `assert` 中断（防"理由通胀"吃掉 §5.3 的短句翻译表）。
**为什么必须闭集**：下游 Scheduler / 对话 / 工作板要把它译成新生代ğuồng emitting 句子与-dropdown conditions；自由文体会让 D4 gossip 与 C7 对话无法在 ≤3 跳内闭合。

- **step 计数约定**：`ContainerEmptied` → `ClaimDisplaced` 消耗 **step 1–2**：`causeRef = S1.container.<id>.state`，`effectRef = S1.claimant.<actorId>.<containerId>.lost`；下游第 3 跳必须落在"该 NPC 改道 / 加岗 / 抬升搜索半径"（属 NPC 模拟 GDD，本份不越界）。**超过 3 跳由下游报警，不由 S1 承担。**

### 5.2 依赖

- **S0·A**：`rollCheck`（MIND/scavenge_eye、HAND/dismantle、VIGOR/stealth、fieldmedic）· `DerivedStats`（carryCap / staminaPool）· `PhysioState`。
- **S0·B**：`onHourTick`（生理/辐照/伤病推进）· `HOURLY_TICK`（mutation 应用）· `DAILY_CUTOVER`（快照与再生）· `absTick`。
- **S0·C**：`rng(seedCtx)` · `makeSeedCtx(streamId, absTick)` · `recordAttribution`。
- **区域内容 GDD**：`ContainerDef[]{ containerId, zoneId, tier, initialItems[], discoveryDC/extractDC/noticeDC 档位, isUniqueAnchor }`。
- **S2 NPC 模拟**：`ScavengeAttempt` 必须经 `claimToken`；这是 S1 对 NPC GDD 的**唯一硬接口请求**。
- **Part E EstateSettlement**：继承 claimant 释放；S1 只保证死亡时刻的最后持仓已落盘。

### 5.3 stub（明示为未完成）

| stub | 当前行为 | 何时替换 |
|---|---|---|
| `zoneRegen` | 仅两类：`WILD_FOOD`（按季，季门槛返回一个固定产量）· `RAIN`（按天，返固定加成）。**其余概不刷新。** | Extended（天气/季节） |
| 天气与季节接口 | 一律返常量 | Extended |
| `hazardLevel` | 区域静态配置，**不做动态扩散** | Vision |
| `CRITICAL` 件的 gossip/工作板预告 | 不广播，仅静态挂载 | 依赖 S6/C9 后接通 |

---

## 6. 玩家可感知表现

1. **先看人，再看货**：进入 `observeRadius`（`4 + 0.6 × MIND`）后，容器先浮出一个**归属标记槽**（谁的名字/他明天来/为什么），凑近后才为 श illnesses- items。这是 P2 的核心物语化。
2. **体力三轮 instead 数字**：打包界面一次呈现 `speedMul` 的即时变化 + 预判行程时间 + 是否在 upcoming tick 进入 `speedMul < 0.70` 的潜行惩罚档。**负重的痛必须发生在决策那一刻，而不是走起来之后才发现。**
3. **大失败必有可见后果**：拆坏必有血（`Injury MINOR` 即时弹 petto）、惊动必有声 NPCs look up。
4. **延迟被发现**：离开后，UI 不立即提示；次日你回到这里，会看见 `isDisturbed` 引发的世界侧变化——绳子被加装、柜子被搬进屋、路面多了脚印。这是 P3 与 D2 兑现的时刻。
5. **items 不显示"+3 零件"，显示"-某人的三天"**：跟以避免数值化，`ContainerEmptied` 后在日志里出现由 `needReason` 翻译的短句（"河口的取水口滤芯没了"）。

---

## 7. 边界情况与失败模式

**E1 · 玩家超载（必须含）**
场景：`tryTransfer` 后 `currentKg > capKg`。
规则：**硬拒绝**——`tryTransfer` 返回 false 且不部分通过；当场提示"塞不进去"。超载**只可能**由尚未落地的拾荒造成（玩家已取但未入库），此时 `speedMul` 落到 `clamp` 下界 `0.45`，并开始加速消耗体力直到 `ActorIncapacitated{EXHAUSTION}`。**禁止自动丢弃**——丢什么必须由玩家决定，这是"负"动词的全部意义。

**E2 · 同刻争抢（必须含）**
场景：同一 `absTick` 内多个 actor 对同一容器求取。
规则：**排序 → 逐个 `claimToken`**。`sortKey = (priority, plannedDay, actorId 字典序)`，**终局 tiebreak 严格为 `ActorId` 字典序**（满足已拍板契约）。落选者收 `reason = DENIED_TOKEN_HELD`，**不得重试本 tick**。
> **契约解释申报 C-03（需主理人确认）**：纯字典序意味着 `"player"`（`p`）恒输给 `"npc.*"` 与 `"anon.*"`（`n`/`a`），平优先级下玩家系统性落后。本份采用"字典序作为终局 tiebreak、前置 priority/plannedDay"的解释以保住公平性；若须严格按原文（字典序为唯一排序键），请回退，但那样玩家在同刻永远抢不过 NPC。

**E3 · 容器在 wardrobe 已手而在队伍中消散**
场景：`ZoneMutation` 已入队未应用时，另一个 actor 读 `ContainerView`。
规则：读到的仍是上一次整点已生效状态（**读昨日/上整点快照，不读队列**）；可能因此发生"两人都成功取票却发现货没了"——**这是合法且不补偿的**，由 E2 的 reason 码回收。符合已拍板"下游读快照"。

**E4 · Claimant 自身死亡 / 失能**
死者 claimant 由 EstateSettlement Phase 1 即时释放（`state = UNCLAIMED`），玩家可立刻合法抢——道德时刻，不需要 S1 写任何判定。若 `ClaimDisplaced` 的主体在事件发出前已 `ActorIncapacitated`，则**不发出**该条（对死者无意义），改为写一条废弃归因。

**E5 · 唯一件丢失**
进入 `LOST` 必须 warn + 归因。若缺失归因 → 视为未实现。**关键件沉默消失会直接击穿 P1**，这条是硬红线。

**E6 · 辐照得了辐照病而且所需药为唯一件**
`requiredItemId` 指向 U 类物品时，治疗路径**不被豁免**：找不到就是找不到，可能会死。**但**这正是 BO 菌的设计意图；唯一救济是 E7。

**E7 · COARSE 层的 NPC 取用**
离屏 NPC 不跑三次 rollCheck（性能预算），改由 `ZoneMutation` 打上去的统一统计近似，ContainerEmptied 仍照常发。**必须**带 causeRef，否则违反 B3.3。

---

## 8. 验收标准与调试钩子

**验收**

1. **静态守恒**：任一 zone 的 `remainingByCluster` **逐日单调不增**（唯一例外是 §5.3 的两类 `zoneRegen` stub），违反一次即判失败——这是 P4"物资不是刷新的"这一主张最可测量的形态。
2. **R2 反重复感**：固定种子 Bot 连跑 30 游戏日按"最近邻 minContainer"策略，其单位 Isabel per(useful item) 的边际收益不得单调 flat（达不到 **至少 30% 的相对下降** 即判失败）——证明"多跑一趟不能平恒单个店的单位收益"，必须深挖/竞争/换目标。
3. **D1 第一跳可达**：脚本化清空某 zone 80% 物资 → 3 游戏日内必然产生 ≥2 条 `ClaimDisplaced`，且每条 `needReason` 在闭集内、`AttributionLink.step ≤ 3`、**`UNATTRIBUTED == 0`**。
4. **主导策略检查**：单一最优资源路线的 10 日物资净值不超过最优混合路线的 1.25 倍（继承 CORE 验收 4）。
5. **确定性与预算**：，S1 部分需用部分让入 `worldHash`;、 S1 的仿真开销纳入底座 §C3 的 6ms/帧预算（本系统目标 ≤1.2ms/帧），同 seed 10 日 save/load 差异 ≤5%。
6. **鉴定式追寻**：だから全游戏 "实现 d20" 只有一处；本系统所有 `rollCheck` 调用均在调试面板的 RNG 检视器可见。

**调试钩子**

- `--dump-container <containerId>`：tier / claimants / items / mutation 队列 / 三次判定最近 20 次 `raw/dc/margin`。
- `--dump-load <actorId>`：currentKg / capKg / speedMul / 体力约束曲线。
- `--force-tier <containerId> <tier>`、`--teleport-unique <uniqueItemDefId> <containerId>`（用来构造 U 类件测试，禁止用于正常流程）。
- `--dump-physio <actorId> <dayRange>`、`--apply-injury <actorId> <type> <sev>`。
- `--dump-mutation-queue`、`--assert-static-conservation`（逐 zone 校验代谢式守恒）。
- `--dump-unique`：打印 `UniqueManifest` 全量 + 每个 U 类件的当前 `[SEALED|IN_TRANSIT|CONSUMED|DESTROYED|LOST]` 与附着容器。

---

## 附录 A · 提请回填底座 §D 的候选常量（本份不就地生效）

> 底座 §D 是唯一定义处，故以下数值**暂以 Proposal 状态存在**，批准前实现应连「待定值」处理。

| # | 候选常量 | 建议值 | 单位 / 说明 |
|---|---|---|---|
| A-1 | `HAZARD_RAD_PER_HOUR` | `[0, 2, 6, 14]` 对应 level 0..3 | 点/h，`hazardLevel` 为静态配置整数 0..3，见 §2.5；不做动态扩散。 |
| A-2 | `STAMINA_DRAIN_MOVE` | 6 | 点/h，满载倍率按secondly `1/speedMul` |
| A-3 | `FRACTURE_TRIGGER_LOAD_RATIO` | 0.95 | 比例，超此占比且在被击中/跌落时判 `FRACTURE` |
| A-4 | `DEPLETION_WINDOW_DAYS` | 3 | 日，`depletionDay` 外推窗口 |

## 附录 B · 待主理人裁决 / 需与并行 GDD 对齐的事项

1. **C-01** `rad` vs `radAccum` 字段名（建议保 `rad`）。
2. **C-02** 概念文档 §3.1-C 的发现率概率式应被本份取代，建议修订概念文档以防火种回流。
3. **C-03** 同刻争抢的排序键解释（见 E2），关系到玩家是否会被字典序系统性压制。
4. **接口请求**：需区域内容 GDD 提供 `ContainerDef` 的完整字段（含三档 DC 档位与 `isUniqueAnchor`）；需 NPC-Simulation GDD 确认 `ScavengeAttempt` 强制经 `claimToken`；需 Economy GDD 确认**只经 `tryTransfer` 写库存**。
5. **可能冲突**：① 经济 GDD 若要"公共仓堆栈"，须 `tryTransfer` 的全有或全无语义，不可要求部分成功；② UI GDD 若要 prognostic "背包格子数"，本域只有 kg 与 speedMul，请按承重叙事而非格子系统；③ 伤病在制作 GDD 里若被复用，请 `requiredItemId` 必须由 S1 单一出具，避免治疗物资在两处被分别定义。

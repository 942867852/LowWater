# S2 · NPC-Simulation（NPC 三层模拟）· 系统设计文档

- **Task ID**：GDD-002｜**阶段**：Phase 2 · 批次 B1｜**优先级**：P0（概念文档指定的差异化核心，也是 R1 最大压力测试点）
- **主笔**：design-strategist（文策渊）｜**调度**：主理人（游承峰）｜**状态**：待拍板（附录 B 共 9 项待对齐）
- **依赖已读**：`design/gdd/game-concept.md` · `design/gdd/systems/00-foundation.md`（S0·A/S0·B/S0·C/§D/Part E）· `design/gdd/systems/01-scarcity-loop.md`（S1）
- **依赖方向（严格单向）**：依赖 S0·A/S0·B/S0·C/S1；**不依赖**社区（S5）与经济（S3）的实现——对二者只发事件，不读其状态、不算其公式。
- **边界（严格）**：L1 需求 → L2 岗位调度与 24h 路线 → L3 行为树扰动与回写；7 天短期记忆 + ≤5 条长期印象；gossip 有界传播；岗位劳动产出。
  **不写**：对话文本与分支（对话 GDD）、定价与仓储会计（经济 GDD）、声望公式（社区 GDD）、UI 布局（UX）、任何 raw RNG stream。

---

## 0.0 规范系统编号对照表（PHASE2-REVIEW · 裁决 C-13，全文已按此表替换）

| 规范编号 | 系统 | 文件 | 本文档原用临时编号 |
|---|---|---|---|
| **S0** | 底座（`S0·A` 角色判定 / `S0·B` 世界时钟 / `S0·C` 确定性 / `S0·D` 常量表 / `S0·E` 死亡清算） | `00-foundation.md` | S1 / S2 / S12 |
| **S1** | 稀缺主循环 | `01-scarcity-loop.md` | S5 |
| **S2** | **NPC 三层模拟（本文档）** | `02-npc-simulation.md` | S6（自称） |
| **S3** | 易货经济与社区仪表 | `03-economy-barter.md` | S9（"经济"） |
| **S4** | 记忆驱动对话与契约账本 | `04-dialogue-contract.md` | "对话 GDD" |
| **S5** | 社区、信条与声望 | `05-community-creed.md` | S7（"社区"） |
| **S6** | 传闻传播 | — | —（gossip **传播拓扑归 S6**，见裁决 22） |
| **S7** | 工作板（待写） | — | C9（概念文档 scope ID，保留原写法） |
| **S8** | 区域内容（待写） | — | "区域 GDD" |
| **S9** | 呈现 / UI（待写） | — | "UX / UI GDD" |

---

## 0. 全局契约（逐字继承，不改字段名）

```text
getNeeds(actorId) -> NeedVector{ safety, social, creed, physio: 只读引用 PhysioState }
getSchedule(actorId, dayKey) -> { blocks:[{startMin,endMin,postId?,locationId,route:Waypoint[],intentTag}] }
getActorState(actorId) -> { alive, locationId, activity, tickLevel, carrying:ItemStack[], injuries[] }
getMemoryView(actorId) -> { recentEvents:[MemoryEvent](dayKey ≥ today−6), impressions:[ImpressionTag ≤5] }
getLaborOutput(communityId, dayKey) -> { byCluster:{...}, filledPosts, ratedPosts }
事件: ActorDied{actorId,causeCode,dayKey,minute,locationId,postId}
      // 裁决 17 已拍板：本 6 字段为唯一发出签名；另保留 causeRef 附带字段供归因链接入（S0 Part E 已同步）
     PostVacated{postId,communityId,dayKey,reason}
     ScheduleChanged{actorId,dayKey,changeKind:"REROUTE"|"ADD_WATCH"|"DEEPEN_STASH"|"SET_TRAP", visibleSignal:string}
     NeedUnmet{actorId,need,severity,dayKey}
     ScavengeAttempt{actorId,containerId,dayKey}
     GossipEmitted{originActorId,topicActorId,claimCode,dayKey}
```

> **契约解释（本份默认采用；涉 C-04/C-05/C-11 见附录 B）**
> - `physio` 是 `PhysioState` 的**只读引用**：本系统可读可监听，写权归 S1。自有需求仅 `safety/social/creed`（0–100）。
> - `NeedUnmet.need` 闭集 = `{SAFETY, SOCIAL, CREED}` ∪ `{PHYSIO_WATER, PHYSIO_FOOD, PHYSIO_SLEEP}`；后三项是 S1 数值的**镜像转发**（每日去重一次），所有权仍在 S1，本系统只让它们进入记忆与 gossip 输入集。
> - `getActorState().tickLevel` 是 S0·B `tickLevel(actorId)` 的镜像（**C-11 已裁决**：统一为 `tickLevel`，旧名 `lodStateOf` **已废弃**，任何 GDD/实现不得再出现），取值沿用底座 §B3 的 `FULL/COARSE/PROTECTED`，本系统不新增 LOD 档位。
> - **`PROTECTED` 档的名单 = `protectedActors`（≤3）**，与 S4 §2.6/§3.5、S5 的 `protectedActors` **是同一份**，唯一定义处为 S0 §D；**禁止实现成两份**（C-27 已裁决）。

---

## 1. 系统概览与目标（对应支柱/动词）

### 1.1 定位

概念文档把"自主日程 NPC"从卖点降格为"资源循环的载体"。本份负责把它落成可实现的三件事：①一个具体的人每天几点去拿哪一口水（P1）；②这份日程能被玩家读懂（P2）；③被偷之后它会变，而且变之前你看得见（D2）。

- **支柱**：**P2（主）**——解决 P2↔P3 张力时的第一性是"可被 metagame"，而非"统计上像人"；**P3**——离屏照样推进，岗位照样缺人；**P1**——任何远方涟漪必须 ≤3 跳落回到 ≤16 个有名字 NPC 里的某个人；**P4**——劳动产出的分子永远是 `在岗人数/额定人数`，你的偷窃被兑换成他人的工作日。
- **动词**：本系统自身不产生动词，但它是 **读 OBSERVE**（NPC 路线本身就是动态地图）、**取 SCAVENGE**（与 NPC 抢同一份有限物资）、**守望 TEND**（顶岗填空缺）三个动词的**唯一发生场**。
- **反目标**：不做效用最大化黑箱、不做 GOAP、不做不可读的"聪明 AI"；不做 NPC 两两全交互；不做随机迟到（概念文档 §P2 已否决）。

### 1.2 一句话职责

> 让地图上每一个你会记住的人，都有一条你能在两天内背下来的 24 小时轨迹——并且当你弄坏它时，他有权利、有信号、有记忆地把它改掉。

### 1.3 三条自建红线（比裁决更严的自我约束）

| # | 裁决 | 理由 |
|---|---|---|
| R-A | **默认岗 = `homePost` 静态世袭，禁止评分制改派** | 追求"真实"的可变分配会直接炸掉 P2。Core 验收 2（位置预测 ≥70%）依赖"他昨天在这儿，今天还在这儿"。只有 §3.2 的四类硬理由允许改派，且每类都留下可见信号。 |
| R-B | **本系统 raw RNG stream 数量 = 0** | 所有随机判定走 `rollCheck`（streamId 所有权在 S0·A）；所有"选择"（gossip 传播对象、改道路线）改为**纯确定性排序**。这是裁决 2/7 与底座附录 B-6 的最严读法。 |
| R-C | **禁止随机迟到与随机抖动** | `startMin` 只由算术（路线长 ÷ 速度）得出，不含随机项。这同时把 R1 的可复现成本降到零。 |

---

## 2. 核心概念与数据模型

### 2.1 三层结构

```text
L1 Needs       NeedVector{ safety, social, creed } + physio:→PhysioState(只读)
               每 HOURLY_TICK 结算；跌破阈值 → 发 NeedUnmet + 写入当日 demand 票
L2 Scheduler   每日 DISPATCH(06:00) 产出 ScheduleDay{ actorId, dayKey, blocks[≤8] }
               输入：PostRoster / demand 票 / 伤病 / DaySnapshot[D-1] / gossip 注入
L3 Behavior    FULL 与 PROTECTED 实体专属行为树；处理扰动 → 回写 L1(demand) 与 L2(次日 route)
```

### 2.2 数据模型

```text
NeedVector   { safety, social, creed : 0..100, physio: Ref<PhysioState> }   // physio 只读
DemandTicket { actorId, need, severity, dayKey, preferPostId? }             // L1→L2，当日有效
Post         { postId, role, locationId, rated, onDuty:ActorId[], nominalOut:Quantity[] }
PostRoster   { communityId, posts: Post[6] }                                // 静态 6 岗位
Block        { startMin, endMin, postId?, locationId, route:Waypoint[], intentTag }
ScheduleDay  { actorId, dayKey, blocks: Block[≤8], homePostId, dispatchedTick }
MemoryEvent  { seq, dayKey, minute, kind, refId, weightQ:1..7, actors:ActorId[] }
             // kind 闭集新增 RUMOR_HEARD（裁决 26 / R-4）：S6 注入传闻时写入，refId = topicSeedId
ImpressionTag{ tagCode, topicActorId, charge:int, writtenDayKey, sourceHop:0..3 }
LossStreak   { actorId, targetRef, pressure:int 0..9, lastLossDay, cleanDays }
WorldSignal  { signalId, refId, kind, installTick, expireTick? }            // visibleSignal 的落点
GossipUnit   { topicSeedId, topicActorId, claimCode, charge, hop:0..3, reachedSet:ActorId[] }
```

**闭集 `intentTag`**（面板与日志共用，禁止自由文本）：`SLEEP` `RITUAL` `COMMUTE` `WORK` `HAUL` `SOCIAL` `WATCH` `PATROL` `TREAT` `EAT` `SEEK` `ALERT`。

**闭集 `ActivityId`**（`getActorState().activity`）：上述 12 值 + `DEAD` + `FLED`。

> **与底座 §C8 面板色块的兼容**：底座规定甘特图只有 7 色 `SLEEP/COMMUTE/WORK/IDLE/SOCIAL/ALERT/DEAD`。本系统给出映射：`EAT/RITUAL→IDLE`、`HAUL→COMMUTE`、`WATCH/PATROL/TREAT/SEEK→WORK`、`FLED→DEAD`。**面板 7 色，日志层保留 14 值。**

### 2.3 岗位编制与其产出（**裁决 5 · 7 已回填；编制表权属归 S5 §2.1**）

**编制长度由社区配置决定，不是硬编码 6 槽**（C-32，随裁决 7 连带回填）：`he_valley` 7 个 `postId`（含 `water_02`）/ `jing_cell` 6 个。本系统一律读 `PostRoster(cid, dayKey)`（S5 §5.1），**不自持槽位表**。

`nominalOut` 口径 = **岗位总额定**（该 `postId` 全体额定岗之和，**不是**"每额定岗"），实际产出 = `nominalOut × (effHead / rated)`。数值逐字取自 S3 §2.3 / §2.4（裁决 7）。⚠ 该口径本身仍待裁决，见 S3 §2.3 的 C-15（开放问题 OQ-1）。

| postId 后缀 | role | "到位"的判定 | `nominalOut`（岗位总额定 / 日） | 备注 |
|---|---|---|---|---|
| `water_01` | 取水 | 到达取水位并完成一次注水 | `he_valley {WATER: 21.0 L}`；`jing_cell {WATER: 21.0 L}` × `postYieldMul 0.7` → **14.7 L** | 河谷两水岗合计 **42 L** vs 日耗 27 L → **+15 L（全图唯一盈余）**；井窖满员 **14.7 L** vs 日耗 21 L → **−6.3 L 致命缺口，缺员即缺水，1 跳传导** |
| `water_02` | 取水 | 同上 | `he_valley {WATER: 21.0 L}`（井窖**无此岗**） | 两社区编制差异的唯一来源，见 S5 §2.1 |
| `watch_01` | 守夜 | 夜间在哨位 | `{}` | 不产簇物资；提供 `watchSuppression`（§3.6） |
| `patrol_01` | 巡线 | 沿线巡检并返回 | `{AMMO: 4 rd, FOOD: 3.0 portion}` | **裁决 7 回填**：`PATROL_AMMO_YIELD(4)` + `PATROL_FOOD_YIELD(3.0)`；**FOOD 挂本岗**（双方均产、均不自给） |
| `mend_01` | 修补 | 在工具位 | `{}` | 提供 `repairMul`，乘到 water 岗 |
| `care_01` | 育儿 / 照护 | 在公共屋 | `jing_cell {MEDS: 1.2 dose, SEED: 0.5 portion}`；`he_valley {}` | **裁决 7 回填**：井窖 `care_01` 兼管地下诊所药圃与温室留种；**河谷无医无圃 → 产出恒 0**。缺岗 → 占走 1 名劳动力（`carePenalty`） |
| `idle_00` | 闲散（**伪岗位**） | 余量人口 | `{}` | **裁决 5（C-05）已拍板**：`rated = pop − Σ其它 rated`（可为 0），**不计入 `filledPosts` 分子、不计入 `ratedPosts`**；gossip 的唯一合法发源地，也是 Part E 的补岗池 |

> **FUEL 无任何岗位产出**（裁决 7）：双方均不自产，唯一来源是 S1 侧的区域拾荒（旧油罐 / 废车 / 旧政权油库）→ `getLaborOutput.byCluster` **永不返回 `FUEL`**。
> **命名统一**：原表写 `idle_01`、C-05 建议写 `idle_00`，本次统一为 **`idle_00`**（与 S5 §2.1 一致；S0 §D 已登记 `role ∈ {water, watch, patrol, mend, care, idle}`）。

### 2.4 记忆模型

- **短期**：`MemoryEvent`，窗口 `dayKey ∈ [today−6, today]`（与契约 `dayKey ≥ today−6` 逐字一致，共 7 个 dayKey = `MEMORY_SHORT_DAYS`）。权重整数化 `weightQ = MEMORY_SHORT_DAYS − (today − dayKey)`（7→1）：**不用指数衰减，规避底座 §C7 的浮点漂移禁令**。
- **长期**：`ImpressionTag ≤ IMPRESSION_MAX(5)`。`charge` 为整数。
  > **裁决 14（C-24）已拍板 · 存储粒度**：`ImpressionTag.tagCode` **必须存 S4 §2.5 的 12 码细分 `tagId`**（如 `stole_well_water` / `broke_word`），**不是** §3.7 的 8 码 `claimCode`。**collapse 到 8 码只发生在 gossip 传播时**（`GossipUnit.claimCode = collapse(tagCode)`）。
  > **为什么**：若只存 8 码，"偷过井水"（`stole_well_water → THIEF`）与"说话不算数"（`broke_word → HIRELING`）在印象层将无法区分，S4 表 #8/#9/#10 各自的"关闭条件"（拒 `quote` / 拒入户 / 拒新承诺）无从分流。
  > **仍是同一张码表**：12 码是 8 码的**细分**，S4 不得新增第 9 个 `claimCode`；`--dump-impression` 须同时打印 12 码与 collapse 后的 8 码。

---

## 3. 规则与公式

> 本节所有常量均为**候选（Proposal）**，按底座 §D"唯一定义处"纪律集中挂在附录 A，**批准前实现按待定值处理**。

### 3.1 L1 需求：每小时推进（只在 `HOURLY_TICK`，禁渲染帧）

```text
safety += (isHome && !curfewViolated) ? +P_SAFETY_HOME : −P_SAFETY_DRIFT
social += 与有名者同处一地       ? +P_SOCIAL_GAIN : −P_SOCIAL_DECAY
creed  += 当日已完成一次信条践行 ? +P_CREED_GAIN  : −P_CREED_DECAY
三项均 clamp(0,100)
```

- 事件型扣减（同 tick 叠加后 clamp）：目击/亲历 `THREAT` −8；收到 `ClaimDisplaced` −4；亲眼看到同社区成员尸体 −12；违反本社区 `CreedConstraints` 一次 −6。
- **severity 分级**：`v<40 → 1`，`v<20 → 2`，`v<5 → 3`。每条 `(actorId, need, dayKey)` 每日只发一次 `NeedUnmet`（取当日最重档），且只在 `HOURLY_TICK` 上发。
- **L1 不读写水/食/睡**：`physio.water < 20` 等情形只转发一条 `NeedUnmet{PHYSIO_WATER}`，数值与衰减仍归 S1。

### 3.2 L2 调度

**SO 1｜定岗**：默认取 worldgen 静态的 `homePostId`。只有四类硬理由可改派：

| 理由 | 判定 | 是否发 `ScheduleChanged` |
|---|---|---|
| R1 死亡/失能补岗 | 收到 `PostVacated` / Part E 结果 | **否**——重复发会污染事件流；可见性由"岗位上少了一个人"承担，且 Part E 已保证"隔天早上才知道" |
| R2 需求抢占 | `DemandTicket.severity ≥ 2` 且带 `preferPostId` | 是，`REROUTE` |
| R3 D2 升级 | §3.5 `LossStreak` 越阈 | 是，`REROUTE/ADD_WATCH/DEEPEN_STASH/SET_TRAP` |
| R4 信条约束 | `curfewMin` 前未能归位 / `tithe` 未缴 | 是，`REROUTE`（归位优先） |

**SO 2｜同岗争位（唯一允许的随机）**：仅当两名 actor 争同一 slot 且**全部确定性排序键相等**时，对二者各发起
`rollCheck(actorId, MIND, survey, DC_TRIVIAL, seedCtx)`，余量大者胜；仍平 → `actorId` 字典序升序。
确定性排序键顺序：`(needDeficit desc, relevantSkill desc, seniority desc, actorId asc)`。`seniority` 由本份定义为 `(入社区日 asc, actorId asc)`，提请 Part E 确认。

**SO 3｜生成 24h 路线**

```text
travelMin(a→b) = ceil( pathLenMeters / (BASE_WALK_SPEED × speedMul(a) × 60) )   // speedMul 只读自 getLoad
startMin(next) = endMin(prev) + travelMin
```

- **每人每日 `blocks ≤ NPC_BLOCK_MAX(8)`**，每个 `Waypoint[]` 顶点 `≤ WAYPOINT_MAX(6)`——这是路径开销的硬上限，也是 COARSE 层能用闭形式的前提。
- 睡眠块必须落在 `[creed.curfewMin, curfewMin + SLEEP_BLOCK_MIN]` 内闭合；无 creed 的匿名实体 → `DEFAULT_CURFEW_MIN(1320)`。
- 匿名实体与有名实体共用同一套块生成函数，只是不参与争位、不产生改写涟漪。

### 3.3 L3 行为树：只在 FULL / PROTECTED 上跑

- **评估时机（事件驱动，非轮询）**：① `HOURLY_TICK`；② 当前 `Block` 的边界分钟；③ 收到扰动事件。**其余分钟一律不评估**——这是 §8 性能回答的第一根柱子。
- 节点深度 `≤ 4`；叶节点动作立刻落为一个写队列条目或一条事件。**禁止"发现→追击→寻路"的长链路，一律用单次 `rollCheck` 定胜负。**

| 扰动 | 触发来源 | 判定 | 结果 |
|---|---|---|---|
| `P_FOUND_LOOTED` | 抵达 `plannedDay` 目标且容器 `DEPLETED` / `isDisturbed` | `VIGOR/stealth, DC_DEMANDING` | 成功 → 现场蹲守 30min（`activity=ALERT`，肉眼可见）；失败 → 返回 + `pressure +1` |
| `P_AMBUSH` | 沿路 encounter 点触发（由 S1/区域 GDD 拥有，本系统只消费告警） | `MIND/survey, DC_PERILOUS` | 失败必见后果：`Injury MINOR` + 同社区 `safety −8` |
| `P_CORPSE` | 视野内出现尸体 | 无（纯确定性） | 写 `MemoryEvent{kind:CORPSE}`，同社区 `safety −12`，并产出一条 gossip 种子 |
| `P_WEATHER` | 天气 stub 返回 `HEAVY` | `MIND/survey, DC_TRIVIAL` | 成功 → `REROUTE`（走屋檐下，路程 +10%）；失败 → 照常走，额外扣体力 |
| `P_STAMINA_OUT` | `stamina ≤ 0` | 无 | 原地休整（`activity=IDLE`），该 block 记 `partial` |

> **改日程是应对，不是惩罚**：所有 `ScheduleChanged` 次日生效，且 `visibleSignal` 指向的对象在当日已装进世界。这是 D2 闭环的发动机，也是裁决 5 的机械落点。

### 3.4 劳动产出（严格遵守 Part E 公式）

```text
实际产出 = 额定产出 × (effHead / rated)        // Part E 给定
effHead  = Σ_worker completeFactor(worker)     // 完成=1，部分完成(迟到>30min)=0.5，缺席=0
```

`getLaborOutput(communityId, dayKey)`：`byCluster` 为各簇 `Quantity[]` 求和（严格按 §D 的 `cluster→unit` 映射）；`filledPosts = |{post : onDuty ≥ 1}|`（`idle` 不计）；`ratedPosts = Σ rated`（不含 `idle`）。
**入仓必须经 S1 `tryTransfer(fromActor, "stock.<community>.public", items[])`——本系统不得直接改仓储数字。**

> **C-06（越界请求）**：`effHead` 是对 Part E 分子的微弱扩展（引入 0.5），目的是让伤病/迟到产生可见代价。若主理人退回，`completeFactor` 取整（部分完成 = 0），公式自动回落为 `(onDuty/rated)`。

### 3.5 D2 · 作息空隙与军备竞赛（完整闭环）

**压力累积**（按 `actorId × targetRef` 独立计数，上限 9）：

```text
ClaimDisplaced(actorId)                       +1
ScavengeAttempt 失败（目标容器 DEPLETED）       +1
读到 isDisturbed=true 且 claimant 是自己        +1
干净日（当日上述事件均未发生）                  −1（日切时结算，floor 0）
```

**升级表**（actor 级）：

| 级 | 触发 | changeKind | 可见信号 `visibleSignal`（信号类 + 指向） | 玩家旧知识如何失效 |
|---|---|---|---|---|
| L1 | `pressure ≥ 2`（近 3 日内） | `REROUTE` | `FOOTPRINT_DEVIATION:<loc>`——岔口出现偏离旧小径的新脚印，且他出发提前/推迟 ≥20min | "09:20 他准到"作废，必须重新掐出发时刻 |
| L2 | `pressure ≥ 4` 或单次丢失 `CRITICAL` 容器 | `ADD_WATCH` | `WATCH_ADDED:<loc>`——目标点旁多一盏过夜灯、一把坐具、一个不再赶路的人 | 夜间窗口关闭；白天窗口仍在，但需绕开视线 |
| L3 | `pressure ≥ 6` 或同一容器连续 2 日被清空 | `DEEPEN_STASH` | `STASH_MOVED:<ctrOld>→<ctrNew>`——原址留拖拽痕与空绳印，目标件进了更深的屋子 | 目标**位置**变了；脚印链成为唯一的线索 |
| L4 | `pressure ≥ 8` 或同社区 ≥2 人同时达到 L2 | `SET_TRAP` | `TRIPWIRE:<ctr>`——绊线/铃铛/松动踏板，在 `observeRadius` 内可见 | 偷转为负收益：触发 → `Injury MINOR` + `safety −8` + 一条负面 gossip 种子 |

**社区级级联**：`communityPressure = Σ pressure`；≥ `COMMUNITY_PRESSURE_TIER(12)` → 请求 S1 把辖区内 `SCARCE/CRITICAL` 容器的 claimant `priority` 抬一档，并给所有执勤 NPC 的 blocks 增加 1 个 `WATCH` 段。**该步属越界请求（C-07），未获批前降级为"只改路线、不动 claimant"。**

**降级回路（必须存在，否则它是世界上唯一的单程机票）**：连续 `CLEAN_STREAK(3)` 个干净日且 `pressure ≤ 本级阈值 − 2` → 降一级，`visibleSignal` 反向安装（拆铃铛、灯灭、柜子被搬回门边），同样发 `ScheduleChanged`。
**泄压阀（P4 ↔ 探索自由的法定出口）**：即便达到 L4，永远保留三条路——`TEND`（顶岗换配给）、`BARTER`（直接买）、`PERSUADE`（求/骗）。黑市走私见 D6（Extended）。

### 3.6 岗位乘子与外部性（Proposal）

```text
repairMul        = clamp(0.7 + 0.3 × (effHead_mend / rated_mend), 0.7, 1.0)
carePenalty      = (onDuty_care == 0) ? −1 effective head : 0
watchSuppression = watch 岗在岗时，夜间 pressure 增量 × 0.5
```
三者只影响 water 岗实际产量、劳动力总账、被盗压力斜率——**不引入第四个数值系统**（防认知过载）。

### 3.7 Gossip：失真如何用机制而非形容词定义

> **裁决 22（PHASE2-CLOSING · R-8 权属迁移）**：**gossip 传播拓扑的所有权归 S6 `06-rumor-network.md`**——包括中继资格判定、fanout 选人、`reachedSet/visited` 维护、hop 推进、inbox 注入与丢弃、去重合并。
> **本份（S2）只保留两项**：① **种子生成** `GossipEmitted{originActorId, topicActorId, claimCode, dayKey}`；② **失真码表定义**——8 码 `claimCode` 闭集、静态失真图（8 码 → 后继码，3 个 terminal）、失真方向打分（印象符号 / dominant need / 社区 `tithe`·`canShelterOutsider` 三项加权，平手取边表索引升序，**零随机**）。
> 本份以纯函数形式暴露 **`distort(claimCode, relayActorId, dayKey) -> { nextCode, scoreDetail }`**（裁决 23 / R-5），供 S6 在每跳调用；S6 **不得**重建第 9 个 `claimCode`、不得重写失真方向规则。
> 下文 §3.7 保留的是**码表与打分规则的定义**，其中的"每跳 fanout / 注入 / 丢弃"等运行时描述**以 S6 为准**（S6 已按此实现并给出更严的规模上界：单日 `RumorInjected` ≤ 32，批处理 p95 ≤ 1.5ms）。
> **裁决 24（R-3）**：每个 actor 每日 **SOCIAL 块 ≤ 1**，把传播面进一步压到最小。
> **裁决 25（印象槽噪声）**：由传闻（`sourceHop > 0`）写入的印象**每人 ≤ 2 条**，terminal 码豁免且永不淘汰；保留 8 码 → 12 码默认映射表（S6 §3）。本份原"未满则写"改为受此闸门约束。

**单元**：`GossipUnit{ topicSeedId, topicActorId, claimCode, charge, hop, reachedSet }`　*(运行时实例由 S6 持有)*。
`claimCode` 闭集（8 码，与 `ImpressionTag.tagCode` 共用）：

```text
WATER_GIFT(+)  HIRELING(0)  KIN(+,terminal)  MARTYR(+,terminal)
WATER_DEBT(−)  THIEF(−)     CREED_BREAKER(−) OUTLAW(−,terminal)
```

**失真图 `DistortionGraph`**（静态有向图，worldgen 只读）。**什么决定失真方向**——由中继者的三项偏差打分决定，而不是随机：

| from → to | 偏置条件（命中则该边 score += w） |
|---|---|
| `WATER_GIFT → HIRELING` | 中继者自身 `creed` 为其最低需求，或对 topic 无正向印象 |
| `WATER_GIFT → KIN` | 中继者对 topic 有 ≥1 条正 charge 印象 **且** 当前 `charge ≥ Q_STRONG` |
| `HIRELING → THIEF` | 中继者的 dominant need = `SAFETY`，或两社区关系 < 0 |
| `THIEF → OUTLAW` | 中继者曾**直接目击**（记忆中存在 `sourceHop == 0` 的相关事件）或 `safety < 20` |
| `WATER_DEBT → THIEF` | 中继者 `physio.water < 20` |
| `THIEF → CREED_BREAKER` | 中继者所属社区 `tithe` 高于阈值 |
| `CREED_BREAKER → OUTLAW` | 中继者所属社区 `canShelterOutsider == false` |
| `任意负面 → MARTYR` | 中继者欠 topic 一笔**已兑现**的 LedgerEntry（社区 GDD 提供只读查询） |

**选边（零随机）**：`score(edge) = impressionSign×w1 + needMatch×w2 + creedFit×w3`，取 max；**平手 → 边表索引升序**。因无 RNG，`--dump-gossip` 能逐跳重演每一次失真，这是可调试性的关键。

**失真到什么程度停止**（四条，任一即停）：
1. `hop ≥ GOSSIP_FANOUT_MAX(3)`——硬停；
2. 当前 `claimCode` 已是 terminal（`KIN/OUTLAW/MARTYR`）——自然收敛，谣言有终点；
3. `charge` 每跳 `−Q_HOP_DECAY(1)`，`≤ 0` 即丢弃（不再发 `GossipEmitted`）；
4. **去重闭合**：接收者已有 `(topicActorId, claimCode)` 且旧 `charge ≥` 新值 → `MERGED_AND_STOP`。

**如何保证 ≤3 跳闭合**：传播在 T+1 06:00 **批量、分层**执行——第 h 跳只由"第 h−1 跳刚到达的 actor"发出，`visited` 集合以 `topicSeedId` 为单位一次性固定，故不可能成环。批处理前把队列物化为 `(topicSeedId, hop)` 两层数组，`hop` 上界是常量 3 → **闭合是结构的性质，不是运行时的运气。**

**配额与时序**：
- 每 actor 每日入站 `≤ GOSSIP_INBOX_MAX(2)`；超出时保留优先级：`ESTATE`（Part E 死亡 gossip，走 `estate.<community>.gossip`）> `charge desc` > `topicActorId asc`。
- 每跳 fanout `GOSSIP_SPREAD_PER_HOP(2)`：候选为与中继者当日有共事/共饮记录者，按 `(共事次数 desc, actorId asc)` 取前 2。
- **只有 `idle` 岗、以及当前处于 `SOCIAL` intent 的 NPC 才能当中继根**——干活不传话。这既符合直觉，又天然把传播面压到最小（呼应 R1 缓解项 ③）。
- T 日生成的种子（含 `ActorDied` 触发的系统 gossip）统一在 **T+1 的 DISPATCH(06:00)** 注入，与 Part E"第二天清晨才被社区知道"完全咬合。

**印象写入（≤5）**：未满则写；满则淘汰 `sort(charge asc, writtenDayKey asc, sourceActorId asc)` 的首条。
**声望不由本系统算**：只发 `GossipEmitted`，`ReputationDelta` 归社区 GDD。

---

## 4. 状态与流程（L1/L2/L3 的时序与回写）

### 4.1 一个游戏日的时间线

```text
任意 tick    L3(FULL) 评估扰动 → MemoryEvent + LossStreak + demand 票
             （全部落写队列 priority 5；判定绝不在渲染帧发起）
HOURLY_TICK  L1 结算并发 NeedUnmet → 应用上个整点入队的 WorldSignal / ScheduleRewrite
日切 00:00   [第4步] 记忆窗口滑动（weightQ −1，<1 剔除）
             [第4步] LossStreak 干净日 −1 与降级判定
             [第6步] Part E Phase 2（岗位继承由 Part E 执行，本份只消费结果）
             [第7步] gossip 种子结算为"待注入队列"（当日不多发）
DISPATCH 06:00（六步顺序不可调换）
             ① 注入 T−1 日 gossip（兑现"T 日生成、T+1 注入"）
             ② 汇总 completeFactor → 结算昨日劳动产出 → tryTransfer 入公共仓
             ③ 岗位分配（R1–R4 改派 → 必要时争位 rollCheck）
             ④ 生成每人 24h blocks（含 D2 升级后的改道/加岗/深藏/设陷）
             ⑤ 发 ScheduleChanged{actorId, dayKey, changeKind, visibleSignal}（非空断言见 A6）
             ⑥ 发 PostVacated（若有 slot 无主）
```

> **写队列纪律（继承底座附录 B-8）**：当日只入队，整点/日切批量应用；跨系统量一律读 `DaySnapshot[D-1]`。**可见信号的安装属于"写"，在决策的下一个 HOURLY_TICK 生效；而日程变更要等次日 DISPATCH——玩家至少提前 6 游戏小时就能读出变化。**这正是 P2↔P3 张力矩阵裁决（牺牲真实性换可读性）的执行形式。

### 4.2 回写通路（三条，缺一即 D2 不闭环）

```text
L3 扰动 ──①──► L1：demand 票 + NeedUnmet + MemoryEvent
       ──②──► L2(次日)：ScheduleRewrite 队列 → DISPATCH 应用 → ScheduleChanged
       ──③──► 世界：安装 WorldSignal（绊线 / 新脚印 / 被搬走的柜子）→ 玩家可读
```

### 4.3 D2 闭环样例（端到端）

```text
D0 09:40  NPC-A 抵达 ctr.he_valley.well_house_02（其 plannedDay = D0），容器已被玩家清空
          → ContainerEmptied → ClaimDisplaced(A)  [S1，D1 第 1 跳]
          → L3 触发 P_FOUND_LOOTED → rollCheck(VIGOR,stealth) 失败 → pressure[A][well_house] = 1
          同时写 MemoryEvent{kind:LOST_SUPPLY, actors:[A, player?=不可见]}
D0 22:00  闲散岗 NPC-C 与 NPC-D 在 SOCIAL 块谈及此事 → 生成 gossip 种子，入待注入队列
D1 06:00  DISPATCH：pressure=1 < L1 阈值 → 日程不变（玩家需要再偷一次才够）
D1 09:20  玩家再搬空同区另一口柜 → pressure = 2
D2 06:00  DISPATCH：达 L1 → ScheduleChanged{REROUTE, visibleSignal:"FOOTPRINT_DEVIATION:loc.he_valley.well_path"}
          世界侧：岔口多了一列偏离旧路的脚印；NPC-A 出发提前 25min
D2        玩家蹲旧窗口落空（他 08:55 就到过并走了）→ 玩家必须重新"读"
…         继续偷 → L2 加岗（夜里多一盏灯）→ L3 深藏（柜子被拖进里屋）→ L4 设陷（绊线）
终局      偷不再划算；玩家转向 TEND / BARTER / PERSUADE，或收手让 pressure 衰减降级
```

### 4.4 COARSE（离屏）如何不炸掉确定性

- COARSE 实体**不跑行为树、不做取用仲裁、不做碰撞**；它持有一份冻结的 `ScheduleDay`，进度按 `minuteOfDay` 闭形式推算：抵达时刻 = `startMin + travelMin`（路线长度来自预计算常量表）。
- **关键判决**：当它在某个边界 tick"名义上抵达"一个已空容器时，**必须在该 `absTick` 补做一次判定，而不是在"被发现的那一帧"补做**。因为 `rollCheck` 的取值是 `(streamId, absTick)` 的函数，只要锚定 absTick，早算晚算结果一致 → **LOD 差异不污染确定性**。这是"save/load 10 日差异 ≤5%"中唯一合法差异源的可控形式。
- 补做判定缺席或失败 → 该 block 记 `completeFactor = 0.5`，在下一个边界结算，**不回滚已流逝的分钟**。

---

## 5. 对外接口（暴露 / 依赖 / stub）

### 5.1 暴露

| 接口 / 事件 | 消费方 | 用法与注意 |
|---|---|---|
| `getSchedule` | 面板、工作板 C9、对话 GDD（判断"他现在该在哪"） | 只读；当日 block 可被 `ScheduleChanged` 取代，**必须按 `dayKey` 取，禁止跨日缓存** |
| `getNeeds` | 面板、经济 GDD（stub） | `physio` 只读不可写；三项自有值由本系统维护 |
| `getActorState` | UI / S0·A（读 injuries 累加修正） / S1 | `tickLevel` 为 LOD 镜像 |
| `getMemoryView` | **对话 GDD（≤5 印象是唯一的记忆型对话条件源）**、面板 | `recentEvents` 按 `(dayKey desc, minute desc, seq desc)` 排序，`weightQ` 已随日衰减 |
| `getLaborOutput` | 社区 GDD（安全天数分子）、经济 GDD、C9 | `byCluster` 已用 §D 单位映射 |
| `ScavengeAttempt{actorId,containerId,dayKey}` | **S1** | S1 对本份的唯一硬接口：这只是"我要来取"的宣告，**真取用必须经 `claimToken`，本系统不得自行扣减容器** |
| `ScheduleChanged` | 面板、对话回调、C9 | `visibleSignal` 必非空，格式 `"<signalKind>:<refId>"` |
| `PostVacated` | Part E / 社区 GDD | `reason ∈ {DEATH, INCAPACITATED, INJURY, CREED_BLOCK, NO_HEIR}` |
| `ActorDied` | Part E | 字段见 C-04；**本系统不执行 Part E 的结算逻辑** |
| `GossipEmitted` | 社区 GDD（→ 声望）、对话 GDD | 本系统不算声望；`claimCode` 闭集共用 |
| `NeedUnmet` | C9 工作板（"缺口"而非任务）、面板 | 每日每 need 去重 |

### 5.2 依赖

- **S0·A**：`rollCheck`（全系统仅三处：`MIND/survey` 争位、`VIGOR/stealth` 发现被盗、`MIND/survey` 改道/天气）· `getDerived` · `PhysioState`（只读引用）。
- **S0·B**：`onDayBoundary`（第 4/6/7 步）· `onHourTick` · `DISPATCH(06:00)` · `subscribe` · **`tickLevel`**（`lodStateOf` 已按 C-11 废弃） · `absTick` · `enqueue`。
- **S0·C**：`recordAttribution` · `exportAttribution`。**本系统 raw `rng` 调用数 = 0**（R-B）。
- **S1**：`ZoneSnapshot`（COARSE 估算）· `getLoad`（→ 旅行时间）· `getPhysio`（只读）· `claimToken`（**唯一取用仲裁**）· `tryTransfer`（唯一入库）· 事件 `ContainerEmptied` / `ClaimDisplaced` / `ActorIncapacitated`。
- **经济**：`RationGrant` / `Warehouse`（只读；写入只经 `tryTransfer`，不算会计）。**社区**：`PostRoster` / `CreedConstraints`（stub）· `heirActorId(postId)`（Part E 提供）。

### 5.3 stub（明示为未完成）

```text
CreedConstraints { canShelterOutsider:bool, tithe:float, curfewMin:int }   // 最小集
PostRoster       { communityId, posts:[6] }                                // 槽位硬编码
heirActorId(postId) -> ActorId | null                                      // 同社区同岗次席
```
其它 stub：天气返回常量（Extended）；`seniority` 排序键待 Part E 确认（§3.2 SO 2）。**`FOOD/MEDS/SEED` 三簇的产地岗位不在本份六岗内**，需经济/社区 GDD 回填 `nominalOut`（C-09）。

---

## 6. 玩家可感知表现（如何让作息"看得见"）

1. **两日可读定律**：任何 NPC 的作息必须在两个完整游戏日内被读懂。为此同时开启三条：`homePost` 世袭 + 零随机抖动 + 每个 NPC 每天在固定地点的一句固定台词（仪式冗余）。
2. **06:00 是整个世界的标点符号**：灯灭、门口聚集、人群同时向不同方向散开。这是无需教学即可读到的日常锚点。
3. **只有四种可见信号**，且必须能被 `observeRadius`（`4 + 0.6 × MIND`）捡到：新脚印链 / 多出来的过夜灯与坐具 / 被拖走的柜子（地上绳印 + 空处的暗色方块）/ 绊线铃铛。**没有第五种**（防认知过载）。
4. **需求气泡只画三条**：安全 / 社交 / 信条；水食睡由 S1 侧统一呈现。**本系统严禁再画一套渴/饿**（裁决 1）。
5. **所有改动都有"预告"**：信号安装先于行为改变，玩家永远有 ≥6 游戏小时的反打窗口——这是 P2↔P3 裁决的可见形式。
6. **gossip 可被偷听**：两位 NPC 的 SOCIAL 块交谈必定漏出恰好一句关于某人的结论（由失真后的 `claimCode` 翻译而来）；`--dump-gossip` 调试版显示"原话 → 你听到的话"的对照。

---

## 7. 边界情况与失败模式

**E1 · NPC 死亡（必须含）**
`ActorIncapacitated{causeCode}`（S1）→ 本系统写 `ActorDied{...}` → 交 Part E。同时：① 该 actor 当日剩余 blocks 全记 `completeFactor=0`，其岗位 `effHead` 当日即时下降（Part E Phase 1 在下一个 `HOURLY_TICK` 处理在岗人数 −1）；② 死者 `MemoryEvent` **不删除**，改由 heir 与 gossip 承接其未了之事；③ 当日未执行的 `ScavengeAttempt` 立即取消，**不补偿**；④ 死于 DISPATCH 前一刻则当日 `ScheduleDay` 留空 slot，次日发 `PostVacated{reason:DEATH}`。
**同一 `postId` 连续 3 日 `onDuty = 0` → 本系统停止自动补岗，只持续发 `PostVacated`，交社区 GDD 裁撤。**

**E2 · 玩家搬空某区域（必须含，D1 的第 2–3 跳）**
`ZoneSnapshot(zoneId).remainingByCluster` 归零 → 依赖该 zone 的 claimant 持续收 `ClaimDisplaced`，本份负责第 2–3 跳：① `pressure` 升至 L1 → `REROUTE` → **搜索半径抬高，改道进入更高 `hazardLevel` 的 zone**（面板可见路线变长）；② `rad` 上升 → `RAD_SICKNESS` → 伤病/死亡；③ `effHead` 下降 → `实际产出 = 额定 × (effHead/rated)` → 缺水 → 安全天数由 18 掉到 11（`SAFE_DAYS` 木桶项立刻被水绑定）。
**全链必须 `step ≤ 3` 且 `UNATTRIBUTED = 0`**，否则视为未实现（继承 Core 验收 1）。

**E3 · gossip 配额溢出**
注入前先做去重选择：每人 inbox ≤ 2，超出按 §3.7 优先级丢弃。**丢失不补，但必须计 INFO 级日志并在面板显示"丢弃条数"——静默丢弃等于无从调试。** 同一 actor 同日被同一 `topicSeedId` 经两条路径命中 → 只计一次到达且不增加后继，闭合性不受影响。

**E4 · FULL 名额打满 / LOD 抖动**
`LOD_FULL_MAX(40)` 是硬顶。超过时按 `sort(有 pending 扰动 desc, 有名字 desc, 距离 asc, actorId asc)` 抢占；被挤下者写 `LodSwitch`，其当前 block 冻结为 COARSE 闭形式（不重算、不回滚已走分钟）。**1 游戏分钟内切换 ≥3 次 → 强制锁 COARSE 60 游戏分钟并告警**（继承底座 §B7-3）。

**E5 · 达到 L4 后的"锁世界"风险**
L4 只让"偷这条路线"不再划算，不是让世界关门：三条替代出口（§3.5）+ 降级回路 + 另一个 `canShelterOutsider` 的社区永久存在。
**若某日出现"偷 / 易 / TEND / 说"四条路全部不可得的状态，必须告警而非静默——这是 P4↔探索自由张力的兜底断言。**

**E6 · 记忆溢出**
`MemoryEvent` 上限 `MEM_EVENT_MAX(40)`，按 `(weightQ desc, dayKey desc, minute desc, seq desc)` 取前 40（`seq` 保证键唯一 → 确定性）。`ImpressionTag` 满 5 时的淘汰键含三级 tiebreak，不可能产生不可复现抖动。

**E7 · 同 tick 争抢同一容器**
仲裁权**不在本系统**：一律经 `claimToken`，`sortKey = (priority, plannedDay, actorId 字典序)`，字典序仅作终局 tiebreak（采用 S1 §7-E2 的 C-03 解释）。落选者**当日不得重试其它点**，避免 NPC 全图扫射把资源池一日清空。

**E8 · 玩家在日切 / DISPATCH 期间睡觉**
`requestSleep` 快进走同一 tick 序列；所有结算按 `chunk = 60 游戏分钟` 边界处理；恢复后的首 tick 必须是 `savedTick + 1`。**不产生"睡醒两个世界"。**

---

## 8. 验收标准与调试钩子

**验收**

1. **A1 · D2 闭环（必备）**：脚本化"连续 2 日搬空同一目标区"→ 3 游戏日内必然观测到 ① `pressure` 增量 → ② `ScheduleChanged`（`changeKind` 在四枚举内）→ ③ `visibleSignal` 指向的世界对象真实存在且 `installTick ≤ 生效 tick` → ④ 玩家蹲旧点落空而新脚印可追。四者缺一即判失败。
2. **A2 · 可读性（继承 Core 验收 2）**：无 UI 提示、自由游玩 2 日后，预测某 NPC 次日 06:00 / 12:00 / 20:00 的位置，**三选三命中率 ≥70%**。
3. **A3 · 性能（必答 R1）**：目标机型 GTX 1060 6GB / 16GB RAM / SATA SSD / 1080p，≤16 有名 + ≤60 匿名，**本系统仿真 p95 ≤ 3.20ms/帧**；`--time-scale {30,60,120}` 与加速 ×1/×8/×60 下仿真步数不跳号。
4. **A4 · 确定性（必答 R1）**：同 seed save/load 续跑 10 日，`max_d |ΔSafeDays| / max(S,1) ≤ 5%`；**COARSE/LOD 近似是唯一合法差异源且必须在面板"差异来源"可见**；归因链 `step ≤ 3`、`UNATTRIBUTED = 0`。
5. **A5 · gossip 有界**：任一话题链长 ≤3 跳；任一 actor 单日入站 ≤2；单个 `reachedSet` 最终规模 ≤7；全部传播路径可在 `--dump-gossip` 逐跳重演（含失真选边的 `score` 明细）。
6. **A6 · P2 机械落点（裁决 5）**：全量 `ScheduleChanged` 的 `visibleSignal` 非空率 = **100%**，且能反查到 `WorldSignalRegistry` 中的活条目；违反时 `assert` 中断而非降级。**本作可读性最低可执行保证，不接受任何例外。**
7. **A7 · 无第二套骰（裁决 2）**：`rollCheck` 是本系统唯一随机出口；全仓检索 `d20` 命中数 = 1（继承底座 A8-①）；本系统 raw `rng` 调用数 = 0（R-B）。
8. **A8 · 人口与 LOD 上限**：有名 ≤16、匿名 ≤60、`LOD_FULL ≤ 40`，超限自动报警。

**性能预算拆解（回应 R1 的 ≤6ms）**

| 项 | 单价 | 数量 | 小计 |
|---|---|---|---|
| 有名 FULL（事件驱动 BT，非轮询） | 0.085ms | ≤16 | 1.36ms |
| 匿名 FULL（浅 BT，深度 ≤2） | 0.060ms | ≤24 | 1.44ms |
| COARSE（闭形式、仅边界结算） | 0.012ms | ≤56 | 0.67ms |
| 写队列 + 归因记录（共享池） | — | — | 0.30ms |
| DISPATCH / 日切 / gossip 批处理（摊销） | — | — | 0.15ms |
| **合计（匿名 FULL=24 时）** | | | **≈3.92ms** |

> **取舍声明**：3.92ms 超出本份自留目标 3.20ms，因此**默认策略为匿名 FULL 上限 16**（总 FULL=32 → ≈3.12ms，给其余系统留 2.4ms）。若 UX/关卡要求更多视锥内匿名细节，必须重新向底座 §C3 申请配额，不得就地放宽。宁明示取舍，不假装装得下。

**离线结算怎么做（回应 R1）**：① COARSE 不跑行为树，只按冻结 `ScheduleDay` + 预计算路程表做闭形式推进；② 所有"抵达 / 取用 / 失败"锚定**边界 absTick**触发（§4.4），COARSE 与 FULL 取同一 `(streamId, absTick)` → 结果一致、仅精度不同；③ 睡觉快进 8 游戏小时 = 8 个 chunk，目标 ≤800ms（继承底座 §B3.4）；④ 离屏允许不可逆结果但必须可归因，死亡须过 B3.3 预告闸门，否则降级为伤病/失踪。

**可视化面板要展示什么（回应 R1，扩展底座 §C8）**
① **24h 作息甘特**：横轴 `minuteOfDay`，每有名 NPC 一行，7 色块（§2.2 映射），可展开 waypoint 与 `intentTag`；
② **Theft pressure 热力面板**：`(actorId × targetRef)` 的压力值、当前 D2 等级、预计触发的 `changeKind`——唯一能提前看到"谁快被逼到升级"的观测手段；
③ **WorldSignal 注册器**：`signalId / refId / installTick / expireTick`，点击跳到世界坐标；
④ **gossip 图谱**：按 `topicSeedId` 的传播树，节点显示 `hop`、原码 → 失真后 `claimCode`、`charge`，边显示选边 `score` 明细；
⑤ **劳动产出账**：逐岗位 `effHead / rated / 实际产出`，昨日→今日变化标红；
⑥ 其余（需求条、LOD 实时视图、RNG 检视器、性能 p50/p95、种子控制与单步 tick）继承底座 §C8 的 ③④⑦⑧⑨。

**调试钩子**

```text
--dump-schedule <actorId> <dayKey>            块序列 / 起止时刻 / 路点 / intentTag
--dump-needs <actorId>                        三项自有值 + physio 只读镜像 + 当日 NeedUnmet
--dump-memory <actorId>                       recentEvents（含 weightQ）+ ≤5 impressions
--dump-pressure [<communityId>]               pressure 矩阵 + 当前等级 + 预测的 changeKind
--dump-gossip <topicActorId>                  传播树 + 每跳失真与选边 score
--dispatch-now                                手动触发 06:00 DISPATCH（走完整 tick 序列）
--force-pressure <actorId> <targetRef> <n>    构造 D2 升级场景（禁用于正常流程）
--npc-bt-trace <actorId>                      最近 200 个 BT 节点与触发 tick
--kill <actorId>                              继承 Part E，走完整死亡流程
--assert-visible-signal                       断言全量 ScheduleChanged 的 visibleSignal 可反查
--export-attribution <dayRange>               继承底座 §C8 导出规格
```

---

## 附录 A · 提请回填底座 §D 的候选常量（本份不就地生效）

> 底座 §D 是唯一定义处；以下以 Proposal 状态存在，批准前实现按「待定值」处理。

| # | 候选常量 | 建议值 | 单位 / 说明 |
|---|---|---|---|
| A-1 | `P_SAFETY_HOME / P_SAFETY_DRIFT` | +1.2 / −0.6 | 点/h |
| A-2 | `P_SOCIAL_GAIN / P_SOCIAL_DECAY` | +4.0 / −1.5 | 点/h |
| A-3 | `P_CREED_GAIN / P_CREED_DECAY` | +8.0 / −1.0 | 点/次 · 点/h |
| A-4 | `NEED_BAND_1/2/3` | 40 / 20 / 5 | `NeedUnmet.severity` 分档 |
| A-5 | `NPC_BLOCK_MAX / WAYPOINT_MAX` | 8 / 6 | 每日块数 / 块内路点上限 |
| A-6 | `DEFAULT_CURFEW_MIN` | 1320（22:00） | 无 creed 时的兜底宵禁 |
| A-7 | `PRESSURE_L1/L2/L3/L4` | 2 / 4 / 6 / 8 | actor 级 D2 升级阈值 |
| A-8 | `CLEAN_STREAK_DEESCALATE` | 3 | 干净日；且 `pressure ≤ 阈值−2` |
| A-9 | `COMMUNITY_PRESSURE_TIER` | 12 | 社区级联阈值（抬 claim priority） |
| A-10 | `PATROL_AMMO_YIELD` | 4 | rd / 额定岗 / 日 |
| A-11 | `SUPPRESSION_NIGHT_WATCH` | ×0.5 | 守夜在岗时的夜间 pressure 倍率 |
| A-12 | `Q_HOP_DECAY / Q_STRONG` | 1 / 4 | gossip 每跳 charge 衰减 / 强印象阈值 |
| A-13 | `GOSSIP_SPREAD_PER_HOP / GOSSIP_INBOX_MAX` | 2 / 2 | 每跳传播对象 / 每人每日入站 |
| A-14 | `MEM_EVENT_MAX` | 40 | 短期记忆裁剪上限 |
| A-15 | `ANON_FULL_DEFAULT_MAX` | 16 | 匿名 FULL 默认上限（性能取舍，见 §8） |
| A-16 | `DEFAULT_REROUTE_OFFSET_MIN` | 20 | L1 改道的最小可辨出发偏移（分钟） |

---

## 附录 B · 待主理人裁决 / 需与并行 GDD 对齐（9 项）

1. **C-04 · `ActorDied` 签名不一致**：本契约给 `{actorId, causeCode, dayKey, minute, locationId, postId}`，底座 Part E 给 `{actorId, dayKey, minuteOfDay, causeRef}`。**建议**：以契约 6 字段作为发出签名，额外保留 `causeRef` 供 `AttributionLink` 使用；`minute` 与 `minuteOfDay` 统一为 `minute`，请底座回填时同步。
2. **C-05 · 第 6 岗位 `idle`**：底座 §D 的 `role` 枚举缺 `"idle"`。建议按 §2.3 的伪岗位方案处理（`rated` 反算、不计 `filledPosts`）。
3. **C-06 · 产能公式扩展**：Part E 给 `额定 × (onDuty/rated)`；本份引入 `effHead`（含 0.5 的部分完成）让伤病/迟到有可见代价。**属越界请求**，请裁决接受或退回（退回则 §3.4 自动回落到原公式）。
4. **C-07 · `Claimant.priority` 写权限**：D2 社区级联需抬高已有 claimant 的 priority。**请求 S1 暴露 `raiseClaimPriority(containerId, actorId, delta, dayKey)`**；未获批前该级联降级为"只改 NPC 路线，不动 claimant"。
5. **C-08 · 印象标签所有权**：本份只出 `tagCode`（闭集 8 码）+ `charge` + 时间戳，**文本与对话分支条件归对话 GDD**。请对话 GDD 直接消费此闭集，勿重建第二套标签名。
6. **C-09 · `FOOD/MEDS/SEED` 无产地岗位**：三簇的社区侧产出需经济/社区 GDD 补齐并回填 `nominalOut`；否则 `getLaborOutput.byCluster` 只会返回 `WATER` 与 `AMMO`。
7. **C-10 · 声望边界**：本份只发 `GossipEmitted`，**不计算 `ReputationDelta`**。请确认社区 GDD 承接，并确认 Part E 的死亡 gossip **不扣声望**（继承 §E3-②）。
8. **C-11 · `tickLevel` 命名**：契约的 `getActorState().tickLevel` 与底座 `lodStateOf()` 是否为同一量，请明示别名关系，避免 UI GDD 读两个源。
9. **C-12 · COARSE 补做判定是否可行**：§4.4 的"锚定 absTick 补做判定"请程基岩/主理人确认可实现（补做必须源自边界 tick 的事件列表，而非渲染帧），否则 §4.4 的确定性论证不成立。

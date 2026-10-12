# 跨系统数据结构契约 · 唯一定义处（data-contracts）

- **文档 ID**：PHASE4-000B ｜ **阶段**：Phase 4 · 第 0 步 ｜ **作者**：design-strategist（文策渊）｜ **调度**：主理人（游承峰）
- **状态**：**冻结（Frozen）**——字段名 / 类型 / 单位 / 取值域在此冻结；三路并行（UX 规格 / 资产规格 / Epic 拆分）**只准引用，不准各自发明或改名**。确需变更 → 先改本文件。
- **上游**：`00–09` 全部 GDD · `06-consistency-review.md` · `00-foundation.md §D`
- **边界（硬）**：本文件**只放数据结构**（长什么样）。**规则 / 公式 / 阈值 / 事件时序**在各系统 GDD。**表现层投影**（`ViewSnapshot` / `WorldSignalRegistry` / `SimView*`）由 `docs/architecture/view-contract.md` 负责，本文件**不重复定义**，只在此引用（见 §3）。
- **闭合**：PHASE2-REVIEW §七-B-5 / §九-5.1（B-2）/ §十一 遗留项。

## §0 三条纪律

1. **唯一定义处**：每个结构**只在本文件出现一次**（下表即定义）。各 GDD 出现同名字段处一律视为**引用**；发现第二处定义 = 违规。
2. **字段名在此冻结**：`cluster / amount / unit / entryId / deadlineDay / communityId / tagCode / sourceKind / urgency …` 逐字固定；跨文档方言（如 `id / payload / dueDayKey / tagId / lodStateOf`）已按 §2 裁平，**旧名作废**。
3. **单位与取值域随字段冻结**：`Milli`/mm/turn 等**跨 ABI 定点**规则见 `view-contract §6`；本文件只记设计层单位。

---

## §1 结构总表（**唯一定义处 = 本文件**；「引用文档」列 = 已知消费方）

| 结构 | 归属系统 | 字段（名 : 类型/单位/取值） | 引用它的文档 |
|---|---|---|---|
| **Quantity** | S0 | `cluster:ClusterId(6键)` · `amount:float` · `unit:"L"\|"rd"\|"dose"\|"portion"`（映射固定） | 全部 |
| **ItemSpec** | S4 | `cluster:ClusterId` · `amount:float` · `unit`（与 Quantity 同形，角色=承诺内容） | S3·S5·`LedgerEntry.content` |
| **ServiceSpec** | S4 | `serviceId:"WATER_RUN"\|"NIGHT_WATCH"\|"BURIAL"` · `postId?:PostId` · `durationMin:int` · `dcTier:DC` | S3·S5 |
| **PhysioState** | S0 | `water,food,sleep,rad : 0..100`（玩家与 NPC 共用；NPC 侧只读引用） | S1读写·S2只读·S4·S9 |
| **LoadState** | S1 | `currentKg,capKg:float(kg)` · `speedMul:0.45..1.15` · `stamina,staminaMax:int` | S2·S7·S9 |
| **Injury** | S1 | `type:"MINOR"\|"INFECTION"\|"FRACTURE"\|"RAD_SICKNESS"` · `severity` · `requiredItemId` · `treatedDay?:DayKey` | S2·S4 |
| **Claimant** | S1 | `actorId` · `containerId` · `plannedDay:DayKey` · `priority(档)` · `needReason:5值闭集` | S2·S5·S7·S8 |
| **ZoneSnapshot** | S1 | `remainingByCluster:{ClusterId:amount}` · `depletionDay:DayKey\|null` · `hazardLevel:0..3` | S2(COARSE)·S7·S9 |
| **ZoneMutation** | S1 | `mutationId` · `containerId` · `actorId` · `kind:TAKE\|LEAVE\|DISTURB` · `items[]` · `enqueueTick,applyTick` | S1 内部 |
| **ContainerDef** | S8→S1 | `containerId` · `zoneId` · `tier:COMMON\|SCARCE\|CRITICAL` · `initialItems[]` · `discoveryDC,extractDC,noticeDC:DC` · `isUniqueAnchor:bool` · `claimantSlots:Claimant[]` | S1·S8·S9 |
| **PriceTable** | S3 | `cluster∈TRADABLE(5键):{base,buy,sell}:RU` | S4·S7·S9 |
| **Warehouse** | S3 | `stock:{ClusterId:amount}(6键)` · `inbound,outbound:Quantity[]` · `reservedByPromise:{ClusterId:amount}` | S2·S5·S9 |
| **SafeDays** | S3 | `safeDays,waterPersonDays,foodPersonDays:float` · `population:int` · `trendDelta7d:float` | S5·S7·S9 |
| **RationGrant** | S3 | `actorId` · `quantities:Quantity[]`（逐人发放） | S2 |
| **ReserveEntry** | S3 | `ledgerId(=entryId)` · `communityId` · `cluster` · `amount` · `dueDayKey:DayKey` · `state:PROMISED\|FROZEN\|FULFILLED\|DEFAULTED\|VOIDED` | S4 |
| **Post** | S5 | `postId` · `role∈{water,watch,patrol,mend,care,idle}` · `locationId` · `rated:int` · `onDuty:ActorId[]` · `nominalOut:Quantity[]` | S2·S7 |
| **PostRoster** | S5 | `communityId` · `dayKey` · `posts:Post[]`（**长度可变 7/6**）· `ratedPosts:int` · `filledPosts:int` · `staffingRatio:float` | S2·S7·S9 |
| **Community** | S5 | `communityId` · `population` · `creeds:CreedId[3]` · `constraints:CreedConstraints` · `rationRule:CreedRationRule` · `posts:Post[]` · `safeDaysRef` | — |
| **CreedConstraints** | S5 | `canShelterOutsider:bool` · `tithe:float` · `curfewMin:int` | S2·S4 |
| **CreedRationRule** | S5→S3 | `titheRate:float` · `priorityOrder:ClusterId[]` | S3 |
| **AllegianceTier** | S5 | `actorId` · `communityId` · `tier:OUTSIDER\|SUPPLICANT\|MEMBER\|SWORN` · `joinedDayKey` | S4 |
| **ReputationState** | S5 | `communityId` · `actorId` · `value:int(−100..+100)` · `band:5档` · `pendingDelta:int` · `pendingDayKey:DayKey` | S4·S3 |
| **MemoryFact** | S5 | `factId` · `kind` · `dayKey` · `actorIds[]` · `delta:int` · `summaryCode` | S4 |
| **GapSignal** | S5 | `signalId` · `communityId` · `cluster` · `deficitUnits:float` · `urgency:1..5` · `targetLocationId` · `sourcePostId?:PostId` · `claimantActorId?:ActorId` · `expiresDayKey:DayKey` · `sourceKind:5值闭集` | S7·S9 |
| **WorkOrder** | S7 | **契约 7**：`orderId,communityId,kind,needCluster,qty,deadlineDay,benefitSpec` ／ **派生**：`sourceSignalIds[],targetLocationId,timeWindowHint?,headlineCode,haulFit,haulKg,tripHint,state,resolvedBy?,evidenceRef?,linkedEntryIds[],sedimented` | S9（`SimViewWorkOrder` 为投影，见 §3） |
| **LedgerEntry** | S5 | `entryId` · `promisorId` · `promiseeId` · `kind:DELIVER\|SERVICE\|ABSTAIN` · `content:ItemSpec\|ServiceSpec` · `qty:float` · `deadlineDay:DayKey` · `witnessIds:ActorId[]` · `state:OPEN\|MET\|BREACHED\|VOID` · `createdDay:DayKey` · `reserved:bool` · `communityId` · `reasonCode?` · `voidedDayKey?` | S4·S3·S2 |
| **ImpressionTag** | S2 | `tagCode(=S4 12码 tagId)` · `topicActorId` · `charge:int` · `writtenDayKey:DayKey` · `sourceHop:0..3` | S4 |
| **MemoryEvent** | S2 | `seq` · `dayKey` · `minute` · `kind(闭集+RUMOR_HEARD)` · `refId` · `weightQ:1..7` · `actors:ActorId[]` | S4·S6 |
| **ActorDied**（发信签名） | S0·E | `actorId` · `causeCode` · `dayKey` · `minute` · `locationId` · `postId`（**6 字段唯一签名**；`causeRef` 为附带字段） | S1·S2·S4·S5 |
| **ActorIncapacitated** | S1 | `actorId` · `causeCode:DEHYDRATION\|STARVATION\|EXHAUSTION\|RAD_OVERDOSE\|INJURY` · `dayKey` | S0·B·S2·S5·S0·E |
| **OwnershipMarker** | S0·E | `resourceRef` · `claimantActorId` · `state:UNCLAIMED\|CLAIMED`（EstateSettlement 的唯一容器态） | S1 |
| **NeedVector** | S2 | `safety,social,creed:0..100` · `physio:Ref<PhysioState>`（只读） | S4 |
| **WorldSignal** | S2 | `signalId` · `refId` · `kind:REROUTE\|ADD_WATCH\|DEEPEN_STASH\|SET_TRAP` · `installTick` · `expireTick?` | S9（经 `view-contract §4` 镜像，**勿另立**） |
| **RumorSeed / GossipUnit** | S6 | `seedId,originActorId,topicActorId,claimCode0,charge0,bornDayKey,kind,holdDays` ／ `topicSeedId,claimCode,charge,hop:0..3,reachedSet:ActorId[]` | S2·S5 |
| **RumorInjected**（发信签名） | S6 | `topicActorId` · `claimCode` · `distortionLevel:0..3` · `hop:1..3`（**4 字段唯一签名**） | S2·S5·S4 |
| **RumorDelivery**（递送包装） | S6 | `recipientActorId` · `seedId` · `relayActorId` · `charge` · `dayKey` | S5 |
| **UniqueManifest** | S8 | `Map<uniqueItemDefId, containerId>`（每键恰好 1 条） | S1 |
| **WaterInTransit** | S5 | `communityId` · `actorId` · `amount:float`（claimer 恒为玩家） | — |
| **Board / BoardNote** | S7 | `communityId,locationId,entries:WorkOrder[≤3],sedimentLine,lastRefreshTick` ／ `frozen:WorkOrder[],frozenAt:{dayKey,minuteOfDay}` | S9 |

**闭集（值域冻结，禁止重排/新增）**

| 闭集 | 取值 | 定义处 |
|---|---|---|
| `ClusterId` | `WATER FUEL AMMO MEDS SEED FOOD`（`TRADABLE` = 前 5 键） | S0 §D（本文件引用） |
| `needReason` | `POST_SUPPLY TREAT_SELF TREAT_OTHER DEPENDENT STOCK` | S1 §5.1 |
| `Claimant.priority` | 档位（高/低）；数值未定 → 待 S1/S5 定 | S1 §2.2 |
| `sourceKind` | `STOCK CREED POST PERSON PROMISE` | S5 §2.2 |
| `WorkOrder.kind` | `POST CREED PERSON STOCK` | S7 §2.1 |
| `GapSignal.urgency` | `1..5`（**玩家侧不显示数字**，见 S5 §8-A9 / 裁决 21） | S5 §2.2 |
| `MemoryEvent.kind` | 既有闭集 + `RUMOR_HEARD` | S2 §2.2 |
| `ImpressionTag.tagCode` | **S4 §2.5 的 12 码 `tagId`**（gossip 时才 collapse 到 8 码 `claimCode`） | S4 §2.5 |
| `reasonCode`（LedgerEntry/Breached） | `NO_STOCK NO_SHOW REFUSED DEATH NO_HEIR BOTH_DEAD TARGET_DEAD TARGET_GONE` | S4 §3.6 |
| `ReserveEntry.state` | `PROMISED FROZEN FULFILLED DEFAULTED VOIDED`（**禁止与 `LedgerEntry.state` 合并**） | S3 §4.3 |

---

## §2 字段级冲突裁平（**已统一，原措辞作废**）

| # | 结构 | 冲突 | 冻结（唯一版本） | 作废方 |
|---|---|---|---|---|
| 1 | **LedgerEntry** | S0·E `{id,payload,dueDayKey,state∈{PENDING,ACTIVE,VOID}}` vs S4·S5 `{entryId,content+qty,deadlineDay,state∈{OPEN,MET,BREACHED,VOID}}` | **以 S4/S5 为准**（裁决 13/18）：`entryId` / `content+qty` / `deadlineDay` / `OPEN` | S0·E 旧字段名降别名（Part E 表已标注） |
| 2 | **PostRoster / Post** | S2 §2.2 `PostRoster{communityId,posts:Post[6]}` + `Post[6]` vs S5 §2.2 完整版 | **以 S5 §2.2 为准**：含 `dayKey/ratedPosts/filledPosts/staffingRatio`；`posts` **长度可变 7/6**（C-32） | S2 §2.2 的 `Post[6]`、S2 §5.3"槽位硬编码"作废 |
| 3 | **Claimant** | S1 运行态含 `containerId` vs S8 §2.4 槽位省略 `containerId` | `Claimant` **恒含 `containerId`**；`ContainerDef.claimantSlots[]` 元素在 spawn 时由父容器补全 | S8 §2.4 表头括注"4 字段"作废 |
| 4 | **ContainerDef** | S1 §5.2 清单缺 `claimantSlots` vs S8 §2.4 实播种表含之 | 以 S8 §2.4 **完整版**为准（含 `claimantSlots[]`） | S1 §5.2 字段清单作废（补 `claimantSlots`） |
| 5 | **ImpressionTag.tagCode 粒度** | S2 8 码 vs S4 12 码 `tagId` | 字段名 **`tagCode`**；值域 = **S4 §2.5 的 12 码**；仅传播时 collapse 到 8 码（裁决 14/C-24） | S2 §2.2 原"8 码"读法作废 |
| 6 | **ItemSpec / Quantity** | 二者同形，易被误并 | **保留为两个结构**（Quantity=通用量 / ItemSpec=承诺内容），**字段名逐字一致**，不合并 | — |
| 7 | **GapSignal → WorkOrder.kind** | S7 §2.1 由可选字段反推 `kind` vs S5 已增 `sourceKind` | **S7 直读 `sourceKind`** 映射 `kind`（裁决 20） | S7 §2.1"反推"作废 |
| 8 | **RumorInjected** | S6 4 字段签名 vs R-1 请求追加 4 字段 | **冻结 4 字段为发出签名**；`charge/recipientActorId/seedId/dayKey` 由 **`RumorDelivery`** 承载 | R-1"直接并入"未采纳（见回传⑥） |
| 9 | **tithe 双字段** | `CreedConstraints.tithe` vs `CreedRationRule.titheRate` | **两字段并存但同源同值**（`0.10`）；实现期断言相等 | — |

> **裁平纪律**：以上均为**字段级**统一，**未推翻任何已裁决项**（13/14/18/20/C-32 等原样保留）。若发现某条已裁决项在新材料下站不住 → 只记入回传，由主理人裁（见回传⑥）。

## §3 与 `docs/architecture/view-contract.md` 的边界（**无重复定义**）

| 归属 | 结构 | 说明 |
|---|---|---|
| **本文件（design）** | 上表 39 个**仿真侧**结构 | 仿真状态的"长什么样"，进 `worldHash` / 存档 |
| **view-contract（tech）** | `ViewSnapshot` / `SimViewActor` / `SimWorldSignal` / `SimViewBoard` / `SimViewWorkOrder` / `SimViewHud` / `SimViewCamera` / `WorldSignalRegistry` | **表现层只读投影**；`SimWorldSignal` 是 **S2 `WorldSignal` 的只读镜像**（本文件定义源，view-contract 定义投影） |
| 交点 | `WorldSignal`（源=S2，本文件）→ `SimWorldSignal`（镜像=view-contract） | 投影字段名与源对齐；**本文件不复制 view-contract 的字段**，反之亦然 |

- **不重复**：本文件**没有** `posMm` / `facingTurn` / `renderHint` / `camera` / `publishedAbsTick` 等**投影专属**字段；它们只在 `view-contract` 定义。
- **不含**：`benefitSpec` 不进投影（S7 §2.1-①）、`resolvedBy` 归 `DebugSnapshot`（view-contract §7-②③）、`tickLevel` 归 `DebugSnapshot`——沿用 view-contract 裁决，本文件仅声明其源结构。

## §4 生命周期 / 归属速查（**谁写谁读**，非公式）

| 结构 | 唯一写权 | 只读消费 | 生命周期 |
|---|---|---|---|
| `PhysioState` | S1 | S2(只读引用)·S4 | 每 `HOURLY_TICK` 衰减、日切结算 |
| `LedgerEntry` | **S5 §5.3**（唯一写 `state` 处） | S4(只读)·S3 | `OPEN→MET\|BREACHED\|VOID`；`VOID` 由 S0·E 写 |
| `ReserveEntry` / `reservedByPromise` | S3（经 `reserve/release/fulfill`，**仅 S4 调用**） | S3 配给 | `PROMISED→FROZEN→FULFILLED\|DEFAULTED\|VOIDED` |
| `Warehouse.stock` | **仅经 S1 `tryTransfer`** | S2·S5 | 日切 3.1/3.3 |
| `ImpressionTag` | S2（经 S4 `ImpressionAdded`） | S4(对话条件源) | `≤5`，淘汰键 `sort(charge asc, writtenDayKey asc, sourceActorId asc)` |
| `GapSignal` | S5（五路来源） | S7(投影)·S9 | 每日 `≤3`；`expiresDayKey` 到期 |
| `WorkOrder` | **S7 写入量 = 0**（纯投影） | S9 | `OPEN→NOTED→RESOLVED\|EXPIRED`；`orderId` 跨日不变 |
| `NeedVector` | S2 | S4 | 每 `HOURLY_TICK` |
| `MemoryEvent` | S2（+S6 `RUMOR_HEARD`） | S4 | 7 日窗口，`weightQ∈1..7` 整数 |
| `OwnershipMarker` | S0·E（EstateSettlement） | S1 | 死亡 Phase1 释放 / `PROTECTED` 直转 heir |
| `UniqueManifest` | S8（worldgen 一次性） | S1 | 只读；三态由 S1 承载 |

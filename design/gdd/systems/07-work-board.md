# S7 · Work-Board（缺口工作板）· 系统设计文档

- **Task ID**：GDD-007｜**阶段**：Phase 2 · 收尾批（与 S6 并行）｜**优先级**：P0（概念文档 C9 的落点）
- **主笔**：design-strategist（文策渊）｜**调度**：主理人（游承峰）｜**状态**：待拍板（附录 B 共 7 项）
- **依赖已读**：`game-concept.md` · `06-consistency-review.md` · `00-foundation.md`（S0）· `01-scarcity-loop.md`（S1）· `02-npc-simulation.md`（S2）· `03-economy-barter.md`（S3）· `05-community-creed.md`（S5）· `04-dialogue-contract.md`（S4，检索）
- **依赖方向（严格单向）**：依赖 S0/S1/S2/S3/S4/S5；**不依赖** UI 布局（S9）与区域地理表（S8）的实现，只消费其只读量。
- **边界（严格）**：把 `GapSignal` 翻译为**玩家可行动的目的地**——优先级排序、目的地呈现与导航语义、过期与撤销、回填确认的证据比对、沉降与去重。
  **不写**：`GapSignal` 的来源与生成规则（**S5**）· `SafeDays` / 配给 / 仓储会计（**S3**）· NPC 调度与岗位占用（**S2**）· 对话分支与 `LedgerEntry` 状态（**S4**）· 面板视觉与交互控件（**S9**）。
- **本系统的写入量 = 0**：S7 是 `GapSignal → WorkOrder` 的**纯函数投影**。它不生成缺口、不填缺口、不奖励任何人、不产生 `ReputationDelta`。

---

## 0.0 规范系统编号对照表（PHASE2-REVIEW · 裁决 C-13）

| 规范编号 | 系统 | 文件 |
|---|---|---|
| **S0** | 底座（`S0·A` 判定 / `S0·B` 世界时钟 / `S0·C` 确定性 / `S0·D` 常量表 / `S0·E` 死亡清算） | `00-foundation.md` |
| **S1** | 稀缺主循环 | `01-scarcity-loop.md` |
| **S2** | NPC 三层模拟 | `02-npc-simulation.md` |
| **S3** | 易货经济与社区仪表 | `03-economy-barter.md` |
| **S4** | 记忆驱动对话与契约账本 | `04-dialogue-contract.md` |
| **S5** | 社区、信条与声望 | `05-community-creed.md` |
| **S6** | 传闻传播 | `06-rumor-network.md` |
| **S7** | **工作板（本文档）** | `07-work-board.md` |
| **S8 / S9** | 区域内容 / 呈现与交互（待写） | — |

---

## 0. 全局契约（逐字继承，不改字段名）

```text
WorkOrder{ orderId, communityId, kind, needCluster, qty, deadlineDay, benefitSpec }
输入: GapSignal（S5）· NeedUnmet（S2）· SafeDays / Warehouse（S3）· PostRoster / 信条约束（S5）
```

> **契约七个字段的落地**：`needCluster ≡ GapSignal.cluster`｜`qty ≡ deficitUnits`（合并后为 `Σ`）｜`deadlineDay ≡ expiresDayKey`（S5 给定，S7 只换算为倒计时）｜`kind` 与 `benefitSpec` 由 S7 定义（见 §2）｜`orderId` 由 §3.5 确定性生成。
> 其余字段（`sourceSignalIds[]` / `targetLocationId` / `haulFit` / `state` / `headlineCode` / `linkedEntryIds[]` / `resolvedBy`）为 **S7 派生量，不进契约**，仅供 S9 渲染与调试面板。

**已裁决（本份逐条落实，不复核）**

1. **没有任务发布者**：缺口是**世界的状态**，不是某人给你的委托。无发布者、无奖励、无勾选框、会过期。
2. **每社区每日 ≤3** 条硬上限（S5 `GAP_SIGNAL_MAX`），本份只排序与呈现，**不放宽**（也不因合并而"腾出配额给新的"——见 §3.4）。
3. 目的地 = 公共仓 **或** 空缺岗位的 `locationId`（S5 §4.3）。
4. 随机纪律：禁止裸 `rand`、禁止渲染帧判定、`streamId` 静态可枚举。**本系统 raw `rng` 调用数 = 0**，排序全确定性。
5. 跨系统回写走「写队列 → 日切/整点应用 → 下游读昨日快照」。本份零世界写入，故只**读** `DaySnapshot[D]`。
6. 目的地由**真实缺口动态生成**，不是 Designer 手写清单 —— R2（第 90 分钟重复感）的第 4 条机制级回答。

---

## 1. 系统概览与目标（对应支柱/动词）

### 1.1 定位

S3 把世界压缩成一个数字（`safeDays`），S5 把社区的缺口压成 ≤3 条 `GapSignal`。**S7 是这两者与玩家脚步之间的最后一层翻译**：它不生产内容，它决定"今天你往哪儿走"这个问题由什么来回答。

- **支柱**：**P1（主）**——每条缺口必须落到"一次短途步行内的、有名有姓的具体位置"，绝不能是一条抽象目标；**P3**——缺口会过期、会被 NPC 自己解决、不因你在路上而暂停；**P2**——板必须一屏读完、用人的话写、不给数字让你算；**P4**——板不给回报，代价与收益都由别的人收。
- **动词**：本系统**不产生动词**，它是动词的**分配器**——把 取 SCAVENGE（无岗位产出的簇）· 负 HAUL（qty 与 `haulFit`）· 易 BARTER（产地在别处的簇）· 守望 TEND（空缺岗位）四个动词指向真实缺口。
- **反目标**：不做任务日志、不做目标追踪器、不做导航箭头与路径线、不做"按等级解锁的委托"、不做第四个数（本系统对外只有 `urgency` 标签、`qty`、`haulFit` 三个可读量）。

### 1.2 一句话职责

> 让"今天往哪儿走"由世界的缺口回答，而不是由一张 Designer 写好的清单回答——并且让这个回答随时可能自己失效。

---

## 2. 核心概念与数据模型

### 2.1 `WorkOrder`（S7 的输出，纯投影）

```text
WorkOrder {
  orderId, communityId, kind, needCluster, qty, deadlineDay, benefitSpec,   // ← 契约 7 字段
  // ↓ S7 派生，不进契约
  sourceSignalIds: SignalId[],        // 合并时 >1
  targetLocationId: LocationId,
  timeWindowHint?: string,            // POST/PERSON 型的班次时段线索（不泄露坐标）
  headlineCode: HeadlineCode,         // UI 取文本的键，禁止自由文本
  haulFit: "FITS"|"TIGHT"|"OVER",
  haulKg: float, tripHint: int,       // 需要几趟（ceil(haulKg / remainingKg)，下限 1）
  state: "OPEN"|"NOTED"|"RESOLVED"|"EXPIRED",
  resolvedBy?: "PLAYER"|"NPC"|"TRADE"|"NONE",
  evidenceRef?: RefId,                // 判定 RESOLVED 的那条证据
  linkedEntryIds: entryId[],          // 只存 ID，不复制 S4 的 6 字段
  sedimented: bool                    // 沉降，见 §3.6
}
```

**`kind` 闭集 4 值（由 `GapSignal` 的可选字段确定性反推，S7 不新增来源）**

```text
sourcePostId != null                                  -> "POST"    岗位空降（TEND 入口）
CreedConflictClusters(cid) 含 cluster                 -> "CREED"   信条约束冲突
claimantActorId != null                               -> "PERSON"  某人明天要去找它
否则                                                   -> "STOCK"   公共仓缺口
```

**`benefitSpec` 是后果说明符，不是回报表**

```text
benefitSpec { kind: "NONE"|"RELIEF"|"CREDIT",
              beneficiaryRef?: RefId,                 // 归因到具体的人/岗/仓，不是玩家
              effectCode?: "RATION_RESTORED"|"POST_STAFFED"|"NEED_MET"|"CREED_RELIEF",
              magnitudeHint?: "SMALL"|"SOME"|"MUCH" } // 四档模糊词，禁止数字
```

三条硬约束：① **行动前 UI 不渲染 `benefitSpec` 的任何字段**——这是"无奖励前置"的机械形式；② 它只在条目转 `RESOLVED` 后，由 `effectCode` 翻译成**一句世界侧短句**进日志（"第二天早上，井窖的水线回到了每人三升"）；③ 它**不可结算**——没有任何系统读它发东西，S9 只是把一句已经发生的事实念出来。

**`kind` → `benefitSpec` 默认映射**：`STOCK → RELIEF/RATION_RESTORED`｜`POST → RELIEF/POST_STAFFED`｜`PERSON → CREDIT/NEED_MET`｜`CREED → NONE`（信条不奖赏你，它只是不再找你麻烦）。

### 2.2 板与笔记（两个不同的东西）

```text
Board        { communityId, locationId, entries: WorkOrder[≤3],
               sedimentLine: string,            // "他们一直缺这个" 一行小字，不占配额
               lastRefreshTick: absTick }
BoardNote    { frozen: WorkOrder[], frozenAt: {dayKey, minuteOfDay} }   // 玩家随身笔记
```

- **板是世界里的一块物件**（每个社区门口一块，物理 `locationId` 由 S8 提供）。玩家必须走到 `observeRadius` 内才能读到——**不是按 Tab 打开的全图任务列表**。
- **笔记是离开板之后的冻结快照**：走出 `observeRadius` 即冻结，回到任一板前才刷新。笔记上的行程时间与 `state` 会**过期**——这是 P3 在 UI 层的唯一兑现形式，也是"世界不为你待机"最廉价的一课。

---

## 3. 规则与公式

### 3.1 `urgency` 1–5 → 玩家可读语言（不给数字）

| urgency | 标签（≤4 字） | 板上那句话 | 世界侧含义 |
|---|---|---|---|
| 1 | 「还行」 | "他们自己大概能对付" | 短fall ≤1 日配给量；`SAFETY_FLOOR_DAYS(2)` 还垫着 |
| 2 | 「明天紧」 | "明天这个时候就该有了" | 短fall >1 日配给量 |
| 3 | 「今天就要」 | "今天日落前没人送就少了" | 岗位空缺（水岗 +2）或个人 `NeedUnmet{severity 2}` |
| 4 | 「有人在挨」 | "今晚有人要空着杯子睡" | `staffingRatio < 0.60`（SCARCE）或 `NeedUnmet{severity 3}` |
| 5 | 「已经在死」 | "已经有人倒下了" | 岗位裁撤（整岗消失） |

> **UI 纪律**：玩家侧**只显示语言标签 + u4/u5 的红标，不显示数字 1–5**；数字仅进调试面板（与 S5 §8-A9 的口径差异见附录 B-1）。五个标签 = 五个情绪档，玩家不需要排序，只需要"哪条听着更糟"。

### 3.2 排序（全确定性，零随机）

```text
sortKey = (
  urgency            desc,     // 世界的紧急度 —— 不因玩家能力改变（否则主导策略 = 只做最省事的）
  stalenessDays      desc,     // today − firstSeenDay；防"永远轮不到老缺口"
  haulFit            asc,      // FITS(0) < TIGHT(1) < OVER(2)；玩家能不能一次扛完
  travelMinQuantized asc,      // 距玩家（量化到 5 分钟档）
  clusterScarcityRU  desc,     // 该簇缺口的 RU 计价量
  communityId        asc,
  orderId            asc       // 终局 tiebreak（字典序，唯一不可再分的键）
)
```

```text
travelMin(a→b)      = ceil( pathLenMeters / (BASE_WALK_SPEED × speedMul(player) × 60) )  // 复用 S2 §3.3
travelMinQuantized  = ceil( travelMin / BOARD_TRAVEL_QUANTUM(5) ) × 5                    // 防抖动
haulKg              = qty × MASS_PER_UNIT[cluster]                                       // S3 附录 A-11
remainingKg         = CARRY_CAP − getLoad("player").currentKg                            // S1 只读
haulFit             = haulKg ≤ 0.80×remainingKg ? FITS
                    : haulKg ≤ 1.00×remainingKg ? TIGHT : OVER
tripHint            = max(1, ceil(haulKg / max(remainingKg, 0.1)))
clusterScarcityRU   = deficitUnits × BASE_PRICE[cluster]        // S3 附录 A-4
                      // FOOD 无 base → 折等效升水：deficitUnits × 96/64 = ×1.5（由 §D 常量导出，非新常量）
```

- **重排时机（防 UI 抖动，也防渲染帧计算）**：仅在 `HOURLY_TICK`、日切、玩家跨 `zoneId`、或 `getLoad().currentKg` 变化 ≥2kg 时重排。**禁止逐帧重排。**
- **`OVER` 降权但不隐藏**：世界不会因为你背不动就少缺一点。`qty` 永不缩水，板明写"还差 20 L（24 kg）· 一趟扛不完"——这是 负 HAUL 动词的落点，不是设计失误。

### 3.3 过期与撤销

- **过期**：`today > deadlineDay` → `state = EXPIRED`。**不是任务失败**：不弹提示、不扣声望、不写印象、不发 gossip。**硬断言：S7 产生的 `ReputationDelta` 事件数 = 0。**
- 过期的世界侧后果由上游承担（社区收紧配给 → `RationIssued.shortfall` 上升 → 次日板自己会报得更重）。**板只报已经发生的事，不预测、不追责。**
- **撤销（revoke）**：`GapSignal` 在 `expiresDayKey` 之前被 S5 撤回 → S7 在下一个 `HOURLY_TICK` 让对应条目转 `RESOLVED`（`resolvedBy = "NONE"`）。S7 不判断撤回理由，只读上游集合的差集。

### 3.4 去重与合并（同一目的地）

```text
mergeKey = kind == "POST"   ? "post:"  + sourcePostId
         : kind == "PERSON" ? "person:" + claimantActorId
         :                    "stock:" + targetLocationId + ":" + cluster
```

- **同 `mergeKey` 合并为 1 条**：`qty = Σ deficitUnits`，`urgency = max`，`sourceSignalIds[]` 全记，`orderId` 保持不变。
- **同 `targetLocationId` 但不同 `cluster` → 不合并**，在板上并列为同一地点的多行（"同一趟能办三件事"，这是负重的几何题，不是重复条目）。
- **配额不被合并释放**：合并省下的名额**不补新条目**（≤3 是每日新生成上限，S5 已有），避免"合并 → 刷新"变成变相的任务板补给。

### 3.5 `orderId` 的稳定性

```text
orderId = "wo." + communityId + "." + mergeKeySlug
```

**同一缺口在存续期间 `orderId` 跨日不变**（不按 `dayKey` 重新编号）。这是"缺口是世界的**持续状态**，不是每天刷新的一批任务"的机械表达，也让玩家的 `NOTED` 标记能跨日存活。

### 3.6 沉降（同源抑制 · R2 的第 4.5 条回答）

```text
若同一 orderId 连续 ≥ SEDIMENT_DAYS(3) 日出现 且 urgency ≤ 2：
    sedimented = true → 移出配额，落到板背面一行小字："他们一直缺这个：燃料 / 药 / 种子"
若其 urgency 升到 ≥3  → 立即浮回配额（当日 HOURLY_TICK 生效）
```

**沉降只影响呈现与配额占用，不影响 `GapSignal` 的生成，也不影响玩家去满足它**（它还在世界里，只是不再是新闻）。板报的是**变化**，不是库存清单——否则第 3 天起板就变成三行每天都一样的字，正好撞上 R2。

---

## 4. 状态与流程（缺口 → 条目 → 玩家响应 → 回填确认）

```text
S5 GapSignal ─(日切后读 DaySnapshot[D])─► 合并/去重 ─► 排序 ─► OPEN ─► NOTED（本地标记）
      ─► 玩家到达 targetLocationId 并做点什么 ─► 世界侧证据出现
      ─► S7 比对证据 ─► RESOLVED（resolvedBy 由归因链首跳判定）
      ─► 次日日切归档，benefitSpec.effectCode 念出一句世界侧短句
```

**回填确认：谁确认它被满足了？——世界自己确认，S7 只是读证据的书记员。**

| kind | 确认者 | 证据（S7 从 `DaySnapshot` 读） |
|---|---|---|
| `STOCK` / `CREED` | **S3** | 次日 `RationIssued.shortfallByCluster[c] == 0`（或 `safeDays` 木桶短板解除） |
| `POST` | **S2** | `PostRoster(cid, D+1)` 该 `postId` 的 `onDuty ≥ 1`；顶岗者是玩家则 S2 另有 `PostFilled` |
| `PERSON` | **S2 / S4** | 该 `claimantActorId` 当日不再发同档 `NeedUnmet`；或对应 `LedgerEntry` 转 `MET` |

```text
resolvedBy = 归因链首跳 actorIds 含 "player" ? PLAYER
           : 证据由 Warehouse 变化推断           ? TRADE
           : 证据存在但归因链不含 player         ? NPC
           : 证据不存在（撤回/过期）             ? NONE
```

> **这是"没有任务发布者"的最后一块拼图**：既然没有发布者，就**没有验收的人**。你送到了，明天公共仓的短fall 自己归零；你没送到，也有人替你送到了——板都会写 `RESOLVED`，区别只在 `resolvedBy`，而那个字段只有调试面板看得到。

**`NOTED` 是纯本地 UI 状态**（进存档，不写世界）。它唯一的作用是解锁 §7-E1 的"路上消失"通知与转介——**它不生成 `LedgerEntry`、不调 `reserve()`、不产生任何承诺**。工作板上没有"接受"按钮。

---

## 5. 对外接口（暴露 / 依赖 / stub）

### 5.1 暴露

| 接口 / 事件 | 消费方 | 用法与注意 |
|---|---|---|
| `getBoard(cid, dayKey) -> WorkOrder[≤3]` | **S9（唯一 UI 投影）** | 纯函数；必须按 `dayKey` 取，**禁止跨日缓存**；含 `sedimentLine` |
| `getOrder(orderId) -> WorkOrder` | S9 | 含 `evidenceRef` 与 `resolvedBy`（调试向） |
| `getBoardNote() -> {frozen[], frozenAt}` | S9 | 冻结快照；`frozenAt` 必须显示在笔记页顶部 |
| **`getLinkedReserve(orderId) -> {entryIds[], reservedByCluster}`** | **S4** | **只读**：供 S4 在调 S3 `reserve()` 前做冲突校验（"你已经许诺出去的水比他们还缺的多"）。**写权仍在 S4/S3，S7 不写 `reservedByPromise`** |
| `WorkOrderResolved{orderId, resolvedBy, dayKey, evidenceRef}` | S9 / 日志 / 面板 | **不驱动世界**，只驱动一行文本 |
| `WorkOrderExpired{orderId, dayKey}` | S9 / 日志 | 同上 |

### 5.2 依赖

- **S5**：`GapSignals(cid)`（唯一输入）· `PostRoster(cid, dayKey)`（POST 型的 `rated/onDuty`）· `CreedConstraints(cid)` · **`CreedConflictClusters(cid)`【请求新增，见附录 B-3】**。
- **S2**：`getSchedule(actorId, dayKey)`（PERSON/POST 型的**时间窗线索**，不取精确坐标）· `NeedUnmet`（PERSON 型证据）· `PostVacated` · `getActorState`（存活校验）。
- **S3**：`SafeDays(cid)`（板头读数）· `Warehouse(cid)`（只读 `effStock`）· `RationIssued`（STOCK 型证据）· `MASS_PER_UNIT` / `BASE_PRICE`（附录 A-11 / A-4，只读）。
- **S4**：`LedgerQuery`（填 `linkedEntryIds`，**只存 ID**）· `resolveSpeaker(intentId, pos)`（PERSON 型指向"此刻谁在这"，零随机）。
- **S1**：`getLoad("player")`（`currentKg` / `speedMul`）· `getDerived`（`CARRY_CAP`）。
- **S0·B / S0·C**：`now()` · `getDaySnapshot(D)` · `subscribe(HOURLY_TICK / DAILY_CUTOVER)` · `recordAttribution`（**S7 不写新归因**，只复读上游链路）。

### 5.3 stub（明示为未完成）

```text
CreedConflictClusters(cid) -> ClusterId[]        // 未定义；本份只读，请求 S5 由 §2.4 六卡导出
pathLenMeters(a, b)                              // S8 提供；缺则 travelMin 恒取 0（不参与区分）
Board.locationId                                 // S8 提供（社区门口）
SEDIMENT_DAYS / BOARD_TRAVEL_QUANTUM             // Proposal，见附录 A
```

---

## 6. 玩家可感知表现（工作板长什么样、为什么不像任务列表）

### 6.1 板面

```
┌─ 河谷 · 缺口板 ──────────────────── 安全天数 8 ↓ ─┐
│ 今天的字是新的。昨天那三条被刮掉了。              │
├──────────────────────────────────────────────────┤
│ 燃料 · 公共仓 · 还差 1.35 升            「还行」  │
│ 没人产这个。旧油罐那边还有。                     │
│ 往东北 · 走 12 分钟 · 一趟扛得完（1.2 公斤）     │
├──────────────────────────────────────────────────┤
│ 药 · 公共仓 · 还差 0.18 剂              「还行」  │
│ 井窖的棚子里有。                                 │
│ 往东北 · 走 12 分钟 · 一趟扛得完                 │
├──────────────────────────────────────────────────┤
│ 种子 · 公共仓 · 还差 0.09 份            「还行」  │
│ 井窖留种。他们不会卖，但会换。                   │
│ 往东北 · 走 12 分钟 · 一趟扛得完                 │
├──────────────────────────────────────────────────┤
│ 板的背面，一行小字：他们一直缺这个：燃料 · 药     │
└──────────────────────────────────────────────────┘
  ↑ 没有"接下"。没有"完成"。没有回报。没有感叹号。
```

**导航语义**：地名 + 八方位 + 量化时间档（5 分钟）+ 一句线索。**不给路径线、不给导航箭头、不给精确坐标**；玩家去过一次后，S9 才在地图上留下一个自己画的记号。这是 Discovery 美学的兑现——你得自己找到它。

### 6.2 【必答】"这和一个任务列表有什么区别"——7 条结构性差异

| # | 差异 | 机械落点 |
|---|---|---|
| 1 | **无发布者** | 没有感叹号 NPC、没有委托、没有人问你"办得怎么样"。条目主语是**仓库 / 岗位 / 某人的需求**，不是请求 |
| 2 | **无奖励前置** | 行动前看不到任何回报数字（无经验 / 无声望预估 / 无金币栏）；`benefitSpec` 是事后归因短句，且**不可结算** |
| 3 | **会过期，且过期不是失败** | `deadlineDay` 到 → `EXPIRED`，不弹提示、不扣声望、不写印象（`ReputationDelta` 数 = 0）。后果落在**世界的配给**上，不落在玩家身上 |
| 4 | **可被 NPC 自行解决** | `resolvedBy ∈ {PLAYER, NPC, TRADE, NONE}`。你在路上，它可能已经没了 |
| 5 | **不因玩家等级 / 声望 / 进度而出现** | 输入只有 `RationIssued.shortfall` / `PostVacated` / `NeedUnmet` / `staffingRatio` / 岗位裁撤——**全部与玩家状态无关**。没有"声望不够接不到"，也没有"做完才刷新下一条" |
| 6 | **没有勾选框与完成提示** | 没有"交任务"这一步。缺口由世界的下一份证据自行确认，玩家不会收到"任务完成" |
| 7 | **它不保证你有事做** | 板可以空着（S5 §7-E7："今天他们自己够了"）。有事可做来自**世界的缺口**，不来自板的产能 |

### 6.3 【必答】Core 期两个社区的样例板（第一天）

> **计算前提（请一并审阅）**：S3 §2.4 只给出 WATER / FOOD 的起始公共仓；**其余四簇本样例按起始 = 0 计算**（待 S3 确认，见附录 B-2）。配给 `perCapita` 取 S3 §3.4（MEDS 0.02 / FUEL 0.15 / SEED 0.01）；`urgency = clamp(ceil(shortfall / 配给target), 1, 5)`。玩家假设 VIGOR 5（`CARRY_CAP` 50 kg）空手站在两社区之间的岔口，距河谷仓 12 分钟、距井窖仓 16 分钟。

**河谷 `he_valley`（pop 9，起始 `safeDays` 8）**
短fall：WATER 0 / FOOD 0 / **FUEL 1.35 L** / **MEDS 0.18 dose** / **SEED 0.09 portion** → 全部 `urgency 1`。
排序键 `clusterScarcityRU`：FUEL `1.35×2.20 = 2.97` > MEDS `0.18×4.50 = 0.81` > SEED `0.09×6.00 = 0.54`。

```
河谷 · 缺口板 ── 安全天数 8
1  燃料 · 公共仓 · 还差 1.35 升   「还行」  往东北 12 分钟 · 一趟扛得完
    没人产这个。旧油罐那边还有。
2  药   · 公共仓 · 还差 0.18 剂   「还行」  往东北 12 分钟 · 一趟扛得完
3  种子 · 公共仓 · 还差 0.09 份   「还行」  往东北 12 分钟 · 一趟扛得完
```

**井窖 `jing_cell`（pop 7，起始 `safeDays` 6）**
短fall：WATER 0 / FOOD 0（126 L 与 42 portion 尚在安全地板之上）/ **FUEL 1.05 L** / **MEDS 0.14 dose** / **SEED 0.07 portion** → 全部 `urgency 1`。
排序：FUEL `1.05×2.20 = 2.31` > MEDS `0.14×4.50 = 0.63` > SEED `0.07×6.00 = 0.42`。

```
井窖 · 缺口板 ── 安全天数 6 ↓
1  燃料 · 公共仓 · 还差 1.05 升   「还行」  往西南 16 分钟 · 一趟扛得完
2  药   · 公共仓 · 还差 0.14 剂   「还行」  往西南 16 分钟 · 一趟扛得完
3  种子 · 公共仓 · 还差 0.07 份   「还行」  往西南 16 分钟 · 一趟扛得完
   过秤台旁边新加了一块查水牌。（入境只准带 9 升）
```

> **第一天的板是平静的，这是设计意图。** 板的诚实性来自它只报**已经发生的短fall**；紧张感由板头 `safeDays` 的下降箭头承担（井窖 6 → 5.7 → 5.4 → 5.1 → **D4 进 TIGHT 4.8**）。
> **更有意思的是**：井窖的"致命缺水"（−6.3 L/日）**一开始根本不出现在板上**——它要等库存跌破安全地板（`stock < 63 L`，约 D11）才变成短fall。中间那 7 天，玩家是**自己**从 `safeDays` 的箭头和 NPC 的空杯子里读出"我该送水去井窖"的。**空板产出自设目标——这正是 Core 验收 3 想要的那种目标。**
> **对照 D4**：若 D3 夜里有取水岗 NPC 死于辐射坑 → 次日出现 `POST` 型条目「取水位 · 缺 1 个人 · 今天就要」（`urgency 3`，水岗 +2）→ 顶到最前并把 `SEED` 挤出配额。

### 6.4 TEND（守望）的衔接：岗位空降如何呈现为"缺口"而不是"任务"

- `kind = "POST"` 的条目**不写"去顶岗"**，它写"**那个位置今晚空着**"：目的地是 `Post.locationId`（不是某个 NPC），文本是「井窖 · 守夜位 · 今晚没人」。
- **时间窗线索**：S7 用 `getSchedule(前任在岗者, yesterday)` 读出该岗昨日的 block 起止 → 只给"这个位子通常是几点到几点"的**时段档**（如「天黑到天亮」），**不给精确坐标、不给路径**。
- **没有"接受"按钮**：玩家在正确的时段站在正确的位置、做出 TEND 动作 → S2 判 `completeFactor = 1` → 次日 `PostRoster` 那个格子里多一个名字。**没有勾选框、没有完成提示。**
- **回报不在这块板上**：实际回报走 S5 §3.1 的 `TEND_REP_DAILY`（+3/+1/0）与 S3 配给，**S7 不显示这些数字**；`benefitSpec.effectCode = "POST_STAFFED"` 只在事后念一句「第二天，井窖的守夜位上有了人」。
- **与河谷信条卡 3「外人可过夜，但要出工」的衔接**：留宿产生的义务**不占板的配额**，它由 S4 的欠条页承担；仅当二者指向同一 `postId` 时，板在该条目下加**一行指针**：「这个位子你也欠着一次（见欠条）」——**不复制 S4 的 6 个字段**（防认知过载）。

---

## 7. 边界情况与失败模式

**E1 · 【必答】玩家在路上，缺口被 NPC 自己解决了**（P3 与挫败感的直接交界）
1. **不静默消失**：`NOTED` 过的条目不从板上蒸发，转 `RESOLVED{resolvedBy:"NPC"}` 并保留到**次日日切**——你必须有机会看见"世界动过了"。
2. **通知在下一个 `HOURLY_TICK` 发生**，不在你推门的那一帧（遵守写队列纪律）。你在半路上（下一个整点）就知道，省下的路程还来得及转向——**不惩罚已经在路上的玩家，也不给他开天眼**。
3. **不补偿**：没有任何"任务失败补偿"。那会把工作板变回任务列表。
4. **转介（redirect）**：条目位置显示 **≤1 条**次优建议——板上还有其它 `OPEN` 条目则指过去，没有则显示「他们今天自己够了」。这是"能否转为别的动线"的答案。
5. **不白跑 ≠ 有补偿**：若你已把物资扛到公共仓，世界**照收**（`tryTransfer` 语义不变），只是受益者已被别人先填上；声望与欠条照走 S5/S4。**世界不会因为你来晚了就退回你的付出，也不会因为你来晚了就奖励你。** 唯一例外：你已为它押上 `reservedByPromise` → **不豁免**，只标注「你为它押上的东西还没有着落」，把你推向 S4 §7 的代偿三选一。

**E2 · 【必答】两条缺口指向同一个目的地**
同 `mergeKey` → 合并为 1 条（`qty = Σ`，`urgency = max`，`sourceSignalIds[]` 全记）；同一 `postId` 缺 2 人 → 合并为「缺 2 个人」；同一 claimant 的多条 → 合并（人只有一个）。同地点**不同簇** → **不合并**，并列为同一地点的多行（"这一趟能办三件事"）。
→ 反直觉但正确的那条：**合并省下的配额不补新条目**（§3.4）。

**E3 · 板空了**：显示「今天他们自己够了」+ 板头 `safeDays` 与 `trendDelta7d`。**不是 bug，是世界在好转的证据**（S5 §7-E7）。

**E4 · 玩家背不动（`haulFit == OVER`）**：条目**不缩水**（`qty` 恒为世界真实的缺口量），只降权并明写「一趟扛不完 · 要跑 2 趟」。**禁止任何形式的"按玩家能力裁剪缺口"。**

**E5 · 目的地不可达**（`canShelterOutsider == false` 且已过 `curfewMin` / 黑市被封）：条目**保留但标灰**并附一行原因，**不静默删除**（与 S5 §7-E2 同款兜底断言）；若某日两社区目的地均不可达 → 面板显示"泄压阀失效"并告警。

**E6 · 跨日/日切瞬间的判定**：`RESOLVED` / `EXPIRED` 固定在日切后第一次 `HOURLY_TICK` 判定，按 `dayKey` 判一次，**不叠加、不回滚**（同 S5 §7-E6 纪律）。

**E7 · 玩家自己就是缺口的原因**（搬空某 zone → `ClaimDisplaced`）：板**只陈述缺口，不指认玩家**。归因链在调试面板可见，但板上不写"这是你干的"——道德时刻留给玩家自己认出来（P4 的情绪核心，不需要 UI 帮忙）。

**E8 · 两条缺口互相矛盾**（河谷要你分粮 vs 井窖要你留种）：板**不做仲裁**，两条都列，各说各话。这是站队代价的合法形态（P1）。

---

## 8. 验收标准与调试钩子

**验收**

1. **A1 · 无发布者 / 无奖励 / 无勾选（C9 主验收）**：全仓检索——工作板条目模板中 `{发布者, 奖励, 完成}` 类字段数 = **0**；`--dump-board` 的输出中**不含任何回报数字**；板上不存在"接受"控件（S9 侧静态检查）。
2. **A2 · 排序确定性**：同 seed + 同一玩家位置脚本 → 排序逐位复现；**本系统 raw `rng` 调用数 = 0**；`--dump-board` 打印完整排序键。
3. **A3 · 配额（已裁决 2）**：脚本化跑 10 游戏日，断言 `|getBoard(cid, d).entries| ≤ 3` 恒成立；且**合并后不补位**。
4. **A4 · 回填确认（谁确认它被满足了）**：脚本化补足河谷 MEDS → 次日 `RationIssued.shortfall.MEDS == 0` → 该条目转 `RESOLVED` 且 `resolvedBy == PLAYER`；对照组（不动手，让上游自行补足）→ `resolvedBy == NPC`，且**两条路径下 S7 都不写任何世界状态**。
5. **A5 · 路上消失（E1）**：构造"玩家 `NOTED` 后由上游自行解决" → 断言 ① 通知发生在下一个 `HOURLY_TICK`；② 条目保留到次日日切；③ `ReputationDelta` 事件数 = 0；④ 提供 ≤1 条转介；⑤ 玩家背包里的物资仍可 `tryTransfer` 入仓。
6. **A6 · 去重（E2）**：构造两条同 `mergeKey` 的 `GapSignal` → 断言板上 1 条且 `qty = Σ`、`urgency = max`；同地点不同簇 → 断言 2 条并列。
7. **A7 · 沉降（R2）**：连续 3 日同 `orderId` 且 `urgency ≤ 2` → 断言 `sedimented == true` 且不占配额；第 4 日 `urgency` 升到 3 → 断言当日 `HOURLY_TICK` 浮回。
8. **A8 · 确定性与预算**：同 seed save/load 续跑 10 日，`getBoard` 输出**逐位相同**（纯函数投影）；本系统仿真 ≤ **0.2ms/帧**（纯读 + 整点/日切批处理）；`AttributionLink` 新增数 = 0。
9. **A9 · 与 Core 验收 3 的关系**：无指引自由游玩 45 分钟，**≥70% 测试者仍有自设目标**——本份的贡献是可验证的：**在板为空的日子里**目标数不得为 0（`safeDays` 与 `trendDelta7d` 必须足以支撑自设目标）。

**调试钩子**

```text
--dump-board [<cid>] [<dayKey>]        条目 + sourceSignalIds + 完整排序键 + haulFit + tripHint
--dump-board-state <orderId>           状态机 + evidenceRef + resolvedBy + 归因链首跳
--dump-board-note                      冻结快照与其 frozenAt（证笔记过期）
--force-gap <cid> <cluster> <qty> <urgency>   构造缺口（走 S5 生成路径，禁用于正常流程）
--expire-gaps <dayKey>                 手动推进过期（验证 E6）
--assert-board-no-reward               断言板上无回报数字、无发布者字段
--assert-board-quota                   断言每社区每日 ≤3 且合并不补位
--assert-board-no-write                断言 S7 侧写队列条目数 = 0、ReputationDelta 数 = 0
--export-attribution <dayRange>        继承底座 §C8 规格（本份只复读上游链路）
```

---

## 附录 A · 提请回填底座 §D 的候选常量（本份不就地生效）

| # | 候选常量 | 建议值 | 单位 / 说明 |
|---|---|---|---|
| A-1 | `BOARD_TRAVEL_QUANTUM` | 5 | 分钟；`travelMin` 量化档，防排序抖动 |
| A-2 | `SEDIMENT_DAYS` | 3 | 日；连续 N 日同 `orderId` 且 `urgency ≤ 2` → 沉降 |
| A-3 | `HAUL_FIT_BANDS` | 0.80 / 1.00 | `FITS` ≤0.80×剩余容量；`TIGHT` ≤1.00；`OVER` >1.00 |
| A-4 | `BOARD_REFRESH_KIND` | `HOURLY_TICK` | 板刷新的唯一 tick 类（禁渲染帧） |
| A-5 | `BOARD_REDIRECT_MAX` | 1 | E1 转介条数上限 |

## 附录 B · 待主理人裁决 / 接口请求（7 项）

1. **B-1 · `urgency` 是否对玩家显示数字**：S5 §8-A9 把 `GapSignal.urgency` 列为"玩家可见数值"之一；本份主张**玩家侧只显示语言标签 + u4/u5 红标，数字仅进调试面板**（防"按数字排序刷"、防认知过载）。二者需择一，请裁决。
2. **B-2 · 起始公共仓的 MEDS / FUEL / SEED 量**：S3 §2.4 只给了 WATER / FOOD。本份样例按 **0** 计算；若 S3 有非零起始量，§6.3 的短fall 数字需重算（板的结构与排序规则不变）。
3. **B-3 · 请求 S5 暴露 `CreedConflictClusters(cid) -> ClusterId[]`**：供 `kind = "CREED"` 的反推。该表可由 S5 §2.4 六卡静态导出（`he_valley → [FOOD]`、`jing_cell → [SEED, WATER]`），本份**只读不定义**。若 S5 不提供，本份降级为"信条型一律归入 `STOCK`"，代价是留宿义务与真实缺口混在一栏。
4. **B-4 · S7 → S3 的接口形态**：契约写"S3（`reservedByPromise`，当玩家接下带承诺的缺口时）"。本份解释为 **S7 只暴露 `getLinkedReserve(orderId)` 供 S4 在 `reserve()` 前做冲突校验，写入仍归 S4/S3**——理由：板上若出现"接下并预留"按钮，就等于给缺口装了勾选框（违反已裁决 1）。请确认此解释，或改由 S9 提供该按钮（那样请一并撤回已裁决 1）。
5. **B-5 · 请 S8 提供**：① 每社区缺口板的物理 `locationId`（社区门口）；② `pathLenMeters(a,b)` 静态表（缺失时 `travelMin` 恒取 0，排序退化为不区分距离）。
6. **B-6 · 请 S9 遵守三条**：① 板是**世界内物件**，读取需进入 `observeRadius`（不做 Tab 全图面板）；② 笔记**会过期**且必须显示 `frozenAt`；③ 不给路径线与导航箭头，只给地名 + 方位 + 时间档。
7. **B-7 · 与并行 GDD 最可能冲突的三点（提请汇编时优先比对）**：
   - **`urgency` 的数字可见性**（B-1）与 S5 §8-A9 直接对撞。
   - **`GapSignal` 缺 `sourceKind` 字段**：本份靠 `sourcePostId` / `claimantActorId` 两个可选字段反推 `kind`，若 S5 未来的五路来源出现"两者皆空但非公共仓"的缺口，反推会误判为 `STOCK`。**建议 S5 增列 `sourceKind` 闭集 5 值**（与五路来源一一对应），本份可改为直读。
   - **配额语义**：S5 的"每社区每日 ≤3"是**生成上限**；本份的"≤3"是**板面配额**，二者在沉降规则下会分叉（沉降后板面 <3 但生成仍可 ≤3）。请确认这不算放宽上限。

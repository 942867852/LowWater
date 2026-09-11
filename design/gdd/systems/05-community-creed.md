# S5 · Community-Creed（社区、信条与声望）· 系统设计文档

- **Task ID**：GDD-005｜**阶段**：Phase 2 · 批次 B2｜**优先级**：P0（Economy 与 Dialogue 的共同上游）
- **主笔**：design-strategist（文策渊）｜**调度**：主理人（游承峰）｜**状态**：待拍板（附录 B 共 9 项）
- **依赖已读**：`game-concept.md` · `00-foundation.md` · `02-npc-simulation.md`（S2）· `03-economy-barter.md`（S3）· `04-dialogue-contract.md`（S4）· `01-scarcity-loop.md`（S1，检索）
- **依赖方向（严格单向）**：依赖 S0·A/S0·B/S0·C/S1/S2/S3/S4；**不依赖**对话树内容、不依赖 UI 布局。
- **边界（严格）**：社区三要素（信条/产能/缺口）、岗位编制与空缺、按社区独立声望、准入仪式与站队互斥、**社区账本 `LedgerEntry` 所有权**、`CreedRationRule` 参数。
  **不写**：经济结算数值（S3）、对话文本与分支（S4）、NPC 行为树与调度（S2）、区域物资池（S1）。

---

## 0.0 规范系统编号对照表（PHASE2-REVIEW · 裁决 C-13，全文已按此表替换）

| 规范编号 | 系统 | 文件 | 本文档原用临时编号 |
|---|---|---|---|
| **S0** | 底座（`S0·A` 角色判定 / `S0·B` 世界时钟 / `S0·C` 确定性 / `S0·D` 常量表 / `S0·E` 死亡清算） | `00-foundation.md` | S1 / S2 / S12 |
| **S1** | 稀缺主循环 | `01-scarcity-loop.md` | S5 |
| **S2** | NPC 三层模拟 | `02-npc-simulation.md` | S6 |
| **S3** | 易货经济与社区仪表 | `03-economy-barter.md` | S9 |
| **S4** | 记忆驱动对话与契约账本 | `04-dialogue-contract.md` | S8 |
| **S5** | **社区、信条与声望（本文档）** | `05-community-creed.md` | S7（自称） |
| **S6** | 传闻传播（待写） | — | —（未引用） |
| **S7** | 工作板（待写） | — | C9（概念文档 scope ID，保留原写法） |
| **S8** | 区域内容（待写） | — | —（未引用） |
| **S9** | 呈现 / UI（待写） | — | "UX / 面板" |

---

## 0. 全局契约（逐字继承，不改字段名）

```text
PostRoster(communityId, dayKey) -> 岗位编制
CreedConstraints{ canShelterOutsider:bool, tithe:float, curfewMin:int }
CreedRationRule{ titheRate, priorityOrder }
Reputation(communityId, actorId) -> int          // 按社区独立结算，−100..+100
protectedActors[≤3]
heirActorId(postId)
声望递推：R_c(t+1日) = R_c(t) × 0.97 + Σ 当日事件值；关键事件写入"社区记得的事"（≤5 条，不衰减）
产能：实际产出 = 额定产出 × (在岗人数/额定人数)，跌破 60% 进紧缺态
```

**主理人裁决落实（逐条，不复核）**

| # | 裁决 | 本份落点 |
|---|---|---|
| 1 | 站队互斥是 P1+P4 下的**硬规则**，不是第五支柱；请实现为**可计算规则** | §3.3 全节：`TIER_W`/`ALIGN_CROSS_BASE` 闭式计算 + 单向不还 + 跨档限速（预告闸门） |
| 2 | 每社区 = 信条+产能+缺口；两社区配置与 S3 一致 | §2.1 逐字段抄 S3 §2.4，不改一个数 |
| 3 | 声望日切 00:00 批量应用，当日对话读昨日快照 | §4.1：应用点 4.2 / 7.4，产物只进 `DaySnapshot[D]` |
| 4 | EstateSettlement 属底座；本份只实现下游消费（VOID/DEATH 不扣声望但必发 gossip；死亡→岗位空缺→`heirActorId`） | §3.5、§7-E1；`ReputationDelta` 数 = 0 为硬断言 |
| 5 | `ClusterId` 是否加 FOOD —— **本份为清单所有者，结论见 §0.1** | §0.1 |
| 6 | 严禁另造 ID 与常量 | 全部新常量进附录 A（Proposal）、新 ID 进附录 B 提请登记；正文不就地生效 |

### 0.1 【最终结论 · 清单所有者裁决】`ClusterId` 加入 `FOOD`

> **裁决：加。作为第六键，且 `FOOD.tradable = false`。**

**理由（三条，缺一不足以支撑改 ID 闭集）**

1. **不加则底座自相矛盾**：`SAFE_DAYS = min(waterL/(3.0×pop), foodUnits/(1.0×pop))` 需要 `foodUnits`，而 `Warehouse.stock` 若只有 5 键，食物只能挂在库存结构外的**私有字段**上 —— 那才是真正的"另造常量"，且会让 `tryTransfer` 出现"食物不是簇"的特例分支（违反 C-19）。
2. **不加则写不出本作最高频的一句邻里话**：S4 §5.3 已陈明"我明天给你一份口粮"写不出来。P1 要求一切落到**具体的人与具体的东西**；口粮是本作唯一一个人人每天都要找的东西，不能因为它不可交易就不许被许诺。
3. **加它不威胁"无通用货币"**：FOOD 不进 `PriceTable`、不进 `quote/execTrade`、无 `base`。**通货簇仍是 5 个**（`TRADABLE_CLUSTERS = {WATER, FUEL, AMMO, MEDS, SEED}`），"五簇通货"的称谓与全部经济公式一字不改。

**回填规格（PHASE2-REVIEW 已据此回填 `00-foundation.md` §D、S3 §2.2、S4 §5.3 —— C-17 / C-26 / B-4 三项全部关闭）**

```text
ClusterId : WATER | FUEL | AMMO | MEDS | SEED | FOOD          // 6 键
TRADABLE_CLUSTERS = {WATER, FUEL, AMMO, MEDS, SEED}           // 5 键，经济公式只认这个子集
Quantity.cluster → unit 映射增列：FOOD → "portion"（与 SEED 同单位，不同簇，无冲突）
FOOD 约束（三条，实现期静态断言）：
  ① 不进 PriceTable（无 base/buy/sell）→ quote/execTrade 收到 FOOD 直接返回拒绝并 assert
  ② 进 Warehouse.stock、进配给、进 SAFE_DAYS 分子、进 reservedByPromise
  ③ 进 ItemSpec.cluster（S4 的 DELIVER 承诺开放 FOOD 类）
```

**我为此承担的连带裁决**：S3 §5.3 的 `ItemSpec.cluster` 限制解除（C-26 关闭）；`CreedRationRule.priorityOrder` 含 FOOD 已由 S3 给出（`[WATER,FOOD,MEDS,FUEL,SEED]`），本份沿用。**风险**：UI/关卡若已按"六簇皆可交易"起草会撞车（S3 的 C-22 第三点），请汇编时优先比对。

---

## 1. 系统概览与目标

### 1.1 定位

S3 把世界压缩成一个数字（`safeDays`），S2 让每个人有一条你能背下来的轨迹，S4 让一句话变成一张欠条。**S5 是这三者共用的"地方"**：它决定这块地方**信什么、产什么、缺什么**，以及**它对你这个人记住了多少**。

- **支柱**：**P1**——声望按社区独立、由具体的人目击与记忆驱动，绝不出现全局声望条；**P4**——站队的代价是**地图变小**，不是数值变红；**P3**——门会关，但关之前你一定看得见它在关；**P2**——三要素（信条/产能/缺口）是玩家唯一需要背下来的社区读数。
- **动词**：**站 ALIGN** 完整落在本系统（准入仪式与信条互斥）；**守望 TEND** 的接纳度回报在此结算；**说 PERSUADE** 的声望门槛在此定义。
- **反目标**：不做全局声望；不做"加入即永久好友"的阵营开关；不做可无限重入洗白的站队；不做第四个数值系统（本系统只有**声望**一个数 + **编制比率**一个比率）。

### 1.2 一句话职责

> 让"你在哪儿是个人"这句话，同时是一个数字、一份编制表、和一张你会越走越窄的地图。

---

## 2. 核心概念与数据模型

### 2.1 社区三要素（配置逐字段对齐 S3 §2.4，不改一个数）

| 项 | `he_valley` 河谷 | `jing_cell` 井窖 |
|---|---|---|
| `population` | **9** | **7** |
| 起始 `safeDays` | **8**（水 8 / 食 8 齐平） | **6**（水 6 / 食 6） |
| 结构性净收支 | **WATER +15 L 唯一盈余**；MEDS −0.3 / FUEL −1.5 / SEED −0.2 全缺 | **WATER −6.3 L 致命缺口**；**MEDS +0.8 / SEED +0.35** 盈余 |
| `CreedRationRule` | `{titheRate:0.1, priorityOrder:[WATER,FOOD,MEDS,FUEL,SEED]}` | 同左（S3 已定两社区同） |
| 岗位 `rated` | water_01:2 / water_02:2 / patrol:1 / watch:1 / mend:1 / care:1 / idle:1 = **9** | water_01:2（`postYieldMul 0.7`）/ patrol:1 / watch:1 / mend:1 / care:1 / idle:1 = **7** |

> **C-32（本份自决，因 `PostRoster` 归本份所有）· PHASE2-REVIEW 已回填 S2**：S2 §2.3 原写"静态 6 岗位"、§5.3 原写"槽位硬编码"，但河谷有 `water_02` → 7 个 `postId`。**裁定**：`PostRoster.posts` 长度由社区配置决定（河谷 7 / 井窖 6），S2 §5.3 的 stub 已改为读 `PostRoster(cid, dayKey)`。
> **裁决 5（C-05）已拍板**：`idle_00` 为**伪岗位**，`rated` 由 `pop − Σ其它 rated` **反算**（可为 0），**不计入 `filledPosts` 分子、不计入 `ratedPosts`**，仅作闲人池与 gossip 发源标记。
> ⚠ 本项属"依裁决 7 连带回填"，请主理人在评审时一并确认（见 `06-consistency-review.md` OQ-2）。

### 2.2 数据模型

```text
Community        { communityId, population, creeds: CreedId[3], constraints: CreedConstraints,
                   rationRule: CreedRationRule, posts: Post[], safeDaysRef: Ref<SafeDays> }
Post             { postId, role, locationId, rated:int, onDuty:ActorId[], nominalOut:Quantity[] }
PostRoster       { communityId, dayKey, posts: Post[], ratedPosts:int, filledPosts:int, staffingRatio:float }
AllegianceTier   { actorId, communityId, tier:"OUTSIDER"|"SUPPLICANT"|"MEMBER"|"SWORN", joinedDayKey }
CommunityMemory  { communityId, entries: MemoryFact[≤5] }            // 不衰减
MemoryFact       { factId, kind, dayKey, actorIds[], delta, summaryCode }   // summaryCode 闭集，禁自由文本
ReputationState  { communityId, actorId, value:int −100..+100, band, pendingDelta:int, pendingDayKey }
GapSignal        { signalId, communityId, cluster, deficitUnits, urgency:1..5,
                   targetLocationId, sourcePostId?, claimantActorId?, expiresDayKey,
                   sourceKind:"STOCK"|"CREED"|"POST"|"PERSON"|"PROMISE" }   // 裁决 20 新增，闭集 5 值
WaterInTransit   { communityId, actorId, amount:float }               // 井窖过秤封存，见信条卡 6
```

**产能与紧缺（本份只算编制，不复制产量）**

```text
effHead(post)    = Σ_worker completeFactor(worker)   // 完成=1；部分完成(迟到>30min)=0.5；缺席=0
staffingRatio    = Σ effHead(post) / Σ rated(post)   // idle 不计入两侧
实际产出          = 额定产出 × staffingRatio           // 契约式；物资端由 S2 getLaborOutput 出，本份不重算
staffingRatio < SCARCE_RATIO_ENTER(0.60) → 社区进 SCARCE；退出需 ≥0.60 连续 SCARCE_RATIO_EXIT_DAYS(2) 日
```
> **裁决 C-06（PHASE2-REVIEW 已拍板 · 机械回填）**：分子由 `onDuty` 改为 **`effHead`**（加权有效人头），与 S0 Part E ④、S2 §3.4 三方一致。60% 阈值 / 滞回 2 日 / `idle` 不计两侧**均不变**。
> **术语分工**：`Post.onDuty: ActorId[]` = 人头名单（补岗用）；`effHead` = 加权强度（产能与 `staffingRatio` 用）。本份 §4.2 与 §7 处出现的"在岗 −1"语义统一为 **`effHead` 当日按 `completeFactor=0` 计入**，非名单删除。
> **C-18 重申（本份与 S3 已一致）**：`SCARCE`（编制 <0.60）与 S3 的 `TIGHT`（`safeDays` ≤5）是**两个不同的量**，UI 禁止合并为一个指示灯。

### 2.3 声望分档（5 档，防认知过载）

| 档 | 区间 | 玩家体感 | 机械后果 |
|---|---|---|---|
| `WARM` | ≥ +25 | 自己人 | 解锁 `MEMBER` 门槛；`CREED` 类选项开放 |
| `NEUTRAL` | −24 … +24 | 外人 | 可交易、可许诺（`reserved=false`） |
| `COOL` | −59 … −25 | 不受欢迎 | `CREED` 类关闭；配给谈判需破例 roll |
| `HOSTILE` | −89 … −60 | 敌人 | 拒绝入户；`quote(isBuy=true)` 返回拒绝；可被悬赏 |
| `OUTLAW` | ≤ −90 | 不能留的人 | terminal；该社区全部非敌对入口关闭（S4 #12 `outlaw_claim`） |

### 2.4 六份信条卡（两社区 × 3，互为镜像）

> 每卡四要素：**条文 / 违反后果 / 准入仪式 / 对 Mechanics 的实际约束**。`CreedId` 为新 ID 命名空间（附录 B 提请登记）。

#### 河谷 `he_valley` —— 核：「水是要走的，人是要聚的」

| 卡 | 条文 | 违反后果 | 准入仪式 | 对 Mechanics 的实际约束 |
|---|---|---|---|---|
| **1** `he_valley.split_food_at_dusk`「**当日分尽，不留隔夜（粮）**」 | 宵禁前，境内任何人手中**余粮**必须入公共仓或分给他人，不许隔夜私囤 | 声望 `−12`；被目击 → `creed_breaker` 印象 + gossip `CREED_BREAKER`(charge 2)；连续 3 日 → `OUTLAW` 候选 | `RITUAL_SHARE_DUSK`：日落前把 ≥1.0 portion FOOD 倒入公共仓，并当众分给 3 名成员 | `curfewMin = 1290`（21:30）。日切检查：过宵禁时玩家在境内且 `carrying.FOOD > FOOD_UNIT_PER_PERSON_DAY(1.0)` → 违反。**这是"送粮换水"的机械形式**——不交粮就是违规 |
| **2** `he_valley.tithe_one_tenth`「**余粮十分之一入公共仓**」 | 境内拾荒所得按十分之一缴公，缴了不可取回 | 声望 `−6`（轻）；不写印象（缴税纠纷不算背信），只发 `PriceShock{TENSION_INJECTION}` | 无独立仪式（随卡 1 一并完成） | `tithe = 0.10`（S3 §3.4 已定，只作用于**个人拾荒所得**，劳动产出 100% 入仓不经 tithe）。主动缴纳 → `+ceil(0.1 × amount) capped +3` |
| **3** `he_valley.open_hearth`「**外人可过夜，但要出工**」 | 外人可留宿；留宿者次日 06:00 必须承接一个班次（TEND），否则违约 | 声望 `−6`；不写印象；次日 06:00 发 `PostVacated{reason:CREED_BLOCK}`（你占的岗被收走） | `RITUAL_HEARTH`：在公共屋睡一夜 + 次日完成 1 个 `ServiceSpec` | `canShelterOutsider = true`。这是 TEND 动词最日常的人口，也是"白嫖住宿"的代价 |

#### 井窖 `jing_cell` —— 核：「水是要留的，人是要筛的」

| 卡 | 条文 | 违反后果 | 准入仪式 | 对 Mechanics 的实际约束 |
|---|---|---|---|---|
| **4** `jing_cell.no_outsider_overnight`「**外人不得过夜**」 | 宵禁后任何非成员不得留在境内 | 声望 `−12`；被目击 → `creed_breaker` + gossip `CREED_BREAKER`(charge 3，比河谷重一档，因被目击概率更高) | `RITUAL_WATER_WEIGH`：交出全部超额水（过秤封存）+ **连续守夜 480min**（`NIGHT_WATCH`） | `canShelterOutsider = false`；`curfewMin = 1260`（21:00）。宵禁后玩家仍在境内 → 违反；**玩家正顶着岗也照算** → `PostVacated{reason:CREED_BLOCK}` |
| **5** `jing_cell.keep_seed`「**留种**」 | 种子不得流出社区（可带入，不可带出） | 声望 `−15`（最重）；`creed_breaker` + gossip `CREED_BREAKER`(charge 3)；**且已带出部分不再追回**（不可逆） | 随 `RITUAL_WATER_WEIGH`；SWORN 需替井窖从河谷运回 ≥10 L 水入公共仓 | 在 SEED 全局 `tradable=false` 之上再加 `exportLocked=true`：出境检查口检测到 `carrying.SEED > 0` 且来源为井窖仓 → 违反。**这是 D6 种子走私的唯一成因** |
| **6** `jing_cell.water_by_weight`「**水过秤**」 | 入境水量按人定量，超额封存于门口水柜，出境归还 | 试图绕过过秤台带水入境并被发现 → 声望 `−8` + `creed_breaker`；超额部分强制封存 | —（是日常约束，非仪式） | 入境限额 `WATER_ENTRY_LIMIT = WATER_L_PER_PERSON_DAY × 3 = 9.0 L`；超额进 `WaterInTransit{communityId, actorId, amount}`（claimant = 玩家，出境归还）。**这不是没收，是把后勤问题做成关卡** |

### 2.5 六卡如何互为镜像并制造 D6 黑市需求

| 维度 | 河谷 | 井窖 | 镜像性质 |
|---|---|---|---|
| 对**物资流动** | 不许**囤**（分尽） | 不许**越境**（留种 / 过秤） | 一个管"进来之后"，一个管"出去之前" |
| 对**外人** | 可过夜，但要出工 | 不得过夜 | 同一个动词"过夜"，一边合法一边违法 |
| 对**时间** | `curfew 21:30` | `curfew 21:00` | 玩家在两地之间的 30 分钟是真实的赶路窗口 |

**由此自然长出的四类黑市需求（不是写出来的，是算出来的）**

1. **河谷 · 寄存**：不能隔夜囤粮 → 想囤的人（老人/病人/跑商者）需要**境外存放点** → 黑市"寄存柜"。
2. **河谷 · 逃秤**：拾荒缴 1/10 → 需要**不过秤的交易点** → 黑市交割（且天然在河谷境外）。
3. **井窖 · 越境与床位**：外人不得过夜 → 需要**铺位**；种子不得外流 → 种子（6.00 RU/份，五簇最高单价）成为**天然高价值走私品**。
4. **井窖 · 不过秤的水**：水过秤 → "背着 30 L 水走进一个每天缺 6.3 L 的地方"是必然后勤需求 → 黑市在**过秤台之外**交割。

> **黑市载体不新增地点**：承接概念文档 C1 已有的「1 个被弃的旧政权设施」，把它指定为**无信条地点**（不是社区，永不结算 `Reputation`）。它是 P4↔探索自由的法定出口，也是"两个社区都关上门时你仍有地方可去"的结构保证。

---

## 3. 规则与公式

### 3.1 声望递推（契约式，本份唯一实现处）

```text
R_c(D+1) = clamp( round( R_c(D) × REPUTATION_DECAY(0.97) + Σ delta(pendingDayKey = D) ), −100, +100 )
band(R)   = WARM ≥+25 | NEUTRAL −24..+24 | COOL −59..−25 | HOSTILE −89..−60 | OUTLAW ≤−90
```

事件值表（`delta` 一律整数，禁浮点）：

| 事件 | `delta` | 归属社区 | 记入"记得的事" |
|---|---|---|---|
| 完成 `ServiceSpec`（TEND 顶岗）第 1 / 2 / ≥3 次·日 | `+3 / +1 / 0` | 该岗位所属社区 | 否 |
| DELIVER/SERVICE 兑现 | `+clamp(round(BREACH_BASE(6) × 0.5 × kindW × qtyW), 1, 9)` | promisee 所属社区 | 否 |
| 违约 `PromiseBreached` | `−round(BREACH_BASE × kindW × qtyW × witnessW × reasonMul)` | 见 §3.4 | `\|delta\| ≥ 15` 或 `REFUSED` |
| 信条违反 | `−round(CREED_VIOLATION_BASE(12) × severityW)`，轻 0.5 / 中 1.0 / 重 1.25 | 违反地社区 | **是**（写 `MemoryFact`） |
| `RumorInjected`（S6 落地）到达 actor a | `sign(claim) × min(\|charge\|, 3)` | `a.communityId` | 否（charge 已由 S2/S6 逐跳衰减，本份不再二次衰减） |
| ↳ **其中 `seedKind` 属 DEATH 系** | **强制 `0`** | — | **是**（`ESTATE_LOSS`，delta=0） |
| 主动缴 tithe | `+clamp(round(0.1 × amount), 1, 3)` | 缴纳地社区 | 否 |
| 准入仪式完成（tier↑） | `+TIER_GAIN[tier]` | 该社区（**且对另一社区记负，见 §3.3**） | **是** |
| 退队 `CreedRenounced` | `R_A ← min(R_A, RENOUNCE_CAP(−20))`（clamp，非 delta） | 退出地社区 | **是** |
| S3 `reputationHint` | FAIR +1 / GENEROUS +2 / HARD_BARGAIN −1 / PRESSED_LUCK 0 / REFUSED 0 / BAD_FAITH −3 | 交易地社区 | 否 |
| **EstateSettlement（DEATH 系）** | **0**（硬约束，见 §3.5） | — | **是**（但 delta=0） |

```text
kindW   : DELIVER 1.0 / SERVICE 0.75 / ABSTAIN 0.5
qtyW    = clamp(amount / perCapitaRef[c], 0.5, 3.0)     // perCapitaRef 取 S3 §3.4；SERVICE 按 durationMin/480
witnessW= (|witnessIds| == 0) ? WITNESS_CREDIT_MUL(0.5) : 1.0 + (1 − WITNESS_CREDIT_MUL) × |witnessIds| / WITNESS_MAX(3)
```

> **裁决 19（PHASE2-CLOSING · 修补既有漏洞 R-6）**：上表 gossip 行原写 `GossipEmitted`，会使 Part E 产生的**死亡系传闻**也走 `sign(claim) × min(|charge|,3)` 算出非零 delta，**击穿 Part E ②「死亡不扣声望但必发 gossip」与 `--assert-estate-no-rep`**（该漏洞已存在，由 S6 审查发现，非 S6 引入）。
> 修正：① 事件源改为 S6 的 `RumorInjected`（S2 只发种子 `GossipEmitted`，种子本身不直接产生声望）；② **DEATH 系 `seedKind` 闭集** `PROMISE_VOIDED_BY_DEATH / NO_HEIR / BOTH_DEAD / TARGET_DEAD / TARGET_GONE` → `delta` **强制 0**，但仍写 `MemoryFact(kind=ESTATE_LOSS)` 且必发 gossip。
> 硬断言：`--assert-estate-no-rep` 现在同时约束 §3.4 表与本节事件值表两处。

**"社区记得的事"（≤5 条，不衰减）**

```text
写入条件：|delta| ≥ MEMORABLE_THRESHOLD(15)  或  kind ∈ {CREED_JOINED, CREED_RENOUNCED, CREED_VIOLATION,
                                                       PROMISE_REFUSED, ESTATE_LOSS}
淘汰键   ：sort(|delta| asc, dayKey asc, factId asc) 取首条   // 全确定性
作用     ：数值会随 ×0.97 淡忘，但"记得的事"永不淡忘 —— 这是站队不可逆的**真正载体**（§3.3）
```

### 3.2 准入与门槛

```text
tier 门槛（按社区独立）：SUPPLICANT  R ≥ 0
                        MEMBER      R ≥ +25  且 完成该社区准入仪式
                        SWORN       R ≥ +60  且 完成 SWORN 仪式 且 无 creed_breaker 印象
```

### 3.3 站队互斥：可计算规则（裁决 1 的落点）

**① 加入 A 必然扣 B —— 闭式**

```text
onCreedJoined(A, tier):
    R_A += TIER_GAIN[tier]                                  // SUPPLICANT +15 / MEMBER +25 / SWORN +40
    对 B = other(A):
        R_B += −ALIGN_CROSS_BASE(12) × TIER_W[tier]         // TIER_W = 1 / 2 / 3
        → MEMBER 扣 B −24；SWORN 扣 B −36
```
即：**走得越深，另一边的门越窄**。这是算术，不是叙事约定。

**② 是否可逆 —— 可退，但敌意不还**

```text
onCreedRenounced(A):
    R_A ← min(R_A, RENOUNCE_CAP(−20))      // 立即 clamp，不是慢慢掉
    R_B 不回补                              // 关键：扣掉的敌意不退
    tier(A) ← OUTSIDER；重入时 TIER_GAIN × 0.5
```
**为什么"数值会衰减但门不会开"**：`R_B` 会随 ×0.97 缓慢爬回 0（NEUTRAL 区间），但 `MemoryFact{CREED_JOINED}` **不衰减且永不淘汰出前 5 条之外**，而 `SWORN`/`MEMBER` 重入要求完成准入仪式，仪式要求 `CreedConstraints` 与印象门槛（`creed_breaker` 存在即拒）。**所以：数字会原谅，人不原谅。** 这正是"不可逆"的机械载体。

**③ 可观测的预警信号（P3"关闭必须有预告"的硬实现）**

| 信号 | 时机 | 形态 |
|---|---|---|
| `RitualScheduled{cid, creedId, dayKey}` | 仪式**最少提前 `RITUAL_NOTICE_DAYS(1)` 个游戏日**公告 | 门口挂牌 + 当事 NPC 亲口告知（不是 UI 弹窗） |
| `pendingDelta` 可见 | 玩家做出站队动作后，当日起 HUD 显示「明日生效：井窖 −24」 | 继承底座"pending 条目显示明日生效" |
| 分档趋势箭头 | `R_B` 处于 COOL 且 `R_B(D) − R_B(D−1) < 0` | 社区仪表持续箭头 + NPC 台词回调（"你最近老往河谷跑"） |
| **跨档限速（硬闸门）** | 任何使 `R` 跨 **≥2 档** 的 delta → **拆为两日应用**并报警 | `BAND_STEP_MAX = 1`。断言：单次日切 `|Δband| ≤ 1` |

> 结论：从 `NEUTRAL` 到 `OUTLAW` 至少需 **4 个游戏日**（4 次跨档），且每一档都至少有一天你看着它发生。**不存在无预告的不可逆。**

### 3.4 【必答】`PromiseBreached.reasonCode` 8 × 分流表

> `reasonCode` 闭集 8 值取自 S4 §3.6，本份不增不减。**归属社区规则**：`reserved=true`（动用公共仓）→ 扣在公共仓所属社区；`reserved=false`（个人许诺）→ 扣在 **promisee 所属社区**（被伤害的是他，他的社区记账）。

| # | `reasonCode` | 含义 | 扣声望？ | `delta`（`reasonMul` 已含） | 发 gossip？ | 备注 |
|---|---|---|---|---|---|---|
| 1 | `NO_STOCK` | 到期时 promisor 无货（个人许诺且背包不足） | **是** | `−BREACH_BASE × kindW × qtyW × witnessW × 1.0` | **是** | DELIVER+WATER → `WATER_DEBT`；其余 → `HIRELING` |
| 2 | `NO_SHOW` | SERVICE 未到岗 / 中途离岗 | **是** | `× 1.0`（同上） | **是** | `HIRELING`；S4 #10 `broke_word` 触发 |
| 3 | `REFUSED` | 追讨分支中**当面拒付** | **是** | `× 1.5`（**最重**：做不到 vs 不愿做） | **是** | `HIRELING`，`charge +1`；无条件记入"记得的事" |
| 4 | `DEATH` | promisor 死亡（Part E ②） | **否** | `0` | **必发** | `PROMISE_VOIDED_BY_DEATH`；**硬断言 `ReputationDelta` 数 = 0** |
| 5 | `NO_HEIR` | promisor 死亡且无继承人 | **否** | `0` | **必发** | 同上，gossip kind 不同 |
| 6 | `BOTH_DEAD` | 两端皆死者 | **否** | `0` | **必发** | 同上 |
| 7 | `TARGET_DEAD` | promisee 死亡 | **否** | `0` | **必发** | 无人可记，但世界仍议论 |
| 8 | `TARGET_GONE` | promisee 迁居 / 失踪 | **否** | `0` | **发**（弱） | `charge = 1`；只写 `MemoryEvent`，**不写印象**（无接收人） |

> **设计意图**：4–8 全部 `delta = 0` 但**全部发 gossip**——这是 Part E ②（死不是违约）与 P4（代价仍要被看见）的唯一共存形式：**账本不罚你，但闲话照传**。死亡系不扣声望，却仍会经 gossip 变成别人的印象（S2 §3.7 的 `任意负面 → MARTYR` 边也由此获得输入）。

### 3.5 EstateSettlement 的下游消费（裁决 4）

```text
ActorDied ──► Part E Phase1（下一个 HOURLY_TICK）──► 本份：onDuty(post) −1 → staffingRatio 重算 → 可能 SCARCE
         ──► Part E Phase2（日切第 6 步）──► 本份：① LedgerEntry VOID/DEATH（不扣声望）
                                              ② heirActorId(postId) 转 promisee
                                              ③ 写 MemoryFact{ESTATE_LOSS, delta=0}
                                              ④ 必发 gossip（本份只发 GossipSeed，不算传播）
次日 06:00 DISPATCH：S2 从闲人池/heir 补岗；连续 3 日补岗失败 → 本份裁撤该岗位（rated → 0）
```

**`heirActorId(postId)` 的 `seniority` 排序键（S2 §3.2-SO2 请求确认，本份确认采纳）**：
`seniority = (入社区日 asc, actorId asc)`，`heir` = 同社区同岗位在岗次席（死者之后第 1 位）。无次席 → `null` → `VOID/NO_HEIR`。

### 3.6 主导策略反制

| 候选主导策略 | 结构性反制 |
|---|---|
| 刷顶岗刷声望 | 每日 `+3 / +1 / 0` 递减（同 S0·A §A3.2 的 `dailyDecayW` 同款哲学） |
| 刷小额 DELIVER | `qtyW` 下限 0.5 → 最小 `+1`；但每次交付消耗**真实物资**（P4，S1 `tryTransfer` 全有或全无） |
| 只侍奉一个社区 | 河谷缺 MEDS/FUEL/SEED、井窖缺 WATER −6.3 L，**单一社区数学上无法自给** → 必须跑商 |
| 反复退队重入刷 `TIER_GAIN` | 退队 Clamp 至 −20 + 重入收益减半 + **敌意不还**（B 侧永久记账） |
| 用 gossip 洗白 | `sign × min(|charge|,3)` 可正可负且失真方向由中继者偏差**确定性决定**（S2 §3.7），无法被玩家稳定操纵 |

---

## 4. 状态与流程

### 4.1 日切时序（挂载底座九步；**裁决 10（C-14 + B-1）已拍板，S0 §B3.5 已同步细分**）

```text
[3.1] 库存 / 岗位产出 / 消耗结算                （底座原有）
[3.2] 价格更新（S3）
[3.3] 配给发放（S3）
[4.1] R_c × 0.97（衰减）
[4.2] 应用 Σ delta(pendingDayKey = D) → clamp(−100,+100) → 分档
[4.3] 写 MemoryFact（≤5，不衰减）；超限时按淘汰键剔除
[4.4] NPC 短期记忆窗口滑动 + LossStreak 干净日结算（S2）
[5]   安全天数重算（S3）；本份读 SafeDays 更新缺口
[6]   EstateSettlement Phase 2（底座）→ 本份消费（§3.5）
[7.1] 承诺到期判定 → BREACHED（S4）
[7.2] release() 释放 reservedByPromise（S3）
[7.3] 发 PromiseBreached（S4）
[7.4] 【本份】应用 7.3 产生的 delta → 进 DaySnapshot[D]   ← 裁决 10 追加，已落进 S0 §B3.5
[8]   全局冲突指数 / 张力预算注入器
[9]   dayKey ← D+1，发布不可变 DaySnapshot[D]
```

> **为什么必须加 7.4**：第 7 步在第 4 步之后，若不在第 7 步内补批，违约 delta 要等到 D+1 日切才应用 → 玩家 **D+2** 才在对话里看到后果，与 S4 §3.6 表（D+1 可读）及裁决 3 不符。加 7.4 后：**D 日到期 → D 日日切判定 → 进 `DaySnapshot[D]` → D+1 全天对话可读**，延迟恰为 1 日。
> **7.4 与裁决 3 不冲突**：它仍在日切批量内、仍只写 `DaySnapshot`、当日对话仍读昨日快照。

### 4.2 站队流程（含预告）

```text
D0   玩家完成准入前置（R ≥ 门槛 + 仪式材料）→ 可发起仪式请求
D0   → RitualScheduled{cid, creedId, dayKey: D0+RITUAL_NOTICE_DAYS(1)}   // 门口挂牌 + NPC 亲口告知
D1   仪式执行 → 成功 → CreedJoined{cid, tier}
D1   写入队列：R_A += TIER_GAIN；R_B += −12 × TIER_W   （HUD「明日生效」，可观测）
D2 00:00 [4.2] 应用 → DaySnapshot[D1] → D2 全天对话/交易读新声望
```

### 4.3 缺口 → C9 工作板（替代任务板）

**输入（本份产出，S3/C9 消费）**

```text
GapSignal{ signalId, communityId, cluster, deficitUnits, urgency:1..5, sourceKind,
           targetLocationId, sourcePostId?, claimantActorId?, expiresDayKey }
```

**来源（五路，全部为"已有事件"，不新增探测）**

| 来源 | `deficitUnits` 取法 | `urgency` |
|---|---|---|
| S3 `RationIssued.shortfallByCluster[c]` | `shortfall[c]` | `clamp(ceil(shortfall / 日耗[c]), 1, 5)` |
| `PostVacated`（岗位空缺） | `rated − onDuty` | 3（固定），水岗 `+2` |
| `staffingRatio < 0.60`（SCARCE） | `1 − staffingRatio` × 额定 | 4 |
| S2 `NeedUnmet{severity ≥ 2}` | 1（人级） | `severity + 1` |
| Part E 岗位裁撤 | `rated`（整岗消失） | **5** |

**目的地化（这是"动态目的地"而非任务板的全部秘密）**

```text
targetLocationId =
  短fall 型  → stock.<cid>.public 的位置（公共仓）
  岗位型     → Post.locationId（你要去的是那个**空着的岗位**，不是某个 NPC）
  claimant 型→ 该 claimant 次日 06:00 的取水点（getSchedule 读出，不泄露精确坐标）
排序： (urgency desc, 距玩家 asc, cluster 稀缺度 desc, communityId asc)
配额： 每社区每日 ≤ GAP_SIGNAL_MAX(3)      // 与 PROMISE_MAX_ACTIVE(3) 同量，防认知过载
```

**它不是任务板的四条纪律**：① **无发布者**——没有感叹号 NPC，只有"明天早上谁会缺这个"；② **无奖励数值**——不显示声望/物资回报；③ **无完成勾选**——`shortfall` 归零即自动消失（S3 下一日 `RationIssued` 为证）；④ **会过期**——`expiresDayKey` 到期未补，不是"任务失败"，而是**社区自行收紧配给**（P3：世界不等人）。

---

## 5. 对外接口

### 5.1 暴露

| 接口 / 事件 | 消费方 | 用法与注意 |
|---|---|---|
| `PostRoster(cid, dayKey)` | **S2**（调度令唯一编制源）、C9、面板 | 只读；`idle_00` 不计 `ratedPosts` |
| `CreedConstraints(cid)` | S2（L2 归位/缴税改派）、S4（`creedConflicts[]`）、UI | 最小三字段，不扩 |
| `CreedRationRule(cid)` | **S3**（配给优先级与 tithe） | 两社区同值，见 §2.1 |
| `Reputation(cid, actorId) -> int` | S4（`RequirementExpr`）、S3、UI | **只读 `DaySnapshot[D-1]`**（裁决 3） |
| **`LedgerQuery(actorId, state?)` / `LedgerQuery.byCommunity(cid, dayKey?)`** | **S4**（`openLedgerEntries[]` 唯一来源）、S3、C9 | **本份拥有 `LedgerEntry`**；S4 只发 `PromiseMade` 事件，不得写 |
| `GapSignals(cid) -> GapSignal[≤3]` | **S7 工作板**、UI | 见 §4.3 |
| **`CreedConflictClusters(cid) -> ClusterId[]`** | **S7**（裁决 20 新增，B-3） | 返回与该社区信条冲突的簇（如井窖"种子不得外流" → `SEED`）。S7 用它判定 `sourceKind="CREED"`，**不再靠可选字段反推**（否则有边缘误判） |
| `CommunityMemory(cid) -> MemoryFact[≤5]` | S4（CALLBACK 文本条件源）、面板 | 不衰减 |
| `heirActorId(postId)` | S4（`HEIR` 分支）、S2 | 实现由 Part E 提供，本份只维护 `seniority` 键 |
| `protectedActors[≤3]` | 底座 LOD、S2、S4 | **与底座 LOD `PROTECTED` 取同一份名单**（C-27，本份支持） |
| 事件 `ReputationDelta{cid, actorId, delta, pendingDayKey, causeRef}` | S3/S4/面板（只读） | 唯一产生口；**DEATH 系不得产生此事件** |
| 事件 `CreedViolated{cid, creedId, actorId, severityW, dayKey}` | S2（写印象+gossip 源）、S4 | 本份算声望，印象文本归 S4 |
| 事件 `CreedJoined` / `CreedRenounced` / `RitualScheduled` | S2/S4/UI | `RitualScheduled` 是 P3 预告的载体 |

### 5.2 依赖

- **S0·B**：`onDayBoundary`（4.1/4.2/4.3/6/7.4 步）· `getDaySnapshot(D)`（唯一跨系统读数源）· `enqueue` · `now()`。
- **S0·C**：`recordAttribution` · `exportAttribution`。**本系统 raw `rng` 调用数 = 0**（继承 S2 R-B 最严读法；本系统无任何随机）。
- **S2**：消费 `PostVacated` / `GossipEmitted` / `NeedUnmet` / `ActorDied`；读 `getLaborOutput`（只读，不重算产量）。
- **S3**：消费 `SafeDays` / `Warehouse`（只读）/ `RationIssued` / `ScarcityStateChanged`（`TIGHT`）/`reputationHint`；写入只经 S1 `tryTransfer`。
- **S4**：消费 `PromiseMade` / `PromiseBreached`；**本份不算对话、不写印象**。
- **S1**：`tryTransfer`（唯一物资落地口）。
- **Part E（底座）**：`heirActorId` · `voidPromisesOf` · `onActorDied`。

### 5.3 `LedgerEntry` 权属定稿（C-23 关闭）

```text
LedgerEntry { entryId, promisorId, promiseeId, kind:"DELIVER"|"SERVICE"|"ABSTAIN",
              content: ItemSpec|ServiceSpec, qty: float, deadlineDay: int,
              witnessIds: ActorId[], state:"OPEN"|"MET"|"BREACHED"|"VOID",
              createdDay: int, reserved: bool, reasonCode?: ReasonCode,
              communityId: CommunityId,          // 【本份新增，见附录 B-2】
              voidedDayKey?: int }
```
**别名冻结（裁决 13 + 18 已拍板；S0 Part E 与 S4 §0 均已同步回填）**：字段集**以 S4 为准**；`id ≡ entryId` · `payload ≡ content + qty` · `dueDayKey ≡ deadlineDay`（旧名一律降为别名）；Part E 的 `PENDING/ACTIVE` **合并为 `OPEN`**，`state` 四态 = `OPEN|MET|BREACHED|VOID`；S3 §4.3 的 `PROMISED/FROZEN/FULFILLED/DEFAULTED/VOIDED` 是**预留实体**状态，以 `entryId` 外键关联，**禁止与 `LedgerEntry.state` 合并**。
> **`communityId` 已获批**（裁决 13，B-2 关闭）：作为 `LedgerEntry` 正式字段，用于按社区路由声望；无此字段则本份无法判断违约扣哪个社区。

---

## 6. 玩家可感知表现

1. **社区面板只有三个读数**：**信条**（3 条，违反的那条当日标红）、**产能**（`staffingRatio` + 哪个岗空着）、**缺口**（≤3 条 `GapSignal`）。没有第四个数字——这是防认知过载的第一道闸门。
2. **站队的代价是看得见的地图**：你在河谷升到 MEMBER 的那一刻，井窖仪表出现「明日生效 −24」，第二天井窖的过秤台多了一块查水牌、巡逻线延长。**你不需要读数值就知道自己少了一个地方可去。**
3. **两个社区的宵禁差 30 分钟**：21:00 井窖赶人、21:30 河谷分粮。这 30 分钟是每日都会发生的一次赶路决策，也是"两个社区真的不一样"最廉价的可感证据。
4. **信条冲突当场可见**：带着口粮进河谷、带着种子出井窖，门口的检查动作与 NPC 的一句提醒先于惩罚发生（P2：先看见，再承受）。
5. **"记得的事"会出现在对话里**：社区记得你做过什么，它不会因为数值衰减就改口——但它可以因为**新的事挤掉旧的事**而改口（≤5 条的淘汰可见于面板）。
6. **缺口板没有感叹号**：它写的是"公共仓 · 药 · 还差 0.3 剂 · 明天早上"。没有发布者，也没有"任务完成"。

---

## 7. 边界情况与失败模式

**E1 · NPC 死亡导致岗位空缺（必须含）**
`ActorDied` → Part E Phase 1 在下个 `HOURLY_TICK` 令 `onDuty −1` → 本份重算 `staffingRatio`。若 <0.60 → 社区进 `SCARCE`（退出需连续 2 日 ≥0.60）。次日 06:00 S2 从闲人池/heir 补岗；**连续 3 日补岗失败 → 本份裁撤该岗位（`rated → 0`）**，并把它升级为 `urgency 5` 的 `GapSignal`——**岗位消失本身成为最强的一条缺口**。
三条硬约束：① **不产生 `ReputationDelta`**（`--assert-estate-no-rep`）；② **必发 gossip**；③ `LedgerEntry` 转 `VOID`，promisee 转 `heirActorId(postId)`，无 heir → `NO_HEIR`。
**玩家占着岗位时死亡/离岗**：④ 照常执行（底座 Part E 末段），且若死于井窖宵禁后 → 额外发 `PostVacated{reason:CREED_BLOCK}`。

**E2 · 站队互斥把玩家逼到无处可去（必须含）**
判定条件：两社区 `band ∈ {HOSTILE, OUTLAW}`。此时**不得静默**——发 `AlignmentLockWarn` 并同时保证三条出口：① **旧政权设施黑市**（无信条地点，永不结算声望，§2.5）；② **赎罪仪式**（S4 #11 `creed_breaker` → 赎罪 intent，代价性仪式，一次只降一档）；③ **以工抵债**（承接 `ServiceSpec`，但受每日 `+3/+1/0` 递减约束，不可速刷）。
**预警（P3 硬要求）**：`OUTLAW` 之前必先经 `HOSTILE` ≥1 个完整游戏日，且 `RitualScheduled` 至少提前 1 日公告；**从 NEUTRAL 到 OUTLAW 数学上至少 4 日**，不存在无预告的不可逆。
**兜底断言**：若某日两社区均 ≥`HOSTILE` **且**黑市节点不可达（被区域事件封锁）→ 告警并在面板显示"泄压阀失效"。这是与 S2 §7-E5 同款的兜底断言，不允许静默。

**E3 · 声望同时被正负事件冲击（同日抵消）**
`Σ delta` 先求和再一次 clamp，因此"今天救了人又偷了水"是**净额**。但 `MemoryFact` **逐条独立写入**：好事与坏事**各自被记住**。这防止"用一次大额善行洗掉一次背信"成为主导策略（数值可抵，记忆不可抵）。

**E4 · 玩家同时是两社区成员（不应可能）**
`tier(A) ≥ MEMBER` 时发起 B 的准入仪式 → **直接拒绝**并明写"你已经是河谷的人了"。机制上用 `AllegianceTier` 唯一性保证：每 actor 最多 1 条 `tier ≥ MEMBER` 记录（SUPPLICANT 可并存，这正是"两边都还没定下来"的窗口期）。

**E5 · 违约时 promisee 已死亡但 gossip 源消失**
`witnessIds` 为空且 promisee 死亡 → `reasonCode = TARGET_DEAD`，`delta = 0`，仍发弱 gossip（`charge = 1`）。**不允许"人死光 = 免罚"，也不允许"无人见证却全村皆知"**（对齐 S4 §7-E5）。

**E6 · 信条违反在日切瞬间（跨宵禁）判定**
检查点在日切，故"21:29 离境、21:31 返回"由 `locationId` 快照决定，不由渲染帧决定。玩家在宵禁边界反复横跳 → 每日只判一次（`dayKey` 去重），**不叠加**。

**E7 · 缺口板空了（社区自给）**
`GapSignals` 为空 → 面板显示"今天他们自己够了"。**不是 bug**：C9 的诚实性来自它只报真实缺口，空板是世界在好转的证据。此时玩家的动线自然转向另一社区或拾荒（D1/D2）。

**E8 · `WATER_ENTRY_LIMIT` 与跑商主循环冲突**
玩家背 30 L 水进井窖只能带 9 L → 剩余封存。**封存不产生任何声望/经济后果**，出境原样归还（`WaterInTransit` 的 claimant 恒为玩家，任何人不得取用）。若封存期间玩家死亡 → 按 Part E ① 释放为 `UNCLAIMED`（可被抢夺）——这是"背水进井窖"的真实风险，且 1 跳可归因。

---

## 8. 验收标准与调试钩子

**验收**

1. **站队互斥可计算（A1，裁决 1 主验收）**：脚本化"在河谷完成 MEMBER 仪式" → 断言 ① `R_he_valley` 增 `+25`；② `R_jing_cell` 减 `−24`；③ 两笔均在 `DaySnapshot[D]` 且**同日**生效；④ `RitualScheduled` 早于仪式 ≥1 日。四者缺一即判失败。
2. **不可逆性（A2）**：`CreedRenounced` 后 10 游戏日，断言 ① `R_A ≤ −20`；② `R_B` **未回补**（与退队前之差 ≤ 0）；③ `MemoryFact{CREED_JOINED}` 仍在 ≤5 条内（`|delta|` 最大的一类，不衰减）。
3. **跨档限速（A3，P3 预告闸门）**：构造单次 `delta = −80` 的极端事件 → 断言被拆为 ≥2 次应用，且**任一单次日切 `|Δband| ≤ 1`**；`OUTLAW` 达成日前至少经过 4 次日切。
4. **死亡不扣声望（A4，裁决 4）**：`--kill <water 岗 NPC>` → 10 日内 `ReputationDelta` 事件数 = **0**，且 `GossipEmitted` 数 ≥1；`LedgerEntry` 转 `VOID`，`heirActorId` 正确。
5. **8 × reasonCode 分流（A5）**：对 8 个 `reasonCode` 逐一 `--force-breach` → 断言 `delta` 与 §3.4 表逐格一致；`DEATH/NO_HEIR/BOTH_DEAD/TARGET_DEAD` 的 `ReputationDelta` 数 = 0 且 gossip 数 ≥1；`TARGET_GONE` 的 gossip `charge == 1`。
6. **`LedgerEntry` 权属（A6）**：全仓检索——除本份外无第二处写 `LedgerEntry.state`；`--dump-ledger` 与 S4 `openLedgerEntries[]` 结果一致；别名冻结（`entryId/deadlineDay/OPEN`）无第三方言残留。
7. **缺口板（A7）**：脚本化制造河谷 MEDS 短fall → 断言 `GapSignal{cluster:MEDS, targetLocationId:"stock.he_valley.public"}` 出现且 `urgency ≥ 2`；补足后**次日自动消失**（无需"交任务"）；每社区每日 `≤3`。
8. **确定性与预算（A8）**：同 seed save/load 续跑 10 日，`max_d |ΔR| ≤ 1`（整数，理想 0）；本系统 raw `rng` 调用数 = 0；仿真预算 **≤ 0.3ms/帧**（纯事件驱动 + 日切批处理）。
9. **无第四套数值（A9）**：本系统对外暴露的玩家可见数值 = `{Reputation, staffingRatio}` 两个，且全部可在社区面板一屏内读完。
   > **裁决 21（PHASE2-CLOSING · B-1）**：**`GapSignal.urgency` 的数字不对玩家显示**。玩家侧只给可读语言（S7 §3：还行 / 明天紧 / 今天就要 / 有人在挨 / 已经在死），数字仅用于调试面板与 `--dump-gaps`。
   > 理由：① 数字一格一格地出现，会让缺口板变回"任务等级 + 感叹号"的熟悉形态，直接抵消 C9 的"无发布者"设计意图；② P2 要求世界读数**不用教学就能懂**——"有人在挨"比"urgency 4"更快被读懂；③ 玩家侧一旦有数字，就会开始为"刷高 urgency"而等待社区恶化，这违反 P4。
   > **UI GDD 须知**：面板与工作板不得渲染 urgency 数值，也不得用星级/条形图等任何可数化替身。

**调试钩子**

```text
--dump-community <cid>              三要素（信条/产能/缺口）+ staffingRatio + SCARCE/TIGHT 双状态
--dump-reputation <actorId>         两社区 R / band / pendingDelta / pendingDayKey / 分档趋势
--dump-memory-facts <cid>           ≤5 条"记得的事" + delta + 淘汰预测
--dump-posts <cid> [<dayKey>]       逐岗 rated/onDuty/nominalOut + 空岗 + seniority 序
--dump-ledger [<actorId>]           继承 S3/S4，增列 communityId 与 VOID 原因（本份为所有权方）
--dump-gaps [<cid>]                 GapSignal 全量 + 来源事件 + 目的地 + urgency 排序键
--force-align <cid> <tier>          构造站队（走完整仪式流程，禁用于正常流程）
--force-creed-violation <cid> <creedId> <sev>   构造信条违反，验证 §2.4 六卡
--assert-estate-no-rep              断言 DEATH 系 ReputationDelta 数 = 0 且 gossip 数 ≥ 1
--assert-band-step                  断言任一日切 |Δband| ≤ 1（P3 预告闸门）
--assert-ledger-ownership           断言除 S5 外无第二处写 LedgerEntry.state
--export-attribution <dayRange>     继承底座 §C8 规格
```

---

## 附录 A · 提请回填底座 §D 的候选常量（本份不就地生效）

| # | 候选常量 | 建议值 | 单位 / 说明 |
|---|---|---|---|
| A-1 | `REPUTATION_CLAMP` | [−100, +100] | 契约给定 |
| A-2 | `REP_BAND` | WARM ≥+25 · NEUTRAL −24..+24 · COOL −59..−25 · HOSTILE −89..−60 · OUTLAW ≤−90 | 5 档 |
| A-3 | `MEMORABLE_THRESHOLD` / `COMMUNITY_MEMORY_MAX` | 15 / 5 | 记入"记得的事"的 delta 阈值 / 条数上限 |
| A-4 | `TIER_GAIN` | SUPPLICANT +15 / MEMBER +25 / SWORN +40 | 准入仪式完成 |
| A-5 | `TIER_THRESHOLD` | 0 / +25 / +60 | 声望门槛 |
| A-6 | `ALIGN_CROSS_BASE` / `TIER_W` | 12 / 1,2,3 | 站队互斥基数 / 层级权重 |
| A-7 | `RENOUNCE_CAP` | −20 | 退队后 `R_A` 的 clamp 上限 |
| A-8 | `BAND_STEP_MAX` | 1 | 单次日切最多跨档数（P3 预告闸门） |
| A-9 | `RITUAL_NOTICE_DAYS` | 1 | 仪式最少提前公告天数 |
| A-10 | `BREACH_BASE` / `KIND_W` | 6 / DELIVER 1.0 · SERVICE 0.75 · ABSTAIN 0.5 | 违约定基 |
| A-11 | `REASON_MUL` | NO_STOCK 1.0 · NO_SHOW 1.0 · REFUSED 1.5 · 其余 0 | §3.4 |
| A-12 | `CREED_VIOLATION_BASE` / `SEVERITY_W` | 12 / 轻 0.5 · 中 1.0 · 重 1.25 | 信条违约定基 |
| A-13 | `TEND_REP_DAILY` | `+3 / +1 / 0` | 每日第 1/2/≥3 次顶岗（反刷分） |
| A-14 | `GAP_SIGNAL_MAX` | 3 | 每社区每日缺口条数（与 `PROMISE_MAX_ACTIVE` 同量） |
| A-15 | `WATER_ENTRY_LIMIT` | 9.0 | L；井窖入境限额 = `WATER_L_PER_PERSON_DAY × 3` |
| A-16 | `CURFEW_MIN` | `he_valley 1290` / `jing_cell 1260` | 21:30 / 21:00（社区静态配置字段） |
| A-17 | `REP_HINT_MAP` | FAIR +1 · GENEROUS +2 · HARD_BARGAIN −1 · PRESSED_LUCK 0 · REFUSED 0 · BAD_FAITH −3 | 承接 S3 §3.2 闭集 |

## 附录 B · 待主理人裁决 / 接口请求（9 项）

1. **B-1 · 日切第 7 步需细分（最高优先）**：请求底座在 S3 的 C-14（第 3 步细分）与 S4 的 C-28（第 7 步细分）之上，追加 **7.4 违约 delta 补批应用**。不加则违约后果延迟 2 日，与 S4 §3.6 表和裁决 3 不符。**本份已按"7.4 存在"书写。**
2. **B-2 · `LedgerEntry.communityId` 为本份新增字段**：用于按社区路由声望。**请确认接受**（否则本份无法判断违约扣哪个社区）。
3. **B-3 · `CreedId` 为新 ID 命名空间**（`he_valley.split_food_at_dusk` 等 6 个）：提请登记进底座 §D ID 命名规范。本份未就地生效。
4. **B-4 · `ClusterId` 增 `FOOD`（§0.1）**：本份已给出最终结论与回填规格。**请主理人据此回填 `00-foundation.md` §D、S3 §2.2（C-17 关闭）、S4 §5.3（C-26 关闭）**。
5. **B-5 · `PostRoster.posts` 长度可变（C-32）**：河谷 7 / 井窖 6。**请 S2 把 §5.3 的"槽位硬编码"改为读 `PostRoster(cid, dayKey)`**；`idle_00` 不计 `ratedPosts`（C-05 方案采纳）。
6. **B-6 · `seniority` 排序键确认**：本份确认采纳 S2 提案 `(入社区日 asc, actorId asc)`，供 Part E `heirActorId` 使用。**请主理人一并回填 Part E 的 stub 说明。**
7. **B-7 · `protectedActors` 与 LOD `PROTECTED` 同名单（C-27）**：本份支持取同一份名单，名单沿用 S4 §2.6（程九 / 药圃持有人 / 河谷取水首席），**唯一定义处建议登记进底座 §D**，S5 只消费不改。
8. **B-8 · 接口请求汇总**：
   - 请 **S2**：① 岗位编制改读 `PostRoster(cid, dayKey)`；② 消费 `CreedViolated` 写 `creed_breaker` 印象并发 gossip（本份不算传播）；③ 确认 `ImpressionTag` 存 12 码（C-24，S4 侧需求，本份支持）。
   - 请 **S3**：① 消费 `CreedRationRule`（两社区同值，本份不改）；② 确认 `TIGHT` 与 `SCARCE` 不合并指示灯（C-18）；③ 确认 `FOOD` 入 `Warehouse.stock` 与配给（C-19）。
   - 请 **S4**：① `LedgerQuery` 为本份所有，S4 只读且只经 `PromiseMade` 创建；② `CreedConstraints` 的最小三字段不变；③ 确认 `REFUSED` 的 `reasonMul = 1.5`（§3.4）。
   - 请 **UX/面板**：社区面板只呈现三个读数（信条/产能/缺口），**不要把 `SCARCE` 与 `TIGHT` 画成一个灯**。
9. **B-9 · 与并行 GDD 最可能冲突的三点（提请汇编时优先比对）**：
   - **食物是否可交易**：本份判 `FOOD ∈ ClusterId, tradable=false`。若 UI/关卡已按"六簇皆可交易"起草，会与 `PriceTable` 无 `FOOD.base` 直接对撞（S3 C-22 第三点，本份已关闭为"不可交易"）。
   - **`CreedRationRule` 是否应两社区分化**：井窖「留种」按叙事应把 SEED 提到前面，但主理人裁决 2 要求与 S3 一致 → **本份沿用 S3 的同值**。若主理人希望分化，建议井窖改为 `[WATER, FOOD, SEED, MEDS, FUEL]`，需 S3 §3.4 同步。
   - **"站队互斥"的实现位置**：S4 §2.3 的 `CREED` 类选项与 S3 §3.2 的 `reputationHint` 都在碰声望。**声望唯一实现处是本份 §3.1**；请确认 S4/S3 均不再自算 `ReputationDelta`（S3 已声明本期返 0，符合）。

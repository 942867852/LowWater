# S6 · Rumor-Network（传闻传播网络）· 系统设计文档

- **Task ID**：GDD-006｜**阶段**：Phase 2 · 收尾批（与 S7 并行）｜**优先级**：P0（D4 落地；P1「≤3 跳可归因」主要压力点）
- **主笔**：design-strategist（文策渊）｜**调度**：主理人（游承峰）｜**状态**：待拍板（附录 B 共 8 项）
- **依赖已读**：`game-concept.md`（§3.2-D4 / C8 / R1）· `06-consistency-review.md` · `00-foundation.md` · `02-npc-simulation.md` · `05-community-creed.md` · `03-economy-barter.md` · `04-dialogue-contract.md`
- **依赖方向（严格单向）**：依赖 S0·B / S0·C / S2（种子与失真）/ S4（`GossipSeed` 入口）；对 S5 只发事件，不算声望。

## 0.0 规范系统编号对照表（裁决 C-13）

| 码 | 系统 | 文件 |
|---|---|---|
| **S0** | 底座（`S0·A`判定/`S0·B`时钟/`S0·C`确定性/`S0·D`常量/`S0·E`死亡清算） | `00-foundation.md` |
| **S1** | 稀缺主循环 | `01-scarcity-loop.md` |
| **S2** | NPC 三层模拟 | `02-npc-simulation.md` |
| **S3** | 易货经济与社区仪表 | `03-economy-barter.md` |
| **S4** | 记忆驱动对话与契约账本 | `04-dialogue-contract.md` |
| **S5** | 社区、信条与声望 | `05-community-creed.md` |
| **S6** | **传闻传播网络（本文档）** | `06-rumor-network.md` |
| **S7 / S8 / S9** | 工作板 / 区域内容 / 呈现（待写） | — |

## 0. 全局契约与职责边界

### 0.1 契约（逐字继承，不改字段名）

```text
GossipEmitted { originActorId, topicActorId, claimCode, dayKey }        // S2 → S6（种子）
RumorInjected { topicActorId, claimCode, distortionLevel, hop }         // S6 → S2 记忆 / S5
ReputationHint                                                          // S6 → S5（语义，见 0.3）
停止条件 4 条：hop = 3 / 命中 terminal 码 / charge ≤ 0 / 去重合并
```

### 0.2 职责边界（主理人裁决，越界即返工）

| 归 **S2**（上游，本份不重做） | 归 **S6**（本份） |
|---|---|
| gossip **种子的生成**（`P_FOUND_LOOTED`/`P_CORPSE`/`SET_TRAP`/`CreedViolated`/`PromiseBreached`/Part E/S4 `GossipSeed`） | **传播拓扑**：谁传给谁（fanout ≤3 跳） |
| **失真码表 8 码 `claimCode` 的定义**（唯一定义处 S2 §3.7） | **跳数控制与衰减**、**注入节奏** |
| **失真方向打分**（印象符号 / dominant need / 社区 `tithe`·`canShelterOutsider` 三项加权，平手取边表索引升序，**零随机**） | 跨路径**去重与合并**、**回环检测** |
| `ImpressionTag` / `MemoryEvent` 的存储与淘汰键 | 落地到接收方记忆与 **S5 `ReputationHint`**、**停止条件**执行 |

> **所有权迁移声明（本份最重要的边界）**：S2 §3.7 现自持 `GossipUnit.reachedSet` / fanout / inbox 配额（S2 §0.0 亦写"gossip 传播本份自持"）。按主理人裁决，**这三处所有权迁至 S6**；S2 §3.7 保留"种子生成 + 失真码表 + 失真打分"，其余改为引用本份。**请汇编时回填**（R-8）。
> **本份不做的三件事**：不新建第 9 个 `claimCode`；不重写失真方向规则；不重定义种子如何产生。

### 0.3 关于 `ReputationHint`（撞名风险，本份主动规避）

S3 §3.2 **已存在** `reputationHint`（交易建议，闭集 `FAIR|GENEROUS|HARD_BARGAIN|PRESSED_LUCK|REFUSED|BAD_FAITH`）。为不造第二个同名实体，本份**不新增 `ReputationHint` 事件**：S6 发 `RumorInjected`，由 **S5 §3.1 事件值表**既有那一行（"`GossipEmitted` 到达 actor a → `sign(claim) × min(|charge|,3)`，归属 `a.communityId`"）承接，**算式一字不改，只换事件名**。即"S6 → S5 的 `ReputationHint`" = `RumorInjected` 承载的该行语义。

## 1. 系统概览与目标

### 1.1 定位

S2 让 NPC 看见你做了什么，S4 让那件事变成一句话，S5 让那句话变成你的处境。**S6 只负责一件事：让那句话在你不在场时自己走、自己变、自己走到你前面去。**

- **支柱**：**P1（主）**——每条传闻 ≤3 跳落到"某个有名有姓的人说的"，路径可导出为归因链；**P3**——你睡觉时它照样跑，隔夜就变；**P2**——每早 06:00 走一步，节奏固定到玩家能掐点去听；**P4**——失真不可逆，澄清有代价，沉默即承认。
- **动词**：**读 OBSERVE**（偷听＝传闻唯一观测手段）· **说 PERSUADE**（澄清 / 主动放话）。本系统是这两个动词的**唯一发生场**。
- **反目标**：NPC 两两全交互；匿名参与传播；全揭示的传闻地图（P1 否决上帝视角 UI）；"一键洗白"的传播操纵。

### 1.2 一句话职责

> 让一句关于你的话，在你走不到的地方、用你控制不了的措辞、以每天一跳的节奏走到你前面——并给你恰好三个早晨去追上它。

### 1.3 四条自建红线（比裁决更严）

| # | 红线 | 理由 |
|---|---|---|
| R-A | **raw RNG 调用数 = 0**；已登记的 `gossip.*` / `estate.*.gossip` 两个 `streamId` **不被取用**（counter 恒 0） | 继承 S2 R-B 最严读法，保住"raw RNG stream = 0" |
| R-B | **批处理两阶段（PLAN 只读 → APPLY 批量写）** | 本批新到达者不得本批再发 → ≤3 跳闭合是结构性质 |
| R-C | **LOD 不参与传播判定**：FULL 与 COARSE 一视同仁 | 否则玩家一走传闻就断（违反 P3），且 LOD 成不可复现差异源 |
| R-D | **图谱对玩家只有已观测部分可见**（fog） | P1 否决全揭示上帝视角；未观测环节显示 `???` |

## 2. 核心概念与数据模型

```text
RumorSeed    { seedId, originActorId, topicActorId, claimCode0, charge0,
               bornDayKey, kind, causeRef?, holdDays }                   // S2 产出，S6 持有
GossipUnit   { topicSeedId, topicActorId, claimCode, charge, hop:0..3,
               reachedSet: ActorId[] }                                    // 沿用 S2 §2.2，所有权迁 S6
HoldState    { claimCode, charge, distortionLevel }                       // 每个已到达者"手里那句话"
RumorInjected{ topicActorId, claimCode, distortionLevel, hop }             // 强制签名（逐字）
RumorDelivery{ recipientActorId, seedId, relayActorId, charge, dayKey }    // 递送包装（本份新增）
ContactGraph { (a,b) → contactCount:int }                                  // 现算，不持久化
```

- **`hop`** = 跳数（`1..3`），即该传闻自种子起已过的 DISPATCH 批次数。
- **`distortionLevel`（失真度）** = 自种子起累计**码变次数**（`0..3`）。与 `hop` 分离是刻意的：`hop=3/distortionLevel=0` 是"传了三人一个字没变"；`hop=2/distortionLevel=2` 是"只传两人口味全变"。供 S4 选台词强度、供面板排序。
- **`reachedSet`** = **接收者集合，不含 root**；`|reachedSet| ≤ GOSSIP_REACH_MAX(7)`（S2 A5 已定"≤7"，本份明确其语义）。**`visited` = `{root} ∪ reachedSet`**，单调增长，每 seed 一份。
- **种子 `kind` 闭集**（沿用既有来源，不新增）：`WITNESS_LOSS`/`CORPSE`/`TRAP`/`CREED`/`PROMISE_BREACH`/`PROMISE_VOIDED_BY_DEATH`/`NO_HEIR`/`BOTH_DEAD`/`TARGET_DEAD`/`TARGET_GONE`/`PLAYER_SEED`。

## 3. 规则与公式

### 3.1 传播拓扑：谁有资格当中继者

```
relayEligible(r, D) ⇔ alive(r,D)
                     ∧ ( homePostId(r) == "<cid>.idle_00"
                         ∨ ∃ block ∈ getSchedule(r, D−1) : block.intentTag == "SOCIAL" )
```

- **只有 `idle` 岗、或昨日日程有 `SOCIAL` 块的人能当起播根与中继者**——"干活不传话"（继承 S2 §3.7）。Core 期 `idle_00` 每社区各 1 人（S5 §2.1：`he_valley 9−8=1`、`jing_cell 7−6=1`），**每社区每天至少一个合法发源地**，传播不会结构性断流。
- 资格一律读**昨日（`D−1`）日程**：S2 的 DISPATCH ①（gossip 注入）早于 ④（生成当日 blocks），注入时当日日程尚不存在。语义也自洽——"他昨天有空闲聊，所以他今天早上把话带出去"。

### 3.2 接触图（谁可能跟谁说话）——现算，不持久化

```text
contactCount(a,b,D) = |{ (ba,bb) : ba ∈ getSchedule(a,D).blocks, bb ∈ getSchedule(b,D).blocks,
                          ba.locationId == bb.locationId, overlap(ba,bb) ≥ GOSSIP_CONTACT_MIN_MIN(10) }|
```

- **只在有名 actor 之间算**（N=16）。匿名**不参与传播**——无 `ImpressionTag`、无长期印象，且 R1 明令禁止 NPC 两两全交互。
- 上界：`C(16,2)=120` 对 × `≤8×8=64` 次区间比较 ≈ **7,680 次/日**（约 0.03ms）。故**不进 `DaySnapshot`、不新增日切步骤**——S6 是消费者型系统，只消费 `getSchedule`。
- **跨社区接触**只在共享 `locationId` 上发生（Core 期主要是被弃的旧政权设施＝无信条地点，S5 §2.5），故跨社区传闻天然稀有。

### 3.3 fanout：每跳挑几个人、按什么顺序

```text
cands = [ c | c ∈ contacts(r) ∧ c ∉ unit.visited          // 回环检测（含 root 与全部已接收者）
              ∧ c != seed.topicActorId                    // 本人不会"听说"自己的事
              ∧ alive(c,D) ∧ inboxCount[c][D] < GOSSIP_INBOX_MAX(2) ]   // 满额者不入选
sort  = ( sameCommunity(r,c) desc, contactCount desc, actorId asc )
take  = min( GOSSIP_SPREAD_PER_HOP(2), GOSSIP_REACH_MAX(7) − |unit.reachedSet| )
picked= 顺序取前 take 个，其中跨社区者 ≤ GOSSIP_CROSS_PER_HOP_MAX(1)
```

- **fanout = 2 恒定**（继承 S2 §A-13），不随 hop 放大。
- **同社区优先**是确定性主键：先传遍本社区，本社区无未接收者才外溢。既压规模，又让"跨社区"成为**事件**而非常态。
- **排序键确定性证明**：`sameCommunity`(bool) / `contactCount`(int) / `actorId`(字典序) 构成全序，无需额外 tiebreak。
- **回环检测不是启发式**：`visited` 单调且候选必须 `∉ visited`；`src(relay)`（你听来的那个人）必然已在 `visited` 中，故"绕回原主"**在集合层面被排除**。

### 3.4 跳转判定与失真（调用 S2，不重写规则）

```text
for c ∈ picked:
    held      = unit.hold[r]                             // 中继者手里那句话
    newCode   = S2.distort( held.claimCode, r, D−1 )      // 零随机；无出边 → 返回原码
    newCharge = held.charge − Q_HOP_DECAY(1)
    if newCharge ≤ 0 → 停止条件③（该分支终止，不入队）
    newDist   = held.distortionLevel + (newCode ≠ held.claimCode ? 1 : 0)
    push PLAN{ seed, relay:r, recipient:c, claimCode:newCode, charge:newCharge,
               hop: unit.hop+1, distortionLevel: newDist }
```

- **失真由中继者偏差决定，不由接收者决定**——"是传话的人把它说歪的"。三项打分与"平手取边表索引升序"**全归 S2 §3.7**，本份只调用（R-5）。
- `distort` 返回原码**不是**停止条件，只是这一跳没变味。

### 3.5 停止条件（4 条，逐字遵守）

| # | 条件 | 判定点 | 语义 |
|---|---|---|---|
| ① | `hop = 3` | 批开头：`unit.hop ≥ GOSSIP_FANOUT_MAX(3)` → 退役 | 硬停 |
| ② | 命中 terminal（`KIN`/`OUTLAW`/`MARTYR`） | 到达后检查 `newCode` | 谣言有终点，自然收敛 |
| ③ | `charge ≤ 0` | 每跳衰减后 | 越传越没劲，最后没人再提 |
| ④ | **去重合并** | APPLY 阶段 | 见 3.6 |

> **实现期前置判定（不改 4 条语义，只是其机械前提）**：`cands` 为空（接触者皆满额/已 visited/已死）→ 在 ④ 的意义上无未合并接收者 → 终止；`|reachedSet| ≥ 7` → 同归 ④。**不新增第 5 条。**

### 3.6 跨路径去重与合并（④ 完整规则）

- **同 seed 内**：同一 `(seedId, recipient)` 被两条边命中 → 取 `(hop asc, charge desc, relayActorId asc)` 首条为 canonical，其余记 `MERGED`（**只计一次到达，不增加后继**）。
- **跨 seed（印象层）**：注入前查 `getMemoryView(c).impressions`，若 `∃ imp : imp.topicActorId == seed.topicActorId ∧ collapse(imp.tagCode) == newCode ∧ |imp.charge| ≥ newCharge` → **`MERGED_AND_STOP`**（不注入、不写记忆、不产生 delta、不作下跳中继）。语义："他早就这么认为了，这句新话没信息量"。
- **inbox 配额**：每 actor 每日入站 `≤ 2`。满额者**在候选筛选阶段即被排除**（3.3），而非"选上再丢弃"，把无谓丢弃降到最低。仍会发生的丢弃按 `ESTATE > charge desc > topicActorId asc`（继承 S2 §3.7）。**丢弃不补，但必计 INFO 日志且面板可见**（S2 E3：静默丢弃＝无从调试）。

### 3.7 注入节奏（T 日生成 → T+1 06:00 起，每日一跳）

```text
hop h 的注入时点 = Day(bornDayKey + h) 的 DISPATCH(06:00)
所有跨系统读数一律取 DaySnapshot[D − 1]（D = 注入日）
```

**默认 `GOSSIP_HOPS_PER_DAY = 1`（一日一跳）**，理由：① **P2**——06:00 是"整个世界的标点符号"，每天清晨一个可听节点，玩家能掐点去听；② **可干预窗口**——D4 的"澄清误会"只有在"还有下一跳可以抢"时才成立，一日一跳给玩家**恰好三个早晨**；③ **与既有契约同构**——Part E"第二天清晨才知道"要求的正是 hop1=T+1，S4 §3.6"D+1 06:00 传播 → witness 与同社区 2 人获印象"描述的正是 hop1 的 fanout=2；二者都不要求三跳同批。

> **待裁决 R-2**：S2 §3.7 原文"T+1 06:00 批量、分层执行"可读作"三跳同批跑完"。若采此读法，把 `GOSSIP_HOPS_PER_DAY` 改为 **3** 即可，**本份其余算法与闭合性证明完全不变**；代价是丧失中间干预窗口（传闻一天早上瞬移完毕）。本份推荐 1。

**种子持有（hold）**：root 在注入日不合格且不属 `idle` → 顺延重试，`holdDays > GOSSIP_SEED_HOLD_MAX(2)` 则丢弃并记日志（"没人再提起这件事"）。**Part E 死亡系豁免丢弃**（必发，见 7-E1）。

### 3.8 落地：写什么到 S2 记忆与 S5

```text
每条到达 →
  ① MemoryEvent{ kind:"RUMOR_HEARD", refId:"S6.seed.<seedId>", actors:[topic, relay],
                 dayKey:D, weightQ = MEMORY_SHORT_DAYS − (today − dayKey) }      // 短期，7 天
  ② ImpressionTag（受 GOSSIP_IMPRESSION_MAX_PER_ACTOR(2) 限制，见下）
  ③ RumorInjected{...} + RumorDelivery{...} → S5 算 delta
  ④ AttributionLink{ step: hop(≤3), causeRef:"S6.seed.<seedId>",
                     effectRef:"S2.memory.<recipient>", actorIds:[topic, relay, recipient], dayKey }
```

**8 码 → 12 码默认映射**（C-24：印象存 12 码；传播时据此反查道听途说落成哪条细分）：

| claimCode | 默认 `tagId`（S4 §2.5 #） | 注 |
|---|---|---|
| `WATER_GIFT` | `gave_water`（#1） | #2/#3/#4/#5 均 collapse 至此，反向不可判定 → 取最朴素 #1 |
| `HIRELING` | `broke_word`（#10） | 沿用 S4 C-24 已提请确认的映射 |
| `KIN` / `MARTYR` | `kin_claim`（#6）/ `martyr_claim`（#7） | terminal |
| `WATER_DEBT` / `THIEF` / `CREED_BREAKER` | `water_debt`（#8）/ `stole_well_water`（#9）/ `creed_breaker`（#11） | |
| `OUTLAW` | `outlaw_claim`（#12） | terminal |

```text
道听途说占印象槽上限：|{ imp : imp.sourceHop > 0 }| ≤ GOSSIP_IMPRESSION_MAX_PER_ACTOR(2)
超出 → 只写 MemoryEvent，不写印象；terminal 印象（#6/#7/#12）不受限且按 S4 E4 永不淘汰
```

> **为什么必须设这道闸门**：≤5 条长期印象是 S4 对话分支的**唯一条件源**。若每次传闻都写印象，玩家一天的行为会让 7 个 NPC 各记一条道听途说，真实亲历（`sourceHop=0`）标签被噪声挤掉，`CALLBACK` 直接失准——认知过载 + 对话污染双重失败。
> **自我调节**：道听途说 `charge` = 原值 − hop，天然低于亲历值；S2 淘汰键 `sort(charge asc, writtenDayKey asc, sourceActorId asc)` 让传闻印象**最先被挤掉**。

### 3.9 为什么 ≤3 跳闭合是结构性质（裁决 3 要求的证明）

1. **两阶段批处理**：PLAN 只读 `arrived_{h−1}`（在本批 APPLY **之前**已固定），APPLY 才写 → **同一 DISPATCH 内每 seed 恰好推进 1 跳**，不存在"新到达者同批再发"的链式展开。
2. **`hop` 是 seed 上的单调整数**，每 DISPATCH 恰好 +1，`≥3` 即退役 → 生命周期 ≤ 3 次 DISPATCH。
3. **`visited[seedId]` 单调增长**且候选必须 `∉ visited` → 传播图是以 root 为根、**深度 ≤3 的分层 DAG，不可能成环**（集合判定，非启发式）。
4. **宽度有硬顶**：每跳 `take ≤ 2`、`|reachedSet| ≤ 7`、每人每日入站 ≤2 → **节点数 ≤ 1+7 = 8**。
5. **复杂度 `O(S·H·R·C)` 而非 `O(N^H)`**：`S`=种子数、`H`=3、`R`≤7、`C`≤15；fanout 是**常数 2** 且宽度被 `reached` 约束 → **不存在指数爆炸**。

### 3.10 Core 期规模上界（16 有名 NPC 不炸 6ms 的证明）

| 量 | 上界 | 依据 |
|---|---|---|
| 参与节点 `N` | **16**（匿名不参与） | §3.2 |
| 单日种子数 `S` | `16 × GOSSIP_SEED_MAX_PER_ACTOR_DAY(2)` = **32** | A-3 |
| **单日 `RumorInjected`** | `16 × GOSSIP_INBOX_MAX(2)` = **32** ← **绑定约束** | §3.6（比 `S×7=224` 更紧） |
| 接触图构建 | 120 对 × 64 次比较 ≈ **7,680 次** | §3.2 |
| PLAN 候选评估 | `32 × 3 × 7 × 15` ≈ **10,080 次整数比较** | §3.9-5 |
| `distort`/印象写/归因 | 各 **≤32 次** | §3.8 |
| **单次 DISPATCH gossip p95** | **≤1.5ms**（约 1.8 万次整数运算 + 百次结构写） | A-10 |
| **摊销到每帧** | `1.5ms / 1440 SIM_TICK` ≈ **0.001ms/帧** | 只在 06:00 跑一次 |

> 相对 S0 §C3 的 **6ms/帧**预算，gossip 摊销 ≈ **0.02%**；占用 S2 §8 已列"DISPATCH/日切/gossip 批处理 0.15ms"中的 ≤0.10ms，**不申请新配额**。
> **常态远低于上界**：正常日只有零星种子（一次被盗/违约/死亡），单日注入预期 **4–10 条**；32 条是"世界极度喧嚣"的最坏日。

## 4. 状态与流程

### 4.1 完整时序（画在日切九步与 DISPATCH 的哪一步）

```text
T 日 任意 tick / HOURLY_TICK
      S2 产种子 → GossipEmitted{originActorId, topicActorId, claimCode, dayKey:T}
      S6 收进 pendingSeeds，按 (bornDayKey asc, seedId asc) 排序         ← 确定性
      当日不传播（"当日不多发"，继承 S2 §4.1）
T 日 日切 00:00
      [第 6 步] Part E Phase 2 → 死亡系种子入 pendingSeeds（bornDayKey = T）
      ★ S6 在日切九步中【不占步骤】：本份无日切逻辑，种子只是被冻结
T+1 DISPATCH 06:00  ← 挂在 S2 §4.1 六步的【第 ① 步】，早于 ②③④⑤⑥
      ①a contact = buildContacts( getSchedule(*, T) )                    // 读昨日
      ①b 对每个 pendingSeed（hop h = D − bornDayKey）：PLAN → APPLY → 发 RumorInjected
          → 写记忆/印象/AttributionLink；死亡系（ESTATE）优先处理
      ②…⑥ S2 原六步（产出结算 / 岗位 / 生成 blocks / ScheduleChanged / PostVacated）
T+2 06:00 → hop 2；T+3 06:00 → hop 3 → 停止条件①退役
```

> **为什么必须在第 ① 步、且在 ④（生成当日 blocks）之前**：注入只依赖**昨日**日程与昨日快照，故不冲突；若放到 ④ 之后，传播就依赖当日尚未发生的社交块，"你今天能不能听到那句话"变成不可预判，直接违反 P2。

### 4.2 批处理伪代码（两阶段，零随机）

```
GOSSIP_DISPATCH(D):
  snap = DaySnapshot[D-1];  inbox = {};  plan = []
  contacts = buildContacts({ a: getSchedule(a, D-1) | a ∈ NAMED, alive(a,D-1) })

  # 阶段 1 · PLAN（只读，不改任何状态）
  for seed in pendingSeeds.sortedBy(bornDayKey asc, ESTATE-first, seedId asc):
      unit = unitOf(seed)
      if unit.hop >= GOSSIP_FANOUT_MAX: retire(seed, STOP_HOP); continue
      senders = (unit.hop == 0) ? [resolveRoot(seed)]                  # 见 7-E1
                                : [ r for r in arrivedAt(seed, unit.hop) ]
      for r in senders.sortedBy(actorId asc):
          if !relayEligible(r, D-1): continue
          cands = [c for c in contacts[r] if c ∉ unit.visited and c != seed.topicActorId
                   and alive(c,D) and inbox[c] < GOSSIP_INBOX_MAX]
          cands.sortBy(sameCommunity(r,c) desc, contactCount desc, actorId asc)
          take = min(GOSSIP_SPREAD_PER_HOP, GOSSIP_REACH_MAX - unit.reachedSet.size)
          for c in selectWithCrossCap(cands, take, GOSSIP_CROSS_PER_HOP_MAX):
              held = unit.hold[r]
              newCode   = S2.distort(held.claimCode, r, D-1)            # 零随机
              newCharge = held.charge - Q_HOP_DECAY
              if newCharge <= 0: mark(seed, STOP_CHARGE); continue
              plan.push(PlanItem(seed, r, c, newCode, newCharge, unit.hop+1,
                                 held.distortionLevel + (newCode != held.claimCode ? 1 : 0)))

  # 阶段 2 · APPLY（批量写；本批新到达者不得在本批再发）
  for item in dedupeByRecipient(plan, key=(hop asc, charge desc, relayActorId asc)):
      if isMergedAndStop(item): log(MERGED);  continue                  # 停止条件④
      if inbox[item.c] >= GOSSIP_INBOX_MAX:   log(DROPPED); continue
      emit RumorInjected{ topicActorId, item.newCode, item.distortionLevel, item.hop }
      emit RumorDelivery{ item.c, seed.seedId, item.r, item.newCharge, D }
      unit.visited += item.c;  unit.reachedSet += item.c
      unit.hold[item.c] = HoldState(item.newCode, item.newCharge, item.distortionLevel)
      if isTerminal(item.newCode) or item.hop >= 3: markBranchTerminal()
      inbox[item.c] += 1;  writeMemoryAndImpression(item);  recordAttribution(item)
```

### 4.3 状态机

```text
SEED_PENDING ─(T+1 06:00, root 合格)─► HOP_1 ─► HOP_2 ─► HOP_3 ─► RETIRED(①)
     │                                    └─► TERMINAL(②) / CHARGE_OUT(③) / MERGED(④)
     └─(root 不合格)─► HOLD(≤2 日) ─► 过期丢弃 + INFO 日志（ESTATE 系豁免）
```

## 5. 对外接口

### 5.1 暴露

| 接口 / 事件 | 消费方 | 用法与注意 |
|---|---|---|
| **`RumorInjected{topicActorId, claimCode, distortionLevel, hop}`** | **S2**（写 `MemoryEvent`+`ImpressionTag`）、**S5**（算 `ReputationDelta`）、**S4**（`CALLBACK` 条件源） | 强制 4 字段逐字；**每条到达发一次** |
| **`RumorDelivery{recipientActorId, seedId, relayActorId, charge, dayKey}`** | **S5**（唯一据此定 `communityId` 与 `charge`）、面板 | 本份新增递送包装（R-1） |
| 事件 `RumorSeeded{seedId, topicActorId, kind, bornDayKey}` | 面板 | 只作可观测，不驱动逻辑 |
| `rumorGraph(seedId)` / `pendingSeeds(dayKey)` / `injectedOn(dayKey)` / `dropStats(dayKey)` | 面板、`--export-attribution` | 图谱**对玩家侧输出前必须过 fog 过滤**（R-D）；丢弃与合并计数必须可见 |
| `contactOf(a,b,dayKey) -> int` | S2（建议复用，R-7）、面板 | 纯函数，零随机 |

### 5.2 依赖

- **S0·B**：`subscribe(DISPATCH)` · `now()` · `enqueue` · `getDaySnapshot(D)`（唯一跨系统读数源）· `absTick`。
- **S0·C**：`recordAttribution`（每条到达一条，`step = hop ≤ 3`）。**raw `rng` = 0**（R-A）。
- **S2**：`GossipEmitted`（收）· `getSchedule`（接触与资格）· `getMemoryView`（跨 seed 去重）· `getActorState`（`alive`/`communityId`/`homePostId`）· **`distort(claimCode, relayActorId, dayKey)`**（请求暴露，R-5）· `PostRoster`（`idle_00`）。
- **S4**：消费 `RumorInjected`（`CALLBACK` 与质问分支）；**接收 `GossipSeed`**（玩家放话唯一入口，S4 §3.3 既有 `EffectExpr`，不新增变体）；偷听走既有 `RevealKnowledge{refId}`（不新增事件）。
- **S5**：发 `RumorInjected` + `RumorDelivery`；**S6 不算声望**。

### 5.3 stub（明示为未完成）

- **匿名 NPC 不参与传播**（≤60 匿名只作背景与竞争拾荒）；Extended 若开放，需先证明 `reachedSet ≤ 7` 不被击穿。
- **跨社区接触点**依赖 S8 定义共享 `locationId`（Core 期仅"被弃的旧政权设施"）。
- `distort()` 返回值与打分明细由 S2 提供；本份只调用并**原样记入面板**。
- `MemoryEvent.kind` 新增 `RUMOR_HEARD`，提请登记进 S2 的 kind 闭集（R-4）。

## 6. 玩家可感知表现（含"为什么玩家会想追查一条传闻"）

### 6.1 玩家怎么"看见"自己的名声在传播

1. **偷听是唯一观测手段**：两名 NPC 在 `SOCIAL` 块交谈必漏出一句（S2 §6.6）。想听清需先过 `VIGOR/stealth, DC_DEMANDING`（S4 §3.2 已有该行）；成功 → 点亮图谱一条边（`RevealKnowledge{refId:"S6.seed.<id>.edge.<r>→<c>"}`）；**大失败 → 被发现，`safety −8` 且不可开启对话**。
2. **隔天开始，陌生人的第一句话变了**：没见过你的 NPC 用 `CALLBACK` 或拒绝句开场（"我听说你答应过他三升水"）。这是传闻**唯一可见后果**，也是 P1"落到具体的人"的兑现。
3. **情报日志「传闻」页（≤5 条，7 天窗口）**：每条显示「你听到的话 / 你以为的真相 / 说话人 / 第几跳（已知才显示）/ 上游 `???`」。**不给聚合数值**——那是 S5 声望面板的活，本份禁止做成第二条声望条（P1 否决全局声望条）。

### 6.2 为什么值得追查（三条动机）

| 动机 | 机械落点 | 为什么现在就得去 |
|---|---|---|
| **它会自己恶化** | 每跳 `distort()` 由中继者偏差**确定性**决定，玩家不可控；terminal 印象**永不淘汰**（S4 E4） | 不追查的代价是**永久的**：`outlaw_claim` 永久关闭该社区全部非敌对入口 |
| **它有死线** | hop 3 后停止传播，但 `OUTLAW` 印象永驻 | 一日一跳 = **恰好三个早晨**，每一天都真的少一天 |
| **它改变你能不能进门** | 到达 → S5 `sign × min(\|charge\|,3)` → 分档 → 准入门槛（S5 §3.2） | 失真到 `OUTLAW` 会让**地图变小**，这是 P4 最锋利的形态 |

### 6.3 三条可执行路径

**① 澄清误会（PERSUADE）**——NPC 以某传闻**当面质问**时（由 `RumorInjected` 生成的 `CALLBACK`）：`[默认否认]` / `[言辞 DC_DEMANDING] 解释` / `[携带 3.0 L 水] 当场补偿` / `[保持沉默]`。**沉默 = 默认承认 → `charge +1` 并发一条新种子**（"他自己都没否认"）——"说"第一次可能是负收益。**澄清成功只改印象，不删传闻**：已传到第三人脑子里的那句话不会因你说清楚而消失（P1 记忆独立 + P4 代价已产生）。**大失败必发 `GossipSeed`**（"他跑来跟我辩了半天"）→ 越描越黑，写入验收 A6。

**② 主动放话（玩家唯一能"用"传播系统的入口）**——经 S4 §3.3 既有的 `EffectExpr.GossipSeed{claimCode, topicActorId, charge}` 入 `pendingSeeds`，**T+1 06:00 起播**，不新增变体。**主导策略反制三重**：① 同样要过 `distort()`（不可控）；② 栽赃需先过 `PRESENCE/persuade`，被识破 → `broke_word`/`creed_breaker`；③ `charge` 每跳 −1，效力天然衰减。呼应 S5 §3.6「用 gossip 洗白」的反制条款。

**③ 利用失真（不澄清，甚至顺水推舟）**——一条歪成 `OUTLAW` 的传闻在 A 社区是灾难，在与 A 互斥的 B 社区可能是入场券（S5 §3.3 站队互斥是算术）。玩家可主动不去澄清，到 B 社区用"我就是他们说的那个人"开门。这是 D4 给 Socializer/Explorer 的最高价值时刻，也是"失真"从惩罚变成**资源**的唯一出口。

## 7. 边界情况与失败模式

**E1 · 传闻主体已死亡（必须含）**

| 子情形 | 规则 |
|---|---|
| `topicActorId` 在 T 日死亡（Part E 系） | 传播**照常**；`topic` 本人永不接收（`c != topicActorId`）；**S5 侧 `delta = 0`**（R-6，否则击穿 `--assert-estate-no-rep`）。这是"账本不罚你，但闲话照传"的执行形式 |
| root 在起播前死亡 | **不得丢弃**（Part E ②"必发 gossip"）。依次尝试：① `heirActorId(root.postId)`；② 同社区 `idle_00`；③ 同社区 `seniority` 首位存活者（`(入社区日 asc, actorId asc)`）。三者必居其一；全社区无人 → 丢弃并**告警**（E6） |
| 中继者在两跳之间死亡 | 该分支终止，**不补人**；`charge` 不再衰减（没传出去就不衰减） |
| 接收者在注入日已死 | 从候选集排除（`alive(c,D)`），不占 fanout 名额 |

**E2 · 传播对象不在同一社区（必须含）**

- **规则**：`sameCommunity` 是排序主键（§3.3）；跨社区只在同社区无未接收者时发生；每跳跨社区名额 `≤ 1`，且跨社区边需 `contactCount ≥ GOSSIP_CROSS_COMMUNITY_MIN_CONTACT(2)`（"见过不止一次"才谈得上闲话）。
- **后果**：跨社区到达 → S5 记在 **`recipientActorId.communityId`** → 这正是"**名声先你一步到达**"的机械落点：你还没走到井窖，井窖已听说河谷的事。
- **无人可传**：某跳 `cands` 为空（同社区全已接收且无跨社区接触）→ 归 ④ 终止，**不是 bug**（谣言在小社区传遍了）。
- 接收者社区 `canShelterOutsider == false`：不改本份规则；它已作为 S2 失真图 `CREED_BREAKER → OUTLAW` 边的偏置条件生效。

**E3 · inbox 配额溢出**：满额者不入选；仍发生的丢弃按 `ESTATE > charge desc > topicActorId asc`；**丢弃必计日志且面板可见**。
**E4 · 同日多 seed 争抢同一 actor**：inbox 全局计数（跨 seed），先到先得；跨 seed 去重优先于配额竞争（已有同类更强印象者根本不进候选）。
**E5 · 玩家作为 topic**：`topicActorId == "player"` 时玩家**没有** `ImpressionTag`；到达只产生 S5 `Reputation(cid,"player")` 的 delta 与到达者本人的 `MemoryEvent`；玩家**永不作为接收者**。
**E6 · 社区消亡 / 全员死亡**：`cands` 与 `senders` 皆空 → `holdDays > 2` 后丢弃并**告警**（与 S2 §7-E5、S5 §7-E2 同款兜底断言：不允许静默）。
**E7 · root 迁居/失踪（`FLED`）**：等同 E1 第二行，但**不触发 `heirActorId`**（人还在，只是走了）→ 直接落到同社区 `idle_00`。
**E8 · `distort()` 返回原码（无出边）**：**不是停止条件**，只是这跳没变味，`distortionLevel` 不加——防止"某码无出边 → 传闻神秘消失"。
**E9 · 同日到达两个社区**：合法且是设计意图；S5 各记各的社区，**互不抵消**（S5 E3：数值可抵，记忆不可抵）。

## 8. 验收标准与调试钩子

### 8.1 验收

1. **A1 · 有界传播**：任一 `seedId` 链长 `≤3`；单 actor 单日入站 `≤2`；`|reachedSet| ≤ 7`；**丢弃/合并/终止原因计数面板可见**（静默丢弃判失败）。
2. **A2 · 零随机**：raw `rng` = **0**；固定 seed 下 `--replay-gossip <seedId>` 输出与当日日志**逐位相同**；**人为把某 actor 锁 COARSE，传播树不变**（R-C）。
3. **A3 · D4 端到端（主验收）**：脚本化"D0 当着 B 的面给 A 三升水"→ 断言 ① D1 06:00 `hop=1` 注入 ≥1 条；② 至少 1 名**未在场** NPC 在 D+2 后首次对话出现 `CALLBACK`；③ 该链 `step ≤ 3` 且 `UNATTRIBUTED = 0`；④ 三跳内出现 ≥1 次 `distortionLevel` 增长，或至少一次 `distort()` 被记录为"无出边"。
4. **A4 · 死亡系**：`--kill` 任一 NPC → `RumorInjected` ≥1 **且 S5 侧 `ReputationDelta` = 0**（复用 `--assert-estate-no-rep`，要求它把 gossip 路径纳入）。
5. **A5 · 规模上界**：16 有名跑 10 日，断言单日 `RumorInjected ≤ 32`、单次批处理 p95 `≤1.5ms`、摊销 `≤0.01ms/帧`；不超 S2 已列 0.15ms 摊销预算。
6. **A6 · 玩家可干预**：澄清与放话两条路径均存在；断言澄清**大失败必产生新 `GossipSeed`**；断言玩家 `GossipSeed` 同样经过 `distort()`（不可免失真）。
7. **A7 · fog（R-D）**：`--assert-gossip-fog` —— 玩家侧输出中未观测节点必须显示 `???`，**不得泄漏任何未观测 `actorId`/`claimCode`**。
8. **A8 · 认知过载**：任一 actor 的 `sourceHop > 0` 印象数 `≤2`；terminal 印象不受限且不被淘汰。

### 8.2 gossip 图谱调试面板规格（细化 S0 §C8 ④ 与 S2 §8 ④）

| 区 | 内容 |
|---|---|
| **左 · 传播 DAG** | 按 `seedId` 展开。节点 = `actorId` + `hop` + `到达 dayKey` + 他听到的 `claimCode` + `charge` + `是否 relay-eligible`（Y/N + 原因：idle / 有 SOCIAL 块 / 否）+ 本跳失真结果。**合并节点用双入边画出**（区别于树） |
| **中 · 边明细** | `relay → recipient`：`contactCount`（排序键首值）、是否跨社区、`distort()` **入参码 → 出参码**、`score` 三项明细（`impressionSign×w1 / needMatch×w2 / creedFit×w3`）、是否"平手取边表索引升序" |
| **右 · 当日总览** | 种子数 / 成功注入 / **DROPPED** / **MERGED** / 终止原因分布（①hop ②terminal ③charge ④merged）——对应 S2 E3"丢弃条数必须可见" |
| **底 · dayKey 时间轴** | 横轴 = 日（T 生成 / T+1 hop1 / T+2 hop2 / T+3 hop3），可拖到某天看当天切片；红色标丢弃与合并 |
| **顶 · 过滤** | 一键只看被丢弃/被合并的分支（调 tuning 时唯一重要的视图） |

**如何重现一次传播**

```text
--replay-gossip <seedId>
  从 DaySnapshot[T] 冻结的 getSchedule 全量 + 各 actor 当日状态重跑整棵传播树，
  逐跳打印 PLAN 候选集 + 排序键 + distort score + APPLY 结果。
  因零随机，输出必须与当日日志逐位相同；不同即判确定性失败。
  （不需要重跑世界；面板按钮"从当前存档重放"则走完整 tick 序列。）
```

**调试钩子**

```text
--dump-gossip <topicActorId>        传播树（扩展 hop/dayKey/inbox/丢弃原因/distortionLevel）
--dump-rumor-graph <dayKey>         当日全量 DAG（可导出 .graphml，走底座 §C8 规格）
--replay-gossip <seedId>            单次传播逐跳重演（不重跑世界）
--dump-contacts <actorId> <dayKey>  接触图切片 + 重叠分钟 + 排序键
--force-distort <claimCode> <relay> 打印该中继者当前状态下 distort() 的 score 明细与选边结果
--inject-rumor <topic> <claimCode> <charge> <origin>   手工注入种子（禁用于正常流程）
--assert-gossip-bounded             链长≤3 / 入站≤2 / reached≤7 / 丢弃计数可见
--assert-gossip-fog                 玩家侧输出无未观测信息
--assert-gossip-zero-rng            本系统 raw rng 调用数 = 0
--export-attribution <dayRange>     继承底座 §C8 规格
```

## 附录 A · 提请回填底座 §D 的候选常量（本份不就地生效）

| # | 常量 | 建议值 | 说明 |
|---|---|---|---|
| A-1 | `GOSSIP_REACH_MAX` | 7 | 人；单 seed 接收者上限（**不含 root**），沿用 S2 A5 |
| A-2 | `GOSSIP_CONTACT_MIN_MIN` | 10 | 游戏分钟；同 `locationId` 重叠达此才计一次接触 |
| A-3 | `GOSSIP_SEED_MAX_PER_ACTOR_DAY` | 2 | 条；单 actor 单日产种上限（仅用于规模上界证明） |
| A-4 | `GOSSIP_SEED_HOLD_MAX` | 2 | 日；种子顺延上限（ESTATE 系豁免） |
| A-5 | `GOSSIP_IMPRESSION_MAX_PER_ACTOR` | 2 | 条；`sourceHop > 0` 印象占 ≤5 槽的上限（terminal 豁免） |
| A-6 | `GOSSIP_CROSS_COMMUNITY_MIN_CONTACT` | 2 | 次；跨社区边所需最低接触次数 |
| A-7 | `GOSSIP_CROSS_PER_HOP_MAX` | 1 | 人；每跳跨社区名额上限 |
| A-8 | `GOSSIP_HOPS_PER_DAY` | **1**（备选 3） | 跳/日；注入节奏开关（§3.7 / R-2） |
| A-9 | `RUMOR_KNOWN_LOG_MAX` | 5 | 条；玩家情报日志「传闻」页上限（7 天窗口） |
| A-10 | `GOSSIP_BATCH_BUDGET_MS` | 1.5 | ms；单次 DISPATCH gossip 批处理 p95 上限（验收 A5） |

> 已继承不重提：`GOSSIP_FANOUT_MAX(3)`（S0 §D）· `GOSSIP_SPREAD_PER_HOP(2)` / `GOSSIP_INBOX_MAX(2)`（S2 A-13）· `Q_HOP_DECAY(1)` / `Q_STRONG(4)`（S2 A-12）。

## 附录 B · 接口请求与待裁决（8 项）

| # | 对象 | 内容 |
|---|---|---|
| **R-1** | 主理人 / S2 / S5 | **`RumorInjected` 需追加 4 字段**：`recipientActorId` · `charge` · `seedId` · `dayKey`（原 4 字段逐字保留、顺序不变）。理由：`charge` 无法由 `hop` 反推（种子初始 charge 各异，S5 §3.4 有 `charge+1`/`charge=1` 分支），而 S5 的 `min(\|charge\|,3)` 需要它；`recipientActorId` 决定 `communityId`。本份先用 `RumorDelivery` 包装落地，**但建议直接并入**以免两份结构 |
| **R-2** | 主理人 | **`GOSSIP_HOPS_PER_DAY` = 1 还是 3**（§3.7）。1 = 三个早晨干预窗口（推荐）；3 = 贴合 S2 §3.7 字面"T+1 一次跑完" |
| **R-3** | **S2** | 请保证每日 `getSchedule` 中 `intentTag == "SOCIAL"` 的块**每 actor 至多 1 个**，且**每社区每日至少 1 人具备**（`idle_00` 已天然满足）。本份只读，不写日程 |
| **R-4** | **S2** | 请登记 `MemoryEvent.kind` 新增 **`RUMOR_HEARD`**；kind 闭集若有定义处请一并返回 |
| **R-5** | **S2** | 请暴露 **`distort(claimCode, relayActorId, dayKey) -> {nextClaimCode, scoreDetail}`**（零随机，平手取边表索引升序）。本份不重写失真规则，只调用并把 `scoreDetail` 原样送面板 |
| **R-6** | **S5** | 请把 §3.1 事件值表"`GossipEmitted` 到达 actor a"改为 **"`RumorInjected` 到达 actor a"**，并追加 **`seedKind ∈ {PROMISE_VOIDED_BY_DEATH, NO_HEIR, BOTH_DEAD, TARGET_DEAD, TARGET_GONE} → delta = 0`**。现状下死亡系 gossip 会经该行产生非零 delta，**击穿 Part E ② 与 `--assert-estate-no-rep`**（已存在的漏洞，非本份引入） |
| **R-7** | **S2** | 建议 L1 `social` 的"与有名者同处一地"判定复用 `contactOf(a,b,dayKey)`（§3.2 同口径）。否则会出现"他加了社交需求，但传闻传不到他那"的不一致 |
| **R-8** | 主理人（汇编） | **回填 S2 三处权属**：S2 §0.0 表"S6 / gossip 传播本份自持"→"拓扑归 S6"；S2 §3.7 的 `reachedSet`/fanout/inbox 三段 → 引用本份 §3.3/§3.5/§3.6；S2 §8-A5 与 `--dump-gossip` 保留（验收与钩子不变，实现方改为 S6） |

### B-9 · 最可能冲突的三点（提请汇编优先比对）

1. **传播所有权（R-8）**：S2 §3.7 现自持传播；本份按裁决接管拓扑。不同步回填 → **两处都能传**的双实现。
2. **死亡系 delta 漏洞（R-6）**：S5 §3.1 那一行不改，Part E ②"不扣声望"会被 gossip 后门击穿。**本轮最需优先修的一处。**
3. **印象槽被道听途说淹没（§3.8）**：S2 §3.7"未满则写"与本份 `GOSSIP_IMPRESSION_MAX_PER_ACTOR(2)` 冲突。若退回闸门，请至少保留 8→12 映射表，否则 S4 的 `CALLBACK` 条件会被噪声污染。

# S4 · Dialogue-Contract（记忆驱动对话与契约账本）· 系统设计文档

- **Task ID**：GDD-004｜**阶段**：Phase 2 · 批次 B2｜**优先级**：P0（R3「对话树 × 模拟 NPC 耦合」的正面解法落在本份）
- **主笔**：design-strategist（文策渊）｜**调度**：主理人（游承峰）｜**状态**：待拍板（附录 B 共 9 项待裁决）
- **依赖已读**：`game-concept.md` · `00-foundation.md`（S0·A/S0·B/S0·C/§D/Part E）· `02-npc-simulation.md`（S2）· `03-economy-barter.md`（S3）· `01-scarcity-loop.md`（S1，检索）
- **依赖方向（严格单向）**：依赖 S0·A/S0·B/S0·C/S1/S2/S3/S5(只读)。**不依赖**任何系统的内部状态写入权。
- **边界（严格）**：记忆驱动对话三类选项、承诺写入 `LedgerEntry`、到期判定与违约后果路由、印象标签写入规格、两态写作法与资产组织。
  **不写**：UI 布局与线框（UX）、NPC 行为树与调度（S2）、经济结算与比价（S3）、声望公式本体（S5）、承诺兑现的物流执行（S1 `tryTransfer`）。

---

## 0.0 规范系统编号对照表（PHASE2-REVIEW · 裁决 C-13，全文已按此表替换）

| 规范编号 | 系统 | 文件 | 本文档原用临时编号 |
|---|---|---|---|
| **S0** | 底座（`S0·A` 角色判定 / `S0·B` 世界时钟 / `S0·C` 确定性 / `S0·D` 常量表 / `S0·E` 死亡清算） | `00-foundation.md` | S1 / S2 / S12 |
| **S1** | 稀缺主循环 | `01-scarcity-loop.md` | S5 |
| **S2** | NPC 三层模拟 | `02-npc-simulation.md` | S6 |
| **S3** | 易货经济与社区仪表 | `03-economy-barter.md` | S9 |
| **S4** | **记忆驱动对话与契约账本（本文档）** | `04-dialogue-contract.md` | S8（自称） |
| **S5** | 社区、信条与声望 | `05-community-creed.md` | S7 |
| **S6** | 传闻传播（待写） | — | —（未引用） |
| **S7** | 工作板（待写） | — | C9（概念文档 scope ID，保留原写法） |
| **S8** | 区域内容（待写） | — | —（未引用） |
| **S9** | 呈现 / UI（待写） | — | "UX / UI" |

---

## 0. 全局契约（逐字继承，不改字段名）

```text
buildContext(actorId) -> DialogueContext{ impressions[], needs[], creedConflicts[], priceTableRef, carriedItems[], openLedgerEntries[] }
DialogueOption{ id, text, kind:"NORMAL"|"SKILL"|"CALLBACK"|"CREED", requirement:RequirementExpr, effects:EffectExpr[] }
RequirementExpr = {attr,min} | {skill,min} | {impression:tagId, polarity:"POS"|"NEG"} | {carried:itemId,qty} | {creedConflict:"NONE"} | {reputation:{communityId,min}}
LedgerEntry{ entryId, promisorId, promiseeId, kind:"DELIVER"|"SERVICE"|"ABSTAIN", content:ItemSpec|ServiceSpec, qty, deadlineDay, witnessIds[], state:"OPEN"|"MET"|"BREACHED"|"VOID", createdDay, reserved:bool, communityId }
              // 裁决 13（C-23 关闭）：字段集以本份为权威；communityId 为 S5 新增（按社区路由声望必需）
              // 权属归 S5 §5.3；本份只发 PromiseMade、只读 LedgerQuery，不写 state
              // 底座 Part E 旧字段名 id/payload/dueDayKey 降为别名；PENDING+ACTIVE 已合并为 OPEN（裁决 18）
事件: PromiseMade(LedgerEntry) → 社区账本 + witness 记忆
     PromiseBreached{entryId,dayKey,reasonCode} → ReputationDelta + gossip 源
     ImpressionAdded{actorId, tagId, polarity, sourceEntryId, dayKey} → NPC 记忆
```

**主理人已裁决（本份逐条落实，不复核）**：
① 对话**只能**经 `EffectExpr` 声明副作用，不得直接改任何模拟状态；② 不得私改 DC，修正只走 `modTotal`；③ 会话内视图冻结，一次会话同一 `tagId` 最多 1 条印象；④ 声望日切 00:00 批量应用，当日对话读昨日快照；⑤ `alive=false` 或不在场 → 拒绝开启对话，走墓碑/替岗分支；⑥ 死亡清算：死者为 promisor → `VOID/DEATH`，不扣声望但必发 gossip；死者为 promisee → 转 `heirActorId`。

---

## 1. 系统概览与目标

### 1.1 定位

S2 让 NPC 会死、会改道、会记住你；S3 让"三升水"有价有仓。S4 是这两件事唯一允许**被人说出来**的地方：把模拟状态翻译成一句话，把一句话翻译成一笔账，把一笔没还上的账翻译成下一个人嘴里的另一句话。

- **支柱**：**P1（主）**——任何一次对话的后果必须在 ≤3 跳内落到"某个有名有姓的人"；**P3**——你答应过的事在你睡觉时照样到期；**P4**——说服成功 ≠ 你有那三升水，契约是**物质**约束而非话术胜利；**P2**——承诺的冻结、到期、违约全部可见且提前一日预告。
- **动词**：**说 PERSUADE** 完整落在本系统；**守望 TEND** 的发起入口在此（`SERVICE` 类承诺）；**易 BARTER** 的"以物抵诺"在此生成承诺而非成交。
- **反目标**：不做通用好感度条；不做"说一次就永久解锁"的开关；不做绑定具体 actorId 的独占台词（除 `protectedActors[≤3]`）；不做对话内的第二套骰子。

### 1.2 一句话职责

> 让你说的每一句话都变成一张可以被追讨的欠条——并且当你还不上时，世界给你的是一场新的对话，不是一个报错。

---

## 2. 核心概念与数据模型

### 2.1 会话与视图冻结（裁决 3 的落地）

```text
DialogueSession {
  sessionId, targetActorId, openedTick, openedDayKey
  sessionMemoryView : 冻结的 getMemoryView(targetActorId)     // 快照，会话内不变
  sessionReputation : DaySnapshot[D-1].reputation[communityId] // 裁决 4
  sessionPriceRef   : Ref<PriceTable(communityId)>            // 引用，非拷贝
  writtenTagIds     : Set<tagId>                              // 本次会话已写，≤1 条/tagId
  pendingEffects    : EffectExpr[]                            // 节点闭合时批量入队
  state             : "OPENING"|"TALKING"|"CLOSING"|"ABORTED"
}
```

- **冻结语义**：`impressions[]` 与 `reputation` 在 `openedTick` 取值；本次会话经 `ImpressionAdded` 写入的标签**下次会话才生效**。理由（裁决 3 原文）：对方不会当场改口。
- **写入时机**：`EffectExpr` 在**对话节点闭合时**批量入队，不是点击瞬间。这条是 §7-E1（对话进行中 NPC 死亡）的竞态解——对话未闭合，承诺未成立。

### 2.2 `DialogueContext` 六字段的来源（逐字段定源）

| 字段 | 来源 | 纪律 |
|---|---|---|
| `impressions[]` | `sessionMemoryView.impressions` | 冻结；≤ `IMPRESSION_MAX(5)`；按 `charge desc, writtenDayKey asc, tagId asc` |
| `needs[]` | `getNeeds(actorId)`，按 deficit desc 取 **top 3** | 闭集 `{SAFETY, SOCIAL, CREED}` ∪ `{PHYSIO_WATER, PHYSIO_FOOD, PHYSIO_SLEEP}`（S2 §0 解释），**S4 只读不写** |
| `creedConflicts[]` | 玩家携带物/活跃承诺 vs 该社区 `CreedConstraints` | 空数组即"无冲突"，供 `{creedConflict:"NONE"}` 求值 |
| `priceTableRef` | `Ref<PriceTable(communityId)>` | **引用**不拷贝；读 `DaySnapshot[D-1]`（继承 S3 快照纯净 A4） |
| `carriedItems[]` | `getActorState("player").carrying` | S2 契约给定；S4 只读 |
| `openLedgerEntries[]` | `LedgerQuery(actorId)` 过滤 `state == OPEN` | **所有权归 S5**；S4 只读，创建只经 `PromiseMade` 事件 |

### 2.3 选项四类与门控显隐

| kind | `requirement` 典型 | 判定 | 不满足时的呈现 |
|---|---|---|---|
| `NORMAL` | `{reputation:{...}}` / `{carried:...}` / `null` | 纯比较 | **置灰 + 明写缺什么**（"需 声望 ≥ 0"） |
| `SKILL` | `{attr,min}` 或 `{skill,min}` | 满足 → `rollCheck`，DC 取自意图节点声明的**四档之一** | 置灰 + "需 言辞 3" |
| `CALLBACK` | `{impression:tagId, polarity}` | 会话快照内存在且 `charge` 符号匹配 | **首次达成为可见**；同一 `tagId` 已用过 → 置灰明写"你跟他说过那晚了"（防复读） |
| `CREED` | `{creedConflict:"NONE"}` + `{reputation:{communityId,min}}` | 双条件与 | 置灰 + 明写冲突项（如"你带着他们的水"） |

> 差异化理由：`CALLBACK` 隐藏是防"玩家知道有隐藏内容"的元游戏；`SKILL`/`CREED` 置灰是 P2 要求——**玩家必须能看见自己为什么做不到**。

### 2.4 `LedgerEntry` 与 `reserved:bool` 的分水岭（契约字段的语义定稿）

```text
ItemSpec    { cluster: ClusterId, amount: float, unit }        // unit 由底座 §D 固定映射导出
ServiceSpec { serviceId: "WATER_RUN"|"NIGHT_WATCH"|"BURIAL", postId?, durationMin, dcTier }
```

| `reserved` | 含义 | 承诺内容来源 | 创建时动作 | 到期核验对象 |
|---|---|---|---|---|
| `true` | **你许诺的是别人的东西**（玩家作为社区成员/经手人，动用公共仓） | 社区公共仓 | 调 `reserve(cid, entryId, Quantity, deadlineDay)` → 冻结 → `effStock` 扣 → 当日配给变少 → 可能发 `PriceShock{PROMISE_FREEZE}` | S3 账本 |
| `false` | **你许诺的是你自己的东西** | 玩家 `carriedItems` | **不冻结**，只入账 | `getActorState("player").carrying` |

> 这条分界是"说服 ≠ 拥有"的机械形式：玩家永远可以许诺自己去捡那三升水，但那一刻社区不会替他垫付——代价从一开始就是他自己的。

**ServiceSpec（stub，首批仅 3 种）**

| serviceId | 顶替 postId | `durationMin` | `dcTier` | 兑现判定 |
|---|---|---|---|---|
| `WATER_RUN` | `<community>.water_01` | 90 | `DC_TRIVIAL` | 完成一次注水（`getLaborOutput` 计 1 个 effHead） |
| `NIGHT_WATCH` | `<community>.watch_01` | 480（22:00–06:00） | `DC_TRIVIAL` | 全程在哨位，中途离开 → `BREACHED/NO_SHOW` |
| `BURIAL` | 无（一次性） | 60 | `DC_TRIVIAL` | 完成埋葬动作 → 写 `buried_mine` 印象 |

**`ABSTAIN` 的主导策略反制**（成本最低的承诺，必须压住）：① 兑现只给 `+1` charge 且**只写 `kept_word`**；② 不解锁 `CREED` 类选项；③ 同一 `promiseeId` 同时最多 1 条 `ABSTAIN`；④ 违反由 S1/S5 事件流判定（如玩家进入禁区触发 `ClaimDisplaced`），S4 只订阅不探测。

### 2.5 【必答】12 条印象标签词表（复用 S2 的 8 码闭集，不重建第二套）

> **关键声明**：`claimCode` 8 码闭集（`WATER_GIFT / HIRELING / KIN / MARTYR / WATER_DEBT / THIEF / CREED_BREAKER / OUTLAW`）是 **gossip 传播码**，唯一定义处为 S2 §3.7。本表 12 条 `tagId` 是该闭集的**细分（subtype）**，每条必须声明 `collapse → claimCode`；**S4 不新增任何第 9 个码**。
>
> **裁决 14（C-24）已拍板 · 存储粒度**：S2 的 `ImpressionTag.tagCode` **必须存本表的 12 码细分**；**只在 gossip 传播时 collapse 到 8 码 `claimCode`**。否则 `stole_well_water`（偷过井水）与 `broke_word`（说话不算数）会混为一谈，#8/#9/#10 的"关闭条件"无法分流。S2 §2.4 已同步回填。

| # | tagId | 中文 | claimCode | pol | charge | 触发条件 | 解锁 | 关闭 |
|---|---|---|---|---|---|---|---|---|
| 1 | `gave_water` | 给过我水 | `WATER_GIFT` | POS | +2 | 玩家兑现 DELIVER 且 `cluster=WATER` 且 `amount ≥ 1.0 L` | CALLBACK·送水回响；`reserved=false` 的 DELIVER 承诺 | 「他不缺我的水」拒绝开场 |
| 2 | `kept_night_watch` | **替我守过夜** | `WATER_GIFT` | POS | +3 | 完成 `NIGHT_WATCH` 且当夜该社区无失窃 | 夜间可入内（豁免 curfew 拒绝）；CALLBACK·那晚 | 夜间驱赶分支 |
| 3 | `buried_mine` | 替我埋了人 | `WATER_GIFT` | POS | +3 | 完成 `BURIAL`，死者为该 actor 同社区 | 死者遗物交接 intent；gossip 边 →`MARTYR` 的候选源 | hostile 开场 |
| 4 | `healed_me` | 救过我 | `WATER_GIFT` | POS | +3 | 该 actor `injuries≠[]` 或 `rad ≥ 60` 时交付 `MEDS ≥ 1 dose` | 紧急求助 intent；入户豁免 | 「别碰我」 |
| 5 | `kept_word` | 说话算数 | `WATER_GIFT` | POS | +2 | 以该 actor 为 promisee：`MET ≥ 3` 且 `BREACHED = 0` | `deadlineDay` 可延至 up to `DEADLINE_MAX_DAYS(3)`；可为人作保（其 witness 有效） | — |
| 6 | `kin_claim` | 算半个自己人 | `KIN`(terminal) | POS | +4 | gossip 收敛至 `KIN` 后回写，或完成该社区准入仪式 | 信条仪式邀请；`CREED` 类选项；社区准入 | 「外人」系列全部拒绝句 |
| 7 | `martyr_claim` | 替我们担过 | `MARTYR`(terminal) | POS | +4 | 玩家在 SERVICE/ABSTAIN 中受伤（Injury）或代为承担他人违约 | 全社区正 `ReputationHint`；D4 反转（负面码可被中继改写来此） | — |
| 8 | `water_debt` | 欠我水 | `WATER_DEBT` | NEG | −2 | DELIVER 类 BREACHED 且 `reasonCode ∈ {NO_STOCK, NO_SHOW}` | 追债 intent；以工抵债 | **全部新承诺**（解除条件：存在以同一 promisee 为对象的已 MET 代偿 entry，见 §3.6） |
| 9 | `stole_well_water` | **偷过井水** | `THIEF` | NEG | −3 | 该 actor **直接目击**（`sourceHop=0`）玩家取走 `claimant=该 actor` 的容器 | 揭发 intent（交 S5 悬赏） | `quote()` 被拒；入户；DELIVER 承诺 |
| 10 | `broke_word` | **说话不算数** | `HIRELING` | NEG | −2 | 任意 BREACHED 且 `reasonCode ∈ {NO_STOCK, NO_SHOW, REFUSED}` | 硬要求「先交货后谈」intent | 全部 `CREED` 类；作保；新承诺（解除条件同 #8） |
| 11 | `creed_breaker` | 坏过规矩 | `CREED_BREAKER` | NEG | −3 | 触发该社区 `CreedConstraints` 任一条且被目击 | 赎罪 intent（代价性仪式） | 信条仪式；`CREED` 类选项 |
| 12 | `outlaw_claim` | 不能留的人 | `OUTLAW`(terminal) | NEG | −4 | gossip 收敛至 `OUTLAW` 后回写，或被 S5 判悬赏 | 悬赏 / 敌对 / 离开（唯一入口） | 该社区全部非敌对入口 |

- **写入与淘汰**：未满 5 条直接写；满 5 条淘汰 `sort(charge asc, writtenDayKey asc, sourceActorId asc)` 首条（继承 S2 §3.7，S4 不重定键）。
- **会话内去重**：`writtenTagIds` 命中 → 本次会话不再写该 `tagId`（裁决 3）。**同一 `(actorId, tagId)` 全局只存 1 条**——请 S2 在 gossip 注入路径上按此键去重（见 C-24）。
- **映射提案**：#10 `broke_word → HIRELING`（S2 中该码电荷为 0，本表按 NEG 处理）是唯一一个"符号不完全对齐"的映射，理由：在中继者耳里"他的话跟雇来的人一样不作数"正是 `HIRELING` 的语义方向。**请主理人确认**（C-24）。

### 2.6 `protectedActors[≤3]` 与 Black Isle 式长文本样板角色

**名单（Core，硬编码，唯一定义处见 §3.5）**

| # | actorId | 代号 | 为什么不可替代 |
|---|---|---|---|
| 1 | `npc.he_valley.mend_01` | **程九**（河谷修补岗，前旧政权水厂技师） | **文本密度样板角色**：长自白、可被反驳、有立场 |
| 2 | `npc.jing_cell.care_01` | 药圃持有人 | MEDS/SEED 的唯一知识源（呼应"有些阀门只有他知道怎么开"） |
| 3 | `npc.he_valley.water_01` | 河谷取水首席 | 供水额定岗首席；其死亡使 `safeDays` 当日可见崩塌，需兜底 |

**保护 ≠ 无敌（边界条件，四条，缺一即 P4 失效）**
1. **离屏免疫**：`protectedActors` 在 COARSE 期间**不判死**，降级为伤病 + 强制回社区（继承底座 B3.3 预告闸门之上再加一层）。
2. **玩家可杀**：在 FULL（玩家在场/视锥内）时，玩家亲手造成的死亡**照常成立**。不能让玩家觉得自己被系统耍了。
3. **叙事兜底（关键）**：protected 死亡后，其 intent 节点**不走通用 `GRAVE` 兜底**，而走 `SUCCESSOR`——worldgen 指定 `successorActorId`，承接其未完文本与知识。
4. **claimant 直转 heir**：不释放（继承 Part E ①）。

**样板角色规格（程九）**

| 维度 | 规格 |
|---|---|
| 结构 | 3 段自白 × 400–600 字（合计 1200–1800 字），按 `dayKey` / 印象分三次解锁 |
| 立场 | 主张"旧政权水厂该被重新点上，为此值得让河谷交出余粮" → 与河谷信条「当日分尽、不留隔夜」正面冲突（R5 的结构性矛盾播种，**非随机**） |
| 可反驳 | 每段末 2–3 个 `RebuttalPoint{ claimId, requires:{RevealKnowledge:refId} \| {carried:itemId,1}, onSuccess, onFail }`；**反驳不掷骰**——用读到的终端日志或手里的遗物说话（避免"嘴炮通关"）；成功 → 他改口，结论句被替换，写 `MemoryEvent` + `martyr_claim` 候选印象 |
| 死亡兜底 | 第 2/3 段自白由 `successorActorId` 以"他跟我说过"承接；`RebuttalPoint.requires` 改判为"持有程九遗稿 `itemId`"——**知识不随人死** |

---

## 3. 规则与公式

### 3.1 会话内视图冻结

```text
开启：sessionMemoryView ← getMemoryView(targetActorId)      // 快照，永久冻结
      sessionReputation ← DaySnapshot[D-1].reputation[cid]   // 裁决 4
求值：所有 {impression:...} 与 {reputation:...} 一律读快照
写入：ImpressionAdded → 写队列（HOURLY_TICK 应用）→ 下次会话可见
去重：writtenTagIds 已含 tagId → 本次会话不再写入
```

### 3.2 技能实显：DC 指派表（不得私改 DC，裁决 2）

```text
成功 ⇔ rollCheck(actorId, attr, skillId, dc, seedCtx).success     // 底座唯一判定式
dc  ∈ { DC_TRIVIAL(10), DC_DEMANDING(14), DC_PERILOUS(18), DC_DEADLY(22) }   // 四档，禁止派生
modTotal 由 S0·A 内部依 actor 状态累加，S4 不得传修正、不得自改 DC
```

| 说服意图 | 属性 / 技能 | DC 档 | 大失败（`critFail`）必然后果 |
|---|---|---|---|
| 求情 / 请求宽限 | `PRESENCE` / `persuade` | `DC_TRIVIAL` | `AddImpression{broke_word?→ 否}`：写该意图指定的 NEG tag + 本次会话锁死该 intent |
| 说服让渡物资 | `PRESENCE` / `persuade` | `DC_DEMANDING` | 同上 + `GossipSeed{claimCode 由意图指定}` |
| 威慑 / 威胁 | `PRESENCE` / `persuade` | `DC_PERILOUS` | 对方叫人 → 同社区 `safety −8`（S2 同款） + 会话强制中止 |
| 说动违反信条 | `PRESENCE` / `persuade` | `DC_DEADLY` | `AddImpression{creed_breaker}` + 必发 gossip |
| 以货抵诺 | `PRESENCE` / `barter` | `DC_DEMANDING` | `reputationHint:HARD_BARGAIN`（S3 保留字） |
| 拆穿谎言 | `MIND` / `survey` | `DC_DEMANDING` | 反被记恨 → `AddImpression` NEG |
| 认出痕迹 / 伪造 | `MIND` / `scavenge_eye` | `DC_TRIVIAL` | 认错人 → 会话提前中止 |
| 当场修好他的东西 | `HAND` / `repair` | `DC_DEMANDING` | 修坏 → 物品耐久归零 + NEG 印象 |
| 现场拆解证明 | `HAND` / `dismantle` | `DC_TRIVIAL` | 拆坏 → 同上 |
| 现场救治 | `VIGOR` / `fieldmedic` | `DC_DEMANDING` | 伤情加重（交 S1） |
| 潜行靠近偷听（**进入对话前**） | `VIGOR` / `stealth` | `DC_DEMANDING` | 被发现 → `safety −8` + 不可开启对话 |

> **RNG 纪律**：本系统 raw `rng` 调用数 = **0**（继承 S2 R-B 最严读法）；唯一随机出口是 `rollCheck`，其 `streamId` 所有权在 S0·A。**判定禁在渲染帧发起**（底座附录 B-6）。

### 3.3 `EffectExpr` 闭集（裁决 1 的落地：只能声明，不能执行）

```text
EffectExpr =
  AddImpression   { tagId, polarity, targetActorId, sourceEntryId? }
| MakePromise     { kind, content, qty, deadlineOffsetDays, reserved, witnessScope }
| FulfillPromise  { entryId }                       // → S3 fulfill() + S1 tryTransfer
| Renegotiate     { entryId, extendDays }           // ≤ RENEGOTIATE_MAX_PER_ENTRY(1)，extendDays ≤ 1
| VoidPromise     { entryId, reasonCode }           // 仅双方协议解除 / 代偿完成
| StartService    { serviceId, dayKey }             // → 事件，S2/S1 执行顶岗
| GrantItem       { itemId, qty }                   // 从 speaker 个人库存，经 tryTransfer
| TakeItem        { itemId, qty }                   // 玩家交出
| ReputationHint  { communityId, code, delta }      // 建议值，S5 裁决是否采纳
| GossipSeed      { claimCode, topicActorId, charge }  // → 交 S2 播，S4 不算传播
| RevealKnowledge { refId }                         // 只写玩家情报日志，非模拟状态
| SetFlag         { flagId, scope:"SESSION" }       // 会话作用域
| EndDialogue     { mood }
```

> **硬约束**：`EffectExpr` 不含任何"直接写 `PhysioState` / `Warehouse.stock` / `NeedVector` / `Reputation`"的操作。任何变更一律入写队列 → `HOURLY_TICK` 或日切应用。实现期可加静态断言：**S4 包内禁止出现 `tryTransfer` / `stock[` / `reputation =` 的赋值**（`--assert-dialogue-pure`）。

### 3.4 【必答】两态写作法：绑定意图，不绑定人

**核心命题**：`取水员承诺明日送水` 是一个可被**任何符合岗位的 NPC 承接**的契约节点。因此对话资产不得包含 `actorId`，只声明"谁能说这句话"。

**绑定与解析（零随机，继承 S2 R-B）**

```text
IntentBinding { intentId, bind:{ postRole, communityAny:bool, minSeniority:int }, fallback:{HEIR,GRAVE,RUMOR} }

resolveSpeaker(intentId, playerPos) -> actorId | null
  候选 = { a | a.alive
             && (bind.communityAny || a.communityId == player.communityId)
             && a.homePost.role == bind.postRole
             && dist(playerPos, a) ≤ DIALOGUE_OPEN_RADIUS }
  排序键 = (seniority desc, isOnDuty desc, dist asc, actorId asc)
  候选空 → null → 走 fallback 三分支
```

**两态 × 三分支（缺位态不是"内容消失"，而是"内容换手"）**

| 态 | 判定 | 分支 | 处理 |
|---|---|---|---|
| **在场 `PRESENT`** | `alive && dist ≤ DIALOGUE_OPEN_RADIUS` | — | 正常 intent 播放 |
| **缺位 `ABSENT`** | `alive == false` | `HEIR` | `resolveSpeaker` 用 `heirActorId(postId)` 重解析 → **同一 intent 节点由继任者原样承接**，承诺自动 `retarget(promiseeId = heir)` |
| | | `GRAVE` | 无 heir → 播 `grave/<roleId>.grave` 文本；玩家可把该交的水倒在坟前 → 无接收人，只写 `MemoryEvent` + `martyr_claim` 候选（**不产生 LedgerEntry**） |
| | `alive && dist > DIALOGUE_OPEN_RADIUS` | `RUMOR` | 播该 NPC 今日 `locationId` 名 + 一句去向台词（信息粒度同 `getSchedule`，不泄露精确坐标）；可就地发起"追上去" |

**写作模板（可操作）**

```yaml
# assets/dialogue/intent/water_promise_tomorrow.dlg
intentId: water_promise_tomorrow
bind:     { postRole: water, communityAny: true, minSeniority: 0 }
fallback: { HEIR: "@self", GRAVE: "grave/water.grave", RUMOR: "rumor/water.rumor" }
lines: { open: "{ROLE_CALL}，{TIME_PHRASE}。{SELF_NEED}" }
options:
  - id: promise_3L                                   # NORMAL：代价前置，创建即显示违约后果
    requirement: { reputation: { communityId: "@community", min: 0 } }
    text: "我明天给你三升水。"
    effects: [ MakePromise { kind: DELIVER, content: {cluster: WATER, amount: 3.0}, qty: 1,
                             deadlineOffsetDays: 1, reserved: false, witnessScope: NEARBY } ]
    costPreview: "若违约：声望 −X；你欠的是自己的水（不冻结社区仓）"
  - id: press_for_more                               # SKILL：dcTier 只能取四档之一
    kind: SKILL
    requirement: { skill: { id: persuade, min: 2 } }
    dcTier: DEMANDING
    text: "[言辞] 三升？你昨晚分到的可不止三升。"
    effects: [ ReputationHint { code: HARD_BARGAIN, delta: -1 } ]
    critFail: [ AddImpression { tagId: broke_word, polarity: NEG },
                GossipSeed { claimCode: HIRELING, charge: 2 } ]
```

**Token 替换表（岗位口吻 ≠ 个人台词）**

| Token | 来源 |
|---|---|
| `{ROLE_CALL}` | `role/<roleId>.profile`（6 岗位各一份称谓/口头禅/自指） |
| `{SELF_NEED}` | `needs[]` 首位 deficit → 闭集句子（S2 的 need 闭集，禁自由文本） |
| `{TIME_PHRASE}` | `minuteOfDay` 四档：晨(05–11) / 午(11–17) / 昏(17–22) / 夜(22–05) |
| `{COMMUNITY}` `{POST}` | `lexicon/<communityId>.lex`（每社区 ≤40 条方言/称谓/禁忌词） |

**资产组织结构**

```text
assets/dialogue/
  intent/<intentId>.dlg           # 与人不绑定；Core 目标 24 个
  role/<roleId>.profile           # 6 岗位 × 口吻替换表
  actor/<actorId>.override.dlg    # 仅 protectedActors 与 ≤3 个特例（含样板角色 3 段自白）
  grave/<roleId>.grave.dlg        # 缺位态兜底（按岗位写，不按人写）
  ledger/<intentId>.contract.dlg  # LedgerEntry 生成模板（模板 → 实例）
  lexicon/<communityId>.lex       # 方言/称谓/禁忌词
```

> **规模纪律**：Core 期 `actor/*.override` 总数 **≤ protectedActors(3) + 2**。任何"我想给这个 NPC 加一段专属台词"的诉求，必须先在 `intent/` 里证明它是**岗位级**的，否则不予立项（R4 的第一道锁）。

### 3.5 `protectedActors` 唯一定义处

```text
protectedActors : ActorId[≤ PROTECTED_ACTOR_MAX(3)]     // worldgen 静态硬编码，运行期只读
每个 protected actor 附带 successorActorId : ActorId    // 叙事兜底，worldgen 指定，不得为 null
```
> **裁决 15（C-27）已拍板**：**全局唯一一份名单（≤3）**。底座 §B2 的 LOD 档位 `PROTECTED` 与本份 / S5 的 `protectedActors` **必须是同一份**，唯一定义处已登记进 **S0 §D**；`successorActorId` 非空为硬约束。**禁止实现成两份名单**（否则会出现"永驻 FULL 但不是叙事关键角色"的配额浪费，以及"叙事不可替代但会在离屏被判死"的 P1 击穿）。

### 3.6 【必答】到期无货：如何把 bug 变成内容

玩家许诺三升水，到期那天他背包里没有水、社区也不给他垫。**四级闭合链**：

| 时点 | 阶段 | 系统动作 | 玩家可见 |
|---|---|---|---|
| **D−1 06:00** | 预告 | `dayKey == deadlineDay − 1` → `PromiseDueSoon`；解锁"补救窗口"三选项：补交 / 以工抵债 / 请求宽限（`Renegotiate` ≤1 次，+1 天） | 欠条页该条转红，写明"还差 3.0 L"；该 NPC 明日 06:00 站村口等 |
| **D 00:00** | 到期 + 应用 | **日切 7.1**：`state==OPEN && dayKey > deadlineDay` → `BREACHED`；`reasonCode ∈ {NO_STOCK \| NO_SHOW \| REFUSED \| DEATH \| NO_HEIR \| BOTH_DEAD \| TARGET_DEAD \| TARGET_GONE}`；**7.2**：`reserved==true` → `release()`；**7.3**：发 `PromiseBreached`；**7.4**：`ReputationDelta` **同日补批应用**（DEATH 系 → `delta = 0`，继承 Part E ②）→ 进 `DaySnapshot[D]` | 发 `PromiseBreached{entryId, dayKey, reasonCode}` |
| **D+1 全天** | 可读 | 对话读 `DaySnapshot[D]` → 声望后果**当日可读**；被冻结的物资经 D+1 日切 3.3 回到配给 | 裁决 4 的可见时点，延迟恰为 1 日 |

> **裁决 10（C-28 + B-1）已拍板**：第 7 步细分 7.1/7.2/7.3 并追加 **7.4 违约 delta 补批应用**（S0 §B3.5 已落地）。**若无 7.4，违约 delta 要等 D+1 日切的 4.2 才应用 → 玩家 D+2 才看到后果**，与本表及裁决 4 不符。
| **D+1 06:00** | 传播 | gossip 注入（T+1）→ witness 与同社区 2 人获印象 | 第三人下次对话出现 CALLBACK："我听说你答应过他三升水。" |
| **D+1 起** | 内容 | **三选一**：① 补交原物 `qty × DEBT_INTEREST_MUL(1.5)` → 新建 DELIVER entry（`reserved=false`）；② 以工抵债（承接一项 `ServiceSpec`）；③ 顶岗 TEND（`StartService` 一日）。任一完成 → 代偿 entry 置 `MET` | 不是洗白——`broke_word`/`water_debt` 仍在 ≤5 条里靠淘汰消退；而是**对账**：`hasCompensatedDebt(promiseeId)` 为真 → #8/#10 的"关闭全部新承诺"被覆盖 → 玩家重获许诺资格 |

**为什么这不是 bug**：① 违约成本在**许诺当刻**已可见（代价前置，S3 六道闸门之⑥）；② 违约后玩家有**三个可执行动作**而非一个"任务失败"弹窗（P4 ↔ 探索自由的法定出口）；③ 声望**按社区**结算，一个社区关门另一个仍可玩（永不完全锁死）；④ 印象 ≤5 条自然淘汰，长期关系可复原；⑤ 违约本身是 gossip 的内容源——**它让世界更吵，不是更空**。

### 3.7 见证人与传播

```text
witnessIds = { a | a.alive && a != targetActorId && dist(openPos, a) ≤ WITNESS_RADIUS }
             排序 (dist asc, actorId asc)，取前 WITNESS_MAX(3)
witnessCharge = sign(charge) × max(1, floor(|charge| × 0.5))      // 目击不如亲历，最小 1
```
> `WITNESS_MAX(3)` 与 `GOSSIP_FANOUT_MAX(3)` 对齐：见证人是"当场就知道"的人，gossip 是"第二天才知道"的人，两条路径共享同一个 ≤3 上界，防止一次对话把全社区点名。

---

## 4. 状态与流程

### 4.1 会话状态机

```text
OPENING ──裁决5 前置校验──┬─ 失败 --> ABORTED（走 fallback 三分支）
                          └─ 成功 --> TALKING ─┬─ ActorDied / 离开半径 --> ABORTED（丢弃 pendingEffects）
                                               └─ 玩家结束 --> CLOSING --> 批量入队(priority 5) --> idle
```

**开启前置校验（裁决 5，顺序不可调换）**：① `alive == false` → `GRAVE`（含 heir 重解析）；② `dist > DIALOGUE_OPEN_RADIUS` → `RUMOR`；③ `activity ∈ {DEAD, FLED}` → `GRAVE`；④ 通过 → 建 `DialogueSession`，取 `sessionMemoryView`。

### 4.2 承诺生命周期（与 S3 预留生命周期的对应关系，见 C-23）

```text
(PROMISED) ── 节点闭合，PromiseMade(LedgerEntry) 入队 ──> 入社区账本（S5 所有）
   │                                                      └─ witness 记忆（ImpressionAdded）
   ├─ reserved=true  → reserve() → FROZEN → 当日配给变少，可能发 PriceShock
   │
   ├─ 玩家交付 → FulfillPromise → tryTransfer 成功 → state = MET → release()
   ├─ 到期未交付 → 日切第 7 步 → state = BREACHED → PromiseBreached → ReputationDelta + GossipSeed
   └─ promisor/promisee 死亡 → Part E ② → state = VOID, reasonCode = DEATH|NO_HEIR|BOTH_DEAD
                                → delta = 0，但必发 gossip
```

**重命名对照（防误并，C-23）**：S3 的 `PROMISED/FROZEN/FULFILLED/DEFAULTED/VOIDED` 是 **`reservedByPromise` 预留实体**的状态；本份 `OPEN/MET/BREACHED/VOID` 是 **`LedgerEntry`** 的状态。二者以 `entryId` 外键关联，**禁止合并为同一枚举**。

### 4.3 三跳归因（P1 硬约束）

```text
PromiseMade:  step1 S4.dialogue.<intentId> → S5.ledger.<entryId>
              step2 S5.ledger.<entryId>    → S2.memory.<witnessId>
              step3 S2.memory.<witnessId>  → S2.gossip.<seedId>          （≤3 ✓）
PromiseBreached: step1 S5.ledger.<entryId> → S5.reputation.<cid>
              step2 S5.reputation.<cid>    → S2.gossip.<seedId>
              step3 S2.gossip.<seedId>     → S2.memory.<thirdPartyId>    （≤3 ✓）
```
任何一条 `UNATTRIBUTED` 即报警（底座 §C7-1）。

---

## 5. 对外接口

### 5.1 暴露

| 接口 / 事件 | 消费方 | 用法与注意 |
|---|---|---|
| `buildContext(actorId)` | UI | 六字段；`priceTableRef` 是引用非拷贝 |
| `DialogueOption` / `EffectExpr` | UI（渲染 + 回传选中项） | UI **不解释** effect，只回传 `optionId` |
| `resolveSpeaker(intentId, pos)` | C9 工作板、UI | 零随机；返回 null 时由调用方走 fallback |
| `canOpenDialogue(actorId) -> {ok, reason}` | UI | `reason ∈ {OK, DEAD, AWAY, FLED}` |
| `PromiseMade(LedgerEntry)` | **S5**（账本所有权）、S2（witness 记忆） | S4 只发事件，不写账本 |
| `PromiseBreached{entryId, dayKey, reasonCode}` | **S5**（→ `ReputationDelta`）、S2（gossip 源） | `reasonCode` 闭集 8 值；`DEATH/NO_HEIR/BOTH_DEAD/TARGET_DEAD/TARGET_GONE` → **delta = 0，仍必发 gossip** |
| `ImpressionAdded{actorId, tagId, polarity, sourceEntryId, dayKey}` | **S2**（写入 `ImpressionTag`） | `tagId` 取 12 条表；S2 存 12 码，gossip 时 collapse 到 8 码（C-24） |
| `PromiseDueSoon{entryId, dayKey}` | UI、S2 | `dayKey == deadlineDay − 1` 发；NPC 次日 06:00 站村口 |
| `GossipSeed{claimCode, topicActorId, charge}` | S2 | S4 不算传播、不决定 fanout |

### 5.2 依赖

- **S0·A**：`rollCheck`（本系统唯一随机出口，禁传修正、禁改 DC）· `getSkill` · `getDerived`。
- **S0·B**：`now()`（DayKey 唯一来源）· `enqueue` · `getDaySnapshot(D)` · `subscribe(HOURLY_TICK / DAILY_CUTOVER)` · `absTick`。
- **S0·C**：`recordAttribution` · `exportAttribution`。**本系统 raw `rng` 调用数 = 0。**
- **S1**：`tryTransfer`（**唯一**物资落地口，接受全有或全无）· 事件流（`ClaimDisplaced` 用于 ABSTAIN 违反判定）。
- **S2**：`getMemoryView` · `getActorState`（`alive` / `locationId` / `carrying`）· `getNeeds` · `getSchedule`（仅 `RUMOR` 分支读去向）· `heirActorId(postId)` · 消费 `ImpressionAdded` / `GossipSeed`。
- **S3**：`PriceTable`（只读引用）· `Warehouse`（只读）· **`reserve/release/fulfill`**（预留生命周期唯一入口，接受 ≤3 笔与参数原子化）· `hasCuriosity("CAP")`（承接 S3 §2.5，用作 `{carried:"cap_bottle", qty}` 条件，**不新增 RequirementExpr 变体**）。
- **S5（stub）**：`Reputation`（读昨日快照）· `CreedConstraints` · `LedgerQuery` · 承接 `ReputationHint` 与 `PromiseBreached`。

### 5.3 stub（明示为未完成）

- `CreedConstraints { canShelterOutsider, tithe, curfewMin }`（沿用 S2 §5.3，不改）。
- `LedgerQuery(actorId, state?) -> LedgerEntry[]`——**所有权归 S5**，本期可读。
- `Reputation(communityId) -> float`——本期恒读 `DaySnapshot[D-1]`。
- `protectedActors` / `successorActorId`——worldgen 静态硬编码（§3.5）。
- **FOOD 类承诺**——待 C-17/C-26 裁决后开放，本期 `ItemSpec.cluster` 仅 5 通货簇。

---

## 6. 玩家可感知表现

1. **三类选项一眼可辨**：普通句 / `[言辞] 三升？你昨晚分到的可不止三升。`（灰 + "需 言辞 3"）/ `[你上次替我们守过夜] 那晚的东西还在我家桌上`。**技能选项显示档位名与数字**（「苛刻 14」），同 S0·A A6。
2. **许诺当场有重量**：`reserved=true` 的许诺，点下去的那一刻社区仓读数就掉、价格牌动一下；`reserved=false` 的许诺，欠条页多一行——**你不可能没注意到自己许诺了**。
3. **欠条页 ≤3 条**：HUD 常驻，每条 6 字段（谁 / 什么 / 多少 / 哪天 / 还差多少 / 冻结在哪）。到期前一日转红。
4. **他不会当场改口**：你刚替他守完夜，他这一句还在说"外人别进屋"——第二天他才会提那晚。这不是 bug，这是**记忆有生效时间**（裁决 3 的叙事表达）。
5. **长文本角色不用美术**：程九的三段自白是一个全屏文本框 + 一张静止肖像；反驳靠你带的东西和读过的终端，不靠骰子。这是"低预算高文本密度"的可行性证明。
6. **人不在了，话还在**：取水员死了，你明天在井边见到的是他徒弟，说的是**同一句承诺**；实在没人了，那口井边的坟前你可以倒下那三升水——没人接收，但世界记得。

---

## 7. 边界情况与失败模式

**E1 · 对话进行中 NPC 死亡 / 离开（必须含，R3 的正面落点）**
- **检测**：每 `SIM_TICK` 复检 `getActorState(targetId).alive` 与距离；收到 `ActorDied` 立即响应。
- **未闭合节点**：`pendingEffects` **全部丢弃**，不入队。规则：**承诺成立的时点是"节点闭合"，不是"点击瞬间"**——故不存在"点击时他还活着"的承诺悬空。
- **已闭合、待应用**（HOURLY_TICK 前死亡）：effect 已在队列，在应用时重查 `alive`：
  - `MakePromise` 遇 promisor 或 promisee 死亡 → 转 `state = VOID`，`reasonCode = DEATH | BOTH_DEAD`，**不扣声望，必发 gossip**（继承 Part E ②，与 S3 E6 同款）。
  - `AddImpression` 遇 target 死亡 → 丢弃，改写 `MemoryEvent`（死者不产生新印象）。
- **表现**：不弹错误框。死亡 → 播 `causeCode` 对应的一句收束（"他把桶放下，没说完"）+ 直接切入 `GRAVE` 分支；离开 → 播一句去向台词 + 切入 `RUMOR`。**玩家永远拿到的是内容，不是中断。**

**E2 · 到期时玩家已无物资**：见 §3.6 四级闭合链。三选一代偿 + 对账恢复许诺资格 + 印象 ≤5 条自然淘汰，共同保证"还不上"是可玩的一段关系，不是死局。

**E3 · 视图冻结导致"我做了好事他却不当场改口"**：这是**设计**不是缺陷。补偿：HUD「欠条」页**即时**显示账本事实（已 MET / 已冻结 / 已释放）。**禁止**为消除这个落差而让会话内写入立即生效——那会直接打开"一次会话刷印象解锁分支"的口子（裁决 3/4 的核心防线）。

**E4 · 印象溢出淘汰掉关键标签**：满 5 条按 `sort(charge asc, writtenDayKey asc, sourceActorId asc)` 淘汰，`kept_word(+2)` 可能被新的 `+3` 挤掉。缓解：terminal 标签（`kin_claim` / `martyr_claim` / `outlaw_claim`，`|charge| = 4`）**永不淘汰**；其余淘汰时写一条 `MemoryEvent`（短期记忆仍记得，只是不再作对话条件）。

**E5 · 见证人缺失或集体死亡**：`witnessIds` 为空 → 承诺仍成立，但**不可抵赖性下降**：违约时无 gossip 源，只写 promisee 本人印象。witness 在到期前全部死亡 → 违约只影响 promisee 一人，`ReputationHint.delta × WITNESS_CREDIT_MUL(0.5)`。**不允许"证人死光 = 免罚"**，也不允许"无人见证却全村皆知"。

**E6 · 同场多 NPC（Extended 议事会）**：`writtenTagIds` 按 `(actorId, tagId)` 独立计数，每名 NPC 各自 ≤1 条/会话；`witnessIds` 排除已在会话中的 NPC。Core 期不实现，此处只预留。

**E7 · `protectedActors` 被玩家杀死**：照常成立（§2.6 边界 2）。触发：① intent 转 `SUCCESSOR`，`successorActorId` 承接未完文本与知识；② claimant 直转 heir（Part E ①）；③ 必发 gossip（`MARTYR` 候选源）；④ 程九第 2/3 段自白改由继任者以"他跟我说过"承接，`RebuttalPoint.requires` 改判为"持有程九遗稿 `itemId`"。**知识不随人死**——这是长文本角色不被死亡机制破坏的三重保险（离屏免疫 / 继任者 / 遗稿）。

**E8 · `SERVICE` 进行中玩家睡觉或离开哨位**：离哨位 > `POST_RADIUS` 持续 > `POST_ABANDON_MIN`，或 `requestSleep` → `BREACHED/NO_SHOW`。因 `CRITICAL_NEED`（水/食归零）中断 → `BREACHED/NO_STOCK` 但 `delta × 0.5`（人要先活着）。

**E9 · 玩家为 promisee 时 NPC 违约**：不产生玩家侧 `ReputationDelta`（玩家不是社区成员）。改为写 `MemoryEvent` + 解锁"追讨"intent（`CALLBACK` 类，以 `openLedgerEntries` 中该条为准）。S4 不替 NPC 承担物质责任——NPC 违约走 S2/S3 的产出与库存路径。

---

## 8. 验收标准与调试钩子

**验收**

1. **契约必然生效（继承 Core 验收 7，本份主验收）**：脚本化"第 D 日许诺 3.0 L 水、不交付" → **D+1 日内**必然观测到：① `PromiseBreached` 发出且 `reasonCode ∈ 闭集`；② `DaySnapshot[D+1]` 中该社区声望下降；③ 至少 1 名 NPC 的下次对话出现提及该具体事件的 `CALLBACK`。三者缺一即判失败。
2. **两态写作法（R3 解法）**：`--kill` 掉任一非 protected 取水岗 NPC → 次日同一 `intentId` 仍可被 `resolveSpeaker` 解析（heir）或被 `GRAVE` 兜底承接；**全仓 `intent/*.dlg` 中出现 `actorId` 的次数 = 0**（protected 的 `override` 文件除外）。
3. **对话纯度（裁决 1）**：`--assert-dialogue-pure` 通过——S4 包内无 `tryTransfer` / `stock[` / `reputation =` 赋值；所有副作用以 `EffectExpr` 声明并出现在写队列日志中。
4. **无第二套骰（裁决 2）**：全仓检索 `d20` 命中数 = 1；本系统 raw `rng` 调用数 = 0；**全部 `rollCheck` 调用的 DC ∈ {10,14,18,22} 四值**（无派生 DC）。
5. **会话冻结（裁决 3）**：同一会话内对同一 `tagId` 触发两次写入条件 → 实际 `ImpressionAdded` 数 = 1；会话内新写入的标签在本次会话的 `buildContext().impressions` 中**不出现**。
6. **声望快照（裁决 4）**：`dayKey = D` 期间任意时刻 `buildContext().openLedgerEntries` 与 `reputation` 求值结果 == `DaySnapshot[D-1]` 值；断言 `dialogueSnapshotPurity`。
7. **死亡不开对话（裁决 5）**：`--kill` 后对该 actor 调 `canOpenDialogue` → `ok=false, reason="DEAD"`，且 UI 收到 `fallback` 三分支之一的可用节点。
8. **印象标签一致性（C-24）**：12 条 `tagId` 的 `collapse → claimCode` 全部落在 S2 的 8 码闭集内；运行时断言"写入的 `claimCode ∉ 闭集 → 中断"。
9. **归因与性能**：全部链路 `step ≤ 3`、`UNATTRIBUTED = 0`；本系统为**事件驱动**（无逐帧逻辑），仿真预算 **≤ 0.4ms/帧**（仅在选项选中与日切时计算）。

**调试钩子**

```text
--dump-dialogue <actorId>       buildContext 六字段 + sessionMemoryView + 冻结标记
--dump-session <sessionId>      pendingEffects / writtenTagIds / 冻结快照 diff
--dump-impression <actorId>     12 码印象 + collapse 后的 8 码 + charge + 淘汰预测
--dump-ledger [<actorId>]       继承 S3，增列 witnessIds 与 reserved 分界
--dump-intent <intentId>        bind / fallback 三分支 / 当前 resolveSpeaker 结果
--resolve-speaker <intentId>    打印候选集与排序键（证明零随机）
--force-promise <promisor> <promisee> <cluster> <amount> <deadlineDay>   构造承诺（禁用于正常流程）
--force-breach <entryId> <reasonCode>                                    构造违约，验证 §3.6
--skip-to-due <entryId>                                                  跳到到期前一日，验证 PromiseDueSoon
--assert-dialogue-pure          --assert-impression-codes                （见验收 3 / 8）
--export-attribution <dayRange> 继承底座 §C8 规格
```

---

## 附录 A · 提请回填底座 §D 的候选常量（本份不就地生效）

| # | 候选常量 | 建议值 | 单位 / 说明 |
|---|---|---|---|
| A-1 | `DIALOGUE_OPEN_RADIUS` | 3.0 | m；可开启对话的最大距离 |
| A-2 | `WITNESS_RADIUS / WITNESS_MAX` | 8.0 / 3 | m / 人；`WITNESS_MAX` 与 `GOSSIP_FANOUT_MAX` 对齐 |
| A-3 | `WITNESS_CREDIT_MUL` | ×0.5 | 无见证人时的 `ReputationHint.delta` 倍率 |
| A-4 | `SESSION_IMPRESSION_PER_TAG` | 1 | 条 / (会话 × tagId)（裁决 3） |
| A-5 | `DEADLINE_DEFAULT_DAYS / DEADLINE_MAX_DAYS` | 1 / 3 | 天；许诺默认次日到期，最长 3 天（需 `kept_word`） |
| A-6 | `RENEGOTIATE_MAX_PER_ENTRY / EXTEND_DAYS` | 1 / 1 | 次 / 天；宽限最多一次、最多延一天 |
| A-7 | `DEBT_INTEREST_MUL` | 1.5 | 代偿补交倍率 |
| A-8 | `PROTECTED_ACTOR_MAX` | 3 | 人；**与底座 LOD `PROTECTED` 上限同值同名单**（C-27） |
| A-9 | `CALLBACK_REUSE_POLICY` | `GREY_ON_REPEAT` | 同 `tagId` 回调二次达成 → 置灰明写而非隐藏 |
| A-10 | `ABSTAIN_MAX_PER_PROMISEE` | 1 | 条；压住"无限许诺不做某事"主导策略 |
| A-11 | `POST_RADIUS / POST_ABANDON_MIN` | 6.0 / 10 | m / 游戏分钟；`SERVICE` 中途离岗判定 |
| A-12 | `SERVICE_SPEC` 三件套 | `WATER_RUN 90min` / `NIGHT_WATCH 480min` / `BURIAL 60min` | 见 §2.4 |

---

## 附录 B · 待主理人裁决 / 与并行 GDD 对齐（9 项）

1. **C-23 · `LedgerEntry` 与底座 Part E / S3 的三重字段冲突（最需优先裁决）**：
   - 底座 Part E E2：`{ id, payload: Quantity, dueDayKey, state, reasonCode }`，`state ∈ {PENDING, ACTIVE, VOID}`；
   - 契约（本份）：`{ entryId, content, qty, deadlineDay, ..., state ∈ {OPEN, MET, BREACHED, VOID} }`；
   - S3 §4.3：`PROMISED/FROZEN/FULFILLED/DEFAULTED/VOIDED`（**预留实体**，非 LedgerEntry）。
   **建议**：以契约字段为权威；`id ≡ entryId`、`payload ≡ content+qty`、`dueDayKey ≡ deadlineDay`；Part E 的 `PENDING/ACTIVE` 合并为 `OPEN`；S3 的枚举保留但**明令禁止与 LedgerEntry.state 合并**，二者以 `entryId` 外键关联。
2. **C-24 · 印象标签存储粒度（最可能与 S2 冲突）**：本份 12 条 `tagId` 是 8 码 `claimCode` 的细分。**请 S2 确认 `ImpressionTag.tagCode` 存 12 码**（gossip 传播时 collapse 到 8 码）。若 S2 只存 8 码，12 条标签无法区分，`stole_well_water` 与 `broke_word` 会混为一谈。另请确认 #10 `broke_word → HIRELING`（码电荷为 0、标签按 NEG 处理）这一映射。
3. **C-25 · `PromiseBreached` 的 `reasonCode` 分流**：请 **S5 社区 GDD** 按闭集 8 值分流——`DEATH / NO_HEIR / BOTH_DEAD / TARGET_DEAD / TARGET_GONE` → `ReputationDelta = 0` **但仍必发 gossip**（继承 Part E ②，与 S3 E6 同款）。这是本份唯一能让"死亡不扣声望"与契约事件签名共存的做法（不新增字段、不新增事件）。
4. **C-26 · `ClusterId` 缺 FOOD → 无法许诺食物**：继承 S3 的 C-17。本份 `ItemSpec.cluster` 暂只支持 5 通货簇；**FOOD 类承诺待裁决后开放**，否则"我明天给你一份口粮"这类最高频的邻里承诺写不出来。
5. **C-27 · `protectedActors` 与底座 LOD `PROTECTED` 同名不同义**（§3.5）：建议取同一份名单并要求 `successorActorId` 非空，登记进底座 §D 为唯一定义处。
6. **C-28 · 日切第 7 步细分**：请求与 S3 的 C-14 一并处理，把第 7 步拆为 `7.1 到期判定 → 7.2 release() 释放预留 → 7.3 发 PromiseBreached`。否则"预留释放"与"违约事件"的相对顺序不确定，S3 的 A5 与本份验收 1 都无法举证。
7. **C-29 · 时序叠加后的"延迟一日"体感（§7-E3）——已拍板：接受为已知取舍，不是缺陷**。会话冻结（会话级）+ 声望快照（日级）+ gossip T+1 注入（S2）三者叠加 → 玩家 D 日做的好事要 **D+2** 才在对话里出现。
   **裁决理由**：放松任一层都会打开主导策略——① 放开会话冻结 → 玩家可在一次会话内反复进出对话刷印象标签，逐条解锁 CALLBACK 分支；② 声望当日生效 → 可在一天内刷满某社区声望换准入；③ gossip 当日注入 → 传播链失去"隔夜发酵"的可读节奏，且 fanout ≤3 跳的闭合性失去固定时点。**三层延迟是同一条原则（写队列 → 日切应用 → 下游读快照）在三个尺度上的同构表现，不可单独豁免一层。**
   **强制补偿（不得省略，列入验收）**：HUD「欠条」页**即时**显示账本事实（`PromiseMade` 落账即写，不等日切）；好感类反馈的即时性由**世界内行为信号**（NPC 让路、留门、多给一份）承担，不由对话文本承担。
   **UI GDD 须知**：不得为"让反馈更快"而在对话层私开即时通道。
8. **C-30 · 接口请求汇总**：
   - 请 **S5**：承接 `ReputationHint` 与 `PromiseBreached` → `ReputationDelta`（按 C-25 分流）；提供 `LedgerQuery(actorId, state?)`；`LedgerEntry` 所有权归 S5，S4 只发 `PromiseMade`。
   - 请 **S2**：承接 `ImpressionAdded`（存 12 码，collapse 到 8 码）；按 `(topicActorId, tagId)` 全局去重（既有 `MERGED_AND_STOP` 需扩到印象层）；消费 `GossipSeed`。
   - 请 **S3**：确认 `reserve/release/fulfill` 只由 S4 调用（≤3 笔、参数原子化）；确认 `hasCuriosity("CAP")` 走 `{carried:"cap_bottle", qty}` 而非新增 `RequirementExpr` 变体。
   - 请 **S1**：确认 `getActorState("player").carrying` 是 `carriedItems[]` 的唯一来源。
9. **C-31 · 与并行 GDD 最可能冲突的三点（提请汇编时优先比对）**：
   - **印象标签粒度**（C-24）：S2 存 8 码 vs S4 需 12 码，且 S2 附录 B 的 C-08 明确要求"对话 GDD 直接消费此闭集，勿重建第二套标签名"——本份**没有**重建（12 条全部 collapse 到 8 码），但需要 S2 侧配合改存储粒度，否则语义上仍是两套。
   - **`LedgerEntry` 三处方言**（C-23）：底座 / S3 / 契约各写一套字段名与状态枚举，是本次批次最容易漏改的一处。
   - **`protectedActors` 与 LOD `PROTECTED`**（C-27）：同名、同为 ≤3，极易被实现为两份独立名单。

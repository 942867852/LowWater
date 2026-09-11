# Phase 2 跨 GDD 一致性评审报告

- **文档 ID**：PHASE2-REVIEW
- **评审对象**：`00-foundation.md` · `01-scarcity-loop.md` · `02-npc-simulation.md` · `03-economy-barter.md` · `04-dialogue-contract.md` · `05-community-creed.md`（共 2663 行）+ `game-concept.md`
- **评审日期**：2026-09-10
- **质量门位置**：Phase 2（系统设计）→ Phase 3（技术搭建）之间的门控

---

## 一、总体判定

> ## CONCERNS（有条件通过）

**结论**：8 份文档**可以**作为 Phase 3 的输入，但**不得**作为实现开工依据——有 4 项架构前置未确认（见第七节 A 组）。

判定为 CONCERNS 而非 PASS 的唯一硬理由：**`docs/architecture/` 尚未建立**，而本作有 4 项确定性/性能契约（可分流 RNG、离屏结算预算、边界 tick 补做判定、带因果引用的事件总线）在架构未确认前只能按理想态书写，存在返工风险。这是外部依赖，不是设计质量问题。
判定为 CONCERNS 而非 FAIL 的理由：9 道闸门**实质全部通过**，无一处阻塞性设计冲突；40+ 条跨文档冲突已全部裁决并机械回填（含收尾批新裁 8 条，见第九节）。

> **收尾批更新（S6/S7 完成后）**：本轮 **S6 审查发现并修复了一处既有的真 bug**——死亡系 gossip 会经 S5 声望表产生非零 delta，击穿 Part E ②「死亡不扣声望」（裁决 19）。同时切分了两处职责重叠（gossip 传播权属、`GapSignal` 来源判定），消除了"两份文档都能做同一件事"的双实现风险。

---

## 二、规范系统编号（裁决 C-13）

各文档编写时用了互不统一的临时编号（经济被写成 S6/S9、稀缺被写成 S5 等）。已统一为**按文件名的规范编号**，并在每份文档文首加对照表：

| 码 | 系统 | 文件 |
|---|---|---|
| **S0** | 底座（角色判定 / 世界时钟 / 确定性） | `00-foundation.md` |
| **S1** | Scarcity-Loop 稀缺主循环 | `01-scarcity-loop.md` |
| **S2** | NPC-Simulation NPC 三层模拟 | `02-npc-simulation.md` |
| **S3** | Economy-Barter 易货经济与社区仪表 | `03-economy-barter.md` |
| **S4** | Dialogue-Contract 对话与契约账本 | `04-dialogue-contract.md` |
| **S5** | Community-Creed 社区、信条与声望 | `05-community-creed.md` |
| S6 | Rumor-Network 传闻传播（待写） | — |
| S7 | Work-Board 缺口工作板（待写） | — |
| S8 | World-Content 区域内容与史线（待写） | — |
| S9 | Presentation-UX 呈现与交互（待写） | — |

---

## 三、18 条裁决的回填结果

| # | 裁决 | 落实位置 | 状态 |
|---|---|---|---|
| 1 | 编号规范化 | 6 份文档文首对照表 + 全文替换 | ✅ |
| 2 | **C-01 字段名统一为 `rad`**，`radAccum` 降为序列化别名 | S1 §0 / S0 §D | ✅ |
| 3 | **C-02 废止概念文档的第二套骰子**（`P(发现)=0.05+0.06×…`） | `game-concept.md` §3.1-C 已改为对 S1 三段 `rollCheck` 的引用 | ✅ |
| 4 | **C-03 claimToken 排序**：priority / plannedDay 前置，字典序仅作终局 tiebreak | S1 §3 | ✅ |
| 5 | **C-04 needReason 闭集 5 值** | S1 §2 | ✅ |
| 6 | **C-05 第 6 岗位 idle 作伪岗位**，`rated` 反算、不计 `filledPosts` | S2 §3 / S5 §4 | ✅ |
| 7 | **C-06 产能用 `effHead`**（含 0.5 部分完成）替代 `onDuty`，60% 阈值不变 | S2 §3 / S5 §4 | ✅ |
| 8 | **C-09 五簇产地归属**：河谷产 WATER；井窖产 MEDS+SEED（挂 `care_01`）；FOOD 挂 `patrol_01`；FUEL/AMMO 双方均不自产 | S3 §2.4 → 回填 S2 `nominalOut` | ✅ |
| 9 | **C-11 统一 `tickLevel`**，`lodStateOf` 废弃 | S2 §0 / §5 | ✅ |
| 10 | **C-14 + B-1 日切细分**：第 3 步→3.1/3.2/3.3；第 4 步→4.1/4.2/4.3；**第 5 步不动**；第 7 步→7.1/7.2/7.3 + **追加 7.4「违约 delta 补批应用」** | S0 §B3 + S3/S4/S5 同步引用 | ✅ |
| 11 | **C-17 / C-26 `ClusterId` 增至 6 键，新增 `FOOD`（`tradable=false`）** | S0 §D（含 `FOOD→"portion"`）· S3 · S4 · S5 | ✅ |
| 12 | **C-18 `SCARCE` 与 `TIGHT` 是两个指示量，禁止合并** | S3 §4 / S5 §4 | ✅ |
| 13 | **C-23 `LedgerEntry` 以 S4 为准 + 新增 `communityId`**；`PENDING/ACTIVE` 合并为 `OPEN`；预留实体状态禁止与 `LedgerEntry.state` 合并（`entryId` 外键关联） | S4 §2 · S5 · S0 Part E | ✅ |
| 14 | **C-24 印象标签存 12 码细分**，仅在 gossip 传播时 collapse 到 8 码 | S2 §2（第 135 行裁决块） | ✅ |
| 15 | **C-27 `protectedActors` 全局唯一一份名单（≤3）**，与 LOD `PROTECTED` 同源 | S2 §3.5 · S5 · S0 §D | ✅ |
| 16 | **C-29 接受"D+2 反馈延迟"为已知取舍**（非缺陷），补偿为欠条页即时显示 + 世界内行为信号；三层延迟同构，不得单独豁免一层 | S4 附录 B-7 | ✅ |
| 17 | **`ActorDied` 发出签名 6 字段** `{actorId, causeCode, dayKey, minute, locationId, postId}`，保留 `causeRef` | S2 §0 / S0 Part E | ✅ |
| 18 | 概念文档与 GDD 的属性/技能映射一致性 | 全部文档 | ✅ |

---

## 四、9 道闸门 Checklist

| # | 闸门 | 结论 | 证据 |
|---|---|---|---|
| ① | 随机纪律：无裸 `rand`、无渲染帧判定、`streamId` 静态可枚举 | ✅ | S0 §C3 红线四条；S1 §3；S2 **raw RNG stream 数 = 0**（全部走 `rollCheck`，gossip 传播对象与改道路线改为确定性排序） |
| ② | `ActorId / ClusterId / DayKey / Quantity` 命名与单位与 S0 §D 逐字一致 | ✅ | S0 §D 为唯一定义处；`ClusterId` 已扩为 6 键含 `FOOD→"portion"` |
| ③ | 无第二套水/食/睡眠数值（L1 与 PhysioState 已合并） | ✅ | `NeedVector{ safety, social, creed, physio: Ref<PhysioState> }`，写权归 S1 |
| ④ | `ActorDied` 四系统消费点齐全 | ✅ | S1 释放 claimant · S2 停止调度+当日 blocks 记 0 · S4 对话 ABORTED + 承诺 VOID · S5 岗位空缺 + `staffingRatio` 重算，连续 3 日补岗失败则裁撤岗位并升级为 urgency 5 缺口 |
| ⑤ | 跨系统回写均走「写队列 → 日切/整点应用 → 下游读昨日快照」 | ✅ | S0 §B3 日切九步（已细分）；S3 明写 `TradeResult.priceImpact` 仅作展示预测，改价只在日切发生一次 |
| ⑥ | `ScheduleChanged.visibleSignal` 每种 `changeKind` 都有非空可见信号 | ✅ | S2 §3 D2 四级阈值 2/4/6/8（REROUTE / ADD_WATCH / DEEPEN_STASH / SET_TRAP），且**信号先于行为**：WorldSignal 下一 `HOURLY_TICK` 安装、日程次日 06:00 才变 → 玩家提前 ≥6 游戏小时可读 |
| ⑦ | `safeDays` 公式取 `min` 而非加总 | ✅ | S3 §3：`safeDays = min(waterPersonDays, foodPersonDays)`；附 A3 双向木桶断言，取到加总值即判失败 |
| ⑧ | `LedgerEntry` 字段集在 S4/S5/S2 三处一致 | ✅ | 见裁决 13；S5 承接所有权并提供 `LedgerQuery` |
| ⑨ | 属性/技能映射与概念文档一致，无系统偷用错属性 | ✅ | S2 的全部判定仅三处：`MIND/survey` 争位、`VIGOR/stealth` 发现被盗、`MIND/survey` 改道 |

**闸门全部通过（9/9 ✅）。**

---

## 五、每份文档判定

| 文档 | 判定 | 说明 |
|---|---|---|
| S0 底座 | **PASS** | 常量表与日切九步已成为事实上的全局契约源头，回填后自洽。三处标「架构 stub」待 Phase 3 |
| S1 稀缺主循环 | **PASS** | 发现率已改为三段 `rollCheck`，无第二套骰子；R2 给了 4 条机制级回答（claimant 先于 items 暴露 / 竞争拾荒 / `isDisturbed` 改道 / 技能闸门决定隐藏层深度） |
| S2 NPC 模拟 | **PASS** | **本轮质量最高的一份**。raw RNG stream = 0、禁随机迟到（`startMin` 纯算术）、记忆权重整数化 `weightQ = 7 − ageDays` 规避浮点漂移；自留预算 ≤3.20ms p95；明写取舍「匿名 FULL 上限砍到 16」 |
| S3 易货经济 | **PASS** | D3 数学闭环：`f'(S) < 0` 证价差严格单调缩小；`c(q) ≥ c_min > 0` 证永不归零。RU（口粮单位）解决"无货币但有比价"且不进任何库存 |
| S4 对话契约 | **PASS**（含 1 条已知取舍） | 两态写作法落地（intent 绑 `postRole` 不绑 `actorId`、`resolveSpeaker` 零随机、缺位三分支 HEIR/GRAVE/RUMOR）；C-29 延迟已拍板为取舍 |
| S5 社区信条 | **PASS** | 站队互斥做成算术（MEMBER −24 / SWORN −36，退队 B 侧敌意不还，「数字会原谅，人不原谅」）；8× 分流表完整 |

---

## 六、主理人补充裁定（本轮追加）

| 项 | 裁定 | 理由 |
|---|---|---|
| 属性是否可成长 | **Core 期不可成长** | 天花板 `maxLvl = floor(attr/2)+1` 已构成双层稀缺，再加成长会让创建期分配失去权重 |
| `TIME_SCALE` | **60**（1 游戏日 = 24 现实分钟） | 下界 15 分钟会让 D2 蹲点失去操作空间，上界 30 分钟诱发读档（违反 P3） |
| 大成功与骰面解耦 | **采纳**（用 `margin ±5` 表达） | 下游只读契约三字段，骰面实现可换 |
| `nominalOut` 读法 | **按"岗位总额定"** | S3 已按此写完所有数值，改读法需全部产能表 ×rated 重算，成本高且无收益 |
| `CreedRationRule` 是否两社区分化 | **Core 期沿用同值**，Extended 再分化 | 井窖「留种」本应把 SEED 提前，但 Core 需要两个社区的可比性来校验验收 1 |

---

## 七、仍未解决的开放问题（进入 Phase 3 前必须处理）

### A. 架构前置（阻塞，需 engineering-lead 确认）
1. **可分流 RNG**：能否提供按 `streamId` 分流、可存档/读档恢复的伪随机源？验收 6 要求 save/load 后 10 日差异 ≤5%。
2. **离屏结算预算**：≤120 实体（FULL ≤32）在 GTX 1060 6GB / 16GB RAM / SATA SSD、1080p 下仿真 ≤6ms/帧；S2 自留 ≤3.20ms p95。需确认 LOD 升级/降级的切换抖动可接受。
3. **边界 tick 补做判定**（S2 §4.4）：LOD 降级时判定锚定边界 `absTick` 而非"发现帧"，需确认可实现——**否则确定性论证不成立**。
4. **事件总线因果引用**：`AttributionLink{step, causeRef, effectRef, actorIds[], dayKey}` 需要事件总线支持"事件携带因果引用"而不只是广播。

### B. 设计开放项（不阻塞，但需在实现前定）
5. S1 附录 A 的 4 个候选常量（`HAZARD_RAD_PER_HOUR` 等）**未就地生效**，待批准后回填 S0 §D。
6. **C-32 河谷 `water_02` → `PostRoster.posts` 长度可变（7/6）**：S5 请 S2 改为读 `PostRoster` 而非硬编码，需实现期校验。
7. `CreedRationRule` 两社区分化 → 已裁定 Core 沿用同值（见第六节）。
8. 四份待写系统（S6 传闻 / S7 工作板 / S8 区域内容 / S9 呈现）的编写批次：建议 Phase 2 收尾批或 Phase 3 并行。

### C. 范围提示
- 5 份 GDD 的实际篇幅为 367–513 行，均超出 250–350 的目标值，**主因是 R1/R2/D2/D3 四个必答项不可压缩**。判断为**内容密度取舍，不做删减**——但 S8 区域内容 GDD 必须控制篇幅，否则总文档量会超过可维护阈值。

---

## 八、下一步建议

1. **立即进入 Phase 3 技术搭建**（`engineering-lead`）：先做架构文档 + 至少 3 条基础层 ADR（RNG 分流策略、离屏 LOD 与 tick 边界、事件总线与归因链），**优先解答第七节 A 组 4 项**——它们决定了 S0/S2 的两处确定性论证是否成立。
2. **并行**：`art-director` 出可访问性分级（Basic/Standard/Comprehensive）——P2「看得见的作息」高度依赖色盲友好的需求气泡与作息可视化。
3. **Phase 2 收尾**（可与 Phase 3 并行）：补写 S6 传闻网络 / S7 缺口工作板两份消费者型系统 GDD，二者只消费既有接口、不新增契约，风险最低。
4. **垂直切片目标不变**：Core 七条验收（概念文档 §四-CORE）仍是唯一放行标准，尤其是验收 1（三跳连锁可复现）与验收 2（NPC 位置预测 ≥70%）。

---

## 九、Phase 2 收尾批（S6 / S7）评审

**新增文档**：
- `systems/06-rumor-network.md`（S6 传闻传播网络，457 行）
- `systems/07-work-board.md`（S7 缺口工作板，427 行）

### 9.1 职责切分（主理人先于派工划定，两份均未越界）

| 系统 | 归 S2/S5 | 归 S6/S7 |
|---|---|---|
| Gossip | S2：种子生成 + **失真码表与打分规则定义** | **S6：传播拓扑**（中继资格 / fanout / hop / reachedSet / 注入与丢弃 / 去重合并） |
| 缺口 | S5：`GapSignal` 数据层产出（五路来源） | **S7：翻译为可行动目的地**（排序 / 呈现 / 过期撤销 / 回填确认） |

### 9.2 收尾批 8 条裁决

| # | 裁决 | 落实 |
|---|---|---|
| **19** | **修补既有漏洞 R-6**：S5 §3.1 事件值表 gossip 行原写 `GossipEmitted`，使 Part E 死亡系传闻也算声望 → 改为源事件 `RumorInjected`，且 **DEATH 系 `seedKind` 闭集**（`PROMISE_VOIDED_BY_DEATH / NO_HEIR / BOTH_DEAD / TARGET_DEAD / TARGET_GONE`）**delta 强制 0**，仍写 `MemoryFact(ESTATE_LOSS)` 且必发 gossip。`--assert-estate-no-rep` 现同时约束 §3.4 与 §3.1 两处 | ✅ S5 |
| **20** | `GapSignal` 增列 **`sourceKind` 闭集 5 值**（`STOCK/CREED/POST/PERSON/PROMISE`）；S5 增暴露 **`CreedConflictClusters(cid) -> ClusterId[]`**，S7 不再靠可选字段反推（消除边缘误判） | ✅ S5 |
| **21** | **`GapSignal.urgency` 数字不对玩家显示**（裁决 B-1）。玩家侧只给可读语言（还行 / 明天紧 / 今天就要 / 有人在挨 / 已经在死）。三条理由：数字会让它变回任务等级；P2 要求不用教学就能懂；玩家会为刷高 urgency 而等待社区恶化（违反 P4）。**禁止星级/条形图等可数化替身** | ✅ S5 §8-A9 |
| **22** | **gossip 传播拓扑所有权归 S6**（R-8）。S2 只保留种子生成 + 失真码表定义，以纯函数暴露 `distort(claimCode, relayActorId, dayKey) -> {nextCode, scoreDetail}`（裁决 23）。消除了"S2 与 S6 都能传"的双实现风险 | ✅ S2 §0.0 / §3.7 |
| **23** | S2 暴露 `distort()` 纯函数，S6 每跳调用；S6 不得重建第 9 个 `claimCode` | ✅ S2 §3.7 |
| **24** | 每 actor 每日 **SOCIAL 块 ≤ 1**（R-3），进一步压缩传播面 | ✅ S2 §3.7 |
| **25** | 由传闻（`sourceHop > 0`）写入的印象**每人 ≤ 2 条**，terminal 码豁免且永不淘汰；保留 8 码 → 12 码默认映射表 | ✅ S2 §3.7 |
| **26** | `MemoryEvent.kind` 闭集新增 **`RUMOR_HEARD`**（R-4），`refId = topicSeedId` | ✅ S2 §2 |

### 9.3 采纳的关键设计（不改，仅记录）

| 项 | 采纳内容 |
|---|---|
| **传播节奏** | `GOSSIP_HOPS_PER_DAY = 1`（一日一跳）而非三跳同批。理由：给玩家**三个早晨的干预窗口**——这是 D4「名声先你一步到达」可行动性的前提。改 3 只需换一个常量，算法与闭合性证明不变 |
| **规模上界** | 16 个有名字 NPC 下，单日 `RumorInjected` ≤ **32**（inbox≤2 是绑定约束）；单次 06:00 批处理 p95 ≤1.5ms，**摊销 0.001ms/帧 ≈ 6ms 预算的 0.02%**。常态日 4–10 条 |
| **S7 写入量 = 0** | `WorkOrder` 是 `GapSignal → 可行动目的地` 的**纯函数投影**，硬断言 `--assert-board-no-write` |
| **回填确认 = 世界自己确认** | S7 只做证据比对（`RationIssued.shortfall==0` / `PostRoster(D+1).onDuty≥1` / S4 `MET`），新增 `resolvedBy ∈ {PLAYER\|NPC\|TRADE\|NONE}`。**没有验收的人**——这是"无发布者"的最后一块拼图 |
| **`qty` 永不缩水** | `haulFit` + `tripHint` 只影响排序与文案，世界不会因玩家背不动而少缺 |
| **沉降规则** | 同 `orderId` 连续 ≥3 日且 `urgency ≤ 2` → 移出配额落到板背一行小字。板报**变化**，否则第 3 天起三行每天都一样，正撞 R2 |
| **第一天样例** | 河谷 FUEL 1.35L > MEDS 0.18 剂 > SEED 0.09 份；井窖 FUEL 1.05L > MEDS 0.14 > SEED 0.07，**全 urgency 1——板是平静的**。井窖致命缺水要到 ~D11 才出现，中间 7 天由 `safeDays` 箭头产出自设目标 → **直接服务 Core 验收 3** |

### 9.4 S6 / S7 判定

| 文档 | 判定 | 说明 |
|---|---|---|
| S6 传闻网络 | **PASS** | 回环检测是**集合判定非启发式**（`visited` 单调，候选必 ∉ visited）；拓扑为分层 DAG，接触图**现算不持久化、不占日切步骤**；并主动发现了 S5 的既有漏洞 R-6 |
| S7 缺口工作板 | **PASS** | 给出 **7 条**结构性差异（要求 ≥4）；「缺口在路上被 NPC 自行解决」5 条处理规则（不静默消失 / 通知在下一 `HOURLY_TICK` / 不补偿 / ≤1 条转介 / 不白跑≠有补偿） |

### 9.5 遗留开放项（不阻塞 Phase 3）

1. **B-2**：S3 需确认并写入**起始仓 MEDS/FUEL/SEED 的具体值**。主理人裁定：**不得为 0**（否则第一天无可交易物、S7 样例无法成立），但**每日产出为 0**（三簇双方均不自产）→ 存量持续消耗、缺口持续存在。具体数值由 S3 定。
2. **B-5**：S8 区域内容需给工作板的 `locationId` 与路径表。
3. **B-6**：S9 呈现需遵守三条渲染纪律（不得渲染 urgency 数值、板是世界物件需进 `observeRadius`、离开后转随身笔记并显示 `frozenAt`）。
4. **配额语义分叉**：S5 的 ≤3 是**生成上限**，S7 的 ≤3 是**板面配额**，沉降后二者会分叉。裁定：**不算放宽**（板面配额恒 ≤ 生成上限）。
5. **篇幅**：8 份 GDD 实际 367–513 行，均超 250–350 目标。按 §七-C 判例**不做删减**（必答项不可压缩），但 **S8 区域内容必须控制篇幅**，否则总文档量超过可维护阈值。

---

## 十、Phase 3 技术搭建评审

**新增资产（`docs/architecture/`，共 8 份 2409 行）**：`architecture.md`(685) · `adr/ADR-000-architecture-review.md` · `adr/ADR-001-engine-selection.md` · `adr/ADR-002-divisible-rng.md` · `adr/ADR-003-offscreen-lod.md` · `adr/ADR-004-event-bus-attribution.md` · `adr/ADR-005-fixed-point.md` · `control-manifest.md`(400)
**可访问性**：`docs/architecture/accessibility.md`（476 行）

### 10.1 四项前置判定（阻塞项，全部解开）

| # | 前置 | 判定 | 结论 |
|---|---|---|---|
| A-1 | 可分流存档 RNG | ✅ **PASS** | `pcg32_at(seed64, absTick×4096+callSeq)` 无状态纯函数，**存档连 per-stream counter 都不用存** |
| A-2 | 离屏结算预算 ≤6ms | ✅ **PASS** | 旧表有双重记账 + 40/80 口径错；改 tick 分级后：最坏单帧 **4.880ms**、摊销 **0.057ms/帧**、快进 8h ≈**18.7ms** |
| A-3 | 边界 tick 补做判定 | ✅ **PASS** | **可实现，不是 FAIL**。锚定 boundary 的 `absTick` + 全局排序键 `(absTick, actorIndex, seqInActor)` + 三条 CI 可断言不变式。最强检验：**快进 ≡ 逐 tick ×480 逐位相同** |
| A-4 | 事件总线因果引用 | ✅ **PASS** | 同步有序单线程 + `step` = 跨系统边界次数 + append-only + 闭合集机器判据 |
| B-5 | S1 附录 A 四个候选常量 | ✅ 批准回填（`HAZARD_RAD_PER_HOUR` 附条件：同一 `causeRef` 24h 去重） |

**A-3 通过是本项目最关键的一次解阻塞**——S2 §4.4 的确定性论证整体依赖它。

### 10.2 本轮发现的两个设计层 BLOCKER（已回填）

| 裁决 | 内容 |
|---|---|
| **27** | **修正 60× 量纲错误（G1）**：S0 §B3.1 累加器原写 `acc += dtReal × TIME_SCALE` 配 `while (acc ≥ 1)` 且 `TIME_SCALE` 释义为"游戏分钟/现实秒" → **1 游戏日 = 24 现实秒**（应为 24 分钟）。修法：`TIME_SCALE` 数值保持 60，释义改为**游戏秒/现实秒**，门槛改为 `SIM_TICK_COST = 60`。✅ 已回填 S0 §B3.1 与 §D |
| **28** | **视锥移出 simLOD（G2）**：视锥是相机朝向的函数，若参与仿真 LOD，则**玩家转一下头就改变了 NPC 的仿真路径**，且重放须额外记录相机 yaw。仿真 LOD 只由距离与 pending 事件决定；视锥只驱动 `renderLOD`（表现层私有、不进存档）。副产品对 P2 是纯利好：转头只会让远处 NPC 变简模，**不会让他的作息改变**。✅ 已回填 S0 §B3.2 |
| **29** | `LOD_FULL_MAX` 40 → **32**（取 40 时 S2 = 3.51ms，超自留 3.20ms）。✅ 已回填 |
| **30** | S0 §C3 预算表**废止**，改用架构文档 tick 分级口径（旧表双重记账 + FULL/COARSE 子集关系误作并列）。✅ 已回填留痕 |
| **31** | 采纳 **milli 定点**（ADR-005）。S0 §C7-3 只给了"禁止浮点漂移"的禁令没给替代，ADR-005 补上 |

### 10.3 可访问性侧的 6 条裁决

| 裁决 | 内容 |
|---|---|
| **32** | `urgency` 4/5 的**红标降级为冗余通道**（非主判据）。主判据改为"板面被改造程度 + 附加物"，与裁决 21「禁数字与可数化替身」一致 |
| **33** | **排序泄漏可接受**：S7 排序键首位 `urgency desc` 使板面顺序隐含序数——但**顺序是世界事实**，接受；**禁止行号、序号、缩进、列表点** |
| **34** | **阅读与减速**：默认**不暂停、不减速**（减速等于软暂停，会让"赶在 06:00 前到水站"这类决策失去意义），但**阅读本身无时限**。**可访问性优先于 P3 的严格性**：Basic / Comprehensive 档提供 0.5× 与减速，存档标记"非标准时间流速" |
| **35** | **绊线可见性**：信号层做 **≥1.5px 最小宽度钳制**（不依赖抗锯齿方案），避免 TAA 吃掉 1px 绊线；抗锯齿方案由技术侧定 |
| **36** | 需求气泡的**信条槽默认关闭**（S2 现画三条：生理/安全/社交） |
| — | 采纳艺术侧的 **10 glyph 全局唯一形状字母表**（水滴/碗/月牙/竖菱形/双扣环/盾/火焰/排夹/十字/芽）+ 底盘 3 值消歧，**禁止任何系统自建第二套图标**；三重编码（形状 → 位置钟位 → 运动），色相仅作冗余 |

### 10.4 Phase 3 质量门判定

> ## **条件性 PASS**

- **4 项技术前置全部 PASS**，其中 A-3 的解阻塞使 S2 的确定性论证成立。
- 两个设计层 BLOCKER（量纲、视锥）已发现并回填，均为**设计文档的错误**而非架构不可行。
- **唯一未决：G5 引擎选型**——程基岩推荐 **方案 E：`simcore`（C++20 静态库 + C ABI，引擎无关）+ Godot 4 表现层**，理由是难点在"确定性 + 可观测 + 低配"而非画面。**真正要拍的不是品牌，是「simcore 引擎无关化」这条**；品牌可延后。
- **不阻塞项**：simcore L1（Fixed / RngStream / EventBus / WriteQueue / 累加器 + CLI 骨架）不依赖 G5，可立即开工。

### 10.5 留给用户的拍板项

1. 🔴 **是否采纳 simcore 引擎无关化**（推荐）+ Godot 4 表现层；或改选 Unity 6 / Unreal 5 / 纯 Godot 单体
2. 是否认可"可访问性优先于 P3 严格性"的裁决顺序（裁决 34）
3. 是否接受"打磨期方可验收"的 S8/S9 缺位（当前 `ContainerDef`、路程常量表、UI 布局尚未定义，simcore 可先跑，垂直切片需等）

### 10.6 下一步

**建议先只做 simcore L1 骨架**——它不依赖 G1/G2 之外的任何裁决，且能立刻跑通 ADR-002（RNG）与 ADR-005（定点）的全部验证，同时为 Core 验收 1（三跳连锁可复现）与验收 6（save/load 差异 ≤5%）提供最早的实测证据。

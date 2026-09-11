# S0 · 底座规格单（Part A 角色判定 · Part B 世界时钟 · Part C 确定性）

- **Task ID**：GDD-000｜**阶段**：Phase 2 · 批次 B0｜**优先级**：P0（后续 5 份 GDD 的公倍数）
- **主笔**：design-strategist（文策渊）｜**调度**：主理人（游承峰）｜**状态**：待拍板（附录 A 三项待定）
- **覆盖**：A · S0·A 角色与判定｜B · S0·B 世界时钟与离屏结算｜C · S0·C 确定性契约｜D · 全局常量表｜E · EstateSettlement 死亡清算
- **依赖已读**：`design/gdd/game-concept.md`｜**尚缺**：`docs/architecture/*`（标注「架构 stub」处需与程基岩对齐回填）

## 〇·〇 规范系统编号对照表（PHASE2-REVIEW · 裁决 C-13，全文已按此表替换）

> **唯一权威编号 = 文件名序号。** 本文档为 S0，其五个部分以 `S0·A/B/C/D/E` 寻址。

| 规范编号 | 系统 | 文件 | 本文档原用临时编号 |
|---|---|---|---|
| **S0·A** | 角色与判定 | `00-foundation.md` Part A | S1 |
| **S0·B** | 世界时钟与离屏结算 | `00-foundation.md` Part B | S2 |
| **S0·C** | 确定性与可观测 | `00-foundation.md` Part C | S12 |
| **S0·D** | 全局常量表（唯一定义处） | `00-foundation.md` Part D | §D |
| **S0·E** | EstateSettlement 死亡清算 | `00-foundation.md` Part E | Part E |
| **S1** | 稀缺主循环 | `01-scarcity-loop.md` | S3 |
| **S2** | NPC 三层模拟 | `02-npc-simulation.md` | S5 |
| **S3** | 易货经济与社区仪表 | `03-economy-barter.md` | —（未引用） |
| **S4** | 记忆驱动对话与契约账本 | `04-dialogue-contract.md` | —（未引用） |
| **S5** | 社区、信条与声望 | `05-community-creed.md` | S7 |
| **S6** | 传闻传播（待写） | — | —（未引用） |
| **S7** | 工作板（待写；概念文档 C9） | — | —（未引用） |
| **S8** | 区域内容（待写） | — | —（未引用） |
| **S9** | 呈现 / UI（待写） | — | —（未引用） |

> **⚠ 两处编号未裁决（不猜，见 `06-consistency-review.md` FAIL-1）**：Part E 中 `S9⟨编号未裁决⟩`（原文"gossip 与账本"，跨两个规范系统）与 `S6⟨编号未裁决⟩`（原文"竞夺合法性"，指涉不可判定）。两处均已就地标记，替换待主理人裁定。

---

## 〇·一 全局契约（逐字遵守，不得改名／改字段）

```text
SeedContext : { worldSeed: int, streamId: string, tick: int }
rollCheck(actorId, attr, skillId, dc, seedCtx) -> CheckResult{ success, margin, critFail }
Quantity    : { cluster: ClusterId, amount: float, unit: "L"|"L"|"rd"|"dose"|"portion" }
```

## 〇·二 已拍板（本份负责落地）

1. NPC 的 L1 需求与玩家 `PhysioState` **共用一套生理数值**；L1 只保留 安全/社交/信条 三项，生理项为**只读引用**。
2. 安全天数 `= min(waterL/(3.0×pop), foodUnits/(1.0×pop))` —— **木桶，不加总**。
3. 跨系统回写一律：**当日只写队列 → 日切/整点批量应用 → 下游读昨日快照**。

---

# Part A · S0·A 角色与判定

## A1. 系统概览与目标

**支柱**：P2（判定必须可读到能预判）· P4（成长必须发生在主循环里）。**动词**：8 动词全部经此系统产出成败，S0·A 自身不产生内容。
**目标**：4 属性 + 8 技能 + 一条判定式覆盖全游戏所有检定，杜绝第二套骰子规则。**反目标**：无等级、无通用 XP、无天赋树、Core 期属性不成长。

## A2. 核心概念与数据模型

属性 `VIGOR/HAND/MIND/PRESENCE`，1–10：创建时各属性**基础值 1 + 10 点自由分配**，单项上限 10。**Core 期属性不可成长。**

| 派生量 | 公式 | 单位 |
|---|---|---|
| 负重上限 `carryCap` | `25 + 5 × VIGOR` | kg |
| 体力池 `staminaPool` | `60 + 10 × VIGOR` | 点 |
| 观察分辨率 `observeRadius` | `4 + 0.6 × MIND` | m |
| 议价偏移 `barterOffset` | `(PRESENCE − 5) × 4%` | 比例 |

**8 技能**（0–5 级），每项由唯一属性管辖，**每属性恰好管 2 项**（无孤儿、无过载）：

| 技能 ID | 中文 | 管辖 | 服务动词 |
|---|---|---|---|
| `scavenge_eye` | 拾荒辨识 | MIND | 读 / 取 |
| `survey` | 测绘 | MIND | 读 |
| `dismantle` | 拆解 | HAND | 取 / 制 |
| `repair` | 修理 | HAND | 制 |
| `fieldmedic` | 野地医护 | VIGOR | 守望 |
| `stealth` | 潜行 | VIGOR | 取 / 守望 |
| `barter` | 议价 | PRESENCE | 易 |
| `persuade` | 言辞 | PRESENCE | 说 / 站 |

> 裁决：`fieldmedic` 归 VIGOR 而非 MIND —— 本作医护是"恶劣条件下处理并背负伤员"的体力活，诊断交给 `observeRadius`（MIND），避免 MIND 一枝独秀。

`SkillState{ level: 0..5, practice: float, gateAttr }`｜`practice` 为**累计实践值，升级不清零**。

**生理状态（玩家与 NPC 共用，见已拍板 1）**
```text
PhysioState { water: 0..100, food: 0..100, sleep: 0..100, rad: 0..100 }
```
玩家读写；NPC 侧由 S2/S5 读写，**L1 只读引用**，L1 自有项仅 `safety/social/creed`（0–100）。换算见 §D。

## A3. 规则与公式

**A3.1 统一判定式**
```text
d      = rng(seedCtx).d20()                      // 1..20
raw    = attrValue + 4 × skillLevel + modTotal
margin = raw − dc
自然 1  → success=false, critFail=true,  margin = min(raw−dc, −5)
自然 20 → success=true,  critFail=false, margin = max(raw−dc, +5)
其余    → success = (margin ≥ 0), critFail = false
```
- `modTotal` 由 S0·A **在 rollCheck 内部**依 actor 状态累加；**调用方不得传修正、不得自改 DC**。合计 `clamp(−8, +6)`。
- 大成功 `isGreatSuccess(r) = (r.margin ≥ 5) && !r.critFail`。**有意与骰面解耦**：下游不依赖不可见的 d 值；代价是"自然 19 且余量 6"也算大成功，接受此模糊以换取下游只读 3 字段。
- 大失败（`critFail`）**必有具体后果**，由消费方定义，S0·A 只标注。禁止"大失败 = 普通失败"。
- 匿名 NPC / 玩家未练技能 → `skillLevel = 0`（不是 −1，也不允许省略 `skillId` 参数）。

**修正表 `modTotal`（唯一定义处，只能引用不得新增）**

| 来源 | 条件 | 修正 | 适用 |
|---|---|---|---|
| 疲劳 | stamina <30% / <10% | −2 / −4 | 体魄·巧手系 |
| 负重 | `speedMul < 0.70` | −1 | 体魄·潜行 |
| 脱水 / 饥饿 | water<20 / food<20 | −1 MIND / −1 VIGOR | 见条件 |
| 辐照病 | `rad ≥ 60` | −3 | 全部 |
| 伤病 | 轻伤 −1 / 骨折 −2 / 感染 −2 | | 伤表指定 |
| 光照 | 夜间无光 / 火光下 | −2 | 辨识·测绘 / 潜行 |
| 天气 | 暴雨 · 沙尘 | −2 | 辨识·测绘 |
| 工具 | 持对应工具且 `quality > 0.6` | +2 | 拆解·修理·医护 |

**A3.2 实践值累积（无 XP）**
```text
ppGain = PP_GAIN_BASE × dcTierW × gapMul × dailyDecayW × outcomeW
  dcTierW     : 平凡1.0 / 苛刻1.6 / 危险2.4 / 致命3.6
  gapMul      = clamp(1 + 0.15 × (dc − (attr + 4×level)), 0.5, 2.0)
  dailyDecayW : 同技能同 DayKey 内第 1–3 次 1.0，第 4–8 次 0.5，第 9 次起 0（当日封顶）
  outcomeW    : 成功 1.0 / 失败 0.25 / 大失败 0（不倒扣）
```
等级阈值（累计，不清零）：L1=5，L2=15，L3=30，L4=50，L5=75。

**A3.3 属性闸门** `maxLvl = clamp(floor(gateAttr/2)+1, 0, 5)` → 断点：属性 2→L2，4→L3，6→L4，**8→L5**。实践值可超阈值继续累积，闸门打开时立即兑现；UI 显示「瓶颈：需 巧手 8」。

**A3.4 为什么没有 XP（设计裁决，后续 GDD 不得引入）**
1. XP 让成长与主循环解耦——可绕过"真的去交易/真的去拆"而变强，直接违反 P4。
2. 实践升级让**每次变强都在世界留下痕迹**（被看见、被 gossip、消耗真实物资），成长即内容。
3. 无 XP 消灭"最优刷分路线"这一主导策略候选：8 技能 = 8 条不同的真实行为链，无法被一条路线吃透。
4. 与闸门形成**双层稀缺**：属性定天花板（创建时不可逆取舍），实践定离天花板多近（时间投入）。

## A4. 状态与流程

判定（纯函数，随机只来自 `seedCtx`）：`读 actor 状态 → 累加 modTotal → 取 d20 → 算 margin/critFail → 返回`。
升级：`结算后 practice += ppGain → 若 practice ≥ 阈值 且 level < maxLvl → level++ → 发 SkillLevelUp{actorId, skillId, level, dayKey}`；若 `level == maxLvl` 且 `gateAttr < 10` → 发 `SkillGateBlocked`（**仅 UI 提示与日志，不发负面事件**）。

## A5. 对外接口

- **暴露**：`rollCheck` · `getDerived(actorId)` · `getSkill(actorId, skillId)` · `grantPractice(actorId, skillId, pp, dayKey)` · `PhysioState`。
- **依赖**：S0·B（`seedCtx.tick`、DayKey）；伤病/辐照/负重状态由 S1 写入 actor 后只读。
- **架构 stub**：状态读取经 `ActorStateView` 接口，待架构回填。

## A6. 玩家可感知表现

判定前 UI 显示**档位名 + 数字**（「苛刻 14」）、成功率与 `modTotal` 明细（可展开）—— P2 要求玩家能预判。大成功/大失败有独立音效与镜头反馈且**必然后果可见**。技能面板显示 `practice/阈值` 与闸门瓶颈。

## A7. 边界情况与失败模式

1. **闸门死锁**：属性压到 1 → 对应两技能天花板 L1。允许，不救济；创建界面明示映射表防"建错号"。
2. **刷廉价判定**：第 9 次起 gain=0，且**只有 `margin ≥ 0` 或 `dc ≥ 14` 的判定才计数**。
3. **修正叠加穿透**：`clamp(−8,+6)` 之外，当 `raw ≥ dc + 8` 时 `success` 保底为 true（"稳过"），防不可玩。
4. **关键剧情大失败**：S? 须显式声明"允许大失败"；S0·A 层不豁免，由消费方兜底。

## A8. 验收标准与调试钩子

**验收**：① 全游戏仅一处判定实现（检索 `d20` 命中数 = 1）；② 固定 seed + 固定序列下结果逐位复现；③ 8 技能映射与概念文档一致。
**钩子**：`--dump-check <actorId>`（最近 50 次 `raw/dc/modTotal/margin`）· `--set-skill <actorId> <id> <lvl>` · `--force-d20 <n>`（仅调试种子流）。

---

# Part B · S0·B 世界时钟与离屏结算

## B1. 系统概览与目标

**支柱**：P3（世界不为你待机）· P2（看得见的作息）。**目标**：唯一时间源与唯一 tick 序列，使所有系统的"一天"是同一天；使离屏推进**可逆推、可归因**。**反目标**：不做暂停式跳过、不做玩家不在即冻结。

## B2. 核心概念与数据模型

`dayKey: int`（从 1 起）· `minuteOfDay: int 0..1439` · `absTick = dayKey×1440 + minuteOfDay`（唯一时间地址，供 RNG）。
**时间流速建议 `TIME_SCALE = 60`（1 现实秒 = 1 游戏分钟 → 1 游戏日 = 24 现实分钟）**，理由：
- **下界**：横穿 2×2km ≈ 25 游戏分钟，一趟"取水+搜刮+交易" ≈ 6–8 游戏小时；若 1 日 <15 现实分钟，玩家一天做不完两件事，D2 蹲点失去操作空间。
- **上界**：若 1 日 >30 现实分钟，"等一天"变成明显浪费，诱发读档而非接受后果（违反 P3）。
- **自洽**：与生理刻度咬合——水 −8 点/h → 12.5h 喝空；3.0 L/人/日 恰等于 24h 满耗（64 点/L × 3 L = 192 点 = 8×24）。**"一天三升水"在数值上就是"一天必须找一次水"。**
- 调试可选 `{30, 60, 120}`，**不改任何系统公式**。

**Tick 三层**：`SIM_TICK`（每 1 游戏分钟 = 1Hz）· `HOURLY_TICK`（`minuteOfDay % 60 == 0`）· `DAILY_CUTOVER`（00:00）· `DISPATCH`（06:00）。
**LOD**：`FULL`（≤40，行为树连续模拟）· `COARSE`（其余，统计近似）· `PROTECTED`（≤3，永驻 FULL）。

## B3. 规则与公式

**B3.1 固定步长累加器（确定性地基）**
```text
// 单位：acc 与 TIME_SCALE 一律【游戏秒】。1 SIM_TICK = 1 游戏分钟 = 60 游戏秒。
acc      += dtReal × TIME_SCALE          // TIME_SCALE = 60 游戏秒 / 现实秒
SIM_TICK_COST = 60                        // 每 tick 消耗 60 游戏秒（= 1 游戏分钟）
while (acc ≥ SIM_TICK_COST && steps < MAX_STEPS_PER_FRAME=4) { SIM_TICK(); acc -= SIM_TICK_COST; steps++ }
// 超出上限：保留 acc（不丢时间），世界变慢，绝不跳步
```
> **裁决 27（PHASE3 · 修 A，G1）—— 修正 60× 量纲错误**：原文 `acc += dtReal × TIME_SCALE` 配 `while (acc ≥ 1)` 且 `TIME_SCALE` 释义为"游戏分钟/现实秒"，会导致 **1 游戏日 = 24 现实秒**（应为 24 现实分钟，差 60 倍），并使 S0 §C3 的全部预算数字作废。
> 修法：`TIME_SCALE` 数值**保持 60 不变**，但释义改为**游戏秒 / 现实秒**；累加器门槛由 `1` 改为 `SIM_TICK_COST = 60`。核算：1 现实秒 → +60 游戏秒 → 1 游戏分钟；1 游戏日 = 1440 游戏分钟 = 1440 现实秒 = **24 现实分钟** ✓。`TIME_SCALE` 调试值 30/120 同理解读，语义不变。

**B3.2 LOD 切换（带滞回）**：升级 = 距离 < 120m；降级 = 距离 > 150m。滞回 120/150 防抖（P2 反对高频抖动）。FULL 名额 >32 时按 `有 pending 事件 + 有名字 + 距离倒数` 抢占，被挤下者立即降级并写 `LodSwitch`。

> **裁决 28（PHASE3 · G2）—— 视锥移出 simLOD**：原文"或在视锥内 / 且不在视锥内"**已删除**。视锥是相机朝向的函数，若参与仿真 LOD 判决，则**玩家转一下头就改变了 NPC 的仿真路径**（FULL↔COARSE 切换会改其行为与产出），且确定性重放必须额外记录相机 yaw——两者都不可接受。
> **仿真与表现的 LOD 彻底分离**：`tickLevel`（FULL/COARSE/PROTECTED）**只由距离与 pending 事件决定**，进存档、参与仿真；视锥只驱动 `renderLOD`（表现层私有、**不进存档、不影响仿真**）。
> 副产品（对 P2 是纯利好）：玩家转头只会让远处 NPC 变简模，**不会让他的作息改变**。
> **裁决 29（PHASE3 · G3）**：`LOD_FULL_MAX` 由 40 降为 **32**——取 40 时 S2 实测 3.51ms，超出其自留 3.20ms。
**B3.3 离屏结算**：原则「**离屏不做细节，只做结果**」——COARSE 实体不跑行为树、不做碰撞与遭遇，只按路点表 + 岗位产出 + 统计需求推进。离屏**允许**不可逆结果（死亡/迁居/被盗），但必须能被 `AttributionLink` 解释，否则视为未实现。**预告闸门**：离屏判死须满足"死亡前 ≥1 游戏日已出现可观测趋势"（水=0 持续 ≥6h 或 `rad ≥ 80` 持续 ≥12h），否则降级为伤病/失踪。FULL 与 COARSE 路径互斥，切换点必写 `LodSwitch`。

**B3.4 玩家睡觉 / 等待**

| 方式 | 步进 | 上限 | 推进方式 |
|---|---|---|---|
| 等待 WAIT | 30 游戏分钟 | 6 小时 | 正常 SIM_TICK（真实流逝，非跳过） |
| 睡觉 SLEEP | 4/6/8 游戏小时 | 8 小时 | **快进批处理**（chunk=60 游戏分钟，跳过渲染与 AI 细节，COARSE 用统计积分） |

睡眠恢复 `+12.5 点/h`；水/食**照常衰减**。**中断规则**（强制退出，已过时间不回滚）：`THREAT_NEARBY` · `CRITICAL_NEED`（water=0 或 food=0）· `COMMUNITY_ALARM`（社区进紧缺态或被袭）。**快进必须走同一 tick 序列**（同 `seedCtx` 流），这是 save/load 一致性的硬约束。性能预算：8 游戏小时快进 ≤800ms。

**B3.5 日切 00:00 批处理顺序（跨系统契约，顺序不可调换）**

> **裁决 10（C-14 + C-28 + B-1）已拍板**：第 3 步细分为 3.1/3.2/3.3；**第 5 步不动**；第 7 步细分为 7.1/7.2/7.3 并**追加 7.4「违约 delta 补批应用」**。第 4 步同步细分 4.1–4.4 以容纳 S5 §4.1 与 S2 §4.1 的既有引用。

| # | 步骤 | 权属 |
|---|---|---|
| 1 | 冻结当日写队列 → 按 `(priority, actorId, seq)` 排序 → 应用 | S0·B |
| 2 | 生理衰减 + 伤病推进（结算到 24:00） | S1 |
| **3.1** | 库存 / 岗位产出 / 消耗结算 | S1 / S2 |
| **3.2** | **价格更新** —— 读 `DaySnapshot[D-1]` 的 `effStock/demand` → 写 `P(D+1)` → 超阈值发 `PriceShock` | S3 §3.1 |
| **3.3** | **配给发放** —— 经 `tryTransfer` 逐人发 → 发 `RationIssued` + `RationGrant` | S3 §3.4 |
| **4.1** | 声望衰减 `R_c × 0.97` | S5 §3.1 |
| **4.2** | 应用 `Σ delta(pendingDayKey = D)` → `clamp(−100,+100)` → 分档 | S5 §3.1 |
| **4.3** | 写 `MemoryFact`（≤5，不衰减）；超限按淘汰键剔除 | S5 §3.1 |
| **4.4** | NPC 短期记忆窗口滑动（`weightQ −1`，<1 剔除）+ `LossStreak` 干净日结算与降级判定 | S2 §4.1 |
| 5 | 社区安全天数重算（§D，木桶 `min`）→ 发 `ScarcityStateChanged` | S3 §3.5 |
| 6 | **EstateSettlement Phase 2**（见 Part E） | S0·E |
| **7.1** | 承诺到期判定 → `state = BREACHED`，写 `reasonCode` | S4 §3.6 |
| **7.2** | `release()` 释放 `reservedByPromise` | S3 §4.3 |
| **7.3** | 发 `PromiseBreached{entryId, dayKey, reasonCode}` | S4 §3.6 |
| **7.4** | **违约 delta 补批应用** —— 应用 7.3 产生的 `ReputationDelta` → 进 `DaySnapshot[D]` | S5 §4.1 |
| 8 | 全局冲突指数检查 → 张力预算注入器（低于阈值注入 authored 缺口） | S0·B / R5 |
| 9 | `dayKey ← D+1`，清空写队列，**发布不可变 `DaySnapshot[D]`** | S0·B |

> **为什么必须有 7.4**：第 7 步在第 4 步之后。若违约 delta 不在本日切内补批，就要等到 D+1 日切的 4.2 才应用 → 玩家 **D+2** 才能在对话里看到后果。加 7.4 后：**D 日到期 → D 日日切判定并入 `DaySnapshot[D]` → D+1 全天对话可读**，延迟恰为 1 日。
> **7.4 不破坏"读昨日快照"纪律**：它仍在日切批量内、仍只写 `DaySnapshot`，当日对话仍读昨日快照。
> **物资侧的延迟与声望侧不同**：7.2 释放的预留物资发生在 3.3 配给**之后**，故该批物资要到 **D+1 日切的 3.3** 才进配给（S3 §7-E8）。**声望 D+1 可见、物资 D+1 才回到配给**，二者都恰为 1 日，不得再写作 D+2。

**下游只读昨日快照**：`dayKey = D+1` 期间，任何系统读跨系统量（价格/安全天数/岗位表）一律读 `DaySnapshot[D]`，**不得直读实时值** → 消除半日更新与顺序依赖。UI 上 pending 条目显示「明日生效」（P2）。

## B4. 状态与流程

时钟状态机：`SIM_TICK → (mod 60 ? HOURLY_TICK) → (minuteOfDay==1439 ? DAILY_CUTOVER) → (minuteOfDay==360 ? DISPATCH)`。`DISPATCH 06:00` 仅**发令**（分配岗位、生成 24h 路线），执行在随后的 HOURLY_TICK。
写队列：`QUEUED → APPLIED(整点/日切) → SNAPSHOTTED(日切后不可变)`。

## B5. 对外接口

- **暴露**：`now() -> {dayKey, minuteOfDay, absTick}` · `enqueue(WriteQueueItem)` · `getDaySnapshot(dayKey)` · `subscribe(tickKind, cb)` · `requestSleep(hours)` · `tickLevel(actorId)`。
> **裁决 C-11（PHASE2-REVIEW 已拍板）**：本接口名统一为 **`tickLevel(actorId)`**，旧名 `lodStateOf` **已废弃**，任何 GDD 与实现不得再出现。取值 `FULL / COARSE / PROTECTED`（§B3）。
- **依赖**：S0·C 提供 `absTick → seedCtx`。
- **架构 stub**：导航与路点插值；天气与季节（Extended）。

## B6. 玩家可感知表现

HUD 常驻时钟（日 / 时:分）+ 昼夜色温；社区仪表带"明日生效"标记。睡觉/等待有过场与「醒来后发生了什么」摘要（≤5 条，全部可点开看归因链）。06:00 有明确"出工"信号（门口聚集、灯灭），构成 P2 的日常锚点。

## B7. 边界情况与失败模式

1. **帧率暴跌**：`MAX_STEPS_PER_FRAME` 用尽 → 世界变慢而非跳步（保确定性），UI 不提示（防玩家利用）。
2. **快进中死亡**：睡觉时饿/渴死 → 中断规则优先，死亡结算在中断点执行，不留在快进尾。
3. **LOD 抢占抖动**：1 游戏分钟内切换 ≥3 次 → 强制锁定 COARSE 60 游戏分钟并告警。
4. **跨日链路**：`AttributionLink.dayKey` 记**效果发生日**，跨日另计 `latencyDays`，不重置 `step`。

## B8. 验收标准与调试钩子

**验收**：① 1 游戏日 = 24 现实分钟 ±0.5%（`TIME_SCALE=60`）；② 连续 10 日无 tick 丢失或重复；③ LOD 切换计数在固定脚本下可复现。
**钩子**：`--time-scale <n>` · `--skip-to <dayKey>:<minuteOfDay>`（走完整 tick 序列，非直接赋值）· `--tick-once` · `--dump-lod`。

---

# Part C · S0·C 确定性与可观测（本批仅确定性契约）

## C1. 系统概览与目标

**支柱**：P1（≤3 跳可归因）· P3（离屏不可逆必须可信）。**目标**：任何一次运行可**逐位复现**，任何一条因果可**导出成证据**。**本批不含**面板视觉设计（交 UX），只给需求规格。

## C2. 核心概念与数据模型

```text
RngStream(worldSeed, streamId) : seed64 = FNV1a64(worldSeed ‖ streamId)
value = pcg32(seed64, counter = seedCtx.tick)
AttributionLink { step: int, causeRef: RefId, effectRef: RefId, actorIds: ActorId[], dayKey: int }
RefId = "<system>.<entityType>.<id>[.<field>]"   例：S5.post.he_valley.water_03.output
```
**RNG 纪律（红线）**：① 禁止裸 `rand`/`Math.random`/系统时间/未初始化内存；② 禁止跨 `streamId` 共享计数器，禁止把 tick 或随机数拼进 `streamId`；③ `streamId` 命名 `<domain>.<sub>.<qualifier>` 且**静态可枚举**（`check.player.scavenge`·`ai.npc.he_valley.water_03.route`·`loot.zone.ruins_a.ctr_12`·`weather.global`·`gossip.he_valley`·`price.jing_cell.water`·`estate.<community>.gossip`）；④ **判定必须在固定 tick 上发起，禁止在渲染帧上发起**——否则调用次数随帧率变化，确定性崩塌。

## C3. 规则与公式

**固定种子重放**：`ReplayRecord{ worldSeed, gameVersion, simSchemaVersion, inputs[] }`，inputs 按 **tick** 打点（非帧）。每 60 tick 生成 `worldHash = FNV1a(生理+库存+位置量化到 0.1m+关系值)`，两次运行须逐位相同。版本不一致 → 拒绝重放并提示，**不静默**。
**save/load 可恢复**：存档须含 `worldSeed / dayKey / minuteOfDay / 每 streamId 的 counter / 写队列全部条目 / 每 actor 的 LOD 状态 / DaySnapshot[D-1..D] / AttributionLog 尾部`。禁止依赖运行时缓存（路径与导航须可由 seed 重建）。恢复后首 tick 必须是 `savedTick + 1`（不跳号、不补跑）。
**归因链**：`step` 从 1 起，**>3 即判定设计违规**（P1 硬约束），工具报警并记入评审。匿名 actor 仅 `step ≤ 2` 时记入 `actorIds[]`。
**验收 6（save/load 10 日差异 ≤5%）**：`max_d |S_orig[d] − S_reload[d]| / max(S_orig[d],1) ≤ 5%`，`d ∈ [d0, d0+10]`。理想值 0%；**LOD 近似是唯一合法差异来源**，且必须在调试面板可见。
**验收 5（仿真 ≤6ms/帧）** 预算拆分（GTX 1060 6GB / 16GB RAM / SATA SSD / 1080p / 实体 ≤120）：

| 项 | 预算（**旧口径，已废止**） |
|---|---|
| ~~FULL ≤40（行为树 + 连续量）~~ | ~~40 × 0.09ms = 3.60ms~~ |
| ~~COARSE ≤80（统计近似）~~ | ~~80 × 0.012ms = 0.96ms~~ |
| ~~写队列 + 归因记录~~ | ~~0.50ms~~ |
| ~~余量~~ | ~~0.94ms~~ |

> **裁决 30（PHASE3 · G4）—— 上表废止，以 `docs/architecture/architecture.md` §性能预算 的 tick 分级口径为准。**
> 废止理由（程基岩 ADR-003 复核）：① **双重记账**——FULL 的 0.09ms 已包含写队列开销，后者被单列二次计入；② **口径错**——FULL ≤40 与 COARSE ≤80 相加为 120，但 FULL 席位是从 COARSE 中"抢占升级"的，两者是**子集关系而非并列**，正确总实体数才是 120。
> 新口径结论：最坏单帧 **4.880ms**、摊销 **0.057ms/帧**、8 游戏小时快进 ≈**18.7ms**（远优于原文的 800ms 上限）。三项均在 ≤6ms 与存档一致性预算内。
> **本表保留仅为留痕，任何新引用一律指向架构文档。**

## C4. 状态与流程

`Boot → LoadSeed → (Load | NewGame) → Running{SIM_TICK 固定步长} → Snapshot(每 60 tick) → Save / ReplayVerify`。重放校验：逐 tick 比对 `worldHash`，首次不一致即中断并 dump 该 tick 全量状态。

## C5. 对外接口

- **暴露**：`rng(seedCtx)` · `makeSeedCtx(streamId, absTick)` · `recordAttribution(link)` · `exportAttribution(range)` · `snapshotHash()`。
- **依赖**：S0·B 提供 `absTick`；所有系统产生跨系统效果时**必须**调用 `recordAttribution`。
- **架构 stub**：序列化格式与存档槽位。

## C6. 玩家可感知表现

玩家看不到 S0·C，但能看到它的产物：世界从不"莫名其妙"变化——任何改变都能在归因链面板里追到一个具体的人。

## C7. 边界情况与失败模式

1. **UNATTRIBUTED 效果**：无法回溯 causeRef → 写入时**立即 warn** 并标 `UNATTRIBUTED`；验收 1 要求核心三跳链 `UNATTRIBUTED == 0`。
2. **新增 streamId 未进存档**：恢复后 counter 归零 → 重放偏移。启动时校验 streamId 枚举与存档一致，缺失即报错。
3. **浮点漂移**：仿真状态累加**禁止** `double` 与超越函数（sin/exp）；确需三角函数走查表 + 定点量化。
4. **超 3 跳链路**：不静默截断，报警 + 记入设计评审（这是设计违规，不是工程问题）。

## C8. 验收标准与调试钩子

**调度可视化面板（需求规格 —— 面板要看什么）**：① 24h 时间轴（横轴 `minuteOfDay` 0–1439），每 NPC 一行，色块 = `SLEEP/COMMUTE/WORK/IDLE/SOCIAL/ALERT/DEAD`，可展开为路点序列；② 岗位表（社区 × 岗位 × 额定/在岗/实际产出%）；③ 需求条（`PhysioState` 水食睡 + L1 安全社交信条，0–100，带阈值线）；④ LOD 实时视图（谁在 FULL/COARSE，切换时刻标红）；⑤ 写队列检视器（当日 pending 按 priority 排序，可手动触发日切）；⑥ 归因链检视器（点任一 `effectRef` → 反查 ≤3 跳，渲染 A→B→C 节点图，节点显示 actor 名字与数值变化）；⑦ RNG 检视器（streamId 列表 + 当前 counter + 最近 20 次取值）；⑧ 性能面板（仿真耗时 p50/p95 滑动窗口、实体数、FULL/COARSE 计数）；⑨ 种子控制（固定种子输入、重放、单步 tick、加速 ×1/×8/×60）。

**归因链导出（需求规格）**：格式 = `.jsonl`（一行一个 `AttributionLink`）+ 可选 `.graphml` + 人类可读 `.md` 摘要（供设计评审与验收 1 举证）；触发 = 面板按钮 / `--export-attribution <dayRange>` / `AttributionLog` 环形缓冲（7 日或 20k 条，超出落盘）；导出必含 `worldSeed / version / dayKey 范围`，**否则视为无效证据**。

---

# Part D · 全局常量表（唯一定义处）

> **其它 GDD 一律引用此表，不得就地定义新常量。需要新常量 → 提回本表。**

| 常量 | 值 | 单位 / 说明 |
|---|---|---|
| `WATER_L_PER_PERSON_DAY` | 3.0 | L / 人 / 日 |
| `FOOD_UNIT_PER_PERSON_DAY` | 1.0 | portion / 人 / 日 |
| `PHYSIO_POINTS_PER_LITER` | 64 | 点 / L（8 点/h × 24h ÷ 3.0 L） |
| `PHYSIO_POINTS_PER_FOOD_UNIT` | 96 | 点 / portion（4 × 24 ÷ 1.0） |
| `PHYSIO_DECAY_WATER / FOOD / SLEEP` | −8 / −4 / −5 | 点 / h |
| `SLEEP_RECOVER_PER_HOUR` | +12.5 | 点 / h（8h 睡满） |
| `SAFE_DAYS` | `min(waterL/(3.0×pop), foodUnits/(1.0×pop))` | 天，**木桶不加总** |
| `DC_TRIVIAL / DEMANDING / PERILOUS / DEADLY` | 10 / 14 / 18 / 22 | 平凡 / 苛刻 / 危险 / 致命 |
| `CRIT_MARGIN` | 5 | 大成功余量阈值 |
| `MOD_CLAMP` | [−8, +6] | 判定修正合计范围 |
| `SKILL_MAX_LEVEL` | 5 | |
| `ATTR_RANGE` | 1..10（基础 1 + 分配 10） | |
| `PP_GAIN_BASE` | 1.0 | |
| `PP_THRESHOLD` | L1=5, L2=15, L3=30, L4=50, L5=75 | 累计，不清零 |
| `CARRY_CAP` | `25 + 5 × VIGOR` | kg |
| `STAMINA_POOL` | `60 + 10 × VIGOR` | 点 |
| `OBSERVE_RADIUS` | `4 + 0.6 × MIND` | m |
| `BARTER_OFFSET` | `(PRESENCE − 5) × 4%` | |
| `SPEED_MUL` | `clamp(1.15 − 0.5 × (cur/cap)², 0.45, 1.15)` | |
| `BASE_WALK_SPEED` | 1.4 | m/s（实际 = 1.4 × SPEED_MUL） |
| `TIME_SCALE` | 60（调试 30/120） | **游戏秒 / 现实秒**（裁决 27 修正单位；`SIM_TICK_COST = 60` 游戏秒 = 1 游戏分钟） |
| `SIM_STEP` / `MAX_STEPS_PER_FRAME` | 1 游戏分钟 / 4 | 固定步长 |
| `LOD_FULL_RADIUS / LOD_DROP_RADIUS` | 120 / 150 | m，滞回 |
| `LOD_FULL_MAX` | 40 | 同时 FULL 上限 |
| `DISPATCH_MINUTE` | 360（06:00） | |
| `SCARCE_RATIO_ENTER / EXIT_DAYS` | 0.60 / 2 | 紧缺态滞回 |
| `GOSSIP_FANOUT_MAX` | 3 | 跳 |
| `MEMORY_SHORT_DAYS / IMPRESSION_MAX` | 7 / 5 | 天 / 条 |
| `REPUTATION_DECAY` | ×0.97 / 日 | |
| `ROAD_WINDING_MUL` | 1.10 | 布线折返系数（S8 §2.3 边长已含；**来源 S8 附录 A-1 · 裁决 B-2**） |
| `TIME_VALUE_RU_PER_GAME_MIN` | 0.03 | RU / 游戏分钟（`C_trip` 换算；**来源 S8 附录 A-2 · 裁决 B-2**） |
| `STAMINA_VALUE_RU_PER_POINT` | 0.05 | RU / 耐力点（`C_stamina` 换算；**来源 S8 附录 A-2 · 裁决 B-2**） |
| `ENCOUNTER_LOSS_RATE` | 0.25 | = S3 A-16 `LOSS_RATE`；单次遭遇平均损失**载货价值的 25%**（**来源 S8 附录 A-3 · 裁决 B-2**） |

> **S8 裁决补充（B-2）**：`C_trip` 采用**往返**口径（`C_trip = 2 × travelMin × TIME_VALUE_RU_PER_GAME_MIN + C_stamina`），单程口径**作废**（裁决 B-1）。`ROUTE_P_MIN / P_MAX` 为**逐路线静态配置**（按 `pathLenMeters` 与路线强制遭遇点给出），**非全局常量**，不登记本表；其唯一来源为 S8 §2.3。其余 S8 附录 A 常量以本表为唯一生效处。

**ID 命名规范（强制执行）**
```text
ActorId     : "player" | "npc.<community>.<role>_<nn>" | "anon.<nnn>"   例 npc.he_valley.water_03
CommunityId : "he_valley" | "jing_cell"
PostId      : "<community>.<role>_<nn>"   role ∈ {water, watch, patrol, mend, care}   // 伪岗 idle_00 见 S2 §2.3
ClusterId   : WATER | FUEL | AMMO | MEDS | SEED | FOOD   // 6 键（裁决 C-17/C-26）；TT 通货子集 = 前 5 键
DayKey      : int，从 1 起｜MinuteOfDay : int 0..1439｜Quality : float 0.4..1.5
LocationId  : "<zoneId>.<kind>_<nn>"   kind ∈ {gate, board, stock, post, market, landmark, exit}   // 来源 S8 §2.2 · 裁决 B-3/B-5
Post.locationId : "<community>.post_<role>_<nn>"   // 与 PostId 一一对应（S8 §2.2 · S5 §5.1）
KnowledgeId : "knowledge.<topic>"   // 史线碎片 refId 命名空间，供 S4 RevealKnowledge 与 S8 程九 RebuttalPoint.requires 引用（来源 S8 §2.7 · 裁决 B-4）
```
**`Quantity.cluster → unit` 固定映射（不得自定义）**：`WATER→"L"`，`FUEL→"L"`，`AMMO→"rd"`，`MEDS→"dose"`，`SEED→"portion"`，**`FOOD→"portion"`**。

> **裁决 C-17 / C-26（PHASE2-REVIEW 回填）**：`ClusterId` 由 5 键扩为 **6 键**，新增 **`FOOD`（口粮，单位 `portion`，`tradable = false`）**。
> **权威声明**：本行（§D）是全项目 `ClusterId` 的**唯一权威枚举**，S3 §2.2 / S1 §2.x 为引用方，不得再各自声明枚举。
> **边界**：`FOOD` 进 `Warehouse.stock` · 配给 · `SAFE_DAYS` 分子 · `reservedByPromise` · `ItemSpec.cluster`；**不进** `PriceTable / quote / execTrade`（S3 实现期静态断言）。"五簇通货"称谓不变。

**`protectedActors` 名单（全局唯一定义处 · 裁决 B-3/B-5 · 来源 S8 §2.6；与 S4 §2.6 / S5 §5.5 / S2 §2.3 同一份，禁止实现成两份）**

| # | actorId | 岗位 | `successorActorId` | 备注 |
|---|---|---|---|---|
| 1 | `npc.he_valley.mend_01` | `he_valley.mend_01` | `npc.he_valley.patrol_01` | 程九；样板角色。河谷 `mend` 岗 `rated==1`，无同岗次席，故 successor 取同社区备位者（**能力缺口为刻意 P4 叙事材料**） |
| 2 | `npc.jing_cell.care_01` | `jing_cell.care_01` | `npc.jing_cell.mend_01` | MEDS/SEED 唯一知识源与产地 |
| 3 | `npc.he_valley.water_01` | `he_valley.water_01` | `npc.he_valley.water_02` | 取水岗首席；`water_02` 为水岗**同岗次席** |

> **硬约束**：① 名单**全局唯一一份**（≤3），S0·B LOD `PROTECTED` 档 / S2 §2.3 `PROTECTED` 行为树与本表**同源**，**禁止实现成两份**（裁决 C-27）；② 每条 `successorActorId` **非空**且**全局单射**——**无任何 actor 被两个岗位引用**（`--assert-successor-injective`）；③ `COARSE` 期不判死（S4 §2.6 ①②③④ 四条保护规则）。原稿 `water_02` 兼任 `mend_01`+`water_01` 两处 successor 的确定性缺陷已按 S8 §2.6 裁决拆解：同 seed 下同时杀死两名 protected actor，继承结果**与死亡顺序无关**。

---

# Part E · 全局规则：EstateSettlement（死亡清算）

## E1. 系统概览与目标

**触发**：`ActorDied{ actorId, causeCode, dayKey, minute, locationId, postId }`

> **裁决 17（已拍板）**：以上 **6 字段为唯一发出签名**（与 S2 §0 契约逐字一致）；`minute` 是该事件内的字段名，语义等同 §B2 的 `minuteOfDay`。**同时保留 `causeRef` 作为附带字段**，供 `AttributionLink.causeRef` 归因链接入。旧签名 `{actorId, dayKey, minuteOfDay, causeRef}` 作废。

**目标**：让"死了一个人"在 **S1**（claimant 释放）· **S2**（岗位与调度、gossip 传播）· **S5**（产能与紧缺态、账本 `LedgerEntry` 权属、声望）产生**一致且可归因**的后果，且**隔天早上才被社区知道**（P2）。**支柱**：P4 + P1。本份定义，其它 GDD 引用。
> ⚠ 原文写作"S1/S2/S5/S9 四系统"。`S9⟨编号未裁决⟩` 所指的"gossip 与账本"两项，按内容已分别归 S2 §3.7 与 S5 §5.3；**该编号本身仍待主理人裁定**（见 `06-consistency-review.md` FAIL-1）。

## E2. 核心概念与数据模型

`OwnershipMarker{ resourceRef, claimantActorId, state }`｜`Post{ postId, rated, onDuty }`

**`LedgerEntry` 权威字段集（裁决 13 · C-23，以 S4 为准；本表旧字段名全部降为别名）**

```text
LedgerEntry {
  entryId, promisorId, promiseeId,
  kind: "DELIVER"|"SERVICE"|"ABSTAIN",
  content: ItemSpec|ServiceSpec, qty: float, deadlineDay: DayKey,
  witnessIds: ActorId[],
  state: "OPEN"|"MET"|"BREACHED"|"VOID",              // 四态，禁止扩充
  createdDay: DayKey, reserved: bool,
  communityId: CommunityId,                           // 裁决 13 新增（S5 提出，按社区路由声望必需）
  reasonCode?: ReasonCode, voidedDayKey?: DayKey      // 可选，由本 Part E 写入
}
```

| 本份旧字段名（降为别名） | 权威字段 |
|---|---|
| `id` | `entryId` |
| `payload: Quantity` | `content` + `qty` |
| `dueDayKey` | `deadlineDay` |
| `state ∈ {PENDING, ACTIVE}` | **合并为 `OPEN`**（裁决 18） |

> **权属**：`LedgerEntry` 由 **S5 §5.3 拥有**（唯一写 `state` 处）；S4 只经 `PromiseMade` 创建、只读查询；本 Part E 仅在死亡清算路径上写 `VOID`。**S3 §4.3 的预留实体状态（`PROMISED/FROZEN/FULFILLED/DEFAULTED/VOIDED`）与本四态禁止合并**，二者以 `entryId` 外键关联（裁决 18）。

## E3. 规则与公式

**两阶段执行**（与已拍板 3 一致）：

| 阶段 | 时机 | 内容 | 理由 |
|---|---|---|---|
| **Phase 1 即时** | 死亡当帧入队，下一个 `HOURLY_TICK` 应用，`priority = 0` | ① 释放 claimant、④ 的在岗人数 −1 | 影响玩家能否"抢"，是可玩性必需的即时反馈 |
| **Phase 2 日切** | 当日 00:00 批处理**第 6 步** | ② Ledger VOID + gossip、③ promisee 转 heir、④ 产能重算与紧缺态 | 账本与继承是"隔夜才知道"的事，符合 P2 |

① **claimant 释放**：`claimantActorId = null, state = UNCLAIMED`，写 `AttributionLink{ step:1, causeRef: ActorDied, effectRef: marker.resourceRef, actorIds:[deadId], dayKey }`。竞夺合法性归 S1/S6（stub）。**PROTECTED 名单（≤3）的 claimant 不释放，直接转 heir。**
② **Ledger VOID**：`WHERE promisorId = deadId AND state ∈ {PENDING, ACTIVE}` → `state = VOID, reasonCode = "DEATH", voidedDayKey = dayKey`。**不扣声望**（死不是违约，禁止触发 `ReputationDelta`）；**必发 gossip**：`GossipSeed{ kind:"PROMISE_VOIDED_BY_DEATH", subjectId: deadId, promiseeId, payloadRef }`，从死者社区内 2 名 NPC 起播（`streamId = estate.<community>.gossip`，fanout ≤3）。
③ **promisee 转 heir**：`heirActorId(postId)` = 同社区**同岗位在岗次席**（按 `seniority` 排序，死者之后第 1 位）；缺失 → `null` → 该 LedgerEntry 转 `VOID, reasonCode = "NO_HEIR"`（gossip kind 不同）。**heir 只继承权利，不继承义务**——防止"死亡自动制造新债务人"突破 3 跳。
④ **岗位降产能**：`实际产出 = 额定产出 × (effHead / rated)`；`ratio < 0.60` → 社区进 `SCARCE`；退出需 `ratio ≥ 0.60` **连续 2 日**（滞回）。S2 在次日 06:00 调度令尝试从闲人或 heir 补岗，下一日切重算。
   ```text
   effHead = Σ_worker completeFactor(worker)   // 完成 = 1；部分完成（迟到 >30min）= 0.5；缺席 = 0
   ```
> **裁决 C-06（PHASE2-REVIEW 已拍板 · 机械回填）**：分子由 `onDuty`（人头计数）改为 **`effHead`**（加权有效人头），令伤病/迟到产生可见代价。60% 阈值、滞回 2 日、`idle` 不计入两侧，**全部不变**。`completeFactor ∈ {0,1}` 时本式自动退化为旧式 `(onDuty / rated)`。
> **术语分工（勿再混用）**：`Post.onDuty: ActorId[]` 仍是**人头名单**（用于补岗与 `filledPosts` 判定）；`effHead` 是**加权强度**（用于产能与 `staffingRatio`）。二者同源、量纲不同，实现期不得互相赋值。
> 本行是全项目 `staffingRatio / 实际产出` 公式的**唯一权威式**；S2 §3.4、S5 §2.2 为引用方。
**玩家例外**：`actorId == "player"` 时**不触发** EstateSettlement（走 S? 结局/读档流程），但 **④ 照常**（若玩家占着岗位，立刻空缺）。

## E4. 状态与流程

`ActorDied → 入队 Phase1 → HOURLY_TICK 应用（释放 + 在岗−1）→ 日切第 6 步 Phase2（VOID / heir / 产能 / gossip）→ 次日 06:00 补岗 → 再次日切重算 ratio`。每一步均写 `AttributionLink`，`step ≤ 3`。

## E5. 对外接口

- **暴露**：`onActorDied(evt)` · `heirActorId(postId)` · `voidPromisesOf(actorId, dayKey)` · `transferClaims(actorId, heirId)`。
- **依赖**：S1（死亡与 claimant）· S2（岗位与调度）· S5（产能与紧缺态）· S9（gossip 与账本）。
- **约定为 stub**：竞夺合法性规则（S1/S6 定义）；`seniority` 排序键（S2 定义）。

## E6. 玩家可感知表现

第二天清晨：社区门口贴出名字、那个岗位空了一个人、有人在议论"他答应我的三升水没了"。玩家可立刻去抢那批无主物资——这是本作最锋利的道德时刻，**必须在 1 跳内可归因到那个死者**。

## E7. 边界情况与失败模式

1. **死者既是 promisor 又是 promisee**：两条路径独立执行不合并；同一 LedgerEntry 两端皆死者 → `VOID, reasonCode = "BOTH_DEAD"`。
2. **heir 同日死亡**：Phase 2 按 `actorId` 升序处理，已死者不再接受转移 → 顺延次席；顺延失败 → `NO_HEIR`。
3. **岗位唯一持有者死亡**（`rated == 1`）：`ratio = 0` → 立即 `SCARCE`；S2 连续 3 日补岗失败 → 触发 S5 岗位裁撤（不属本份）。
4. **离屏死亡**：仍须满足 B3.3 预告闸门，否则不判死、不触发本规则。

## E8. 验收标准与调试钩子

**验收**：① 固定种子下杀死指定 NPC，10 日内 ①②③④ 全部按序触发且 `UNATTRIBUTED == 0`；② ② 路径中 `ReputationDelta` 事件数 = 0；③ 每条链路 `step ≤ 3`。
**钩子**：`--kill <actorId>`（走完整死亡流程，非直接删除）· `--dump-estate <dayKey>` · `--assert-no-unattributed`。

---

## 附录 A · 待用户拍板（3 项）

1. **属性是否允许成长**：本份按"Core 期不可成长"写。若需要，建议只允许经信条仪式 +1、总量 ≤+2，但会削弱属性闸门的取舍张力。
2. **`TIME_SCALE = 60`（24 分钟/游戏日）是否接受**：偏慢，但保住 D2 蹲点的操作空间。备选 30（12 分钟/日，节奏快但一天做不了两件事）。
3. **大成功与骰面解耦**（自然 19 且余量 6 也算大成功）是否可接受：换来下游只读 3 个字段，符合固定契约。

## 附录 B · 后续 GDD 必须遵守的约束清单

1. 判定只有 `rollCheck` 一条；不得自建骰子、不得自改 DC、不得向 `rollCheck` 传修正参数（修正只走 actor 状态）。
2. `modTotal` 修正项**只能引用 A3.1 修正表**，新增须提回本表。
3. **没有 XP**：任何系统不得发放通用经验、等级或"技能点"。
4. 属性 Core 期不可成长；技能天花板 `maxLvl = floor(attr/2)+1`（属性 8 才解锁 L5）。
5. 8 技能管辖映射锁定：MIND = 拾荒辨识/测绘；HAND = 拆解/修理；VIGOR = 医护/潜行；PRESENCE = 议价/言辞。
6. 所有随机必须经 `rng(seedCtx)`；`streamId` 静态可枚举；**禁止在渲染帧上发起判定**。
7. 时间只有 S0·B 一个源；`dayKey / minuteOfDay / absTick` 不得由其它系统自造。
8. 跨系统回写只走写队列；下游只读 `DaySnapshot[D]`，禁止直读实时值。
9. 日切九步顺序不可调换；Phase 2 死亡清算固定在第 6 步。
10. 常量与 ID 命名**只在 §D 定义**；`Quantity.cluster → unit` 为固定映射，不得自定义。
11. `PhysioState` 为玩家与 NPC 共用结构；NPC 的 L1 只保留 安全/社交/信条，生理项只读引用。
12. 安全天数按木桶 `min(...)`，不得加总或加权平均。
13. 任何跨系统效果必须写 `AttributionLink` 且 `step ≤ 3`；无法归因即报警。
14. 实体上限：**FULL ≤32**（裁决 29 下调）/ 总 ≤120 / 有名字 NPC ≤16 / 匿名 ≤60；仿真 ≤6ms/帧（预算口径见 §C3 裁决 30，以架构文档为准）。
15. 死亡清算的 ② **不得**触发声望变化；④ 的紧缺态退出需连续 2 日。

# S3 · Economy-Barter（易货经济与社区仪表）· 系统设计文档

- **Task ID**：GDD-003｜**阶段**：Phase 2 · 批次 B2｜**优先级**：P0（D3 汇差套利的燃料；"安全天数"唯一读数产出方）
- **主笔**：design-strategist（文策渊）｜**调度**：主理人（游承峰）｜**状态**：待拍板（附录 B 共 10 项待裁决）
- **依赖已读**：`game-concept.md` · `00-foundation.md`（S0·A/S0·B/S0·C/§D/Part E）· `01-scarcity-loop.md`（S1）· `02-npc-simulation.md`（S2）
- **依赖方向（严格单向）**：依赖 S0·A/S0·B/S0·C/S1/S2；依赖社区（S5）的只读量 `population`/`CreedRationRule`/`Reputation`。**不依赖**对话（不读对话树）。
- **边界（严格）**：五簇进出比价、议价 roll、公共仓与配给、每日价格更新、安全天数与安全状态、`reservedByPromise` 生命周期。
  **不写**：对话内容与分支、UI 布局与线框、NPC 行为树与调度、声望公式本体、区域物资池。

---
## 0.0 规范系统编号对照表（PHASE2-REVIEW · 裁决 C-13，全文已按此表替换）

| 规范编号 | 系统 | 文件 | 本文档原用临时编号 |
|---|---|---|---|
| **S0** | 底座（`S0·A` 角色判定 / `S0·B` 世界时钟 / `S0·C` 确定性 / `S0·D` 常量表 / `S0·E` 死亡清算） | `00-foundation.md` | S1 / S2 / S12 |
| **S1** | 稀缺主循环 | `01-scarcity-loop.md` | S5 |
| **S2** | NPC 三层模拟 | `02-npc-simulation.md` | S6 |
| **S3** | **易货经济与社区仪表（本文档）** | `03-economy-barter.md` | S9（自称） |
| **S4** | 记忆驱动对话与契约账本 | `04-dialogue-contract.md` | S8 |
| **S5** | 社区、信条与声望 | `05-community-creed.md` | S7 |
| **S6** | 传闻传播（待写） | — | —（未引用） |
| **S7** | 工作板（待写） | — | C9（概念文档 scope ID，保留原写法） |
| **S8** | 区域内容（待写） | — | —（未引用） |
| **S9** | 呈现 / UI（待写） | — | "UI GDD" |

---

## 0. 全局契约（逐字继承，不改字段名）

```text
PriceTable(communityId) -> { cluster: { base, buy, sell } }
quote(communityId, actorId, cluster, amount, isBuy) -> { unitPrice, total, inReserveRange:bool }
execTrade(...) -> TradeResult{ executed, qty, unitPrice, priceImpact, reputationHint }
Warehouse(communityId) -> { stock:{cluster:amount}, inbound, outbound, reservedByPromise }
SafeDays(communityId) -> { safeDays, waterPersonDays, foodPersonDays, population, trendDelta7d }
RationGrant{ actorId, quantities[] }                                  // → S2 NPC 模拟
事件: PriceShock{communityId,cluster,oldPrice,newPrice,dayKey,causeCode}
     RationIssued{communityId,dayKey,perCapita,shortfallByCluster}
     ScarcityStateChanged{communityId,state:"STABLE"|"TIGHT",dayKey}
价格更新 P(t+1) = P(t) × (1 + 0.25 × (需求 − 库存)/(库存 + ε))，clamp [0.6, 1.8] × 基准
成交价 = P × [1 + 0.04×(卖家气场 − 买家气场)] × (1 − 0.03×议价等级)
```

> **主理人裁决（本份逐条落实，不复核）**：① 安全天数取木桶 `min(...)`，绝不加总；② 一律读昨日快照，不读实时库存；③ 无通用货币，瓶盖不得成为通货；④ 判定禁在渲染帧发起，随机经 `rng(seedCtx)`；⑤ 本份负责给出五簇各自的产地与产能归属（回填 S2 的 C-09）。

---
## 1. 系统概览与目标

### 1.1 定位
S3 是本作唯一把"宏观"翻译成"一个数字"的地方。它不做内容，它做**换算**：把一个社区的生存处境压缩成 `safeDays`，把一个社区的欲望压缩成五个价格，把你扛在背上的 30 升水压缩成"井窖明天会不会因为你而多活三天"。

- **支柱**：**P2（主）**——安全天数是不用教学就能懂、会自己变的世界读数；**P3**——你不来，价格照涨、配给照发、水照样见底；**P1**——每一次价格变动必须能落到"某社区某岗位少了一个人"；**P4**——你买走的三升水，今晚不进某个人的喉咙。
- **动词**：**易 BARTER** 完整落在本系统（议价 roll 在此）；**负 HAUL** 的收益端在此结算；**守望 TEND** 的回报经配给在此兑现。
- **反目标**：不做拍卖行/订单簿；不做可持有的通用货币；不做玩家商店；不做期货与借贷（Extended 再议）。

### 1.2 一句话职责
> 让"哪里缺什么"这件事，同时是一个你可以背得动的重量、一个你算得清的价差、和一个人人盯着的数字。

---
## 2. 核心概念与数据模型

### 2.1 记账单位 RU（不是货币）
五簇之间要有可比价，就必须有一个**计价基准**。本份定义 **RU（Ration Unit，口粮单位）**：`1 RU ≡ 1.0 L 净水`（= 1/3 人日饮水 = PHYSIO 64 点）。

**RU 的硬约束（防"通用货币"悄悄复活）**：① 不是 `ClusterId`、不进 `Warehouse.stock`、不可被 `tryTransfer`，任何背包里**永远没有 RU**；② 只出现在 `PriceTable.base/buy/sell`、`TradeResult.priceImpact`、`--dump-arbitrage` 三处；③ 实现期断言：任何以 RU 为 `Quantity.cluster` 或试图把 RU 写入库存的调用 → 中断并报 `assert-no-currency`。
> 有价格 ≠ 有货币。RU 是一把**尺子**，不是一种**东西**；它不可持有，故不可囤积、不可刷、不可能坍缩掉五簇比价。

### 2.2 库存键域：`ClusterId` = 6 键（**C-17 / C-26 已裁决**）
```text
ClusterId          : WATER | FUEL | AMMO | MEDS | SEED | FOOD      // 6 键，已回填 S0 §D
TRADABLE_CLUSTERS  : {WATER, FUEL, AMMO, MEDS, SEED}               // 5 键，全部经济公式只认此子集
Quantity.cluster → unit 增列：FOOD → "portion"
```
`FOOD.tradable = false` 的三条边界（实现期静态断言）：
1. **不进** `PriceTable`（无 `base/buy/sell`）、**不进** `quote/execTrade` —— 收到 `FOOD` 直接拒绝并 `assert`；
2. **进** `Warehouse.stock`、进配给、进 `SAFE_DAYS` 分子、进 `reservedByPromise`；
3. **进** `ItemSpec.cluster`（S4 的 `DELIVER` 承诺已开放 FOOD 类）。

> **"五簇通货"的称谓与本份全部经济公式一字不改**——通货簇仍是 5 个，FOOD 只是一个不可交易的生存簇。本份后续按 6 项键域书写。

### 2.3 【必答】五簇产地与产能归属（回填 S2 的 C-09）
| 簇 | 产地社区 | 产能归属（岗位 / 来源，回填 `nominalOut`） | 天然短缺方 | 结构性缺口来源 |
|---|---|---|---|---|
| **WATER** | `he_valley` 河谷（季节河 + 砂滤棚） | `he_valley.water_01/02` 取水岗，nominalOut 见 §2.4 | `jing_cell` | 井枯：`postYieldMul = 0.7`，满员仅 14.7 L/日 vs 日耗 21 L |
| **MEDS** | `jing_cell` 井窖（旧政权地下诊所 + 药圃） | `jing_cell.care_01`（照护岗兼管药圃）`{MEDS:1.2 dose}` | `he_valley` | 河谷无医、无圃，产出恒为 0 |
| **SEED** | `jing_cell` 井窖（地下温室留种） | `jing_cell.care_01` `{SEED:0.5 portion}` | `he_valley` | 河谷沙化土 + 信条"当日分尽、不留隔夜" |
| **FUEL** | **双方均不自产** | 仅来自区域拾荒（旧油罐/废车/旧政权油库） | **双方** | 无岗位产出，纯 S1 侧竞争拾荒 |
| **AMMO** | 双方均产，量极小 | `*.patrol_01` 巡线回收 `{AMMO:4 rd}`（`PATROL_AMMO_YIELD`） | **双方** | 额定岗各 1，回收量远低于消耗 |
| **FOOD**（非通货） | 双方均产，量不足 | `*.patrol_01` 顺路采集 `{FOOD:3.0 portion}`（`PATROL_FOOD_YIELD`）+ S1 `WILD_FOOD` 再生 | **双方** | 故意让两端都不自给 → 拾荒即命脉（D1 经济侧） |

**这张表回答三件事**：① **谁出口**——河谷出 WATER、井窖出 MEDS+SEED，**双向互补**，一趟商路两程都不空跑；② **谁天然短缺**——FUEL 与 AMMO 双缺，**两条路都跑不出汇差**，只能靠拾荒与劫掠（D1/D2/D6 的合法出口，不被跑商吃掉）；③ **为什么没有主导策略**——WATER 是"大批量低单价"（闸门在 `carryCap`），MEDS/SEED 是"小批量高单价"（闸门在日产量，一天只多 0.8 dose），两条曲线形状完全不同，无法被一条路线吃透。
> `nominalOut` 读法声明（**C-15 · 仍待主理人裁决 —— PHASE2-REVIEW 未裁定，列为开放问题 OQ-1**）：本份采用**岗位总额定**（`nominalOut` = 该 `postId` 全体额定岗之和），实际产出 = `nominalOut × (effHead / rated)`；理由：只有此读法能让"满员恰好自给 / 缺员即缺水"成立。
> **PHASE2-REVIEW 处理**：裁决 7（C-09）已采纳本份 §2.3/§2.4 的产地与产能配置，并据此回填 S2 §2.3 的 `nominalOut`，**回填时一并按本份"岗位总额定"口径落数**（S2 §2.3 原表头"每额定岗每日"已同步改写）。**若主理人改采"每额定岗"读法，S2 §2.3 与本份 §2.4 的全部产能数值须 ×rated 重算。**

### 2.4 Core 期两社区配置（概念实体化）
| 项 | `he_valley` 河谷 | `jing_cell` 井窖 |
|---|---|---|
| `population` | 9 | 7 |
| 信条（S5 stub） | 当日分尽 · 余粮十分之一入公共仓 | 留种 · 外人不得过夜 |
| `CreedRationRule` | `{titheRate:0.1, priorityOrder:[WATER,FOOD,MEDS,FUEL,SEED]}`（默认，两社区同） | 同左 |
| 岗位 `rated` | water_01:2 / water_02:2 / patrol:1 / watch:1 / mend:1 / care:1（idle 反算 1） | water_01:2（`postYieldMul 0.7`）/ patrol:1 / watch:1 / mend:1 / care:1（idle 1） |
| 满员产出 / 日耗（同序） | WATER 42/27 · FOOD 3.0/9 · AMMO 4/3.3 · MEDS 0/0.3 · FUEL 0/1.5 · SEED 0/0.2 | WATER 14.7/21 · FOOD 3.0/7 · AMMO 4/2.6 · MEDS 1.2/0.4 · FUEL 0/1.0 · SEED 0.5/0.15 |
| **结构性净收支** | **WATER +15 L**（唯一盈余）· FOOD 紧平衡（靠 `WILD_FOOD` +4 与拾荒补足）· **MEDS −0.3 / FUEL −1.5 / SEED −0.2 全缺** | **WATER −6.3 L**（致命）· FOOD −2（靠菌类 +2 与拾荒）· **MEDS +0.8 / SEED +0.35**（唯一盈余） |
| 起始公共仓 / `safeDays` | WATER 216 L · FOOD 72 → **8 天**（水 8 / 食 8，双项齐平，任何一跳立刻可见） | WATER 126 L · FOOD 42 → **6 天**（水 6 / 食 6） |

> 设计意图：河谷的木桶短板会**从水平衡漂移到食物**（它产水不产粮），井窖的短板**永远是水**。玩家对河谷的正确动作是"送粮换水"，对井窖是"送水换药"——两个社区需要的是**相反的服务**，这才有跑商的必要。

### 2.5 瓶盖在数据模型里的位置（裁决 3 的落地）
```text
ItemDef.kind ∈ { CONSUMABLE | TOOL | PART | CURIOSITY | UNIQUE }
BottleCapStack { count:int }   →  ItemStack{ defId:"cap_bottle", kind:CURIOSITY, count }
```

四条红线：**不在** `ClusterId`、**不在** `Warehouse.stock`、**不在** `PriceTable`（无 `base/buy/sell`）、**不能**作为 `Quantity.cluster` 或 `execTrade` 的支付物（编译期类型已挡）。

唯一消费路径：`hasCuriosity(actorId,"CAP") -> {count}` → **对话 GDD** 判条件 → 老兵型 NPC 专属文本；不产生任何经济数值、不进 `TradeResult`、不写 `AttributionLink`（它是文本条件，不是资源事件）。
> **为什么必须写死**：瓶盖一旦可定价，五簇比价立刻坍缩为单一硬通货，D3 与 C4（每社区独立比价）同时消失，概念文档 §3.1-C 的"防止通用硬通货坍缩掉整个动态经济"被击穿。故 `--assert-no-currency` 是本系统的**设计红线断言**，不是工程洁癖。

### 2.6 仓储与预留
```text
Warehouse(communityId){
  stock:{cluster:amount},              // 6 键域；只允许经 S1 tryTransfer 写入
  inbound / outbound: Quantity[],      // 当日写队列镜像，不并入 stock、不参与当刻计算
  reservedByPromise:{cluster:amount}   // 见 §4.3
}
有效库存 effStock[c] = max(0, stock[c] − reservedByPromise[c])    // 定价与配给唯一口径
```

`inbound/outbound` 只供调试面板与 `priceImpact` 预估使用（P2：玩家看到的数字一天只变一次）。

---
## 3. 规则与公式

### 3.1 基准价与买卖价（日更）
```text
P(D+1) = clamp( P(D) × (1 + PRICE_DRIFT × (demand[c] − effStock[c]) / (effStock[c] + PRICE_EPS)),
                0.6 × base[c],  1.8 × base[c] )          // PRICE_DRIFT = 0.25（契约给定）
demand[c] = 社区日耗[c] × DEMAND_COVER_DAYS              // Proposal 5（社区想囤 5 天）
buy  = P × (1 + TRADE_SPREAD/2)      // actor 从社区买入（社区卖出价）
sell = P × (1 − TRADE_SPREAD/2)      // actor 卖给社区（社区买入价）
```

`base[c]`（worldgen 静态，Proposal，单位 RU）：`WATER 1.00`（定义基准）· `FUEL 2.20` · `AMMO 0.85` · `MEDS 4.50` · `SEED 6.00`。FOOD 无 base（不可交易）。

### 3.2 成交价与议价 roll
```text
成交价 = P × [1 + 0.04 × (卖家气场 − 买家气场)] × (1 − 0.03 × 议价等级)     // 契约给定，唯一实现处
```

**`rollCheck(PRESENCE, barter)` 的位置（不修改上式）**：上式算完若**落在双方保留价区间内** → 直接成交，`inReserveRange = true`，**不发起 roll**（省算力、也避免"每笔都掷骰"）。若**落在区间外** → 这是"请对方破例"，发起一次 `rollCheck(actorId, PRESENCE, barter, exceptionDC, seedCtx)`：

| 破例幅度 `|成交价 − 保留价| / 保留价` | `exceptionDC` | 结果 |
|---|---|---|
| ≤ 5% | `DC_TRIVIAL` | 成功 → 按破例价成交，写 `reputationHint:PRESSED_LUCK`；失败 → 不成交，`REFUSED` |
| ≤ 15% | `DC_DEMANDING` | 同上；失败额外 `reputationHint:HARD_BARGAIN` |
| ≤ 30% | `DC_PERILOUS` | 成功 → 成交 + `PRESSED_LUCK`；失败 → 不成交 + `HARD_BARGAIN`；**大失败 → 不成交 + `BAD_FAITH`，并发一条 `GossipSeed`（交 S2，fanout ≤3）** |
| > 30% | — | **直接拒绝，不发起 roll**（防无限压价，也防骰子刷次数） |

- 社区侧保留价：`reserveFloor = base[c] × RESERVE_FLOOR_MUL`；**硬闸门**：当 `safeDays < SAFE_DAYS_SELL_LOCK`（Proposal 4）且该簇 ∈ `{WATER, FOOD}` 时，社区**停止卖出**（`quote` 对 `isBuy=true` 返回 `inReserveRange=false` 且 `unitPrice=null`）。"快渴死的社区不卖水"——这条比任何数值都更能防"搬空产地"主导策略。
- `reputationHint` 是**给 S5 的输入建议** `{code, delta}`，`code` 闭集：`FAIR|GENEROUS|HARD_BARGAIN|PRESSED_LUCK|REFUSED|BAD_FAITH`。本期 `Reputation` 返 0，**字段照发、事件照记，由 S5 后续承接**。

### 3.3 `priceImpact`：只展示，不当日生效
`TradeResult.priceImpact` = 用 §3.1 公式、以"本笔已入队后的 effStock"试算出的 `P(D+1)/P(D) − 1`——**只是预测值**，写进当刻 UI（"你这一笔会让明天河谷的水价 +3%"）。真正的改价只在日切第 3.2 步发生一次。**读昨日快照纪律因此不可被玩家在一天内绕过。**

### 3.4 配给（日切第 3.3 步）
```text
对每个 c ∈ priorityOrder:   // [WATER, FOOD, MEDS, FUEL, SEED]
  avail[c] = max(0, effStock[c] − SAFETY_FLOOR_DAYS × 日耗[c])      // Proposal SAFETY_FLOOR_DAYS = 2
  target   = perCapita[c] × pop
  grant    = min(avail[c], target)
  tryTransfer("stock.<cid>.public", "actor.<id>", [{cluster:c, amount:grant/pop}])   // 逐人发放
  shortfall[c] = target − grant
发 RationIssued{communityId, dayKey, perCapita, shortfallByCluster}
```

- `perCapita` 目标：`WATER 3.0 L` · `FOOD 1.0 portion`（底座常量）· `MEDS 0.02 dose` · `FUEL 0.15 L` · `SEED 0.01 portion`（`RATION_EXTRA`，Proposal）。
- `titheRate = 0.1` **只作用于个人拾荒所得**：NPC 劳动产出 100% 入公共仓（不经 tithe）；玩家/NPC 把**拾荒**所得缴入公共仓时，按 10% 记 `tithe`，该部分不可取回。

### 3.5 安全天数与安全状态（日切第 5 步）
```text
waterPersonDays = stock.WATER / (WATER_L_PER_PERSON_DAY × pop)     // = waterL / (3.0 × pop)
foodPersonDays  = stock.FOOD  / (FOOD_UNIT_PER_PERSON_DAY × pop)   // = foodUnits / (1.0 × pop)
safeDays        = min(waterPersonDays, foodPersonDays)             // 木桶，绝不加总、不加权平均
trendDelta7d    = safeDays(D) − safeDays(D−7)
```

- **为什么必须取 min**（复核裁决 1）：加总会让"缺水 0.5 天 + 余粮 60 天"报出 30 天安全，直接击穿 P4——玩家读到的世界与实际死因不符。木桶让 `safeDays` 恒等于"最先耗尽的那一项"，玩家永远知道该送什么。
- `ScarcityStateChanged`：`safeDays ≤ SAFE_DAYS_TIGHT_ENTER(5)` → `TIGHT`；`≥ SAFE_DAYS_TIGHT_EXIT(8)` **连续 2 日** → `STABLE`（滞回，与底座 `SCARCE_RATIO_ENTER/EXIT_DAYS` 纪律一致）。
> **C-18 已裁决（主理人拍板 · 禁止合并）**：`SCARCE`（**岗位在岗率 < 0.60**，产能态；权属 S0 Part E ④ / S5 §2.2）与 `TIGHT`（**`safeDays` ≤ 5**，库存态；权属本份 §3.5）是**两个不同的指示量**。禁止合并为同一状态机 / 同一枚举 / 同一指示灯，UI 必须两灯并列。概念文档 §3.1-F 的"跌破 60% 进入紧缺状态"指的是 **`SCARCE`**（已在概念文档就地标注）。

### 3.6 【必答】D3：为什么汇差套利收益收敛但永不归零
**① 收敛**（价格公式的负反馈）：
```text
f(S) = P·(1 + λ(D−S)/(S+ε)),  λ = 0.25
f'(S) = −λ·P·(D+ε)/(S+ε)²  <  0            // 库存↑ → 价格↓，严格单调
```
玩家从 A 买、运到 B 卖，搬运 q：`S_A −= q, S_B += q`。价差 `Δ = sell_B − buy_A` 的变化：
```text
Δ' − Δ = q·( f_B'(ξ_B) + f_A'(ξ_A) )  <  0        // 只要两侧都未触及 clamp
```
→ **每搬一次，价差严格缩小一次**；且 `|f'(S)|` 随 S 增大而衰减 → 收敛而非震荡发散。clamp 区间 `[0.6,1.8]×base` 同时是**单笔冲击的上界**（一次搬运最多把价格打到边界），因此不存在"一笔搬爆"的失控。

**② 永不归零**（运输成本存在正下界）：
```text
单趟净收益 π(q) = q·(Δ − c(q))
c(q) = 单趟固定成本/q  +  比例风险成本
     = (C_time + C_stamina)/q  +  p_enc(q)·LOSS_RATE·V
Q_max = floor( carryCap / MASS_PER_UNIT[c] )        // carryCap = 25 + 5×VIGOR，Core 期属性不成长 → 常量上界
⇒ (C_time + C_stamina)/q  ≥  C_trip / Q_max  ≡  c_trip  >  0
   且 p_enc(q) 随 q 单调不减（负重↑ → speedMul↓ → 暴露时间↑）
⇒ c(q) ≥ c_trip + p_min·LOSS_RATE·V  ≡  c_min  >  0        // 与 q 无关的正下界
```
搬运在 `Δ > c(q)` 时有正收益；`Δ → c(q)` 时 `π → 0`，玩家自然停手。**收敛点是 `Δ* = c(q) ≥ c_min > 0`，不是 0。**

**③ 为什么 `c_min` 在 Core 期不可能被玩家消掉**（三根结构性支柱，非数值调参）：① `carryCap` 上界固定（属性 Core 期不成长）→ 无法靠"一次搬更多"摊薄固定成本；② 路程固定成本 > 0，且无 fast travel（Extended 才解锁，解锁方式是**加价换时间**，不改变 `c_min > 0`）；③ 风险率下界 > 0，途中必经 `hazardLevel ≥ 1` 区域，黑市只改路线不改负重。

**④ 汇差被谁重新拉开**（外生扰动 ξ）：停手后 `Δ_{t+1} = Δ_t − λ_eff·q_t + ξ_t`；ξ 来自岗位 `effHead` 波动（伤病/失能/死亡）· `ClaimDisplaced` 致拾荒入库下降 · 死亡清算降产能 · 张力预算注入器（R5）· 配给消耗速率差。**Core 期 16 名 NPC 每 2–3 游戏日至少产生一次 ξ**，故 Δ 在 `c_min` 上方持续被抬离 → 稳态是**均值回复 + 正下界**的带宽 `[c_min, c_min + Ξ]`。

**⑤ 示意算例**（Proposal 参数手算，**仅说明形状，非验收基线**）：河谷→井窖运水，VIGOR 5（`carryCap` 50 kg，水 1.2 kg/L → 实载约 30 L），行程 2.2 km，`speedMul ≈ 0.89` → 约 29 游戏分钟，`c(q) ≈ 0.05 RU/L`。

| 趟数 | 井窖 S (L) | 井窖 P_B | 河谷 P_A（长期贴 clamp 下沿 0.60） | 毛利 Δ (RU/L) | 单趟净 π (RU) |
|---|---|---|---|---|---|
| 0 | 126 | 0.96 | 0.60 | 0.96×0.91 − 0.60×1.09 = 0.22 | ≈ 5.1 |
| 1 | 156 | 0.85 | 0.60 | 0.12 | ≈ 2.1 |
| 2 | 186 | 0.76 | 0.60 | 0.04 | ≈ −0.3（停手） |

约 **2–3 趟抹平**；井窖自身 `−6.3 L/日` 使 S 约 **5–6 游戏日**后回落至 126 附近 → Δ 重新张开。**节奏：3 日饱和 / 6 日复张**，玩家永远有事可做，但永不值得"挂机刷线"。

**⑥ 反主导策略**：MEDS/SEED 反向路线（井窖→河谷）汇差高达 `≈4.4 RU/剂`，但井窖日盈余仅 `0.8 dose`，**数量闸门在产量侧而不是负重侧**；WATER 路线数量闸门在 `carryCap` 侧。两条曲线形状不同 → 无单一最优。再加上 §3.2 的卖断硬闸门与 §7-E7，跑商无法把任一社区搬空。

---
## 4. 状态与流程

### 4.1 日切时序（挂载底座九步；**顺序不可调换**）
> **C-14 已裁决（主理人拍板 · S0 §B3.5 已同步细分）**：第 3 步细分为 3.1/3.2/3.3，**第 5 步不动**；第 7 步细分为 7.1/7.2/7.3 并追加 **7.4 违约 delta 补批应用**（B-1）。

```text
[3.1] 库存 / 岗位产出 / 消耗结算          （底座原有）
[3.2] 价格更新   —— 读 DaySnapshot[D-1] 的 effStock/demand → 写 P(D+1) → 超阈值发 PriceShock
[3.3] 配给发放   —— 按 §3.4，经 tryTransfer 逐人发 → 发 RationIssued + RationGrant
[5]   安全天数重算 —— 读 3.3 之后的 stock → 发 ScarcityStateChanged（如需）
[7.1] 承诺到期判定 → BREACHED（S4）
[7.2] release() 释放 reservedByPromise（**本份**，见 §4.3）
[7.3] 发 PromiseBreached（S4）
[7.4] 违约 delta 补批应用 → 进 DaySnapshot[D]（S5）—— 保证违约后果 D+1 可见而非 D+2
```

> **7.2 与 3.3 的先后是刻意的**：释放发生在配给之后，故被释放的物资要到 **D+1 日切的 3.3** 才进配给（§7-E8）。**声望后果 D+1 可读（经 7.4）、被冻结的物资 D+1 回到配给**——两侧延迟都恰为 1 日。

关键推论（**请 S2 对齐**）：S2 的 DISPATCH 06:00 第 ② 步才把昨日劳动产出 `tryTransfer` 入公共仓，因此**06:00–24:00 全天看到的交易价都是"昨日收盘价"**，今日的产出要到下一个日切才反映到价格。这是 P2 想要的性质（一天一个价，不是一刻一个价），但**必须写进 UI 文案**：价格牌带"今日价 · 明天变"。

### 4.2 交易流程（F1）
```text
1 玩家/NPC 发起 → quote(communityId, actorId, cluster, amount, isBuy)
2 读 DaySnapshot[D] 的 P(D) → 算 buy/sell → 算成交价（§3.2 气场与议价项，纯算术，无随机）
3 落保留价区间？ ── 是 ──► inReserveRange=true → 直接成交
                 └─ 否 ──► 幅度 >30% ? 直接拒绝 : rollCheck(PRESENCE, barter, exceptionDC)
4 成交 → tryTransfer 双向（唯一入库口，全有或全无）→ 写队列（不当刻改价）
5 返回 TradeResult{ executed, qty, unitPrice, priceImpact, reputationHint }
6 记 AttributionLink{ step:1, causeRef:"S3.trade.<cid>.<cluster>", effectRef:"S3.stock.<cid>.<cluster>", dayKey }
```

所有判定在 `SIM_TICK`/`HOURLY_TICK` 上发起，**禁在渲染帧**。**RNG 纪律**：本系统 raw `rng` 调用数 = **0**（继承 S2 的 R-B 最严读法），唯一随机出口是 `rollCheck(PRESENCE, barter)`，其 `streamId` 所有权在 S0·A，本份**不新增任何 streamId**。

### 4.3 【必答】`reservedByPromise` 完整生命周期
```text
(对话侧) reserve(cid, ledgerId, Quantity, dueDayKey)
   │
   ▼ 入写队列 → 下一 HOURLY_TICK 应用
PROMISED ──► FROZEN ──┬─► FULFILLED   （tryTransfer 兑现成功）
                      ├─► DEFAULTED   （日切第7步：dayKey > dueDayKey 且未兑现）
                      └─► VOIDED      （promisor 死亡，Part E ② 已置 VOID/DEATH）
```

| 阶段 | 触发 | 系统动作 | 玩家可见 |
|---|---|---|---|
| **许诺 PROMISED** | S4 对话创建 `LedgerEntry` 后调 `reserve()` | `reservedByPromise[c] += amount`；**并发 `PriceShock{causeCode:PROMISE_FREEZE}`（若达阈值）** | 承诺当刻，价格牌就动了一下 |
| **冻结 FROZEN** | 应用后持续 | `effStock` 扣除 → **该簇当日配给变少**（承诺的代价由全社区一起付）· 价格上行 | 社区仪表"水 −12 L（已许诺）" |
| **兑现 FULFILLED** | `tryTransfer("stock.<cid>.public", promiseeId, payload)` 成功 | `reserved −=` 且 `stock −=`；`LedgerEntry → FULFILLED` | 无提示即成功（好事不打扰） |
| **违约 DEFAULTED** | 日切第 7 步 | `reserved −=`（**释放回可动用**）；发 `PromiseDefaulted` → S5 算声望、S2 发 gossip。**释放的物资当天不进配给**（配给已在 3.3 做完）→ **次日才可用** | "他答应我的三升水没了" |
| **VOIDED** | 收到 Part E ② | `reserved −=`；**不扣声望**（继承 Part E 硬约束） | 门口贴名字，岗位空一人 |

**"玩家不知道自己许诺了什么"——六道防认知过载闸门**：① 同时活跃 `LedgerEntry ≤ PROMISE_MAX_ACTIVE(3)`，达上限时新许诺选项**置灰并明写"你已经欠了 3 笔"**；② `reserve()` 参数强制"单一簇 + 单一数量 + 单一到期日"，**禁止复合承诺**（类型层拦截）；③ **冻结即反馈**——许诺当刻价格牌与仓储读数就变，**你不可能没注意到自己许诺了**；④ `dayKey == dueDayKey − 1` 发 `PromiseDueSoon` → 次日 06:00 该 NPC 站在村口等（P2 可见预告）；⑤ 所有承诺汇总为 HUD"欠条"页 ≤3 条，每条 6 字段（谁/什么/多少/哪天/还差多少/冻结在哪）；⑥ **代价前置**——创建时即显示"若违约：声望 −X，被冻结的 Y 升水 D+2 才回到配给"，不是事后惊喜。

---
## 5. 对外接口

### 5.1 暴露
| 接口 / 事件 | 消费方 | 用法与注意 |
|---|---|---|
| `PriceTable(cid)` | UI / 工作板 C9 / S2（stub） | 只读；`base` 静态，`buy/sell` 每日一变 |
| `quote(...)` / `execTrade(...)` | UI / 玩家输入 / NPC 交易意图 | `quote` 纯算术无随机（可安全预览）；`execTrade` 是唯一成交口，**必须经 `tryTransfer`**，全有或全无；**二者禁在渲染帧调** |
| `Warehouse(cid)` | UI / S2 / S5 | `stock` 6 键域；写入**只能**经 S1 `tryTransfer` |
| `SafeDays(cid)` | **UI（唯一核心仪表）** / S5 | 木桶值；`trendDelta7d` 供"在好转/在恶化"箭头 |
| `reserve/release/fulfill(...)` · `hasCuriosity(actorId,"CAP")` | **S4 对话 GDD** | 预留生命周期唯一入口（`reserve` 参数原子化）；`hasCuriosity` 只返 count，不产生经济效果 |
| `PriceShock` | UI / S5 / 日志 | `causeCode` 闭集：`TRADE_VOLUME·POST_SHORTFALL·RATION_SHORTFALL·PROMISE_FREEZE·PROMISE_DEFAULT·ESTATE_LOSS·TENSION_INJECTION·CLAMP` |
| `RationIssued` / `RationGrant{actorId, quantities[]}` | S5 / C9 工作板 · **S2** | `shortfallByCluster` 就是"缺口"，喂 C9；`RationGrant` 逐人发放，NPC 侧写个人库存 |
| `ScarcityStateChanged` | UI / S0·B（睡觉中断 `COMMUNITY_ALARM`）/ S5 | `STABLE`/`TIGHT`，**与岗位 `SCARCE` 不是同一个量**（C-18） |

### 5.2 依赖
- **S0·A**：`rollCheck(PRESENCE, barter)` 唯一随机出口 · `getDerived`（气场值）· `getSkill`。**不传修正、不自改 DC**。
- **S0·B**：`onDayBoundary`（3.2/3.3/5/7 步）· `getDaySnapshot(D)`（**唯一跨系统读数来源**）· `enqueue` · `absTick`。
- **S0·C**：`recordAttribution` · `exportAttribution`（raw `rng` 调用数 = 0）。
- **S1**：`tryTransfer`（**唯一入库口，接受其全有或全无语义，不要求部分成功**）· `ZoneSnapshot`。
- **S2**：`getLaborOutput`（`byCluster` 为产能输入）· `filledPosts` · `getNeeds`（stub）。
- **S5**：`population` · `CreedRationRule` · `Reputation`（**本期返 0**，本份只发 `reputationHint`）。

### 5.3 stub（明示为未完成）
```text
CreedRationRule { titheRate:float, priorityOrder:ClusterId[] }  // 默认 {0.1, [WATER,FOOD,MEDS,FUEL,SEED]}，两社区同
Reputation(...) -> 0              // 本期恒 0；reputationHint 字段照发照记
reservedByPromise                 // 字段已实现；本期由 S4 对话侧调用 reserve()
天气 / 季节 → 常量                 // Extended 后接入 ξ 扰动源
```

---
## 6. 玩家可感知表现

1. **一个数字**：社区仪表只把 `safeDays` 做成大号读数，旁注**木桶短板是哪一项**（"卡在食物"／"卡在水"）。玩家永远知道该送什么——这是 P2 的最高产物，也是本系统存在的理由。
2. **一天一个价**：价格牌带"今日价 · 明天变"；成交后立刻显示 `priceImpact` 预测（"明天这里的水会 +4%"）——**看得见的后果**，且只预测不改价，不击穿可读性。
3. **议价是赌博不是折扣**：常规成交无骰子，只有你想压到对方保留价之外时才掷一次，DC 档位与幅度在 UI 明示（"破例 12% · 苛刻 14"）。大失败会被议论——省钱还是买坏名声，是同一道选择题。
4. **许诺当场有重量**：许诺的一刻，公共仓读数就掉、价格就动、欠条页就多一行；违约时被冻结的物资**第二天才回来**——你骗到的不是物资，是延迟。
5. **产地贴底、缺地触顶**：河谷水价、井窖药价长期趴在低位——这不是 UI bug，是产能结构的价格表达，玩家一眼读出"谁产什么"。
6. **瓶盖只是瓶盖**：可堆积、可送人、能触发老兵的一段话；交易界面里**不可作为支付物出现**（UI 无入口，实现层有断言）。

---
## 7. 边界情况与失败模式

**E1 · 库存归零时的定价**：`effStock = 0` → `(D−0)/(0+ε) = D/ε`，`PRICE_EPS` 决定峰值斜率。ε 过大 → 稀缺不敏感（P4 塌）；过小 → 单笔过度反应（P2 塌）。取 `PRICE_EPS = 1.0`（与簇同量纲的 1 单位）：库存 1 与库存 0 的价差有限，而库存 0 必然冲上 clamp 上沿。

**E2 · 睡觉/快进期间的交易**：`requestSleep` 走同一 tick 序列；快进中被触发的交易（NPC 商队）一律用 `DaySnapshot[D]` 价格，**不因快进中已变化的库存改价**。醒来摘要出现"你睡着的时候水价变了"——而不是"变了三次"。

**E3 · 过度许诺（`reservedByPromise > stock`）**：`reserve()` 前校验 `amount ≤ effStock[c]`，不足即拒绝，由 S4 把该许诺选项置灰。冻结后若 `stock` 因死亡清算/被盗下降 → `effStock` floor 到 0，**配给归零但不产生负库存**，当日发 `RationIssued.shortfallByCluster` 并置 `TIGHT`。**不允许"负债配给"。**

**E4 · 价格触及 clamp 后失去负反馈**：一侧贴 clamp 时 `f'` 实际为 0，价差停止缩小（§3.6①）。这不是失效而是**结构性地板**：产地过剩产能无法把价格压到 0.6 以下，故"无限搬空产地"永远有正价差，但也永远被 §3.2 卖断硬闸门（E7）拦住。

**E5 · 双缺簇（FUEL/AMMO）汇差消失**：两社区同向高价 → 无套利。**不是 bug，是设计**——这两簇的获取路径是拾荒/劫掠（D1/D2/D6）而非跑商（D3），保证三条动线互不替代。

**E6 · 交易对手在成交前死亡**：`quote` 后、`execTrade` 前对方死亡 → Part E Phase 1 释放 claimant；本份在 `execTrade` 时重查 `getActorState(actorId).alive`，`false` → `executed=false`，**不扣物资、不写 `reputationHint`**，只写一条废弃归因（与 S1 §7-E4 同款）。

**E7 · 玩家搬空产地（主导策略反制）**：`safeDays < SAFE_DAYS_SELL_LOCK(4)` 且簇 ∈ `{WATER, FOOD}` → 社区**停止卖出**（`isBuy=true` 直接拒绝，UI 明写"他们自己也不够了"）。叠加 `carryCap` 上界与缺地社区吸收饱和（§3.6⑤ 的 2–3 趟），"无限跑水线"在数学与规则两侧同时被封死。

**E8 · 违约释放的物资当日不可用**：配给在 3.3 步、违约在 7 步 → 释放的物资次日才进 `effStock`。**这是刻意的**：违约的收益来得太晚，而当夜社区已经饿着——代价由具体的人先付，再由玩家后收。

---
## 8. 验收标准与调试钩子

**验收**

1. **D3 收敛（A1）**：固定种子 Bot 单线跑 20 游戏日，`Δ` 曲线单调下降且**收敛值 ≥ `0.5 × c_min`**（不归零）；最后 5 日 `min|Δ| ≥ 0.5·c_min`。
2. **D3 永不归零（A2）**：Bot 停手后，`--kill <water 岗 NPC>` 注入外生扰动 → 3 游戏日内 `|Δ|` 回升 ≥ 20%；不回升即判失败（说明 ξ 通道没接上）。
3. **木桶断言（A3）**：脚本化把河谷水灌满、食物清空 → 断言 `safeDays == foodPersonDays`；反向（水空、食满）→ 断言 `safeDays == waterPersonDays`。**任一次取到加总值即判失败。**
4. **快照纯净（A4）**：日切后第一笔 `quote` 的 `unitPrice` 必须 `== DaySnapshot[D].price`；断言 `pricePurity`。当日任意时刻多次 `quote` 返回**同一价格**。
5. **预留生命周期（A5）**：`reserve → FROZEN（配给减少）→ DEFAULTED → 次日释放` 全链 `AttributionLink.step ≤ 3`、`UNATTRIBUTED == 0`；且冻结当日 `PriceShock{causeCode:PROMISE_FREEZE}` 可达。
6. **无货币（A6）**：全仓检索——`PriceTable`/`execTrade` 支付路径中出现 `CURIOSITY`/`BottleCap`/`RU` 作为 `Quantity.cluster` 的次数 = **0**；`--assert-no-currency` 通过。
7. **确定性与预算（A7）**：同 seed save/load 续跑 10 日，`max_d |ΔsafeDays| / max(S,1) ≤ 5%`；本系统仿真 ≤ **0.6ms/帧**（日更批处理，无逐帧逻辑）。
8. **可读性（A8，继承 Core 验收 2）**：不给文字提示，测试者游玩 2 游戏日后能说出"河谷缺药、井窖缺水"，命中率 ≥70%。

**调试钩子**

```text
--dump-price <cid> [<dayRange>]              base/buy/sell 曲线 + clamp 触边日 + PriceShock 明细
--dump-warehouse <cid>                       stock(6 键) / inbound / outbound / reservedByPromise
--dump-safedays <cid> <dayRange>             safeDays + 水/食双项 + trendDelta7d + 短板标注
--dump-ledger [<actorId>]                    LedgerEntry 全量 + reserved 冻结明细 + 到期倒计时
--dump-arbitrage <cidA> <cidB> <cluster>     Δ / c(q) / π(q) / Q_max / 收敛曲线（证明 D3 用）
--force-price <cid> <cluster> <p>            构造价格场景（禁用于正常流程）
--sim-trade <cid> <cluster> <amount> <isBuy> 走完整 execTrade（含 rollCheck，可指定 seedCtx）
--assert-no-currency                        断言瓶盖/RU 未进入任何定价或支付路径
--assert-snapshot-purity                    断言当日多次 quote 价格一致且等于快照
--dump-ration <cid> <dayKey>                 perCapita / 实际发放 / shortfallByCluster
--export-attribution <dayRange>              继承底座 §C8 规格
```

---
## 附录 A · 提请回填底座 §D 的候选常量（本份不就地生效）

| # | 候选常量 | 建议值 | 单位 / 说明 |
|---|---|---|---|
| A-1 | `PRICE_EPS` | 1.0 | 与簇同量纲的 1 单位，防除零并决定稀缺敏感度 |
| A-2 | `DEMAND_COVER_DAYS` | 5 | 日；社区目标囤量 = 日耗 × 5 |
| A-3 | `TRADE_SPREAD` | 0.18 | 买卖价差比例；`buy = P×(1+s/2)`，`sell = P×(1−s/2)` |
| A-4 | `BASE_PRICE` | WATER 1.00 / FUEL 2.20 / AMMO 0.85 / MEDS 4.50 / SEED 6.00 | RU；FOOD 无（不可交易） |
| A-5 | `PRICE_SHOCK_THRESHOLD` | 0.20 | 单日涨跌 ≥20% 发 `PriceShock` |
| A-6 | `RESERVE_FLOOR_MUL` | 0.85 | 社区侧保留价 = `base × 0.85` |
| A-7 | `SAFE_DAYS_SELL_LOCK` | 4 | 天；低于此值且簇 ∈ {WATER,FOOD} → 停止卖出 |
| A-8 | `SAFE_DAYS_TIGHT_ENTER / EXIT` | 5 / 8 | 天；EXIT 需连续 2 日（滞回） |
| A-9 | `SAFETY_FLOOR_DAYS` | 2 | 天；配给不动用的安全库存 |
| A-10 | `RATION_EXTRA` | MEDS 0.02 dose / FUEL 0.15 L / SEED 0.01 portion | 人/日 |
| A-11 | `MASS_PER_UNIT` | WATER 1.2 / FUEL 0.9 / AMMO 0.05 / MEDS 0.1 / SEED 0.15 / FOOD 0.6 | kg / 单位（含容器） |
| A-12 | `PATROL_FOOD_YIELD` / `CARE_MEDS_YIELD` / `CARE_SEED_YIELD` | 3.0 portion / 1.2 dose / 0.5 portion | / 额定岗 / 日；回填 S2 §2.3 的空 `nominalOut` |
| A-13 | `POST_YIELD_MUL` | `jing_cell.water_01` = 0.7 | 社区静态配置字段（非全局常量），交 S5 承接 |
| A-14 | `PROMISE_MAX_ACTIVE` | 3 | 玩家侧同时活跃承诺上限（防认知过载） |
| A-15 | `EXCEPTION_DC_BANDS / HARD_REJECT` | 0.05 / 0.15 / 0.30 ；>0.30 拒掷 | 破例幅度 → DC 档位 |
| A-16 | `ENCOUNTER_LOSS_RATE` / `C_TRIP_MIN` | 0.25 / 0.05 | 遇袭损失比例 · `c_min` 工程化下界（RU/单位·趟，供 A1 比对） |

## 附录 B · 待主理人裁决 / 与并行 GDD 对齐（10 项）

1. **C-13 · 系统编号冲突**：S1 文档把经济写成 **S2**、社区写成 S5；S2(NPC) 文档把经济写成 **S3**、社区 S5。本份采用 **经济 = S3**。请回填 S1 文档 §5.2 与"依赖方向"段。
2. **C-14 · 日切第 3 步需细分**（§4.1）：请求底座把第 3 步拆为 3.1 产出结算 / 3.2 价格更新 / 3.3 配给，第 5 步保持安全天数。否则"价格更新 → 配给 → 安全天数"无处安放。
3. **C-15 · `Post.nominalOut` 读法**（§2.3）：本份按**岗位总额定**写全部数值。若采"每额定岗"读法，§2.4 全部产能需 ×rated 重算——**请优先裁决**。
4. **C-16 · 记账单位 RU**（§2.1）：不可持有、不可转移、仅作计价。请确认不违反"没有通用货币"裁决。
5. **C-17 · `ClusterId` 缺 FOOD**（§2.2）：安全天数公式要求 `foodUnits`。建议增至 6 项并标 `FOOD: tradable=false`。
6. **C-18 · 两个"紧缺"撞名**（§3.5）：岗位 `SCARCE`（在岗率 <0.6）与库存 `TIGHT`（`safeDays` ≤5）是两个量，请社区/UI GDD 勿合并指示灯。
7. **C-19 · `Warehouse.stock` 键域**：随 C-17 变为 6 项；请 S1 确认 `tryTransfer` 对 `FOOD` 不需特殊分支（它只是一个不进 `PriceTable` 的簇）。
8. **C-20 · 接口请求**：① 请 **S4 对话 GDD** 只经 `reserve/release/fulfill` 写 `reservedByPromise`，并接受"同时 ≤3 笔、参数原子化"约束；② 请 **S5 社区 GDD** 承接 `reputationHint` 与 `PriceShock`/`RationIssued` 消费，并承接 `POST_YIELD_MUL` 字段；③ 请 **S2** 按 §2.3 回填 `patrol_01`(FOOD) 与 `care_01`(MEDS/SEED) 的 `nominalOut`（C-09 授权）。
9. **C-21 · 与 S2 的时序对齐**（§4.1 推论）：S2 在 DISPATCH 06:00 才把昨日产出入仓，故当日全天交易价 = 昨日收盘价。请 S2 与 UI 确认这一性质并写进价格牌文案。
10. **C-22 · 最可能与并行 GDD 冲突的三点**（提请汇编时优先比对）：
    - **产能归属**：本份把 MEDS/SEED 挂在 `care_01`（S2 原 stub 写 `nominalOut {}`）、FOOD 挂在 `patrol_01`。若 S2/社区 GDD 已另行归属，需一次性定稿。
    - **"紧缺"语义**：本份 `TIGHT` vs 底座 `SCARCE` vs 概念文档"跌破 60% 进入紧缺状态"——三处同词，需统一到 C-18 的两量方案。
    - **食物是否为通货**：本份判 FOOD 不可交易。若对话/UI GDD 已按"五簇含食物"或"六簇皆可交易"起草，会与 `PriceTable` 无 `FOOD.base` 直接对撞。

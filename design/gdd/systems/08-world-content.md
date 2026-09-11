# S8 · World-Content（区域内容与史线）· 系统设计文档

- **Task ID**：GDD-008｜**阶段**：Phase 2 · 遗留批（与 S9 并行）｜**优先级**：**P1**（S1 / S3 / S7 的硬接口供方）
- **主笔**：design-strategist（文策渊）｜**调度**：主理人（游承峰）｜**状态**：**已裁决回填**（PHASE2-CLOSING：§2.6 successorActorId 单射已修正，附录 A/B 回填裁决；原名 "待拍板" 作废）
- **依赖已读**：`game-concept.md` · `06-consistency-review.md` · `00-foundation.md`(S0) · `01-scarcity-loop.md`(S1) · `03-economy-barter.md`(S3) · `05-community-creed.md`(S5) · `07-work-board.md`(S7) · `02-npc-simulation.md`(S2，检索) · `04-dialogue-contract.md`(S4，检索) · `docs/architecture/accessibility.md`(检索)
- **依赖方向（严格单向）**：S8 是 **worldgen 期一次性产出、运行期只读** 的静态表集合。S8 不读任何系统运行时状态；S1/S3/S7 只读 S8，S8 不读它们。
- **边界（严格）**：2×2km 手写区域布局 · 废墟与容器播种 · `WaypointGraph` 与路程常量表 · 终端日志/遗书/宣传品文本 · 旧政权史线 · 1 个 Black Isle 式样板角色 · `protectedActors` 名单与 `successorActorId`。
  **不写**：拾荒判定（S1）· 经济数值（S3）· UI 布局（S9）· NPC 行为与调度（S2）· 声望与信条（S5）· 对话分支本体（S4）。
- **规模纪律**：地区硬上限 **2×2 km**；有名字 NPC **≤16**（S8 不新增有名字者，仅指定其中 3 名为 `protectedActors`）；容器 **21 个**；`WaypointGraph` 节点 **13**；史线碎片 **10**。

---

## 1. 系统概览与目标（对应支柱/动词）

S8 是"世界第一次呼吸"的那一层：它决定**哪里有东西、走过去要多久、路上会碰到什么、以及你不来的那几十年里这里死过什么人**。它不生产任何数值，它生产**玩家脚下的地面**。

- **支柱**：**P1（主）**——两社区相隔 2.2 km，一切宏观缺口必须落成一次短途步行；**P2**——同一废墟白昼与夜里是两处地方（作息错位），地形与路况必须肉眼可读；**P4**——每个容器属于某个具体的人（claimant 播种），每具尸体都有一个能被反推的死因；**P3**——史线是**已经死透的过去**，不因玩家到来而改变。
- **动词**：S8 是 取 SCAVENGE（容器与其内容）与 读 OBSERVE（场景证据、终端、墙面）的**材料供方**；负 HAUL 的难度由 S8 的路程与风险表定义；易 BARTER 的价差前提由 S8 的两社区地理不对称定义。
- **反目标**：不做程序化地形（hand-authored only）；不做会刷新的据点；不做"任务区域"式布局；**不做第二处有名字的聚居点**（信条互斥只对两社区成立）。

### 1.1 一句话职责
> 让"两社区为什么要互相依赖"这件事，在你走到岔路口之前就已经写在地上、写在尸体朝向里、写在配水配额表的最后一栏。

---

## 2. 核心概念与数据模型（含全部播种表与路径表）

### 2.1 区域布局（2×2 km，坐标原点 = 西南角；x→东、y→北，单位 m）

```
y=2000 ┌──────────────────────────────────────────────┐
       │  ●ward(700,1780)         ●well_gate(480,1720) │ 井窖(黄土台地)
       │   │420                     │440              │
       │  ●embankment(720,1400)──●tank_bend(1000,1260) │
       │                           │200               │
       │ ●tank_farm(1300,1140)──●admin_yard(980,1080) │ ←旧政权配水总站/黑市
       │   │690                    │420               │
       │ ●rad_pit(1760,1560)     ●crossroad(1180,760) │
       │ ●propaganda(1320,860) ╱    │410              │
       │ ●pumphouse(820,900)──430╱  ●convoy(940,480)  │
       │                          ●ferry(1420,440)    │
y=0    │               ●valley_gate(1680,220) 河谷    │ 河谷(低洼冲积/沙化)
       └──────────────────────────────────────────────┘
         x=0                                     x=2000
```

| zoneId | 地形 | `hazardLevel` | 主要簇 | 角色 |
|---|---|---|---|---|
| `he_valley` | 低洼冲积·季节河·**沙化土（不可耕）** | 0 | WATER | 社区（信条·产水不产粮） |
| `jing_cell` | 黄土台地·竖井窑院·**水层深** | 0 | MEDS, SEED | 社区（信条·产药产种但致命缺水） |
| `admin_yard` | 旧政权配水总站·混凝土外院 | **1** | — | **无信条地点**·黑市·跨社区唯一接触点 |
| `pumphouse` | 旧水厂/泵站废墟 | **1** | PART·情报 | 程九旧岗·终端日志 |
| `convoy` | 废车队·翻覆卡车 | **1** | AMMO, FUEL | 遗书·宣传品 |
| `tank_farm` | 旧油罐区 | **1** | FUEL | 全图唯一燃料源地 |
| `ward` | 旧诊所/病房 | **1** | MEDS | 药圃知识源·地窖遗体 |
| `propaganda` | 宣传站/广播塔基座 | **1** | — | 广播磁带·传单 |
| `rad_pit` | 弹坑·沉降池 | **3** | UNIQUE·AMMO | 高价值高风险 |

> `hazardLevel` 静态整数 0..3，辐射速率取 S1 附录 A-1 `HAZARD_RAD_PER_HOUR = [0,2,6,14]` 点/h，**不做动态扩散**（S1 §5.3 stub）。**两社区天生产不同物**：河谷有河无诊所（`ward` 远在 1230m 外），井窖有诊所与温室但井产仅 `0.7×`（S3 §2.4）——**这不是配置，这是地形**。

### 2.2 `locationId` 命名规范（交付 #4）

```text
LocationId = "<zoneId>.<kind>_<nn>"     kind ∈ { gate, board, stock, post, market, landmark, exit }
Post.locationId = "<community>.post_<role>_<nn>"   // 与 PostId "<community>.<role>_<nn>" 一一对应
```

| zoneId | gate | board | stock | post 锚点 | 其它 |
|---|---|---|---|---|---|
| `he_valley` | `he_valley.gate_01` | `he_valley.board_01` | `he_valley.stock_01` | `he_valley.post_water_01/_water_02/_watch_01/_mend_01/_care_01` | — |
| `jing_cell` | `jing_cell.gate_01`（过秤台） | `jing_cell.board_01` | `jing_cell.stock_01` | `jing_cell.post_water_01/_watch_01/_patrol_01/_mend_01/_care_01` | — |
| `admin_yard` | `admin_yard.exit_01` | — | — | — | **`admin_yard.market_01`**（黑市·S6 跨社区接触点）· `admin_yard.landmark_01`（配额墙） |
| 其余 6 zone | `<zoneId>.exit_01` | — | — | — | `<zoneId>.landmark_01` |

> `he_valley.board_01` / `jing_cell.board_01` 即 S7 §2.2 `Board.locationId`（社区门口）；`admin_yard.market_01` 是 S6 §3.3 唯一跨社区共享 `locationId`。

### 2.3 `WaypointGraph` + 路程常量表（交付 #3 — **直接决定 S3 的 `c_min`**）

13 节点；边长为**已含 `ROAD_WINDING_MUL` 的实际行走米数**（布线折返系数，Proposal A-1）。

| 节点 | 坐标 | | 边 | 米 | 强制遭遇点 |
|---|---|---|---|---|---|
| `wp.valley_gate` | (1680,220) | | V–F | 380 | — |
| `wp.ferry` | (1420,440) | | F–X | 420 | **`enc.crossroad_wreck`** |
| `wp.crossroad` | (1180,760) | | X–A | 420 | — |
| `wp.admin_yard` | (980,1080) | | A–B | 200 | **`enc.tank_bend`** |
| `wp.tank_bend` | (1000,1260) | | B–E | 340 | — |
| `wp.embankment` | (720,1400) | | E–W | 440 | **`enc.embankment`** |
| `wp.well_gate` | (480,1720) | | X–P | 430 | — |
| `wp.pumphouse` | (820,900) | | F–C | 530 | — |
| `wp.convoy` | (940,480) | | C–X | 410 | — |
| `wp.tank_farm` | (1300,1140) | | A–T | 360 | — |
| `wp.ward` | (700,1780) | | W–D | 250 | — |
| `wp.propaganda` | (1320,860) | | E–D | 420 | — |
| `wp.rad_pit` | (1760,1560) | | B–R | 900 | **`enc.rad_pit_edge`** |
| | | | T–R | 690 | — |
| | | | X–G | 190\* | — |

**关键里程表（S7 §5.3 需要的 `pathLenMeters` 静态表）**

| O → D | 路径 | `lenM` | `tripMin`@`speedMul=1.0` | `hazardMax` | `p_min` | `p_max` |
|---|---|---|---|---|---|---|
| 河谷 → 井窖（**主路**） | V-F-X-A-B-E-W | **2200** | 27 | 1 | **0.06** | **0.30** |
| 河谷 → 旧政权设施（黑市） | V-F-X-A | 1220 | 15 | 1 | 0.04 | 0.22 |
| 井窖 → 旧政权设施 | W-E-B-A | 980 | 12 | 1 | 0.03 | 0.18 |
| 河谷 → 旧水厂 | V-F-X-P | 1230 | 15 | 1 | 0.04 | 0.20 |
| 井窖 → 旧诊所 | W-D | 250 | 3 | 1 | 0.01 | 0.08 |
| 河谷 → 废车队 | V-F-C | 910 | 11 | 1 | 0.03 | 0.16 |
| 旧政权 → 油罐区 | A-T | 360 | 5 | 1 | 0.02 | 0.10 |
| 旧政权 → 辐射坑 | A-B-R | 1100 | 14 | **3** | 0.08 | 0.34 |

\* `X–G` 为宣传站接入边（`wp.propaganda` 记作 G）。`tripMin = ceil(lenM/(1.4×60))`；S3 §3.6⑤ 的 2.2km / 29 分钟示例即主路在 `speedMul≈0.89` 下的取值（29 = ceil(2200/74.8)）。

**遭遇点表（S2 §3.3 `P_AMBUSH` 的唯一来源；S8 拥有）**

| `encId` | 所在边 | `hazard` | 判定（S0·A，S8 不私改 DC） | 必有后果 |
|---|---|---|---|---|
| `enc.crossroad_wreck` | F–X | 1 | `MIND/survey, DC_DEMANDING` | 失败 → `Injury MINOR` + 丢失 1 件持有物 |
| `enc.tank_bend` | A–B | 1 | `VIGOR/stealth, DC_DEMANDING` | 失败 → 载货损失 25%（`LOSS_RATE`） |
| `enc.embankment` | E–W | 1 | `MIND/survey, DC_PERILOUS` | 失败 → `Injury MINOR` + 载货损失 |
| `enc.rad_pit_edge` | B–R | 3 | `VIGOR/stealth, DC_PERILOUS` | 失败 → `rad +30` + 载货损失 |

### 2.4 `ContainerDef` 播种表（交付 #1 · 实际播种，非格式说明）

`D/E/N` = `discoveryDC / extractDC / noticeDC` 档位（`平`10 `苛`14 `危`18 `致`22，取 S0 §D 四档，**不新增**）；`K` 列 = `isUniqueAnchor`；`claimantSlots` 采 S1 `Claimant{actorId, plannedDay, priority, needReason}`，`needReason` 限 S1 闭集 5 值。

| `containerId` | zoneId | tier | `initialItems`（簇:量 / 物件） | D/E/N | K | `claimantSlots` | 叙事 |
|---|---|---|---|---|---|---|---|
| `ctr_ph_01` | pumphouse | 普通 | PART×3 | 平/平/平 | — | — | 拆散的阀件 |
| `ctr_ph_02` | pumphouse | 普通 | PART×2, FUEL 0.5L | 平/苛/平 | — | — | 工具箱 |
| `ctr_ph_03` | pumphouse | 稀缺 | WATER 滤芯×1 | 苛/苛/苛 | — | `he_valley.water_02 / POST_SUPPLY` | 两水岗共用备件 |
| `ctr_ph_04` | pumphouse | **关键** | `frag.pump_scada_log`, PART×4 | 苛/危/苛 | ✅ | `he_valley.mend_01 / STOCK` | **泵房终端·程九旧岗** |
| `ctr_cv_01` | convoy | 普通 | FOOD 2 portion | 平/平/平 | — | — | 车厢干粮 |
| `ctr_cv_02` | convoy | 稀缺 | AMMO 8 rd | 苛/苛/苛 | ✅ | `he_valley.patrol_01 / POST_SUPPLY` | 押运车厢 |
| `ctr_cv_03` | convoy | 稀缺 | FUEL 4 L | 苛/苛/苛 | — | `jing_cell.patrol_01 / POST_SUPPLY` | 油箱未空 |
| `ctr_cv_04` | convoy | 普通 | `frag.engineer_letter`, `frag.leaflet_ration` | 平/平/苛 | ✅ | — | 水浸的公文包 |
| `ctr_tf_01` | tank_farm | 普通 | FUEL 1.5 L | 平/苛/平 | — | — | 虹吸底油 |
| `ctr_tf_02` | tank_farm | 稀缺 | FUEL 6 L | 苛/苛/苛 | — | `he_valley.patrol_01 / STOCK` | 未开罐 |
| `ctr_tf_03` | tank_farm | **关键** | PART×5（`uid_valve_core` 候选） | 危/危/苛 | ✅ | `jing_cell.care_01 / POST_SUPPLY` | 主阀门芯 |
| `ctr_wd_01` | ward | 普通 | MEDS 0.3 dose | 平/平/平 | — | — | 药柜零散 |
| `ctr_wd_02` | ward | 稀缺 | MEDS 1.5 dose | 苛/苛/苛 | — | `jing_cell.care_01 / TREAT_OTHER` | 冷藏柜 |
| `ctr_wd_03` | ward | **关键** | `frag.ward_cellar_family`, SEED 培养皿×1 | 苛/危/苛 | ✅ | `jing_cell.care_01 / DEPENDENT` | **地窖一家同死** |
| `ctr_wd_04` | ward | 普通 | 病历（物件） | 平/平/平 | — | — | 走廊柜 |
| `ctr_pg_01` | propaganda | 普通 | `frag.radio_last_tape`, 海报 | 平/平/苛 | — | — | 磁带标签「照常」 |
| `ctr_pg_02` | propaganda | 稀缺 | PART×2, 电池 | 苛/苛/平 | ✅ | `he_valley.mend_01 / STOCK` | 发射机零件 |
| `ctr_rp_01` | rad_pit | **关键** | AMMO 6 rd（`uid_valve_core` 候选） | 危/危/危 | ✅ | `jing_cell.patrol_01 / STOCK` | 沉降池军用箱 |
| `ctr_rp_02` | rad_pit | 稀缺 | MEDS 0.8（`uid_seed_culture` 候选） | 危/苛/危 | ✅ | `jing_cell.care_01 / DEPENDENT` | 医疗弃置箱 |
| `ctr_hv_02` | he_valley | 稀缺 | WATER 3 L（封存） | 苛/苛/苛 | — | `he_valley.water_01 / STOCK` | 待分配净水 |
| `ctr_jc_02` | jing_cell | 稀缺 | SEED 0.15, MEDS 0.4 | 苛/苛/苛 | ✅ | `jing_cell.care_01 / DEPENDENT` | **留种柜** |

> **tier 判定不可迁移**（S1 §2.2）：`稀缺` = 含 ≥1 单位 MEDS/FUEL/AMMO 或错季物资；`关键` = 承载唯一件锚点或某 `PostId` 的唯一补给锚点。**`claimantSlots` 对 `稀缺`/`关键` 恒非空，且 `关键` 的 `needReason` 必指向某 Post**（S1 硬约束）。
> **`observeRadius` 地板 4.0m**（可访问性 §3.1 R-S3）：每个容器的贴面必须能从一条 ≥4.0m 的不可阻挡走道上接近；同一立面每 ≤8m 至少 1 个可读锚（容器 / 尸位 / 涂写），保证 `MIND=1` 玩家不漏读。

### 2.5 `UniqueManifest`（交付 #2）

**候选集划分原则（三条）**：① **按知识轴切**——每件唯一件只属于一个轴（技术 / 水利能源 / 医疗），轴内候选 **|K| = 3 且跨 ≥3 个 zoneId**；② **候选集互斥**——同一容器只作一件唯一件的候选，故一容器最多承载 1 件唯一件；③ **与社区无关**——K 不得全部落在任一社区的补给半径内，防"清空一个区 = 全图关键知识团灭"。

| `uniqueItemDefId` | 轴 | 候选集 K（容器） | 跨 zone | 玩家用途 |
|---|---|---|---|---|
| `uid_pump_manual` | 技术 | `ctr_ph_04` / `ctr_pg_02` / `ctr_cv_04` | 3 | 驳倒程九第 2 段自白 |
| `uid_valve_core` | 水利能源 | `ctr_tf_03` / `ctr_rp_01` / `ctr_cv_02` | 3 | 修复 `pumphouse` 泵阀（`he_valley` 产能前提） |
| `uid_seed_culture` | 医疗 | `ctr_wd_03` / `ctr_jc_02` / `ctr_rp_02` | 3 | 药圃/留种的知识补全 |

**seed 抽签（worldgen 期一次，之后只读）**

```text
containerId_anchor = K[ rng(makeSeedCtx("gen.unique."+uniqueItemDefId, 0)).nextInt() % |K| ]
```
`streamId = gen.unique.<uniqueItemDefId>`（S1 §3.5 已登记，静态可枚举）。`worldSeed` 固定则归属固定；改 seed 可重抽而**不手改数据**。

**三态追踪（不新增事件，全部走 S1 既有负载 + `AttributionLink`）**

```text
SEALED --容器开启且该槽位被取走--> IN_TRANSIT --> CONSUMED | DESTROYED | LOST
RefId = "S1.unique.<uniqueItemDefId>.state"
```
- `IN_TRANSIT` 必须与一个 `LoadState` 绑定的 actor 同在；持有者死亡由 S0 Part E 释放，S1 只保证最后 `lastContainerId` 已落盘（遗物可寻回）。
- **`LOST` 三类成因**（S1 §2.3）：所在 `ZoneMutation` 已应用且持有者死亡无 heir / 被弃于进入不可达的 zone / 玩家销毁。**每次进入 `LOST` 必写 warn 级 `AttributionLink`**，缺归因即视为未实现——**关键件无声消失直接击穿 P1**。
- 启动校验：每个 `uniqueItemDefId` 在 manifest 内**恰好 1 条**；`|K| ≥ 3`、`zoneSpan(K) ≥ 3`、候选集两两不相交——违反即报错（R5"无菌荒土"硬保险）。

### 2.6 `protectedActors` 初始名单（交付 #5 · ≤3，含 Black Isle 样板角色）

| # | actorId | 名字 | `successorActorId` | 为什么**不可**被随机死亡破坏 |
|---|---|---|---|---|
| 1 | `npc.he_valley.mend_01` | **程九** | `npc.he_valley.patrol_01`（同社区备位者·能力缺口） | **文本密度样板角色**：全图唯一承载 1200–1800 字长自白与"可被空间证据反驳"的立场（§3.4）。他一死，`uid_pump_manual` 的双向闭环与旧政权史线的**唯一口头来源**同时消失，P1「一切落到具体的人」失去承载物。 |
| 2 | `npc.jing_cell.care_01` | **苗青** | `npc.jing_cell.mend_01` | **全图 MEDS/SEED 的唯一知识源与唯一产地**（S3 §2.3：两簇仅挂 `jing_cell.care_01`）。随机死亡 → 河谷 MEDS 恒缺 + 井窖唯一盈余归零 → D1/D3 的经济结构**整体塌一条腿**，且 S7 的 `care_01` 缺口永久不可补（板会卡死）。保护她 = 保护**结构**，不是保护一个人。 |
| 3 | `npc.he_valley.water_01` | **石根** | `npc.he_valley.water_02`（同岗次席） | **河谷是全图唯一的水出口**（+15 L 盈余，S3 §2.4）。他是取水岗首席，也是 `pumphouse` 泵阀知识的持有人在。随机死亡 → 当日 `safeDays` 可见崩塌（S4 §2.6 兜底理由）+ 唯一商路 D3 断供 → 全图无任何可持续经济动线。 |

**保护 ≠ 无敌（四条，与 S4 §2.6 逐字一致）**：① **离屏免疫**——`tier == COARSE` 期内**不判死**，降级为伤病 + 强制回社区（在 S0·B §B3.3 预告闸门之上再加一层）；② **玩家可杀**——FULL 内玩家亲手致死**照常成立**；③ **叙事兜底**走 `SUCCESSOR`（`successorActorId` 非 null 为硬约束），不走通用 `GRAVE`；④ claimant **不释放，直转 heir**（S0 Part E ①）。

> **`successorActorId` 全局单射（裁决 · 阻塞项 · PHASE2-CLOSING）**：三条记录的 `successorActorId` 两两互异——`he_valley.mend_01 → he_valley.patrol_01`、`jing_cell.care_01 → jing_cell.mend_01`、`he_valley.water_01 → he_valley.water_02`，**禁止任何 actor 被两个岗位同时引用**（一人一岗）。
> 原稿 `npc.he_valley.water_02` 同时作 `mend_01`（程九）与 `water_01`（石根）的 successor：若两人在**同一日切内相继死亡**，Part E 会对同一 `postId` 的解析路径调用两次 `heirActorId`，**继承结果依赖应用顺序** → 违反 S0 确定性纪律（同 seed 必须逐位可复现），并让 S7 岗位满员判定出现"一人占两岗"的矛盾态。
> **修法**：`water_01 → water_02` 保留（`water_02` 是水岗**同岗次席**，唯一符合 `heirActorId`"同社区同岗位在岗次席"语义的接替者）；`mend_01` 的 successor 改指 `npc.he_valley.patrol_01`（河谷巡线岗，**无泵阀/旧政权配水知识**——**该能力缺口是刻意的、可接受的 P4 叙事材料**，非缺陷；程九的 2/3 段自白仍由 S4 §2.6 死亡兜底经其遗稿 `itemId` 承接，**知识不随人死**）。河谷 `mend` 岗 `rated == 1`，本就无 `mend_02` 次席可指。

### 2.7 旧政权史线：10 片可拼凑的碎片（交付 · 特别要求）

**碎片不写成线性文本，而写成"空间证据"**：玩家在 `observeRadius` 内读到场景，或在容器里拾到文本物件，两者都经 S4 `RevealKnowledge{refId}` 落进玩家情报日志——**由玩家自己把碎片连成结论**。

| `fragmentId` | 类别 | 位置（容器 / 场景） | `refId` | 佐证 claim |
|---|---|---|---|---|
| `frag.pump_scada_log` | 终端日志 | `ctr_ph_04` | `knowledge.pump_scada` | 配水政权 · 资源崩溃 |
| `frag.quota_wall` | 设施用途 | `admin_yard.landmark_01` 墙面（场景，非容器） | `knowledge.quota_wall` | 配水政权 |
| `frag.radio_last_tape` | 宣传品 | `ctr_pg_01` | `knowledge.radio_tape` | 信息滞后 |
| `frag.leaflet_ration` | 宣传品 | `ctr_cv_04` | `knowledge.leaflet` | 配水政权 · 信息滞后 |
| `frag.engineer_letter` | 遗书 | `ctr_cv_04` | `knowledge.eng_letter` | 被遗弃（非战败） |
| `frag.corpse_at_post` | 尸体位置 | `pumphouse` 岗位旁（场景） | `knowledge.corpse_post` | 被遗弃（非战败） |
| `frag.convoy_facing_out` | 尸体位置/场景 | `convoy` 车队朝向（场景） | `knowledge.convoy_heading` | 被遗弃（非战败） |
| `frag.ward_cellar_family` | 尸体位置 | `ctr_wd_03` 地窖 | `knowledge.ward_cellar` | 资源崩溃 |
| `frag.tank_empty` | 设施用途 | `tank_farm` 油罐液位（场景） | `knowledge.tank_empty` | 资源崩溃 |
| `frag.torn_last_page` | 终端/缺页 | `admin_yard` 档案柜（**空柜**，仅有撕痕与水渍） | `knowledge.torn_page` | **终局留白** |

**玩家如何从空间证据反推"旧政权怎么死的"**（S8 只供碎片，S4 供 `RevealKnowledge`）：
1. **尸体不在阵地上，而在岗位上**（`frag.corpse_at_post`）＋车队**车头朝外、物资仍在**（`frag.convoy_facing_out`）→ 不是战死，是**被遗弃**；撤离命令来得突然，且**没人回来**。
2. **配水总站墙上最后一栏配额表**（`frag.quota_wall`）与**泵房终端停机记录**（`frag.pump_scada_log`）**同日**→ 政权不是被打死的，是**配水系统自己停了**。
3. **油罐区已空**（`frag.tank_empty`）＋**地窖里一家人同死、未逃散**（`frag.ward_cellar_family`）→ 崩溃的传导顺序：燃料停 → 泵停 → 配水中断 → 医疗与平民首先死。
4. **广播磁带到最后仍宣称"照常"**（`frag.radio_last_tape`、`frag.leaflet_ration`）→ 信息与事实彻底脱节，是**弃守先于公告**。

**至少一条永远无法被完全证实的留白**：`frag.torn_last_page` 是**文件正文缺失的最后一页**（只有撕痕与水渍）。因此"最高决策层究竟是**下令弃守**、还是**根本没人下令（指挥真空）**"——**永不给出补充文本**。玩家能证明"系统停了"，但永远无法证明"是命令还是崩溃"。该 `claimId = claim.final_decision` 的闭合条件在 S4 情报日志里恒为「证据不足」，且**不设任何后续碎片去补它**。

---

## 3. 规则与公式（路程、风险、唯一件抽签、文本触发条件）

### 3.1 路程与运输成本下界（**回应 S3 §3.6 的 `C_trip` / `LOSS_RATE`**）

```text
travelMin(a→b) = ceil( pathLenMeters / (BASE_WALK_SPEED(1.4) × speedMul(actor) × 60) )   // 与 S2 §3.3 同式
C_trip         = 2 × travelMin × TIME_VALUE_RU_PER_GAME_MIN + C_stamina                  // 往返口径
p_enc(q)       = clamp( p_min + (p_max − p_min) × (q / Q_max), 0, 1 )                    // 随载货单调不减
constraint:  c(q) = C_trip/q + p_enc(q) × LOSS_RATE × V  ≥  C_trip/Q_max + p_min·LOSS_RATE·V  ≡ c_min > 0
```

- **`LOSS_RATE = ENCOUNTER_LOSS_RATE = 0.25`**（S3 附录 A-16 已有此值，S8 为其提供语义来源：单次遭遇平均损失**载货价值的 25%**，与 §2.3 `enc.tank_bend` 的"载货损失 25%"一致）。
- **主路实测算例（河谷→井窖，VIGOR 5 → `carryCap` 50 kg，水 1.2 kg/L → `Q_max` = 41 L）**：往返 `travelMin = 54`；取 `TIME_VALUE_RU_PER_GAME_MIN = 0.03`、`C_stamina ≈ 0.27 RU` → **`C_trip ≈ 1.90 RU`**；`C_trip/Q_max = 0.046 RU/L`；风险项 `p_min×LOSS_RATE×V = 0.06×0.25×0.85 = 0.013 RU/L` → **`c_min ≈ 0.059 RU/L`**，与 S3 §3.6⑤ 的 `c(q) ≈ 0.05 RU/L` 同量级。
- **`p_min > 0` 的结构性来源（S3 §3.6③ 要的"三根支柱"之一）**：**主路无论载货多少都必须经过 2 个 `hazardLevel ≥ 1` 的强制遭遇点**（`enc.crossroad_wreck`、`enc.embankment`）→ `p_min = 0.06 > 0`，**无法靠玩法消除**；辐射坑路线 `p_min = 0.08` 更高。黑市只改路线、不改负重，故不改 `p_min > 0`。
- **`C_trip` 口径 = 往返（已裁决 · 裁决 B-1）**：采用**往返**（`C_trip = 2 × travelMin × TIME_VALUE_RU_PER_GAME_MIN + C_stamina`，玩家最终要回到有货的一端），得主路 `C_trip ≈ 1.90 RU`、`c_min ≈ 0.059 RU/L`。**理由**：运费应按一次完整商业行程计，单程会系统性低估玩家实际成本，使 D3 的套利收益显得比实际更差、误导玩家决策。单程口径（`C_trip ≈ 0.95 RU`、`c_min ≈ 0.036`）**作废**；两者虽均 `> 0`、不动摇 D3 结论，但往返是本份**唯一生效口径**。

### 3.2 风险（遭遇点的唯一来源）

每个遭遇点的判定**一律取 S0 §D 四档常量**，S8 不私改 DC；失败后果由 S8 声明（上表），执行归 S2 行为树 / S1 伤病。`p_enc` 随 `q` 上升的物理理由：载货↑ → `speedMul`↓（S1 §3.2 平方项）→ 暴露时间↑。**该耦合是刻意的**：贪心超重同时被代谢到搬运、遭遇、耐力三条线上。

### 3.3 唯一件抽签

见 §2.5。**RNG 纪律**：S8 的 raw `rng` 调用数 = **1/uniqueItemDefId**（仅 worldgen tick 0，`gen.unique.*` 流），其余全静态。**禁止在渲染帧发起任何抽签**。

### 3.4 文本触发条件

| 触发源 | 条件 | 产物 |
|---|---|---|
| **容器内文本物件** | 走 S1 三段 `rollCheck`；`discoveryDC` 成功 → 文本物件可见可拾 | 拾取 → S4 `RevealKnowledge{refId}`（**无骰子、无口才**） |
| **场景证据**（墙面/尸位/车队朝向/油罐液位） | 进入 `observeRadius`（`4 + 0.6×MIND`，地板 4.0m）即自动读入 | 直接 → `frag.*` 解锁 |
| **程九的 `RebuttalPoint`** | `requires = {RevealKnowledge: knowledge.pump_scada \| knowledge.eng_letter} \| {carried: uid_pump_manual}` | 反驳**不掷骰**；成功 → 他改口、结论句被替换、写 `MemoryEvent` + `martyr_claim` 候选印象 |

---

## 4. 状态与流程

**worldgen 一次性管线（`Boot → LoadSeed`，产物全部只读）**

```text
seed → 布局表(§2.1) → 节点/边(§2.3) → 播种容器(§2.4)
     → 每 uniqueItemDefId 抽签(§2.5) → 写只读 UniqueManifest → 校验(|K|≥3, zoneSpan≥3, 候选互斥, 每条恰好 1)
     → 落 protectedActors(§2.6) + successorActorId → 落史线碎片(§2.7)
     → 发布静态表；之后 S8 无任何写操作
```

**运行期（唯一状态）**：唯一件三态（`SEALED → IN_TRANSIT → CONSUMED|DESTROYED|LOST`），由 S1 承载；S8 只提供 manifest 与 K。容器状态机完全归 S1（§4.1）；zone 物资守恒归 S1（§8 验收 1）。

---

## 5. 对外接口（暴露 / 依赖 / stub）——**逐条回应 S1 / S3 / S7**

### 5.1 暴露（全部只读静态表）

| 接口 | 消费方 | 内容 |
|---|---|---|
| `ContainerDef[]` | **S1** | §2.4：`containerId/zoneId/tier/initialItems/D-E-N 档位/isUniqueAnchor/claimantSlots` |
| `UniqueManifest` + `K(uniqueItemDefId)` | **S1** | §2.5 |
| `ZoneConfig(zoneId)` | **S1** | §2.1：`hazardLevel`（静态 0..3） |
| `pathLenMeters(a,b)` · 遭遇点表 | **S3 / S7 / S2** | §2.3 |
| `locationId` / `Board.locationId` | **S7 / S5 / S6** | §2.2 |
| `protectedActors[]` + `successorActorId` | **S0·B LOD / S2 / S4 / S5** | §2.6（**全局唯一一份名单**，唯一定义处登记进 S0 §D） |
| 史线碎片表 + `refId` | **S4** | §2.7（程九 `RebuttalPoint.requires` 的 refId 来源） |

### 5.2 逐条回应（S1 / S3 / S7 对 S8 的请求——**全部闭合**）

| 请求方 | 请求 | S8 回应 |
|---|---|---|
| **S1** §5.2 / 附录 B-4 | `ContainerDef` 完整字段（含三档 DC 与 `isUniqueAnchor`） | ✅ §2.4（**实际播种表**，非格式说明） |
| **S1** §3.5 | `zoneId`/`containerId`/`slot` 静态可枚举 | ✅ §2.1 + §2.4，启动即可完整列出，满足 S0 §C3 存档校验 |
| **S1** §2.3 | 唯一件候选集 K（|K|≥3，跨不同 zone）+ seed 抽签 + 三态追踪（`LOST` 必写 warn 归因） | ✅ §2.5 |
| **S1** §2.5 | `hazardLevel` 静态配置整数 0..3 | ✅ §2.1 |
| **S3** §3.6 | `C_trip` 与 `LOSS_RATE` 的真实来源 | ✅ §3.1（主路 2200m / 54 分钟往返 → `C_trip ≈ 1.90 RU`；`LOSS_RATE = 0.25`；`p_min = 0.06 > 0` 有来源） |
| **S7** §5.3-B5① | 每社区缺口板物理 `locationId` | ✅ §2.2：`he_valley.board_01` / `jing_cell.board_01` |
| **S7** §5.3-B5② | `pathLenMeters(a,b)` 静态表 | ✅ §2.3 关键里程表（缺则 S7 `travelMin` 恒取 0，排序退化） |
| **S7** §5.2 / S2 §3.3 | 遭遇点（`P_AMBUSH` 来源） | ✅ §2.3 遭遇点表 |
| **S6** §3.3 | 跨社区共享 `locationId` | ✅ §2.2：`admin_yard.market_01`（Core 期唯一） |
| **S5** §5.1 | `Post.locationId` | ✅ §2.2 命名规范（`<community>.post_<role>_<nn>` ↔ `PostId`） |
| **S4** §2.6 / §3.5 | `protectedActors` + `successorActorId` 非空；程九 `RebuttalPoint` 的 refId | ✅ §2.6 + §2.7 |

### 5.3 依赖 / stub

- **依赖（只读）**：S0 §D 常量（四档 DC / `BASE_WALK_SPEED` / `MASS_PER_UNIT` / `HAZARD_RAD_PER_HOUR`）· S0·C `rng(seedCtx)`（仅 worldgen）。
- **stub（明示未完成）**：天气/季节（Extended，现一律常量）· `hazardLevel` 动态扩散（Vision，现静态）· 导航网格与路点插值（架构 stub，S0 §B5）· 社区内建筑内部布局（S9 表现层接管）。

---

## 6. 玩家可感知表现（区域如何被"读懂"）

1. **地形即经济表**：河谷在低洼、泥泞、沙化土上（有水无粮无医），井窖在黄土台地上、院里有温室与药圃（有药有种但井深）。**玩家走一趟就明白谁依赖谁**，不需要 NPC 说。
2. **路程的痛在岔路口就发生**：主路 2.2 km、27 分钟，中途两个遭遇点——"背 30 升水过去"是一个**在地图上就能算清的几何题**（S1 打包界面同屏显示 `speedMul` 与预计行程）。
3. **旧政权史线由玩家自己拼**：没有人给你讲历史。你在**岗位上的尸体**、**朝外停的车队**、**配水总站的配额墙**、**泵房终端**之间做连线；连线成功的那一刻是 Discovery 的高潮。**最后那页缺失的文件**则永远让你无法确定"是命令还是崩溃"。
4. **两社区相隔 30 分钟的宵禁差**（河谷 21:30 / 井窖 21:00）在这条主路上变成每日一次的赶路决策（承 S5 §6-3）。
5. **可读性的地板是 4.0m**：`MIND=1` 的玩家在最窄走道上也能读全容器贴面与场景证据；`observeRadius` 更高的玩家在更远处先看见。

---

## 7. 边界情况与失败模式（≥3 类）

**E1 · 唯一件抽签把候选全塞进一个区**：候选集划分若违反"|K|≥3 跨 ≥3 zone"，则一次清区即可团灭关键知识。**防护**：worldgen 校验三条件（`|K|≥3`、`zoneSpan≥3`、候选集两两不相交），**违反即报错拒绝开局**；`--assert-unique-anchor-third` 常态断言。
**E2 · 唯一件进入 `LOST` 而无归因**：关键件无声消失 = P1 击穿。**规则**：三态每一跳写 `AttributionLink`（`RefId = S1.unique.<id>.state`），`LOST` 必写 **warn 级**；缺归因即判未实现。救济路径：`IN_TRANSIT` 期间持有者死亡 → Part E 释放 → 遗物可寻回。
**E3 · `protectedActors` 在离屏被判死**：必须满足 §2.6 ①（`tier==COARSE` 期不判死）。若实现为 COARSE 判死 → 违反 C-27，`--assert-protected-full` 报警——这是"叙事不可替代却在离屏死掉"的 P1 击穿。
**E4 · 史线碎片从未被发现（留白变成"没有叙事"）**：每条可闭合 claim 至少 **2 处**碎片冗余播种（§2.7 已满足：配水 3 / 崩溃 4 / 遗弃 3 / 滞后 2）；仅 `claim.final_decision` 例外，恒为「证据不足」且**永不补页**。
**E5 · 两社区间主路不可达**（塌方/封锁）：主路是**唯一陆路**，故须保证 `admin_yard.market_01` 至少落在一条可达路径上；若某日两社区目的地均不可达 → 触发 S5 §7-E2 兜底告警"泄压阀失效"，**不静默**。

---

## 8. 验收标准与调试钩子

**验收** ① **守恒前置**：逐 zone 容器 `initialItems` 求和 == `ZoneSnapshot` 首日快照（S1 A1 基线）。② **S3 `c_min` 有来源**：断言主路 `pathLenMeters==2200`、强制遭遇点 ≥2 且 `hazardLevel≥1`、`p_min>0` → 重算 `c_min≈0.059>0`。③ **S7 接口**：`Board.locationId` 非空、全部 O-D 的 `pathLenMeters` 有值（**不得回落为 0**）。④ **唯一件**：固定 seed 下 manifest 逐位复现 + 三条件校验 + `LOST` 全链 `UNATTRIBUTED==0`。⑤ **protectedActors**：名单全局唯一且与 S0 §D / S2 / S4 / S5 同源、`successorActorId` 非空、`COARSE` 期 10 日 `--kill` 三人**不生效**而 FULL 内生效。**且 `successorActorId` 在全局构成单射**（`--assert-successor-injective`：无任何 actor 被两个岗位引用）；**固定 seed 下同时杀死两名 protected actor，继承结果与死亡顺序无关**（`--kill` 交换顺序重跑，`heirActorId` 解析结果 + 岗位满员判定逐位一致）。⑥ **史线可拼**：`--dump-lore` 见 4 条可闭合 claim + 1 条恒「证据不足」的 `claim.final_decision`。⑦ **确定性**：运行期 raw `rng`=0（仅 worldgen）；静态表可由 seed 重建、不进 `worldHash`。

**调试钩子**
```text
--dump-zone <zoneId> / --dump-route <a> <b> / --dump-manifest / --dump-protected / --dump-lore [<claimId>]
--assert-unique-anchor-third      断言 |K|≥3 且 zoneSpan≥3 且候选集互斥（§2.5）
--assert-protected-full          断言 protected 仅 FULL 内可死，离屏告警（§2.6①）
--assert-successor-injective     断言 successorActorId 全局单射（无 actor 被两个岗位引用，§2.6 裁决）
```

## 附录 A · 已批准回填底座 §D 的常量（**裁决 B-2 · PHASE2-CLOSING 已生效**；唯一定义处已迁往 S0 §D）

> 以下三行**已批准回填 S0 §D 常量表**（来源标注 S8 · 裁决 B-2），本份不再就地定义，实现统一从 §D 取。`ROUTE_P_MIN·P_MAX` 为**逐路线静态配置**，非全局常量，故不登记。

| # | 常量 | 值 | 说明 |
|---|---|---|---|
| A-1 | `ROAD_WINDING_MUL` | 1.10 | 布线折返系数（§2.3 边长已含） |
| A-2 | `TIME_VALUE_RU_PER_GAME_MIN` / `STAMINA_VALUE_RU_PER_POINT` | 0.03 / 0.05 | `C_trip` 与 `C_stamina` 的 RU 换算（§3.1） |
| A-3 | `ENCOUNTER_LOSS_RATE` / `ROUTE_P_MIN·P_MAX` | 0.25 / 见 §2.3 | = S3 A-16 `LOSS_RATE`；`p_min/p_max` 逐路线静态配置，**非全局常量** |

## 附录 B · 裁决结果回填（PHASE2-CLOSING · 逐条已裁决，机械回填不再讨论）

1. **B-1 · `C_trip` 口径**（§3.1）：**已裁决 = 往返**（`c_min ≈ 0.059 RU/L`）；单程口径作废。理由：运费按一次完整商业行程计，单程系统性低估实际成本、误导玩家决策。
2. **B-2 · Proposal 常量**（A-1..A-3）：**批准 · 已回填 S0 §D**（标注来源 S8 · 裁决 B-2），本份附录 A 已迁为"已生效"。`ROUTE_P_MIN·P_MAX` 非全局常量，不登记。
3. **B-3 / B-5 · 登记**：**批准 · 已回填 S0 §D**——`protectedActors` 名单（含 `successorActorId`，标注"全局唯一定义处"）与 `locationId` 命名规范（`<zoneId>.<kind>_<nn>` / `<community>.post_<role>_<nn>`）。
4. **B-4 · 史线 `refId` 命名空间 `knowledge.*`**：**批准 · 已登记 S0 §D ID 命名规范区**，供 S4 `RevealKnowledge` 与程九 `RebuttalPoint.requires` 引用。
5. **B-6 · 规模比对**：S8 播 21 容器、**不新增有名字 NPC**（3 名 protected 从既有 16 人名单指定）。**已逐名核对 S2 §2.3 岗位编制表**：`he_valley.mend_01` / `he_valley.water_01`（河谷 `mend`/`water` 岗）与 `jing_cell.care_01`（井窖 `care` 岗）三个 `postId` **均存在于 S2 名单**，故 S8 指定的三个 actorId **无需改名**，与 S2/S4 §2.6 逐字一致。原 `water_02` 兼任两处 successor 的撞车已按 §2.6 裁决拆解（见 §2.6 注）。
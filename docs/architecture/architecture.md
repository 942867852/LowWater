# 《枯水期 / Low Water》主架构文档

- **版本**：v0.1（Phase 3 · PHASE3-001）
- **作者**：engineering-lead（程基岩）
- **状态**：**草案** —— 依赖 2 个 BLOCKER 签收（见 §11）与 ADR-001 引擎选型拍板
- **上游**：`design/gdd/game-concept.md`、`06-consistency-review.md`、`systems/00..07`（10 份）
- **配套**：`adr/ADR-000`（架构评审）· `adr/ADR-001`（引擎）· `adr/ADR-002`（RNG）· `adr/ADR-003`（离屏 LOD）· `adr/ADR-004`（事件总线）· `adr/ADR-005`（定点数）
- **纪律**：本文档**不重写设计**。所有 ID / 常量 / 接口签名与设计文档逐字一致；如发现设计问题，只在 §11「待裁决」提出，不就地改动。

---

## 1. 这份文档回答什么

| 问题 | 章节 |
|---|---|
| 代码怎么分层，谁能依赖谁 | §2 |
| 一帧里发生什么，时间怎么推进 | §3 |
| 四种 tick 各管什么 | §4 |
| LOD 怎么分、怎么切、切了会不会破坏确定性 | §5 |
| 数据层有哪些契约类型，怎么表示 | §6 |
| 离屏怎么算，存档存什么 | §7 |
| 性能预算怎么分给各系统 | §8 |
| 需要哪些工具才能迭代 | §9 |
| 最大的 5 个风险是什么 | §10 |

---

## 2. 技术栈与分层

### 2.1 五层结构

```
┌──────────────────────────────────────────────────────────────────────────┐
│ L4  工具链     headless CLI · 重放器 · 归因导出 · 调试面板后端 · CI 守卫   │
│                只链 simcore，不链引擎                                      │
├──────────────────────────────────────────────────────────────────────────┤
│ L3  表现层     渲染 · 动画 · 导航求解 · UI · 输入 · 音频                    │
│                （引擎：待 ADR-001 拍板；推荐 Godot 4）                     │
│                只读 simcore 的 ViewSnapshot + 提交 InputCommand             │
├──────────────────────────────────────────────────────────────────────────┤
│ L2  系统层     S0·B 时钟 │ S0·C 确定性 │ S1 稀缺 │ S2 NPC │ S3 经济 │      │
│                S4 对话契约 │ S5 社区信条 │ S6 传闻 │ S7 工作板              │
├──────────────────────────────────────────────────────────────────────────┤
│ L1  仿真核     Fixed(Milli) │ RngStream │ EventBus+AttributionLog │        │
│  (simcore)     WriteQueue │ DaySnapshot │ BoundaryScheduler │ SaveGame      │
├──────────────────────────────────────────────────────────────────────────┤
│ L0  平台       C++20 · 单线程 · 固定编译选项 · 无引擎头 · 无异常跨界        │
└──────────────────────────────────────────────────────────────────────────┘
```

### 2.2 依赖方向（唯一的、不可协商的规则）

```
L4 ──┐
L3 ──┼──▶ L2 ──▶ L1 ──▶ L0          （箭头 = 允许依赖）
     └──▶ L1 ──────────┘

禁止：
  ✗ L1 依赖 L2/L3/L4        （仿真核不得知道任何系统或引擎）
  ✗ L2 依赖 L3              （系统层不得知道渲染）
  ✗ L3 写 L1/L2 的状态       （表现层只读 + 提交输入）
  ✗ 任何层依赖引擎头（除 L3）
```

**CI 静态守卫**（失败即构建失败）：

| 检查 | 命令（`rg` 等价） | 期望 |
|---|---|---|
| simcore 无引擎头 | `rg '#include\s*<(godot\|unity\|unreal)' simcore/` | 空 |
| simcore 无浮点 | `rg '\bdouble\b\|\bfloat\b\|std::sin\|std::cos\|std::exp\|std::pow\|std::log\|std::sqrt' simcore/` | 空 |
| 表现层无判定 | `rg 'rollCheck\|makeSeedCtx\|pcg32_at' src/render src/ui` | 空 |
| 业务系统无裸随机 | `rg '\brand\b\|srand\|Math\.random\|chrono::high_resolution\|time(' simcore/ src/` | 空 |
| `d20` 字面量唯一 | `rg -c '\bd20\b' simcore/ --glob '!*test*'` | 汇总 == 1 |
| `lodStateOf` 已废弃 | `rg 'lodStateOf' .` | 空（C-11 已裁决） |

### 2.3 技术选型摘要

| 项 | 选择 | 状态 |
|---|---|---|
| 仿真核语言 | **C++20**（静态库 + C ABI） | 推荐 |
| 表现层引擎 | Godot 4 / Unity 6 / Unreal 5 | **待主理人拍板**（ADR-001） |
| 数值表示 | milli 定点（1/1000） | 待确认（ADR-005） |
| 随机 | 无状态 `pcg32_at` | 待确认（ADR-002） |
| 线程模型 | **仿真单线程**；渲染/资源加载可多线程 | 已定 |
| 构建 | CMake（simcore）+ 引擎工程 + GitHub Actions | 待定 |
| 序列化 | 自研二进制（定长 + 变长混合），版本化 | 待定（S0 §C5 明示为 stub） |

---

## 3. 运行时结构 · 固定步长累加器

### 3.1 修正后的累加器（⚠️ 修掉 S0 §B3.1 的 60× 量纲错误）

**问题**：原文 `acc += dtReal × TIME_SCALE`（`TIME_SCALE=60`）+ `while (acc ≥ 1)` 在 60fps 下产生 **60 tick/现实秒** → 1 游戏日 = 24 现实秒，与 §B2「24 现实分钟」差 60 倍。

**修法 A（推荐，不动常量表数值）** —— 把累加器单位显式化为**游戏秒**：

```cpp
constexpr int    SIM_TICK_GAME_SECONDS = 60;   // 1 游戏分钟 = 60 游戏秒
constexpr int    MAX_STEPS_PER_FRAME   = 4;    // S0 原值不变
constexpr double TIME_SCALE_DEFAULT    = 60;   // 游戏秒 / 现实秒（调试 30 / 60 / 120）

void advance(double dtRealSec) {
    acc += dtRealSec * timeScale;              // 单位：游戏秒
    int steps = 0;
    while (acc >= SIM_TICK_GAME_SECONDS && steps < MAX_STEPS_PER_FRAME) {
        simTick(++absTick);                    // 1 次 = 1 游戏分钟
        acc -= SIM_TICK_GAME_SECONDS;
        ++steps;
    }
    // 超出上限：保留 acc（不丢时间），世界变慢，绝不跳步   ← S0 原约束
}
```

**核算**：1 现实秒 → 60 游戏秒 → 1 `SIM_TICK`；1440 tick × 1 s = 1440 s = **24 现实分钟/游戏日** ✅；`TIME_SCALE` 30/120 → 0.5× / 2× 调试速度 ✅。
（备选「修 B」：保留 `while (acc ≥ 1)` 而把 `TIME_SCALE` 改为 1 —— 会动常量表与 §B2 文字，**不推荐**。）

### 3.2 一帧里发生什么

```cpp
// ── 表现层，60fps ──────────────────────────────────────────────
void frame(float dtRealSec) {
    // 1) dtReal 的唯一合法用途：喂累加器
    simcore_advance(dtRealSec);        // 可能触发 0..4 个 SIM_TICK + 批处理分片

    // 2) 只读快照，插值渲染
    const ViewSnapshot* v = simcore_view_snapshot();   // 位置(mm) / 动画状态 / 色温 / 面板数据
    renderInterpolate(v, dtRealSec);                   // renderLOD 由视锥决定（见 §5.2）

    // 3) 输入只入队，下一个 SIM_TICK 消费
    simcore_submit_input(gatherInput());

    // 4) 面板（L4）读 ViewSnapshot + DebugSnapshot，不回写
    devPanelDraw(simcore_debug_snapshot());
}

// ── 仿真核，1Hz ────────────────────────────────────────────────
void simTick(uint64_t absTick) {
    consumePlayerInput(absTick);                       // 输入按 tick 打点（S0 §C3）
    for (uint32_t i = 0; i < actorsOrder.size(); ++i) { // 固定顺序，禁哈希容器迭代
        Actor& a = actorsOrder[i];
        switch (tickLevel(a.id)) {
            case FULL: case PROTECTED: behaviorTreeTick(a, absTick); break;  // 禁随机判定
            case COARSE:               closedFormAdvance(a, absTick); break;
        }
        processBoundaries(a, absTick);                 // 两条路径共用（ADR-003 D3）
    }
    if (minuteOfDay(absTick) % 60 == 0) enqueueBatch(HOURLY,  absTick);
    if (minuteOfDay(absTick) == 1439)   enqueueBatch(CUTOVER, absTick);
    if (minuteOfDay(absTick) == 360)    enqueueBatch(DISPATCH,absTick);
    if (absTick % 60 == 0)              worldHashCheckpoint(absTick);        // S0 §C3
    runBatchSlice();                                   // 批处理分片（见 §4.3）
}
```

### 3.3 时间地址

```text
dayKey      : int32，从 1 起
minuteOfDay : int32，0..1439
absTick     : int64 = dayKey × 1440 + minuteOfDay        // 全局单调，唯一时间地址
```

---

## 4. Tick 分层

### 4.1 四层职责

| Tick | 频率 | 触发 | 职责 | 主要归属 |
|---|---|---|---|---|
| `SIM_TICK` | **1 Hz** | 累加器 | 行为树（FULL/PROTECTED）、闭形式推进（COARSE）、**boundary 补做判定**、玩家输入消费、连续量累积 | S0·B / S2 / S1 |
| `HOURLY_TICK` | 1/60 Hz | `minuteOfDay % 60 == 0` | L1 需求推进、岗位产出/劳动结算、生理衰减 −8/−4/−5、伤病与辐照、容器 mutation、库存/消耗、S4/S5/S7 每小时钩子 | S1 / S2 / S3 |
| `DISPATCH` | 1/日 | `minuteOfDay == 360`（06:00） | **只发令**：分配岗位、生成当日 24h 路线与 **boundary 数组**、发 `ScheduleChanged` | S2 |
| `DAILY_CUTOVER` | 1/日 | `minuteOfDay == 1439` 之后 | 日切九步（1 / 2 / 3.1–3.3 / 4.1–4.4 / 5 / 6 / 7.1–7.4 / 8 / 9），发布不可变 `DaySnapshot[D]` | S0·B 编排 |

> `DISPATCH` 只发令、执行在随后的 `HOURLY_TICK`（S0 §B4）。

### 4.2 日切九步（顺序不可调换，S0 §B3.5 已拍板）

```
 1   冻结写队列 → 按 (priority, actorId, seq) 排序 → 应用          S0·B
 2   生理衰减 + 伤病推进（结算到 24:00）                            S1
3.1  库存 / 岗位产出 / 消耗结算                                     S1 / S2
3.2  价格更新（读 DaySnapshot[D-1] 的 effStock/demand → 写 P(D+1)）  S3 §3.1
3.3  配给发放（经 tryTransfer 逐人发 → RationIssued + RationGrant）  S3 §3.4
4.1  声望衰减 R_c × 0.97                                            S5 §3.1
4.2  应用 Σ delta(pendingDayKey = D) → clamp(−100,+100) → 分档      S5 §3.1
4.3  写 MemoryFact（≤5，不衰减）；超限按淘汰键剔除                   S5 §3.1
4.4  NPC 短期记忆窗口滑动（weightQ −1）+ LossStreak 结算             S2 §4.1
 5   社区安全天数重算（木桶 min）→ ScarcityStateChanged              S3 §3.5
 6   EstateSettlement Phase 2                                        S0·E
7.1  承诺到期判定 → BREACHED + reasonCode                            S4 §3.6
7.2  release() 释放 reservedByPromise                                S3 §4.3
7.3  发 PromiseBreached{entryId, dayKey, reasonCode}                 S4 §3.6
7.4  违约 delta 补批应用 → 进 DaySnapshot[D]                         S5 §4.1
 8   全局冲突指数检查 → 张力预算注入器                               S0·B / R5
 9   dayKey ← D+1，清空写队列，发布不可变 DaySnapshot[D]             S0·B
```

**下游只读昨日快照纪律**：`dayKey = D+1` 期间，任何系统读跨系统量（价格 / 安全天数 / 岗位表）一律读 `DaySnapshot[D]`，**不得直读实时值**。UI 上 pending 条目显示「明日生效」（P2）。

### 4.3 批处理分帧规则

- `DISPATCH`（≈2.17ms）与 `DAILY_CUTOVER`（≈4.5ms）**排在 `SIM_TICK` 帧之后的独立帧段**，单帧仿真预算 **≤1.5ms**；期间 **时钟冻结**（`absTick` 不推进）→ 分片不改变 tick 序列与确定性。
- **分片只切帧，不切序**：九步顺序严格遵守 §4.2。
- 需要持久化 `batchPhase / batchCursor` 以支持"日切中途存档"。

---

## 5. LOD 分级

### 5.1 `simLOD` 三档（契约，进存档）

| 档 | 上限 | 行为 | 判定 |
|---|---|---|---|
| `FULL` | **≤32**（含 PROTECTED） | 行为树连续模拟，逐分钟细化 | 距离 < 120m（升级）且名额未满 |
| `PROTECTED` | **≤3** | 永驻 FULL；`protectedActors` 唯一定义处为 S0 §D，**禁止实现成两份**（C-27） | 名单静态 |
| `COARSE` | 其余（Core 期 44） | 不跑行为树、不做碰撞与遭遇；冻结 `ScheduleDay` + 闭形式推进 + 边界结算 | 距离 > 150m 或名额挤出 |

- **滞回**：升级 < 120m，降级 > 150m（防抖，P2）。
- **名额抢占**（>32 时）：按「有 pending 事件 → 有名字 → 距离倒数」；被挤下者立即降级并写 `LodSwitch`。三个排序键**全为确定性量**。
- **抖动锁**：1 游戏分钟内切换 ≥3 次 → 强制锁定 `COARSE` 60 游戏分钟 + 告警（S0 §B7-3）。

### 5.2 ⚠️ `simLOD` 与 `renderLOD` 必须分离（BLOCKER，F-7）

S0 §B3.2 原文「升级 = 距离 < 120m **或在视锥内**」会把**相机朝向**变成仿真的隐性输入。

| 层 | 决定因素 | 影响 | 视锥 |
|---|---|---|---|
| **`simLOD`**（`tickLevel(actorId)` 的返回值） | 距离 + PROTECTED + 名额 | 是否跑行为树 | **❌ 禁止** |
| `renderLOD`（表现层私有，不进契约/存档） | 视锥 + 距离 + 遮挡 | 模型/动画/面部/贴花 | ✅ 允许 |

- `tickLevel(actorId)` 的**命名与三档取值完全不变**（C-11 已裁决），只是从它的**输入**里拿掉视锥。
- 收益：玩家转头只让远处 NPC 变简模，**不会**让他的作息改变 —— 这正是 P2「看得见的作息」要的。

### 5.3 离屏原则（S0 §B3.3）

> 「**离屏不做细节，只做结果**」

- COARSE 不跑行为树、不做碰撞与遭遇，只按路点表 + 岗位产出 + 统计需求推进。
- 离屏**允许**不可逆结果（死亡/迁居/被盗），但**必须能被 `AttributionLink` 解释**，否则视为未实现。
- **预告闸门**：离屏判死须满足"死亡前 ≥1 游戏日已出现可观测趋势"（水=0 持续 ≥6h 或 `rad ≥ 80` 持续 ≥12h），否则降级为伤病/失踪。
- FULL 与 COARSE 路径互斥，切换点必写 `LodSwitch`。

---

## 6. 数据层契约类型

> **所有字段名、枚举值、ID 命名与设计文档逐字一致。改名 = 违约，需走裁决。**

### 6.1 基础 ID

```text
ActorId      : "player" | "npc.<community>.<role>_<nn>" | "anon.<nnn>"
               例 npc.he_valley.water_03 / anon.007
CommunityId  : "he_valley" | "jing_cell"
PostId       : "<community>.<role>_<nn>"   role ∈ {water, watch, patrol, mend, care}
ClusterId    : WATER | FUEL | AMMO | MEDS | SEED | FOOD          // 6 键（C-17/C-26）
               FOOD：unit="portion"，tradable=false
               FOOD 进 Warehouse.stock / 配给 / SAFE_DAYS 分子 / reservedByPromise / ItemSpec.cluster
               FOOD 不进 PriceTable / quote / execTrade（S3 实现期静态断言）
DayKey       : int32，从 1 起
MinuteOfDay  : int32，0..1439
RefId        : "<system>.<entityType>.<id>[.<field>]"
               例 S5.post.he_valley.water_03.output
Quality      : 0.4 .. 1.5
```

**`Quantity.cluster → unit` 固定映射（不得自定义）**：
`WATER→"L"` · `FUEL→"L"` · `AMMO→"rd"` · `MEDS→"dose"` · `SEED→"portion"` · `FOOD→"portion"`

### 6.2 核心结构

```cpp
// ── 数量（S0 §D unit 映射固定） ───────────────────────────────
struct Quantity { ClusterId cluster; Milli amount; };      // amount = 真值 × 1000

// ── 账本条目（S4 权威字段集，裁决 13 · C-23） ──────────────────
struct LedgerEntry {
    EntryId   entryId;
    ActorId   promisorId, promiseeId;
    Kind      kind;          // "DELIVER" | "SERVICE" | "ABSTAIN"
    Content   content;       // ItemSpec | ServiceSpec
    Milli     qty;
    DayKey    deadlineDay;
    ActorId   witnessIds[];
    State     state;         // "OPEN" | "MET" | "BREACHED" | "VOID"   四态，禁止扩充
    DayKey    createdDay;
    bool      reserved;
};

// ── 缺口信号（S5 产出，S7 唯一输入） ──────────────────────────
struct GapSignal {
    SignalId   signalId;
    CommunityId communityId;
    ClusterId  cluster;
    Milli      deficitUnits;
    uint8      urgency;        // 1..5
    LocationId targetLocationId;
    PostId     sourcePostId;   // 可选
    ActorId    claimantActorId;// 可选
    DayKey     expiresDayKey;
    SourceKind sourceKind;     // "STOCK"|"CREED"|"POST"|"PERSON"|"PROMISE"  闭集 5 值
};

// ── 工作板条目（S7 输出，纯投影，7 字段契约） ──────────────────
struct WorkOrder {
    OrderId    orderId;        // §3.5 确定性生成
    CommunityId communityId;
    Kind       kind;           // 闭集 4 值
    ClusterId  needCluster;    // ≡ GapSignal.cluster
    Milli      qty;            // ≡ deficitUnits（合并后 Σ）
    DayKey     deadlineDay;    // ≡ expiresDayKey
    BenefitSpec benefitSpec;
    // 派生量（不进契约）：sourceSignalIds[] / targetLocationId / haulFit /
    //                     state / headlineCode / linkedEntryIds[] / resolvedBy
};

// ── 写队列条目（ADR-004 D5 新增 causeRef / sourceSystem） ──────
struct WriteQueueItem {
    uint64   seq;  uint8 priority;  ActorId actorId;
    RefId    targetRef;  Payload payload;
    RefId    causeRef;       // 【新增】为空则 enqueue 断言失败
    uint8    sourceSystem;   // 【新增】
    DayKey   dayKey;
};   // 状态：QUEUED → APPLIED(整点/日切) → SNAPSHOTTED(日切后不可变)

// ── 归因链（S0 §C2，append-only） ────────────────────────────
struct AttributionLink {
    uint8   step;        // 1..3，跨系统边界次数（ADR-004 D2）
    RefId   causeRef;    // "UNATTRIBUTED" 表示断链
    RefId   effectRef;
    ActorId actorIds[];  // 匿名 actor 仅 step ≤ 2 时记入
    DayKey  dayKey;      // 效果发生日
    uint16  latencyDays; // 跨日另计，不重置 step
};

// ── 边界（ADR-003 D3） ───────────────────────────────────────
struct Boundary {
    uint64_t    absTick;      // 锚定时刻（闭形式：startMin + travelMin）
    uint32_t    actorIndex;   // 固定顺序下标
    BoundaryKind kind;        // ARRIVE|PICKUP|FAIL|BLOCK_END|NEED_UNMET|DEATH_GATE
    RefId       ref;
    StreamId    streamId;
    uint32_t    seqInActor;
};

// ── 日快照（不可变） ─────────────────────────────────────────
struct DaySnapshot {          // 发布后只读
    DayKey dayKey;
    PriceTable    price[CommunityId];        // 6 cluster（FOOD 不进价格）
    Milli         safeDays[CommunityId];
    StaffingRatio staffingRatio[CommunityId];
    PostRoster    roster[CommunityId];
    Warehouse     warehouse[CommunityId];    // 6 cluster
    GapSignal     gaps[CommunityId][≤3];
    LedgerEntry   ledger[];
};   // 保留窗口 D-1 .. D+7
```

### 6.3 派生量（公式契约，实现不得改写）

```text
safeDays        = min(waterL / (3.0 × pop), foodUnits / (1.0 × pop))      // 木桶不加总
effHead(post)   = Σ_worker completeFactor(worker)     // 完成=1，迟到>30min=0.5，缺席=0
staffingRatio   = Σ effHead(post) / Σ rated(post)     // idle 不计两侧
实际产出         = 额定产出 × staffingRatio
SCARCE 进入      = staffingRatio < 0.60；退出需 ≥0.60 连续 2 日（SCARCE_RATIO_EXIT_DAYS）
SPEED_MUL       = clamp(1.15 − 0.5 × (cur/cap)², 0.45, 1.15)
BARTER_OFFSET   = (PRESENCE − 5) × 4%
CARRY_CAP       = 25 + 5 × VIGOR (kg)
STAMINA_POOL    = 60 + 10 × VIGOR
OBSERVE_RADIUS  = 4 + 0.6 × MIND (m)
```
> **注意**：`SCARCE`（编制 < 0.60）与 `TIGHT`（`safeDays ≤ 5`）是两个量，**不得合并**。

### 6.4 数值表示（ADR-005）

| 量 | 刻度 | 类型 |
|---|---|---|
| `Quantity.amount` / 价格 / `safeDays` / 比率 / `Quality` | ×1000 | `Milli`（`int32`，真值 ±2147） |
| 声望 `Reputation`（−100..+100） | ×100 | `int32`（专用） |
| 生理点 / 时间 / 序列 | 整数 | `int16` / `int32` / `int64` |
| 世界坐标 | ×1000（mm） | `int32` per axis；`worldHash` 量化到 0.1m |
| 角度 | 1/65536 圈 | `uint16` + 65536 项查表 |

**四则运算唯一实现**：`mulM`（`(a*b + 500)/1000`，四舍五入）· `divM`（`(a*1000 + b/2)/b`）· `addSat`（饱和 + debug 断言）。**禁 `float`/`double`/超越函数**。

---

## 7. 离屏结算与存档

### 7.1 边界补做判定（核心，ADR-003 D3）

```cpp
void processBoundaries(Actor& a, uint64_t absTick) {
    while (a.nextBoundaryIdx < a.boundaries.size()
           && a.boundaries[a.nextBoundaryIdx].absTick <= absTick) {
        const Boundary& e = a.boundaries[a.nextBoundaryIdx];
        SeedCtx ctx = makeSeedCtx(e.streamId, e.absTick);   // ★ 锚定 boundary 的 absTick
        applyBoundary(a, e, ctx);                            // 只允许经 rollCheck 取值
        ++a.nextBoundaryIdx;
    }
}
```

**三条不变式（CI 断言）**

| # | 不变式 | 静态/动态检查 |
|---|---|---|
| **I1** | `makeSeedCtx` 第二参永为 `e.absTick`，绝不传当前 tick | 静态：`rg 'makeSeedCtx\([^,]+,\s*(now\|absTick\|tick)\)' simcore/` == 空 |
| **I2** | 全局处理序 = `(absTick, actorIndex, seqInActor)`；逐 tick 路径与批量快进路径用同一排序键 | 单测：两条路径结果逐位相同 |
| **I3** | `behaviorTreeTick` / `closedFormAdvance` 内不得调用 `rollCheck` | 静态：这两个 TU 内 `rollCheck` 命中数 == 0 |

**最强检验**：`fastForward(8 游戏小时)` 的结果必须与连续 `simTick × 480` **逐位相同**。

### 7.2 快进（SLEEP）

S0 §B3.4：8 游戏小时 ≤800ms，`chunk = 60 游戏分钟`，跳过渲染与 AI 细节，COARSE 用统计积分，**必须走同一 tick 序列**。

**朴素实现会爆**：480 × ~1.21ms ≈ 582ms 工作量，摊到 48 帧 = 12.1 ms/帧，超 6ms。

**正确做法 —— 边界事件批量模式**（因为 COARSE 是 `minuteOfDay` 的闭形式函数，本就不必逐 tick 遍历）：

```cpp
void fastForward(uint64_t t0, uint64_t t1) {
    auto evs = collectBoundaries(actorsOrder, t0, t1);                 // 纯函数，不逐 tick
    sortBy(evs, [](a,b){ return tie(a.absTick, a.actorIndex, a.seqInActor)
                              <  tie(b.absTick, b.actorIndex, b.seqInActor); });  // ← I2 同键
    for (const Boundary& e : evs)
        applyBoundary(actorsOrder[e.actorIndex], e, makeSeedCtx(e.streamId, e.absTick));
    integrateClosedForm(t0, t1);                                        // 一次算完
    for (uint64_t h : hourlyTicksIn(t0, t1)) hourlyTick(h);             // 一个都不能跳
    if (crosses(t0, t1, 360))  dispatch(...);
    if (crosses(t0, t1, 1439)) dailyCutover(...);
}
```
**核算**：≈203 个 boundary × 0.020ms = 4.1ms + 8 × 1.493ms = 11.9ms + 积分 0.5ms + DISPATCH 2.17ms ≈ **18.7ms**，对 800ms 有 **42× 余量**，摊到 48 帧 = **0.39 ms/帧**。

**中断规则**（S0 §B3.4）：`THREAT_NEARBY` · `CRITICAL_NEED`（water=0 或 food=0）· `COMMUNITY_ALARM` → 强制退出，**已过时间不回滚**；快进中死亡 → 在中断点结算，不留到快进尾。

### 7.3 存档（`SaveGame v1`）

```text
必存（缺一即拒绝加载）：
  worldSeed : uint64
  gameVersion / simSchemaVersion / streamIdRegistryVersion    // 任一不匹配 → 拒绝重放 + 明确提示，不静默
  dayKey / minuteOfDay / absTick
  全部写队列条目（seq / priority / payload / causeRef）
  每 actor 的 simLOD 状态 + LodGuard 剩余锁定 tick
  每 actor 的 ScheduleDay（冻结）+ boundary 数组 + nextBoundaryIdx 游标
  DaySnapshot[D-1] 与 [D]
  AttributionLog 尾部（7 日 或 20k 条）
  worldHash 环形缓冲（最近 24 个）
  batchPhase / batchCursor（支持日切中途存档）
  eventId（用于对账，不影响确定性）

恢复后首 tick 必须是 savedTick + 1（不跳号、不补跑）
禁止依赖运行时缓存：导航路径 / 路线长度 / 路程常量表 必须可由 seed 或静态数据重建
```

**体积与耗时**：仿真状态 ≤4 MB → 压缩后 <1 MB → SATA SSD 写入 **< 25 ms**。

### 7.4 `worldHash`

```
worldHash = FNV1a(生理 + 库存 + 位置量化到 0.1m + 关系值)，每 60 tick
规范子集 ≈ 3.76 KB → FNV1a @ ~1–2 GB/s ≈ 2–4 µs/次 → 摊销 < 0.0001 ms/tick
两次运行须逐位相同；首次不一致即中断并 dump 该 tick 全量状态
```

---

## 8. 性能预算

### 8.1 基线

```text
目标机    : GTX 1060 6GB / 16GB RAM / SATA SSD / 1080p
场内实体  : ≤120 = 有名 NPC 16 + 匿名 NPC 60 + 玩家 1 (=77 actor) + 容器/世界信号 ≤43
simLOD    : FULL ≤32（16 有名 + 16 匿名，含 PROTECTED ≤3）+ 玩家 1；COARSE = 44
第三人称近视角；Core 规模 2×2 km / 2 社区
```

### 8.2 每 `SIM_TICK`（1 Hz）

| 项 | 单价 (ms) | 数量 | 小计 (ms) | 来源 |
|---|---:|---:|---:|---|
| 有名 NPC FULL（事件驱动 BT） | 0.085 | 16 | 1.360 | **已承诺**（S2 §8） |
| 匿名 NPC FULL（浅 BT ≤2 层） | 0.060 | 16 | 0.960 | **已承诺**（S2 §8） |
| NPC COARSE（闭形式 + 边界结算） | 0.012 | 44 | 0.528 | **已承诺**（S2 §8） |
| 写队列 + 归因记录（共享池） | — | — | 0.300 | **已承诺**（S2 §8） |
| **S2 小计（自留 ≤3.20ms p95）** | | | **3.148** | ✅ 余 0.052 |
| 玩家 actor（永驻 FULL，输入驱动） | 0.085 | 1 | 0.085 | 🆕 新分配 |
| S1 每游戏分钟钩子（负重/体力/辐照） | 0.002 | 77 | 0.154 | 🆕 新分配 |
| **SIM_TICK 合计 p95** | | | **3.387** | |

### 8.3 每 `HOURLY_TICK` 增量（1/60 Hz）

| 项 | 小计 (ms) | 来源 | 归属系统自留 |
|---|---:|---|---|
| S2 L1 需求推进（77 × 0.005） | 0.385 | 🆕 新分配 | S2（已计在 3.20 内则改列此处，需 S2 确认） |
| S2 岗位产出 / 劳动结算 | 0.300 | 🆕 新分配 | S2 |
| S1 生理衰减 + 伤病 + 辐照（77 × 0.004） | 0.308 | 🆕 新分配 | **S1 自留 ≤1.20** ✅ |
| S1 容器 mutation + 发现率 | 0.200 | 🆕 新分配 | S1 |
| S3 库存 / 消耗 / 预留 | 0.150 | 🆕 新分配 | S3（未自留） |
| S4 承诺时钟 + S5 信条 + S7 工作板钩子 | 0.150 | 🆕 新分配 | S4/S5/S7（未自留） |
| **HOURLY 增量合计** | **1.493** | | |

### 8.4 单帧 / 摊销 / 批处理

| 口径 | 计算 | 结果 | 判定 |
|---|---|---:|---|
| **最坏单帧**（SIM + HOURLY 同帧） | 3.387 + 1.493 | **4.880 ms** | ✅ ≤6.0，余 18.7% |
| 日切帧（`minuteOfDay==1439`，无 HOURLY） | 3.387 + 1.5 | **4.887 ms** | ✅ |
| 批处理帧（独立帧段） | ≤1.5 | **1.500 ms** | ✅ |
| **每现实秒仿真总耗时** | 3.387 + 1.493/60 + 6.67/1440 | **≈3.417 ms/s** | |
| **60fps 摊销** | 3.417 / 60 | **≈0.057 ms/帧** | ✅ 对 6ms 有 **105×** 余量 |
| 调试加速 ×8 | ×8 | 0.455 ms/帧 | ✅ |
| 调试加速 ×60 | ×60 | 3.417 ms/帧 | ✅ |
| **快进 8 游戏小时** | ≈18.7 ms 总量 | **0.39 ms/帧**，总墙钟 ≪800ms | ✅ 42× 余量 |
| `DAILY_CUTOVER` 总预算 | S6 单日批处理 ≤1.5（**已承诺**）+ 其余八步 3.0（🆕） | **4.5 ms/日**，分 3 帧 | ✅ |
| `DISPATCH` 总预算 | — | **2.17 ms/日**，分 2 帧 | ✅ |

> **口径声明**：Core 验收 5「仿真 ≤6ms/帧」在本架构下有**两个可举证口径** —— ① 单帧硬上限 `max_per_frame ≤ 6.0ms`（最坏 4.880ms）；② 摊销 ≈0.057ms/帧。**二者都满足。**
> **注 1**：`SIM_TICK = 1 Hz` 是全部数字的成立前提（见 §3.1 的 60× 修正）。
> **注 2**：`LOD_FULL_MAX` 取 **32**（非 S0 §D 的 40）；取 40 时 S2 = 1.36+1.44+0.408+0.30 = 3.51ms，**超出 S2 自留 3.20ms**。
> **注 3**：RNG 与 `worldHash` 的开销（<5 µs / <4 µs per tick）已含在"写队列 + 归因记录"0.300ms 内，不单列。
> **注 4**：所有"🆕 新分配"数字均为**架构分配值**，非实测值。责任人须在各自系统首個可玩版本交付时用 `--perf` 实测回填；任一系统超支 → 走预算复议，不得就地放宽。

### 8.5 内存

| 项 | 估算 |
|---|---:|
| 77 actor 常驻（日程 24 block + 记忆 7 日 + 库存 + 生理） | ≈1.1 MB |
| 写队列（日均 ~2k 条 × 64B） | ≈0.13 MB |
| `AttributionLog` 环形缓冲（20k × 96B） | ≈1.9 MB |
| `DaySnapshot[D-1..D+7]`（8 × ~60KB） | ≈0.5 MB |
| 容器 / zone / boundary 表 | ≈0.05 MB |
| **合计上界** | **≈3.7 MB**（记账 **≤4 MB**） |

---

## 9. 工具链（C8 进 Core，一等公民）

### 9.1 调度可视化面板（S0 §C8 + S2 §8 扩展）

| # | 面板 | 内容 | 需求方 |
|---|---|---|---|
| ① | 24h 作息甘特 | 横轴 `minuteOfDay` 0–1439，每有名 NPC 一行，7 色块（`SLEEP/COMMUTE/WORK/IDLE/SOCIAL/ALERT/DEAD`），可展开 waypoint 与 `intentTag` | S0 §C8-① / S2 |
| ② | 岗位表 | 社区 × 岗位 × 额定/在岗/实际产出% | S0 §C8-② |
| ③ | 需求条 | 水食睡 + L1 安全社交信条，0–100 带阈值线 | S0 §C8-③ |
| ④ | LOD 实时视图 | 谁在 FULL/COARSE，切换时刻标红 | S0 §C8-④ |
| ⑤ | 写队列检视器 | 当日 pending 按 priority 排序，可手动触发日切 | S0 §C8-⑤ |
| ⑥ | 归因链检视器 | 点 `effectRef` → 反查 ≤3 跳，A→B→C 节点图 | S0 §C8-⑥ |
| ⑦ | RNG 检视器 | streamId 列表 + 当前 counter + 最近 20 次取值 | S0 §C8-⑦ |
| ⑧ | 性能面板 | 仿真 p50/p95 滑动窗口、实体数、FULL/COARSE 计数 | S0 §C8-⑧ |
| ⑨ | 种子控制 | 固定种子输入、重放、单步 tick、加速 ×1/×8/×60 | S0 §C8-⑨ |
| ⑩ | Theft pressure 热力 | `(actorId × targetRef)` 压力值、D2 等级、预测 `changeKind` | S2 |
| ⑪ | WorldSignal 注册器 | `signalId / refId / installTick / expireTick`，点击跳世界坐标 | S2 |
| ⑫ | gossip 图谱 | 按 `topicSeedId` 的传播树，节点显示 `hop`、原码→失真 `claimCode`、`charge`，边显示选边 `score` | S2 / S6 |
| ⑬ | 劳动产出账 | 逐岗位 `effHead / rated / 实际产出`，昨日→今日变化标红 | S2 / S5 |

### 9.2 确定性重放与 headless

```bash
# CI 里跑（无渲染，秒级）
simcore-cli replay    --seed 20250910 --days 10 --world-hash-every 60
simcore-cli replay    --seed 20250910 --days 10 --save-at 3 --load-and-continue   # 验收 6
simcore-cli replay    --seed 20250910 --days 10 --lod all-coarse                  # ADR-003 V4
simcore-cli replay    --seed 20250910 --days 10 --lod all-full
simcore-cli assert-attribution --days 10                                          # ADR-004
simcore-cli assert-static-conservation                                            # S1
simcore-cli export-attribution --from 1 --to 10                                    # ADR-004 D9
```

`ReplayRecord{ worldSeed, gameVersion, simSchemaVersion, inputs[] }`，`inputs` 按 **tick** 打点（非帧）。版本不一致 → **拒绝重放并提示，不静默**。

### 9.3 归因链导出

```text
触发：面板按钮 / simcore-cli export-attribution / 环形缓冲（7 日 或 20k 条）超限自动落盘
产物：attribution.<worldSeed>.<D0>-<D1>.jsonl   （一行一 link，字段顺序固定）
      attribution.<...>.graphml                 （可选）
      attribution.<...>.md                      （人类可读摘要：按 step 分组 + 闭合集命中高亮）
文件头必含：worldSeed / gameVersion / simSchemaVersion / streamIdRegistryVersion /
           dayKeyRange / exportedAt        ← 缺任一即视为无效证据
```

### 9.4 调试钩子（跨系统汇总）

```text
--time-scale <n>              --skip-to <dayKey>:<minuteOfDay>（走完整 tick 序列，非直接赋值）
--tick-once                   --dump-lod                --dump-rng <streamId>
--dump-container <id>         --dump-load <actorId>     --dump-physio <actorId> <dayRange>
--dump-mutation-queue         --dump-unique             --assert-static-conservation
--dump-schedule <actorId> <dayKey>                      --dump-needs <actorId>
--dump-memory <actorId>       --dump-pressure [<cid>]   --dump-gossip <topicActorId>
--dispatch-now                --force-pressure <a> <ref> <n>   --npc-bt-trace <actorId>
--kill <actorId>（走完整死亡流程）                        --assert-visible-signal
--export-attribution <dayRange>                         --dump-check <actorId>
--set-skill <actorId> <id> <lvl>                        --force-d20 <n>（仅调试种子流）
--perf（p50/p95/max per-tick 与 per-frame）
```
> ⚠️ 所有 `--force-*` / `--teleport-*` / `--set-*` 钩子**禁用于正常流程**，release 构建中移除或加权限锁。

### 9.5 CI 守卫清单（失败即构建失败）

| 类别 | 守卫 |
|---|---|
| 依赖方向 | §2.2 的 6 条静态检查 |
| RNG | ADR-002 D5 的 5 条 |
| 确定性 | ADR-002 V1/V2 + ADR-003 V5（快进 ≡ 逐 tick） |
| 归因 | ADR-004 V1（`UNATTRIBUTED==0`）/ V2（`step ≤ 3`）/ V3（三跳闭合集） |
| 数值 | ADR-005 V1（常量精确）/ V3（无浮点）/ V4（无溢出） |
| 预算 | `--perf` 断言 `max_per_frame ≤ 6.0ms`、S2 `p95 ≤ 3.20ms`、S6 单日批处理 `p95 ≤ 1.5ms`、快进 ≤800ms |
| 验收 | Core 七条中可自动化的 1 / 4 / 5 / 6（见 `control-manifest.md`） |

---

## 10. 风险登记（前 5 大）

| # | 风险 | 概率 | 影响 | 触发信号 | 缓解 | 责任人 |
|---|---|---|---|---|---|---|
| **R1** | **FULL/COARSE 的连续量分叉**使 10 日 `safeDays` 差异 >5%（Core 验收 6） | 中 | 高 | ADR-003 V4 差异 >5% | ① 连续量在 boundary 量化到设计步长（R-B1）；② COARSE 积分分段到 boundary 再量化，禁整段平均；③ 整数余数累加器（R-B2）；④ CI 常跑 V4 并把差异来源在面板可见 | 主程 + S2 |
| **R2** | **60× 量纲错误未被签收** → 时间流速错 60 倍，全部预算与数值自洽论证失效 | 中 | **极高** | 首个可玩版本里"一天"明显过快/过慢 | ADR-000 F-6 / §3.1 修 A；**列为 G1 放行门**，未签收不开工 | 主理人 + 主程 |
| **R3** | **归因链在日切批处理中断链** → `UNATTRIBUTED > 0`，Core 验收 1 不成立 | 中高 | 高 | `assert-attribution` 报 `UNATTRIBUTED` | ① `WriteQueueItem` 强制带 `causeRef`（`enqueue` 断言）；② 派生效果继承上游 `effectRef`；③ 多因一果取 `step` 最小者为主链 | 主程 + S4/S5 |
| **R4** | **定点数精度或溢出**在 30 日长跑后暴露（价格递推累积、库存饱和） | 中 | 中高 | ADR-005 V4 溢出计数 >0；V2 偏差接近 0.01 | ① 全局 `mulM/divM` 唯一实现 + debug 断言；② 30 日常跑回归；③ 兜底：价格升 micro（×1e6） | 主程 + S3 |
| **R5** | **引擎表现层与 `simcore` 的同步协议**出缝（渲染插值读到半更新状态、输入延迟一帧导致手感差） | 中 | 中 | 手感测试反馈"操作发黏"；渲染抖动 | ① `ViewSnapshot` **双缓冲 + 只在 tick 边界发布**；② 渲染只读上一已发布快照；③ 输入入队 → 下个 `SIM_TICK` 消费，**表现层立即做本地预测但不回写** | 主程 + 表现层 |

> **次级风险（已记录，不进前 5）**：S8 未写导致 `ContainerDef` / 路程常量表缺位；GTX 1060 渲染冒烟未做；`AttributionLog` 内存 1.9MB 偏大；`LOD_FULL_MAX` 口径未统一。

---

## 11. 待裁决与知识缺口

### 11.1 放行门（必须先签收）

| 门 | 内容 | 出处 |
|---|---|---|
| **G1** 🔴 | S0 §B3.1 累加器 60× 量纲修正（修 A） | ADR-000 F-6 / 本文 §3.1 |
| **G2** 🔴 | `simLOD` 剔除视锥 | ADR-000 F-7 / 本文 §5.2 |
| G3 | `LOD_FULL_MAX` 40 → **32** | ADR-000 F-8 |
| G4 | S0 §C3 预算表替换为本文 §8 的 tick 分级口径 | ADR-000 F-9 |
| G5 | ADR-001 引擎选型（**可延后**，`simcore` 引擎无关化使表现层不阻塞仿真） | ADR-001 |
| G6 | ADR-005 milli 定点（**阻塞 `simcore` 第一行代码**） | ADR-005 |

### 11.2 需回填的设计修订（不改设计意图，只修自洽）

| # | 位置 | 现状 | 建议 |
|---|---|---|---|
| R-1 | S0 §B3.1 | 60× 量纲错误 | 修 A（游戏秒单位 + 门槛 60） |
| R-2 | S0 §B3.2 | 升级条件含"或视锥内" | 删除；补「视锥仅影响 `renderLOD`」 |
| R-3 | S0 §D | `LOD_FULL_MAX = 40` | → 32 |
| R-4 | S0 §C3 | 预算表双重记账 + 40/80 口径 | → 本文 §8 表 |
| R-5 | S0 §B3.4 | 快进未说明如何 ≤800ms | 补「boundary 批量模式，共享排序键」 |
| R-6 | S0 §C2 | `counter` 构造未定 | → `absTick × 4096 + callSeq`（ADR-002 D2） |
| R-7 | S0 §C3 | "每 streamId 的 counter" | → "注册表版本 + 调试用 counter 快照" |
| R-8 | S0 §B4 | `WriteQueueItem` 无 `causeRef` | 补 `causeRef / sourceSystem` |
| R-9 | S0 §C7-3 | 只禁浮点未给替代 | 补「milli 定点，见 ADR-005」 |
| R-10 | S1 附录 A | 4 常量待批 | **建议批准**；A-2 补"逐 tick 取值"、A-3 补"同一 `causeRef` 24h 去重" |

### 11.3 知识缺口（不臆造，需补资料）

1. **引擎参考缺失**：无 `docs/engine-reference/<engine>/VERSION.md`。Godot 4 / Unity 6 的当前版本、GDExtension ABI、Burst 浮点确定性边界均需实测或官方依据。
2. **GTX 1060 渲染冒烟未做**：2×2km 地形 + 77 角色 + 昼夜色温的实际帧预算未知。建议在 ADR-001 拍板后、Core 早期做一次 2 天冒烟。
3. **S8 区域内容 / S9 呈现与交互未写**：`ContainerDef` 完整字段、`WorldSignalRegistry` 字段级契约、`travelMin` 路程常量表来源未定。Core 期需占位实现并保持接口不变。
4. **S0 §B5 明示的 stub**：导航与路点插值、天气与季节（Extended）、序列化格式与存档槽位。Core 期需简化实现，且不得引入超越函数。
5. **S3 完整价格公式未参与精度核算**：ADR-005 的 10 日递推偏差（<1e-3）是在简化模型上算的，S3 实装后需重跑。

---

## 12. 附录 · 一眼速查

```text
时间   1 现实秒 = 1 游戏分钟 = 1 SIM_TICK(1Hz)   1 游戏日 = 1440 s = 24 现实分钟
tick   SIM_TICK 1Hz │ HOURLY_TICK 1/60Hz │ DISPATCH 06:00 │ DAILY_CUTOVER 00:00
LOD    simLOD{FULL≤32, PROTECTED≤3, COARSE=44} 滞回 120/150m │ renderLOD 含视锥
RNG    pcg32_at(FNV1a64(worldSeed‖streamId), absTick×4096 + callSeq)   无状态
数     Milli = 真值×1000；声望 ×100；位置 mm；角度 1/65536 圈
序     全局处理序 = (absTick, actorIndex, seqInActor)
预算   最坏单帧 4.880ms ≤6.0 │ 摊销 0.057ms/帧 │ 快进 8h ≈18.7ms ≤800ms │ 内存 ≤4MB
纪     仿真单线程 │ 禁浮点 │ 判定只在 boundary │ handler 只写写队列 │ 下游只读昨日快照
```

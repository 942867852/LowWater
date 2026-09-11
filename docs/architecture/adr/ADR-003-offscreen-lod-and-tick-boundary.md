# ADR-003 · 离屏 LOD 与 tick 边界补做判定（阻塞项 · 已判定 **PASS**）

- **状态**：**PASS（可实现）**，附 3 条生效条件与 2 处须回填的设计修订
- **日期**：Phase 3 · PHASE3-001
- **相关**：S0 §B2 / §B3.1 / §B3.2 / §B3.3 / §B3.4 / §B7、S2 §4.4 / §8、ADR-001、ADR-002
- **回答**：`06-consistency-review.md §七-A-2`（离屏结算预算）与 **§七-A-3（边界 tick 补做判定）**

---

## 1. 背景（Context）

S2 §4.4 的原文判决：

> 当 COARSE 实体在某个边界 tick"名义上抵达"一个已空容器时，**必须在该 `absTick` 补做一次判定，而不是在"被发现的那一帧"补做**。因为 `rollCheck` 的取值是 `(streamId, absTick)` 的函数，只要锚定 absTick，早算晚算结果一致 → LOD 差异不污染确定性。

评审问的是：**这是否可实现？否则整个确定性论证不成立。**

本 ADR 的结论：**可实现，判 PASS**。依据是三条可验证的事实：

- **F1**：`SIM_TICK` 的触发时刻 `absTick` 是**全局可预测的**（`absTick = dayKey×1440 + minuteOfDay`），边界时刻可在 `DISPATCH` 时**离线预计算**成一个有序数组。
- **F2**：`rollCheck` 的值在 ADR-002 之后是 `(streamId, absTick, callSeq)` 的**纯函数**，与"何时算、被谁算、算之前跳过多少次"无关。
- **F3**：FULL 与 COARSE 的**唯一差异**被收敛为"boundary 之间是否做逐分钟细化"，而细化路径**不得发起任何随机判定**（判定只在 boundary 上）→ 两条路径的随机序列逐位相同。

---

## 2. ⚠️ 先修两个会摧毁本论证的前置缺陷

### 2.1 缺陷 P1（BLOCKER）· S0 §B3.1 累加器存在 **60× 量纲错误**

```text
原文：acc += dtReal × TIME_SCALE          （TIME_SCALE = 60）
      while (acc ≥ 1 && steps < MAX_STEPS_PER_FRAME=4) { SIM_TICK(); acc -= 1 }
```
按 60fps 计算：`dtReal ≈ 1/60 s` → `acc += 1.0` / 帧 → **60 SIM_TICK / 现实秒** → 1 游戏日 = 1440 tick ÷ 60 = **24 现实秒**。
但 S0 §B2 明确写「1 现实秒 = 1 游戏分钟 → 1 游戏日 = 24 **现实分钟**」，§B8-① 的验收也是「1 游戏日 = 24 现实分钟 ±0.5%」。**相差 60 倍。**

**唯一自洽的读法**：`SIM_TICK = 1 Hz`（S0 §B2 自己也写了 "`SIM_TICK`（每 1 游戏分钟 = **1Hz**）"），即 **1 现实秒推进 1 游戏分钟**。文字对，公式错。

**推荐修法（修 A · 不动常量表数值）**：把累加器单位显式化为「游戏秒」，SIM_TICK 门槛改为 60 游戏秒：

```text
constexpr int SIM_TICK_GAME_SECONDS = 60;        // 1 游戏分钟 = 60 游戏秒
acc += dtReal * TIME_SCALE;                      // 单位：游戏秒；TIME_SCALE=60 → 60 游戏秒/现实秒
while (acc >= SIM_TICK_GAME_SECONDS && steps < MAX_STEPS_PER_FRAME) {
    SIM_TICK(); acc -= SIM_TICK_GAME_SECONDS; steps++;
}
// 超出上限：保留 acc（不丢时间），世界变慢，绝不跳步   ← S0 原约束保留
```
核算：`1 现实秒 → 60 游戏秒 → 1 SIM_TICK`；`1440 SIM_TICK × 1 s = 1440 s = 24 现实分钟` ✓；`TIME_SCALE=30/120` → 0.5×/2× 调试速度 ✓。
（备选「修 B」：保留 `while (acc ≥ 1)`，把 `TIME_SCALE` 数值改为 1；会动常量表与 §B2 文字，**不推荐**。）

> **对本 ADR 的意义**：`SIM_TICK = 1 Hz` 是整个离屏预算成立的前提（见 §5）。若按错误的 60 Hz 读，仿真开销会 ×60，预算直接爆表。

### 2.2 缺陷 P2（BLOCKER）· 视锥不得进入 `simLOD`

S0 §B3.2 原文：「升级 = 距离 < 120m **或在视锥内**」。

**问题**：视锥由**相机朝向**决定。若视锥参与 `simLOD`（即决定是否跑行为树），则玩家转一下头就改变仿真路径 →

- 重放时必须把相机 yaw/pitch 也记进 `inputs[]`，否则不可复现；
- 即便记了，LOD 抖动会让"同一 tick 的判定次数"剧烈变化，调试与归因链解释成本爆炸。

**决策 D2（本 ADR 提出，需主理人裁决）**：把 LOD 拆成两层，**契约名不变**：

| 层 | 决定因素 | 影响 | 是否可含视锥 |
|---|---|---|---|
| **`simLOD`**（`tickLevel(actorId)` 的返回值，`FULL/COARSE/PROTECTED`） | 距离（< 120m 升 / > 150m 降）+ `protectedActors`（≤3）+ FULL 名额抢占 | 是否跑行为树、是否逐分钟细化 | **❌ 禁止** |
| `renderLOD`（表现层私有，不进契约、不进存档） | 视锥 + 距离 + 遮挡 | 模型/动画/面部/贴花细节 | ✅ 可以 |

- `tickLevel(actorId)` 的**返回值与取值集合完全不变**（C-11 已裁决的命名与三档不动），只是把"视锥"从它的**输入**里拿掉。
- FULL 名额抢占的排序键（「有 pending 事件 + 有名字 + 距离倒数」）**全部是确定性量**，不受影响。
- 玩家转头只会让"远处的 NPC 从精细模型变成简模"，**不会**让他的日程与判定变化——这正是 P2「看得见的作息」要的：看得见的只是**呈现**，作息本身稳定。

---

## 3. 决策（Decision）

### D1 · Tick 分层（职责与频率，与 §2.1 修正配套）

| Tick | 频率 | 触发条件 | 承载内容 | 归属 |
|---|---|---|---|---|
| `SIM_TICK` | **1 Hz**（1 现实秒 1 次） | 累加器 | 行为树 tick（仅 FULL/PROTECTED）、闭形式推进（COARSE）、**boundary 补做判定**、玩家输入消费、连续量累积 | S0·B + S2 + S1 |
| `HOURLY_TICK` | 1/60 Hz | `minuteOfDay % 60 == 0` | L1 需求推进、岗位产出/劳动结算、生理衰减 −8/−4/−5、伤病与辐照、容器 mutation、库存/消耗、S4/S5/S7 每小时钩子 | S1 + S2 + S3 |
| `DISPATCH` | 1/日 | `minuteOfDay == 360`（06:00） | **只发令**：分配岗位、生成当日 24h 路线与 **boundary 数组**、发 `ScheduleChanged` | S2 |
| `DAILY_CUTOVER` | 1/日 | `minuteOfDay == 1439` 之后 | 日切九步（3.1–3.3 / 4.1–4.4 / 7.1–7.4 / 8 / 9），发布不可变 `DaySnapshot[D]` | S0·B 编排 |

### D2 · `simLOD` / `renderLOD` 分离（见 §2.2）

### D3 · 边界事件化 + 补做判定算法（核心）

**数据结构**（`DISPATCH` 时生成，`ScheduleChanged` 时增量重生成）：

```cpp
enum class BoundaryKind : uint8_t { ARRIVE, PICKUP, FAIL, BLOCK_END, NEED_UNMET, DEATH_GATE };
struct Boundary {
    uint64_t   absTick;     // 锚定时刻（闭形式：startMin + travelMin，travelMin 来自预计算路程常量表）
    uint32_t   actorIndex;  // 在固定 actorsOrder 中的下标（不是指针、不是哈希）
    BoundaryKind kind;
    RefId      ref;         // 容器 / 岗位 / 位置
    StreamId   streamId;    // 静态模板实例化而来
    uint32_t   seqInActor;  // 同一 actor 内的边界序号
};
// 每 actor 持有一个按 (absTick, seqInActor) 升序的 vector<Boundary> + 游标 nextBoundaryIdx
```

**补做判定（FULL 与 COARSE 走同一份代码，唯一差异在 advance 方式）**：

```cpp
void processBoundaries(Actor& a, uint64_t absTick) {
    while (a.nextBoundaryIdx < a.boundaries.size()
           && a.boundaries[a.nextBoundaryIdx].absTick <= absTick) {   // ← 处理所有"已过期未处理"的
        const Boundary& e = a.boundaries[a.nextBoundaryIdx];
        SeedCtx ctx = makeSeedCtx(e.streamId, e.absTick);             // ← 锚定 boundary 的 absTick
        applyBoundary(a, e, ctx);                                     // 内部只允许经 rollCheck 取值
        ++a.nextBoundaryIdx;
        if (e.kind == BoundaryKind::ARRIVE) a.blockComplete = true;   // 缺席补做 → completeFactor = 0.5
    }
}

void simTick(uint64_t absTick) {
    consumePlayerInput(absTick);                                      // 输入按 tick 打点
    for (uint32_t i = 0; i < actorsOrder.size(); ++i) {               // 固定顺序，禁哈希容器迭代
        Actor& a = actorsOrder[i];
        switch (tickLevel(a.id)) {
            case FULL:      behaviorTreeTick(a, absTick); break;      // 允许连续量细化，但【禁止发起随机判定】
            case PROTECTED: behaviorTreeTick(a, absTick); break;
            case COARSE:    closedFormAdvance(a, absTick); break;     // 位置/连续量闭形式，无 BT、无碰撞、无遭遇
        }
        processBoundaries(a, absTick);                                // 两条路径共用
    }
    if (absTick % 60 == 0)  hourlyTick(absTick);
    if (minuteOfDay(absTick) == 1439) dailyCutover(absTick);
    if (minuteOfDay(absTick) == 360)  dispatch(absTick);
}
```

**三条保证「早算晚算一致」的不变式（写成断言）**：

- **I1 · 锚定不变式**：`makeSeedCtx` 的第二个参数**永远是 `e.absTick`，绝不传当前 `absTick`**。CI 静态检查：`rg 'makeSeedCtx\([^,]+,\s*(now|absTick|tick)\)' simcore/` 必须为空（除了测试）。
- **I2 · 全局处理序不变式**：同一 `absTick` 上多个 actor 的 boundary，处理顺序 = `actorIndex` 升序；同一 actor 内 = `seqInActor` 升序。快进模式下的批量路径必须用 **`(absTick, actorIndex, seqInActor)`** 排序键，与逐 tick 路径**逐位一致**。
- **I3 · 判定只在 boundary 上不变式**：`behaviorTreeTick` / `closedFormAdvance` **不得**调用 `rollCheck`。CI 静态检查：这两个函数的翻译单元内 `rollCheck` 命中数 = 0。

**缺席补做失败的处理**（S2 §4.4）：`applyBoundary` 抛错或资源不可解 → `completeFactor = 0.5`，在**下一个 boundary** 结算，**不回滚已流逝的分钟**。

### D4 · 快进（SLEEP）走「边界事件批量模式」，不逐 tick

S0 §B3.4：8 游戏小时快进 ≤800ms，`chunk = 60 游戏分钟`，「跳过渲染与 AI 细节，COARSE 用统计积分」，且**必须走同一 tick 序列**。

**朴素实现会爆**：480 SIM_TICK × ~1.21ms ≈ **582ms** 仿真工作量；若摊到 800ms/60fps ≈ 48 帧 → 12.1 ms/帧，**超过 6ms/帧**。

**正确做法**（因为 COARSE 是 `minuteOfDay` 的闭形式函数，本来就不必逐 tick 遍历）：

```cpp
void fastForward(uint64_t t0, uint64_t t1) {              // 8 游戏小时 = 480 tick
    // 1) 一次性收集窗口内的全部 boundary（纯函数，不逐 tick）
    vector<Boundary> evs = collectBoundaries(actorsOrder, t0, t1);
    sortBy(evs, [](a,b){ return tie(a.absTick, a.actorIndex, a.seqInActor)
                              <  tie(b.absTick, b.actorIndex, b.seqInActor); });   // ← I2 同一排序键
    for (const Boundary& e : evs) {
        applyBoundary(actorsOrder[e.actorIndex], e, makeSeedCtx(e.streamId, e.absTick));
    }
    // 2) 连续量闭形式积分（一次算完，不逐 tick）
    integrateClosedForm(t0, t1);
    // 3) 窗口内的 HOURLY / DISPATCH / DAILY_CUTOVER 一个都不能跳，按正常顺序执行
    for (uint64_t h : hourlyTicksIn(t0, t1)) hourlyTick(h);
    if (crosses(t0, t1, DISPATCH_MINUTE)) dispatch(...);
    if (crosses(t0, t1, 1439))            dailyCutover(...);
}
```

**核算**：`76 actor × ~8 boundary/日 × 8/24 日 ≈ 203 个 boundary` × 0.020ms = **4.1ms**；`+ 8 × HOURLY(1.493ms) = 11.9ms`；`+ 闭形式积分 0.5ms`；`+ 窗口跨 06:00 时的 DISPATCH 2.17ms` → **≈ 18.7ms**，
对 800ms 上限有 **42× 余量**；摊到 48 帧 = **0.39 ms/帧**，不触发 6ms/帧 争议。
（保守兜底：若不做事件化，582ms 也仍 ≤800ms，但需在快进期间声明"模态豁免 6ms/帧"。**推荐必须做事件化**，顺带把常规 COARSE 成本也降下来。）

### D5 · LOD 滞回与抖动锁（S0 §B3.2 / §B7-3）

```cpp
struct LodGuard { uint64_t lockedUntilTick = 0; uint8_t switchesInMinute = 0; uint64_t minuteMark = 0; };
// 升级：dist < 120m ；降级：dist > 150m     （滞回，均不含视锥 —— D2）
// 1 游戏分钟内切换 ≥3 次 → 强制锁定 COARSE 60 游戏分钟 + 告警（记入 debug 日志与面板）
// FULL 名额 > LOD_FULL_MAX 时按【有 pending 事件 → 有名字 → 距离倒数】抢占，被挤下者立即降级并写 LodSwitch
// LodGuard 状态必须入存档（含剩余锁定 tick），否则 save/load 后抖动窗口会错位
```

### D6 · 批处理（`DISPATCH` / `DAILY_CUTOVER`）分帧执行，且不与 `SIM_TICK` 同帧

- 批处理排在 `SIM_TICK` 帧之后的**独立帧段**，单帧仿真预算 **≤1.5ms**；期间**时钟不推进**（`absTick` 冻结），故不影响 tick 序列与确定性。
- `DAILY_CUTOVER` 总预算 ≈ 4.5ms（S6 单日批处理 ≤1.5ms 已承诺 + 其余八步 3.0ms 新分配）→ 分 3 帧。
- `DISPATCH` 总预算 ≈ 2.17ms → 分 2 帧。
- 顺序严格遵循 S0 §B3.5 的九步（含 3.1–3.3 / 4.1–4.4 / 7.1–7.4），**分片只切帧不切序**。

### D7 · 连续量的 LOD 差异必须收敛到量化栅格

FULL 逐分钟累积 vs COARSE 按小时积分，会让 `pressure` / 需求值在阈值附近分叉。规则：

- **R-B1**：所有会跨阈值触发事件的连续量（theft `pressure`、三项需求、辐照累计），在**每次 boundary 判定时量化到设计步长**（例：`pressure` 量化到 0.5），COARSE 的积分必须**分段到每个 boundary** 再量化，禁止"整段平均后一次量化"。
- **R-B2**：衰减/累积用**整数余数累加器**，不用浮点：
  `accMilli += rateMilliPerHour; step = accMilli / 60; accMilli %= 60;`（ADR-005 milli 定点）
- **R-B3**：CI 常跑 V4（全 COARSE 跑 10 日 vs 全 FULL 跑 10 日），差异必须 ≤5%，且面板「差异来源」可见（S2 A4）。

---

## 4. 帧循环（表现层 ↔ 仿真层，ADR-001 R1/R2 的落地）

```cpp
// 表现层每帧（60fps）
void frame(float dtRealSec) {
    // 1) 唯一合法用途：喂累加器
    simcore_advance(dtRealSec);        // 内部：修正后的累加器 → 可能触发 0..4 个 SIM_TICK + 批处理分片
    // 2) 只读快照做插值渲染；禁止写仿真状态
    const ViewSnapshot* v = simcore_view_snapshot();
    renderInterpolate(v, dtRealSec);   // 位置/动画/色温；renderLOD 由视锥决定（D2）
    // 3) 输入只入队，不立即生效
    simcore_submit_input(gatherInput());   // 下一个 SIM_TICK 消费
}
// 硬禁：表现层调用 rollCheck / makeSeedCtx / 直接写 actor 字段 —— CI 静态检查失败即构建失败
```

---

## 5. 离屏结算预算（回答 §七-A-2）

**规模基线**：场内模拟实体 ≤120 = 有名 NPC 16 + 匿名 NPC 60 + 玩家 1 = **77 actor**，其余 ≤43 为容器/世界信号（不跑 AI）。
**simLOD 基线**：`FULL ≤ 32`（16 有名 + 16 匿名，含 `PROTECTED ≤ 3`）+ 玩家 1；`COARSE = 76 − 32 = 44`。

### 表 A · 每个 `SIM_TICK`（1 Hz）

| 项 | 单价 (ms) | 数量 | 小计 (ms) | 来源 |
|---|---:|---:|---:|---|
| 有名 NPC FULL（事件驱动 BT） | 0.085 | 16 | 1.360 | **已承诺**（S2 §8） |
| 匿名 NPC FULL（浅 BT ≤2 层） | 0.060 | 16 | 0.960 | **已承诺**（S2 §8） |
| NPC COARSE（闭形式 + 边界结算） | 0.012 | 44 | 0.528 | **已承诺**（S2 §8） |
| 写队列 + 归因记录（共享池） | — | — | 0.300 | **已承诺**（S2 §8） |
| **S2 小计（自留 ≤3.20ms）** | | | **3.148** | ✅ 余 0.052 |
| 玩家 actor（永驻 FULL，输入驱动） | 0.085 | 1 | 0.085 | 新分配 |
| S1 每游戏分钟钩子（负重/体力/辐照） | 0.002 | 77 | 0.154 | 新分配 |
| **SIM_TICK 合计 p95** | | | **3.387** | |

### 表 B · 每个 `HOURLY_TICK` 的**增量**（1/60 Hz）

| 项 | 小计 (ms) | 来源 |
|---|---:|---|
| S2 L1 需求推进（77 actor × 0.005） | 0.385 | 新分配 |
| S2 岗位产出 / 劳动结算（2 社区 × ~30 岗） | 0.300 | 新分配 |
| S1 生理衰减 −8/−4/−5 + 伤病 + 辐照（77 × 0.004） | 0.308 | 新分配（S1 自留 ≤1.2ms/tick ✅） |
| S1 容器 mutation 队列 + 发现率（≤43 容器） | 0.200 | 新分配 |
| S3 库存 / 消耗 / 预留（2 仓 × 6 cluster） | 0.150 | 新分配 |
| S4 承诺时钟 + S5 信条 + S7 工作板每小时钩子 | 0.150 | 新分配 |
| **HOURLY 增量合计** | **1.493** | |

### 表 C · 单帧与摊销

| 口径 | 计算 | 结果 |
|---|---|---:|
| **最坏单帧**（`SIM_TICK` 与 `HOURLY_TICK` 同帧，`minuteOfDay%60==0`） | 3.387 + 1.493 | **4.880 ms ≤ 6.0** ✅ 余 18.7% |
| 日切帧（`minuteOfDay==1439`，无 HOURLY） | 3.387 + 1.5（分片） | **4.887 ms** ✅ |
| 批处理帧（独立帧段，无 SIM_TICK） | ≤1.5 | **1.500 ms** ✅ |
| **每现实秒仿真总耗时** | 3.387 + 1.493/60 + (4.5+2.17)/1440 | **≈ 3.417 ms/s** |
| **60fps 摊销** | 3.417 / 60 | **≈ 0.057 ms/帧**（对 6ms 有 **105×** 余量） |
| 调试加速 ×8 | ×8 | **0.455 ms/帧** ✅ |
| 调试加速 ×60 | ×60 | **3.417 ms/帧** ✅（仍 ≤6ms） |
| **快进 8 游戏小时**（D4 事件化） | ≈18.7 ms 总量 / 48 帧 | **0.39 ms/帧**，总墙钟 ≪ **800ms** ✅（42× 余量） |

> **口径声明**：Core 验收 5 的「仿真 ≤6ms/帧」在本架构下有**两个可举证的口径**——
> **① 单帧硬上限 `max_per_frame ≤ 6.0ms`**（最坏 4.880ms，余 18.7%）；
> **② 摊销值 ≈0.057ms/帧**。
> 二者都满足，故 **§七-A-2 判 PASS**。**但 S0 §C3 的旧预算表必须修订**（见 §7）。

### 内存（仿真侧）

| 项 | 估算 |
|---|---:|
| 77 actor 常驻（日程 24 block + 记忆 7 日 + 库存 + 生理） | ≈1.1 MB |
| 写队列（日均 ~2k 条 × 64B） | ≈0.13 MB |
| `AttributionLog` 环形缓冲（20k × 96B） | ≈1.9 MB |
| `DaySnapshot[D-1..D+7]`（8 × ~60KB） | ≈0.5 MB |
| 容器 / zone / boundary 表 | ≈0.05 MB |
| **合计上界** | **≈3.7 MB**（取 **≤4 MB** 记账） |
| 存档落盘（压缩后） | **< 1 MB** → SATA SSD ≈ **< 25 ms** ✅ |

---

## 6. 判定（Verdict）

| 评审项 | 判定 | 依据 |
|---|---|---|
| **§七-A-3 边界 tick 补做判定** | **✅ PASS** | F1/F2/F3 + D3 的三条不变式 I1/I2/I3 可在代码里断言 + CI 静态检查。**可实现，不是 FAIL。** |
| **§七-A-2 离屏结算预算 ≤6ms** | **✅ PASS（附条件）** | 表 C：最坏单帧 4.880ms、摊销 0.057ms、快进 ≪800ms。**条件**：S0 §C3 旧预算表须按 §7 修订；`LOD_FULL_MAX` 须从 40 收敛到 32。 |

**3 条生效条件**：
1. **C1**：S0 §B3.1 累加器按 §2.1「修 A」修正（否则 60× 量纲错误让所有预算失效）。
2. **C2**：`simLOD` 剔除视锥（§2.2 / D2）。
3. **C3**：`LOD_FULL_MAX` 由 40 收敛到 **32**（Core 验收 5 与任务硬约束均为 ≤32；S0 §D 的 40 与之冲突，见 §7）。

---

## 7. 需要回填的设计修订（不阻塞实现，但阻塞"文档自洽"）

| # | 位置 | 现状 | 建议修订 | 级别 |
|---|---|---|---|---|
| **R-1** | S0 §B3.1 | 累加器 60× 量纲错误 | 采用「修 A」（游戏秒单位 + 门槛 60） | **BLOCKER** |
| **R-2** | S0 §B3.2 | `simLOD` 升级条件含"或视锥内" | 删除"或视锥内"；补一句「视锥仅影响 `renderLOD`，不影响 `tickLevel`」 | **BLOCKER** |
| **R-3** | S0 §D `LOD_FULL_MAX` | 40 | 改为 **32**（与 Core 验收 5、S2 §8 取舍声明、任务硬约束一致） | 高 |
| **R-4** | S0 §C3 预算表 | `FULL≤40 × 0.09 = 3.60` + `COARSE≤80 × 0.012 = 0.96` + `写队列 0.50` = 6.00，**余量仅 0.94ms**；且 `40 FULL` 与 S2 §8「默认 32 FULL」冲突；`COARSE ≤80` 也超过"实体总数 ≤120 减去 FULL 与容器" | 替换为「表 A + 表 B + 表 C」的 tick 分级口径，并把 `COARSE` 数量改为 **44** | 高 |
| **R-5** | S0 §B3.4 | 快进未说明如何做到 ≤800ms | 补一句「快进走 boundary 批量模式（不逐 tick），与常规路径共享同一排序键」 | 中 |

> **关于 §七-A-2 里的"LOD 升级/降级的切换抖动是否可接受"**：可接受。滞回 120/150m 把抖动频率压到"玩家跨越 30m 环带"的量级；残余抖动由 D5 的「1 分钟内 ≥3 次切换 → 锁定 COARSE 60 游戏分钟」兜底。且因 `simLOD` 不再含视锥（D2），抖动的**仿真后果**被限制在"是否逐分钟细化"，随机序列完全不受影响。

---

## 8. 备选方案（Options）

| 方案 | 描述 | 为何不取 |
|---|---|---|
| O1 · 在"被发现的那一帧"补做判定 | 实现最省事 | **正是 S2 §4.4 明令禁止的做法**：取值锚定当前 `absTick` → LOD 切换即改变结果 → 验收 6 崩 |
| O2 · COARSE 实体完全冻结，不推进 | 最省性能 | 违反 P3「世界不为你待机」，违反 S0 §B3.3「离屏允许不可逆结果」 |
| O3 · 全实体常驻 FULL | 无 LOD 差异，确定性最干净 | 76 actor × 0.085 ≈ 6.46ms **单 tick 已超 6ms**，且 S2 明确"宁明示取舍，不假装装得下" |
| O4 · 用多线程加速 COARSE | 看似提升吞吐 | 引入线程调度不确定性，直接违反 ADR-002 D7-1；且本方案已有 105× 余量，无需优化 |
| **O5 · 本 ADR（boundary 事件化 + 锚定 absTick + 全局排序键）** | — | **采纳** |

---

## 9. 验证（如何证明 §七-A-3 真的成立）

| 验证 | 手段 | 判据 |
|---|---|---|
| **V1 · 锚定不变式** | CI 静态检查：`makeSeedCtx` 第二参数不得是 `now()/absTick/当前 tick` | 命中数 = 0 |
| **V2 · 顺序不变式** | 单测：把同一批 boundary 分别按"逐 tick 路径"与"批量快进路径"处理，比对结果 | 逐位相同 |
| **V3 · 早算/晚算等价** | 强制某 actor 在 FULL 与 COARSE 之间切换 100 次（含跨 boundary 切换），跑 10 日 | `worldHash` 与"全程单一 LOD"的运行逐位相同（除位置量化误差） |
| **V4 · LOD 差异上界** | 全 COARSE 跑 10 日 vs 全 FULL 跑 10 日 | `max_d |ΔSafeDays| / max(S,1) ≤ 5%`，差异来源在面板可见 |
| **V5 · 快进一致性** | 走 `fastForward(8h)` vs 连续 `simTick ×480` | `worldHash` 逐位相同（**这是 I2 排序键不变式最强的检验**） |
| **V6 · 抖动锁** | 脚本化玩家在 120/150m 环带来回穿越 | `LodSwitch` 计数 ≤ 阈值；触发锁定后 60 游戏分钟内不再切换 |
| **V7 · 预算** | `--perf` 采集 p50/p95/max per-tick 与 per-frame | `max_per_frame ≤ 6.0ms`；S2 `p95 ≤ 3.20ms`；快进 ≤800ms |
| **V8 · 无遗漏补做** | 断言：`nextBoundaryIdx` 与 `boundaries.size()` 在日切时相等（当日边界全消费） | 不等即 assert 失败 |

---

## 10. 待主理人拍板（Ask）

1. **Q1（BLOCKER）**：批准 S0 §B3.1 的「修 A」吗？（不批准则 60× 量纲错误保留，全部预算数字作废）
2. **Q2（BLOCKER）**：批准 `simLOD` 剔除视锥吗？（这是本 ADR 中**最可能推翻现有设计**的一条；但也是让"玩家转头不改变世界"成立的唯一方式）
3. **Q3**：`LOD_FULL_MAX` 40 → 32，确认吗？
4. **Q4**：S0 §C3 预算表替换为本文表 A/B/C 的 tick 分级口径，确认吗？

---

## 11. 知识缺口

- `travelMin` 的**预计算路程常量表**由关卡/区域内容（S8，待写）提供。Core 期若无 S8，需先用"直线距离 ÷ 常量步速 × 地形系数"的占位实现，并在 S8 落地后保持同一接口。**该表是确定性输入，必须静态烘焙，不得运行时算**。
- S0 §B5 明示"导航与路点插值"为**架构 stub**。本 ADR 的 `closedFormAdvance` 依赖路点插值，Core 期用简化实现（路点线性插值 + 地形系数），需在使用前确认不引入超越函数（ADR-005）。

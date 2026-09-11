# ADR-004 · 事件总线与因果引用（AttributionLink）

- **状态**：**已提出，待确认 1 处收窄（见 §7）后生效**
- **日期**：Phase 3 · PHASE3-001
- **相关**：S0 §C2 / §C3 / §C5 / §C7 / §C8、S0 Part E、S2 §4.2、S4 §3.6、S5 §4.1、S7 §3、ADR-002
- **回答**：`06-consistency-review.md §七-A-4`（事件总线因果引用）

---

## 1. 背景（Context）

S0 §C2 已定死：

```text
AttributionLink { step: int, causeRef: RefId, effectRef: RefId, actorIds: ActorId[], dayKey: int }
RefId = "<system>.<entityType>.<id>[.<field>]"   例：S5.post.he_valley.water_03.output
```

已拍板的硬约束：

- `step` 从 1 起，**>3 即判定设计违规**（P1），报警 + 记入评审，**不静默截断**（S0 §C7-4）。
- 匿名 actor 仅 `step ≤ 2` 时记入 `actorIds[]`（S0 §C3）。
- 核心三跳链 `UNATTRIBUTED == 0`（S0 §C8 验收 1）。
- 跨日链路：`dayKey` 记**效果发生日**，跨日另计 `latencyDays`，**不重置 `step`**（S0 §B7-4）。
- 导出格式 `.jsonl` + 可选 `.graphml` + `.md` 摘要；必含 `worldSeed / version / dayKey 范围`，**否则视为无效证据**（S0 §C8）。
- 归因链面板：点任一 `effectRef` → 反查 ≤3 跳，渲染 A→B→C 节点图。

评审的问题是：**事件总线要支持"事件携带因果引用"，而不只是广播**。

同时设计里有两处会导致"链条断掉"的隐患，必须在本 ADR 里堵上：

- **H1 · 跨系统 vs 同系统的界限没定义**：如果同系统内部每一步都 `recordAttribution`，`step` 会在两步内耗光；如果都不记，又漏掉真正的跨系统链。
- **H2 · 日切批处理里的"多因一果"**：如 `PromiseBreached` → `ReputationDelta`，在日切第 7 步内发生；而日切是批量应用写队列，**"谁引发了我"在批量场景下容易丢**。

---

## 2. 决策（Decision）

### D1 · 总线形态：**同步、有序、单线程、按 tick 分代**的 `EventBus`

明确**不做**什么：不做异步队列、不做跨线程投递、不做优先级抢占、不做事件合并、不做延迟到下一帧。理由：以上每一条都会引入"顺序不确定"，与 ADR-002 D7-1 冲突。

```cpp
struct SimEvent {
    uint64_t   eventId;      // 全局单调递增（= 已发出事件总数），确定性
    uint64_t   absTick;
    uint32_t   emitSeq;      // 同一 tick 内的发出序号
    EventType  type;
    RefId      subjectRef;   // 事件的主体（谁的什么事）
    RefId      causeRef;     // 可为空 → 见 D4 的 UNATTRIBUTED 处理
    ActorId    actorIds[4];  // 定长数组（避免热路径分配），超出部分记溢出计数
    uint16_t   payloadKind;  // 强类型 payload 的判别式（禁 void*/禁 RTTI）
    Payload    payload;      // union，≤64B
};
```

**投递规则**：
- 事件在**发出的那一刻同步投递**（深度优先），但 **handler 只允许写"写队列"，不允许直接改世界状态**（与 S0「写队列 → 整点/日切应用」纪律一致）→ 消除"同 tick 内 handler 顺序影响结果"的隐患。
- 订阅表在启动时**静态构建**（`subscribe(tickKind, cb)` 只允许在 boot 期调用），运行期不可增删 → 投递顺序完全确定。
- 每次 `SIM_TICK` 开始时 `emitSeq = 0`；`eventId` 全局累加并**入存档**（用于对账，不影响确定性）。

### D2 · `recordAttribution` 的调用边界（补 H1）

> **规则 A-BOUNDARY**：**只有"跨系统"的因果才记 `AttributionLink`。同系统内部的状态推进不记。**

判定"跨系统"的机械标准：**`causeRef` 与 `effectRef` 的 `<system>` 段不同**（`RefId` 的第一段）。

```cpp
bool isCrossSystem(RefId cause, RefId effect) { return systemOf(cause) != systemOf(effect); }
```

- 例：`S1.zone.ruins_a.ctr_12.remaining` → `S2.actor.npc.he_valley.water_03.need` = 跨系统，**记**，`step = parent.step + 1`。
- 例：`S2.actor.X.need` → `S2.actor.X.schedule` = 同系统，**不记**（S2 内部自有记录，不进归因链）。
- 无 parent 的链首：`step = 1`。

**收益**：S0 §C7-4 的「>3 即设计违规」变成一条**可被机器持续验证**的规格；`step` 的语义也从"调用深度"变成"跨越了几个系统的边界"，与设计 P1「三跳可归因」的意图一致。

### D3 · `step` 的传递算法

```cpp
// 每个 effectRef 在当日维护一个 map: effectRef -> minStep（确定性有序容器）
void recordAttribution(RefId causeRef, RefId effectRef, ActorId* actorIds, uint8_t n, uint64_t absTick) {
    if (causeRef.empty()) { onUnattributed(effectRef, absTick); return; }          // D4
    uint8_t step = 1;
    if (auto* parent = linkIndex.find(causeRef); parent) step = parent->step + 1;  // 沿 causeRef 上溯
    if (step > 3) { reportDesignViolation(causeRef, effectRef, step); }            // 不截断，报警 + 记评审
    AttributionLink link{ step, causeRef, effectRef, {}, dayKeyOf(absTick) };
    for (uint8_t i = 0; i < n; ++i)
        if (step <= 2 || !isAnonymous(actorIds[i])) link.actorIds.push_back(actorIds[i]);  // 匿名仅 step≤2
    link.dayKey = dayKeyOf(absTick);                       // 效果发生日
    if (auto* prev = linkIndex.find(effectRef); prev && dayOf(prev->absTick) != link.dayKey)
        link.latencyDays = link.dayKey - dayOf(prev->absTick);   // 跨日另计，不重置 step
    append(link);                                          // 只追加，永不修改（D6）
}
```

### D4 · `UNATTRIBUTED` 的强处理（S0 §C7-1）

- 无 `causeRef` 的写入 → **立即 warn（debug 构建 assert 中断）**，写 `AttributionLink{ step: 0, causeRef: "UNATTRIBUTED", ... }`。
- 验收要求：**核心三跳链 `UNATTRIBUTED == 0`**。机械定义：对 `step ≤ 3` 且 `effectRef` 属于"核心链闭合集"（见 D8）的 link，`causeRef == "UNATTRIBUTED"` 的计数必须为 0。
- **CI 门禁**：`simcore-cli assert-attribution --days 10` 返回非 0 即构建失败。

### D5 · 日切批处理中的因果（补 H2）

日切九步是"批量应用写队列"，最容易丢 `causeRef`。三条强制：

1. **写队列条目自带 `causeRef`**：`WriteQueueItem{ seq, priority, actorId, targetRef, payload, causeRef, sourceSystem, dayKey }`。**没有 `causeRef` 的条目不允许入队**（`enqueue` 断言）。
2. **批处理中产生的派生效果，继承上游 `causeRef`**：如 7.3 `PromiseBreached` → 7.4 `ReputationDelta`，7.4 的 `causeRef` = 7.3 产生的 `effectRef`（`S4.ledger.<entryId>.state`），形成 `S4 → S5` 的一跳。
3. **多因一果**：多个写队列条目指向同一 `effectRef` 时，**取 `step` 最小的那条作为主链**，其余进 `secondaryCauses[]`（调试字段，不进契约最小集），**不合并 `step`**。

### D6 · 不可变与补偿

- `AttributionLink` 一旦写入**永不修改**。
- 需要"撤销/回滚"语义时（如 `VOID`），**追加一条新的反向 link**（`causeRef` = 原 `effectRef`），不修改原记录。
- 存储：`AttributionLog` 环形缓冲 **7 日 或 20k 条**（先到先落盘）；落盘 `.jsonl`（一行一 link）。

### D7 · `RefId` 的解析与校验

```text
RefId 语法（S0 已定）：  <system>.<entityType>.<id>[.<field>]
  system     ∈ {S0,S1,S2,S3,S4,S5,S6,S7}        ← 闭集，启动期断言
  entityType ∈ 各系统自报的白名单（post / actor / zone / ctr / ledger / warehouse / rumor / order ...）
  id         可含点号（如 he_valley.water_03）→ 用【最后一段】判定是否为 field
  field      可选，取自各系统白名单
```
- 提供 `parseRefId()` 与 `validateRefId()`；**debug 构建下每次 `recordAttribution` 都校验两端**，非法即 assert。
- 启动期断言：所有系统注册的 `entityType` 白名单无重名。

### D8 · "核心链闭合集"（用于验收 1 的机器判据）

Core 验收 1 的三跳连锁是：**清空 zone 物资 → 某 NPC 需求链变化 → 日程改道 → 社区安全天数下降 ≥10%**。对应的闭合集：

```text
S1.zone.<zoneId>.<containerId>.remaining
  → S2.actor.<actorId>.need                 (step 1)
  → S2.actor.<actorId>.schedule             (step 2)
  → S5.community.<communityId>.safeDays     (step 3)
```
- CI 用例：脚本化清空 X 区 80% 物资 → 3 游戏日内断言闭合集中出现一条 `step == 3` 且终点为 `safeDays` 的链，且 `ΔsafeDays ≤ −10%`，且全链 `UNATTRIBUTED == 0`。
- 该闭合集写入 `simcore/attribution/core_chain.h`，作为验收 1 的**唯一机器判据**。

### D9 · 导出规格（S0 §C8）

```text
触发：面板按钮 / simcore-cli export-attribution --from D0 --to D1 / 环形缓冲超限自动落盘
产物：
  attribution.<worldSeed>.<D0>-<D1>.jsonl    一行一个 AttributionLink，字段顺序固定
  attribution.<...>.graphml                  可选，供图工具
  attribution.<...>.md                       人类可读摘要：按 step 分组 + 闭合集命中高亮
文件头（必须，否则视为无效证据）：
  { "worldSeed":…, "gameVersion":…, "simSchemaVersion":…, "streamIdRegistryVersion":…,
    "dayKeyRange":[D0,D1], "exportedAt":… }
```

---

## 3. 备选方案（Options）

| 方案 | 描述 | 为何不取 |
|---|---|---|
| O1 · 异步事件队列（跨帧/跨线程） | 解耦彻底 | 引入顺序不确定性，与 ADR-002 D7-1 冲突；归因链无法保证逐位复现 |
| O2 · 每次事件都 `recordAttribution`（不分跨系统） | 最完整 | `step` 会在同系统内部耗光；S0 §C7-4「>3 即设计违规」会被同系统噪声刷屏，失去信号 |
| O3 · 只记 `step==1` 的直因，不传播 | 实现最省 | 无法回答 Core 验收 1（要三跳），也违反 P1 |
| O4 · 用指针/对象引用做 `causeRef` | 反查最快 | 不可序列化、不可跨存档、不可导出；`RefId` 字符串已被 S0 钉为契约 |
| O5 · 超限静默截断到 3 | 表好看 | 违反 S0 §C7-4「不静默截断，报警 + 记入评审」 |
| **O6 · 本 ADR（同步有序总线 + 跨系统才记 + step 上溯 + 不可变追加）** | — | **采纳** |

---

## 4. 后果（Consequences）

**正面**
- 归因链从"设计主张"变成"可被 CI 断言的数据结构"：闭合集（D8）+ `UNATTRIBUTED == 0`（D4）+ `step ≤ 3`（D2）三条都可机器验证。
- handler 只写写队列（D1）→ 事件顺序不改变世界状态，只改变"谁先入队"，而入队顺序由 `(priority, actorId, seq)` 决定（S0 已定）→ 完全确定。
- 补偿用追加而非修改（D6）→ 归因链是**append-only 账本**，可 audit。

**负面 / 成本**
- 每次跨系统写入都要构造 `RefId` 字符串 → 热路径分配风险。**缓解**：`RefId` 用**定长 64B 栈上缓冲 + 小字符串优化**，禁堆分配；写队列本来就是批处理，不在最热路径。
- `AttributionLog` 内存 ~1.9MB（20k × 96B），是仿真侧最大的单项。**缓解**：可调到 10k 条；Core 期 1.9MB 可接受。
- 需要各系统维护 `entityType` 白名单（D7），有一次性成本。

**中性**
- `secondaryCauses[]` 是调试字段，不进契约最小集；若主理人认为应进契约，需回 S0 §C2 扩字段。

---

## 5. 受影响文档

| 文档 | 建议 |
|---|---|
| `design/gdd/systems/00-foundation.md §C2` | 建议补一句：「`step` = 跨系统边界的跨越次数（`causeRef` 与 `effectRef` 的 system 段不同才 +1）」——不改字段名 |
| `design/gdd/systems/00-foundation.md §C7-1` | 建议把"核心三跳链"机械化为 D8 的闭合集 |
| `design/gdd/systems/00-foundation.md §B4` | `WriteQueueItem` 建议补 `causeRef / sourceSystem` 两字段（D5-1） |
| `docs/architecture/control-manifest.md` | 已写入 4 条门禁（D4 / D8 / D9 / 静态检查） |

---

## 6. 验证

| 验证 | 手段 | 判据 |
|---|---|---|
| **V1 · 无 UNATTRIBUTED** | `simcore-cli assert-attribution --days 10` | 闭合集内 `causeRef=="UNATTRIBUTED"` 计数 == 0 |
| **V2 · step ≤ 3** | 同上 | 闭合集内 `max(step) ≤ 3`；越界则构建失败并列出违规链 |
| **V3 · 三跳连锁（Core 验收 1）** | 脚本化清空 X 区 80% → 跑 3 日 → 断言闭合集出现 `step==3` 终点 `safeDays` 的链 | 命中 + `ΔsafeDays ≤ −10%` |
| **V4 · 顺序确定性** | 同一 seed 跑两次，`eventId → (type, subjectRef)` 序列比对 | 逐位相同 |
| **V5 · 日切继承** | 构造一个跨 7.3→7.4 的违约场景 | 7.4 的 `causeRef == ` 7.3 的 `effectRef`；`step` 递增 1 |
| **V6 · 跨日不重置** | 构造跨日链 | `latencyDays > 0` 且 `step` 未重置 |
| **V7 · 匿名规则** | 构造匿名 actor 参与的链 | `step ≥ 3` 时 `actorIds[]` 中无 `anon.*` |
| **V8 · 导出完整性** | 导出文件头缺任一必填字段 | 工具拒绝并报错 |
| **V9 · 不可变** | 代码审查 + 静态检查：无 `link.step =` / `link.causeRef =` 赋值（构造期除外） | 命中数 = 0 |

---

## 7. 待主理人确认（Ask）

1. **Q1**：`step` 的语义收窄为「跨系统边界次数」（D2 规则 A-BOUNDARY），是否批准？这是**实现级定义**，`AttributionLink.step` 字段名与类型不变；但不定义就会出现"同系统内部也 +1"的实现，三跳预算立刻耗尽。
2. **Q2**：`WriteQueueItem` 增加 `causeRef / sourceSystem` 两个字段（D5-1），是否批准进 S0 §B4 契约？
3. **Q3**：`secondaryCauses[]`（D5-3）保持为调试字段、不进契约最小集，还是进契约？

---

## 8. 知识缺口

- S4 §3.6 的 `PromiseBreached` 与 S5 §4.1 的 `ReputationDelta` 之间的**字段级 `RefId` 命名**（`S4.ledger.<entryId>.state` / `S5.actor.<actorId>.reputation`）在 GDD 中未显式给出。本 ADR 按 `RefId` 语法推导，需 S4/S5 归属确认后固化进 `entityType` 白名单。
- S6 传闻网络的 `RumorNode` 是否也要进归因闭合集（作为 `step 4` 的候选）——**未定**。建议 Core 期不进（避免触发 >3 违规），留待 S6 实装时评估。

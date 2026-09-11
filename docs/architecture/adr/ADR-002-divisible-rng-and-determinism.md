# ADR-002 · 可分流 RNG 与确定性执行模型

- **状态**：**已提出，待主理人确认 3 处细化（见 §7）后生效**
- **日期**：Phase 3 · PHASE3-001
- **相关**：S0 §C2 / §C3 / §C7、S2 §4.4、S1 §3.5、ADR-001、ADR-003、ADR-005
- **回答**：`06-consistency-review.md §七-A-1`（可分流 RNG）

---

## 1. 背景（Context）

S0 §C2 已定死契约：

```text
RngStream(worldSeed, streamId) : seed64 = FNV1a64(worldSeed ‖ streamId)
value = pcg32(seed64, counter = seedCtx.tick)
```

红线四条：① 禁裸 `rand`/`Math.random`/系统时间/未初始化内存；② 禁跨 streamId 共享计数器、禁把 tick 或随机数拼进 streamId；③ streamId 命名 `<domain>.<sub>.<qualifier>` 且**静态可枚举**；④ **判定必须在固定 tick 发起，禁在渲染帧发起**。

`06-consistency-review.md §七-A-1` 问的是：能否提供**按 streamId 分流、可存档/读档恢复**的伪随机源，使 save/load 后 10 日 safeDays 差异 ≤5%。

工程上还有两个 S0 未写死的空档，必须在这里补，否则实现会各自发挥：

- **空档 N1**：`counter = seedCtx.tick` 时，**同一 (streamId, absTick) 内取第二次值会得到同一个数**。S6 日切批处理、S2 边界补做都可能在"同一个 absTick"上对同一 streamId 多次取值。
- **空档 N2**：`pcg32` 是**有状态**算法（LCG 推进 + XSH-RR 输出）。要让它成为 `(seed64, counter)` 的**纯函数**（S2 §4.4 的"早算晚算结果一致"完全依赖这一点），必须有明确的 counter→state 映射，不能靠"记住上次状态"。

**N2 是决定性的**：如果 RNG 是"有状态顺序推进"，那么 FULL 与 COARSE 两条路径因为**取值次数不同**（FULL 每分钟判定、COARSE 只在边界判定），后续所有取值都会错位 → S2 §4.4 的论证直接不成立，Core 验收 6（≤5% 差异）无保证。所以 **RNG 必须是"以 absTick 为自标的无状态纯函数"**。

---

## 2. 决策（Decision）

### D1 · 唯一随机原语：`pcg32_at(seed64, counter)`，无状态、可跳步

```cpp
// simcore/rng/pcg32.h —— 全项目唯一随机原语，禁止第二套
constexpr uint64_t LCG_A = 6364136223846793005ULL;
constexpr uint64_t LCG_C = 1442695040888963407ULL;

struct LcgFn { uint64_t a, c; };                      // f(x) = a*x + c   (mod 2^64)
inline LcgFn compose_then(LcgFn f, LcgFn g) {          // 先 f 后 g
    return LcgFn{ g.a * f.a, g.a * f.c + g.c };        // 模 2^64 乘法即 uint64 回绕
}
inline LcgFn lcg_pow(uint64_t n) {                     // (LCG_A, LCG_C)^n
    LcgFn r{1, 0}, b{LCG_A, LCG_C};
    while (n) { if (n & 1) r = compose_then(r, b); b = compose_then(b, b); n >>= 1; }
    return r;
}
inline uint32_t pcg_xsh_rr(uint64_t s) {
    uint32_t xs = uint32_t(((s >> 18u) ^ s) >> 27u);
    uint32_t rot = uint32_t(s >> 59u);
    return (xs >> rot) | (xs << ((-rot) & 31u));
}
inline uint32_t pcg32_at(uint64_t seed64, uint64_t counter) {
    LcgFn f = lcg_pow(counter);
    return pcg_xsh_rr(f.a * seed64 + f.c);             // 纯函数：只依赖 (seed64, counter)
}
```

**性能**：`lcg_pow` 为 O(log counter)（≤64 次迭代 × 2 次乘法 ≈ 128 mul）。加两级缓存后实际远低于此：

```cpp
struct RngStreamCache { uint64_t seed64=0, lastCounter=0, lastState=0; bool valid=false; };
// 快路径：counter 递增且 delta 小时顺序步进；delta 大时跳步。缓存仅为性能，不改变结果。
```
每 tick 取值量级 ~10²，p95 开销 **< 5 µs/tick** → 可忽略（预算表按 0.000ms 计，见 architecture.md §8 注 3）。

### D2 · counter 构造（补 N1）：`counter = absTick × 4096 + callSeq`

```text
absTick  : 全局单调递增（dayKey × 1440 + minuteOfDay），永不回退
callSeq  : 同一 (streamId, absTick) 内的调用序号，从 0 起；每个 SIM_TICK 开始时对所有活跃 seedCtx 归零
counter  : uint64 = absTick * 4096 + callSeq
断言     : callSeq < 4096，越界即 assert 失败（不是静默回绕）
seedCtx  : { streamId, seed64, baseTick: absTick, callSeq: 0 }     // 签名与字段名不变
```

- **不改 S0 契约**：`makeSeedCtx(streamId, absTick)` 与 `rng(seedCtx)` 的**签名与字段名逐字保留**，本条只补充 `counter` 的构造方式（S0 写的是 `counter = seedCtx.tick`，本条把它解释为 `baseTick × 4096 + callSeq`，语义兼容且更严）。
- `absTick` 上界核算：10 游戏年 = 3650 日 → `absTick ≤ 5.26e6` → `counter ≤ 2.15e10`，远在 uint64 内。
- **4096 次/stream/tick 的调用上限**如何核算：最大单点负载是 S6 `gossip.<community>` 在日切批处理内的失真选边——`reachedSet ≤ 7 × hop ≤ 3`，量级 10¹，余量 2 个数量级。若将来超界，断言会先炸，不会静默。

### D3 · 无状态带来的结构性收益：存档**不需要**存 per-stream counter

因为 `value = pcg32_at(FNV1a64(worldSeed‖streamId), absTick×4096 + callSeq)` 是**纯函数**：

- 只要 `worldSeed / absTick / 调用顺序` 三者一致，取值必然一致；
- **S0 §C7-2 的风险（"新增 streamId 未进存档 → 恢复后 counter 归零 → 重放偏移"）被结构性消除**——新增 streamId 不改变任何已有 stream 的取值（`seed64` 由 `worldSeed ‖ streamId` 独立派生）。

**但仍然保留一条显式护栏**：存档写入 `streamIdRegistryVersion`（见 D4），启动时与当前二进制的注册表版本比对，**不一致即拒绝重放并提示，不静默**（S0 §C3 硬要求）。

### D4 · streamId 静态注册表（补红线③的可执行形态）

```cpp
// simcore/rng/stream_registry.h —— 唯一定义处；新增 streamId 必须改这里 + 提升 STREAM_REGISTRY_VERSION
#define STREAM_REGISTRY_VERSION 1
enum class StreamId : uint16_t { /* 由下表生成 */ };
static constexpr const char* kStreamName[] = {
  "check.player.scavenge", "check.player.barter", "check.player.dialogue",
  "ai.npc.he_valley.<role>_<nn>.route", "ai.npc.jing_cell.<role>_<nn>.route",
  "loot.zone.<zoneId>.<containerId>",
  "price.he_valley.<cluster>", "price.jing_cell.<cluster>",
  "gossip.he_valley", "gossip.jing_cell",
  "estate.he_valley.gossip", "estate.jing_cell.gossip",
  "weather.global", "wild.global",           // 保留位（Extended）
};
```

- 命名规则沿用 S0 §C2-③：`check.*`（判定，S0·A）· `ai.*` · `loot.*` · `price.*` · `gossip.*` · `estate.*` · `weather.*`。
- 带 `<...>` 的是**参数化模板**：运行时实例化，但**模板本身静态**，实例化参数只允许 `ActorId / ClusterId / zoneId / containerId`（均来自确定性数据），**禁止把 tick、随机数、指针、遍历序号拼进去**（红线②）。
- Debug 构建下每次 `makeSeedCtx` 断言 `streamId ∈ 注册表` 且实例化参数匹配模板。

### D5 · 唯一判定出口：`rollCheck`，全仓 `d20` 字面量命中数 = 1

```cpp
// simcore/check/roll_check.h —— 全项目唯一判定实现
struct RollResult { bool success; int raw; int dc; int modTotal; int margin; bool crit; bool fumble; };
RollResult rollCheck(ActorId actor, StreamId streamId, int dc, int modTotal, uint64_t absTick, uint8_t& callSeq);
//  raw     = 1 + (uint32_t)((uint64_t)pcg32_at(seed64, counter) * 20u >> 32)   // 乘移取模，无取模偏置，永久固定
//  margin  = raw + modTotal - dc
//  success = margin >= 0 || raw >= dc + 8        // S0 §A7-3 "稳过"保底
//  crit    = margin >= CRIT_MARGIN(5)            // 大成功
//  fumble  = margin <= -CRIT_MARGIN
```

**静态守卫（CI 强制）**：
| 检查 | 命令/手段 | 失败即构建失败 |
|---|---|---|
| `d20` 字面量命中数 = 1 | `rg -c '\bd20\b' simcore/ src/ --glob '!*test*'` 汇总必须 == 1 | ✓ |
| 业务系统 raw rng 调用数 = 0 | `rg 'pcg32_at\|makeSeedCtx\|rng\(' simcore/s1..s7/` 必须为空（只允许 `rollCheck`） | ✓ |
| 表现层禁判定 | `rg 'rollCheck\|makeSeedCtx' src/render src/ui` 必须为空 | ✓ |
| 禁裸随机 | `rg '\brand\b\|srand\|Math\.random\|time(\|chrono::high_resolution' simcore/ src/` 必须为空 | ✓ |
| 禁 double / 超越函数 | `rg '\bdouble\b\|\bsin\b\|\bcos\b\|\bexp\b\|\bpow\b\|\blog\b' simcore/` 必须为空 | ✓ |

### D6 · 存档的确定性字段集（S0 §C3 落地）

```text
SaveGame v1 必存字段（缺一即拒绝加载）：
  worldSeed : uint64
  gameVersion / simSchemaVersion / streamIdRegistryVersion   // 三者任一不匹配 → 拒绝重放 + 明确提示
  dayKey : int32        minuteOfDay : int32        absTick : int64
  全部写队列条目（含 seq / priority / payload）
  每 actor 的 simLOD 状态 + LodHysteresisGuard 剩余锁定 tick
  每 actor 的 ScheduleDay（冻结）+ 已消费的 boundary 游标
  DaySnapshot[D-1] 与 [D]（不可变快照）
  AttributionLog 尾部（最近 7 日或 20k 条）
  worldHash 环形缓冲（最近 24 个，用于加载后首帧自检）
恢复后首 tick 必须是 savedTick + 1（不跳号、不补跑）  ← S0 §C3 硬约束
禁止依赖运行时缓存：导航路径 / 路线长度 / 路程常量表 必须可由 seed 或静态数据重建
```

### D7 · 确定性执行的五条纪律（工程侧）

1. **单线程仿真**：`simcore` 全程单线程、固定遍历顺序（容器用 `std::vector` + 索引，禁 `unordered_map` 迭代序）。
2. **禁渲染帧发起判定**：判定只能在 `SIM_TICK / HOURLY_TICK / DISPATCH / DAILY_CUTOVER` 的回调里发起（ADR-003 §4 帧循环）。表现层只能提交 `InputCommand`，由下一个 SIM_TICK 消费。
3. **禁把帧时间/dt 引入任何仿真公式**；`dtReal` 唯一合法用途是喂累加器（ADR-003 §3）。
4. **禁 `double` 与超越函数**；数值表示走 ADR-005（milli 定点），三角函数走 65536 项查表 + 线性插值（插值本身也用定点）。
5. **编译选项锁定**：`-O2 -ffp-model=strict`（或 MSVC `/fp:strict`）、禁 `-ffast-math`、禁自动向量化改变归约顺序（CI 里把编译选项写进 `simcore/CMakeLists.txt` 并在 `worldHash` 自检里验证）。

---

## 3. 备选方案（Options）

| 方案 | 描述 | 为何不取 |
|---|---|---|
| O1 · 有状态顺序推进 pcg32（每 stream 存 state） | 最快，一次乘法一步 | **致命**：FULL/COARSE 取值次数不同 → 状态错位 → S2 §4.4 论证不成立、验收 6 无保证 |
| O2 · SplitMix64 / xoshiro** 分流 | 真正的"可拆分"生成器 | 同样有状态，同 O1 问题；且 S0 已钉 `pcg32`，**常量/接口名是契约不可改名** |
| O3 · 纯哈希式 `value = FNV1a32(seed64 ‖ counter)` | 完全无状态，最简单 | 统计质量弱于 PCG（FNV 不是好的 PRNG，低位相关性强）；S0 已钉 pcg32。可作为**降级兜底**写进测试用例做交叉校验 |
| O4 · 每 stream 持久化 counter（S0 §C7-2 的字面读法） | 与 §C3 "存档须含每 streamId 的 counter" 一致 | 与本 ADR **不冲突**：我们仍然可以在存档里写一份 counter 快照用于**调试对账**，但它**不是**取值的输入。最终以 D3 为准，§C3 该句建议改为"存档须含 streamId 注册表版本 + 用于调试对账的 counter 快照" |
| **O5 · 本 ADR（无状态 pcg32 + counter = absTick×4096 + callSeq + 两级缓存）** | — | **采纳** |

---

## 4. 后果（Consequences）

**正面**
- S2 §4.4「早算晚算结果一致」在数学上成立：`rollCheck` 的值只由 `(streamId, absTick, callSeq)` 决定，与"何时算、算几次、谁先算"无关（只要 `callSeq` 分配顺序确定）。
- LOD 切换、离屏快进、渲染掉帧**都不改变随机序列** → 验收 6 的 5% 差异预算几乎可以全部留给"COARSE 闭形式 vs FULL 逐分钟"的**数值精度差**，而这个差是可枚举、可在面板展示的（S2 A4 要求"差异来源可见"）。
- 存档更小（无需 per-stream 状态）、加载校验更简单。
- headless 重放可以在 CI 里跑任意天数，回归成本极低。

**负面 / 成本**
- `lcg_pow` 有 O(log counter) 开销，需缓存层（约 60 行）才能把热路径压到 O(1)。
- `callSeq` 的分配顺序必须确定 → **任何容器遍历顺序的改变都是破坏性变更**。缓解：D7-1 固定容器 + CI 里跑固定种子回归。
- 4096 上限是硬约束；若未来某系统单 tick 单 stream 需更多取值，必须**新增 streamId**（而不是复用），这要求注册表演进流程。

**中性**
- 浮点相关风险不变，仍由 ADR-005 承接。

---

## 5. 受影响文档

| 文档 | 变更建议 |
|---|---|
| `design/gdd/systems/00-foundation.md §C2` | 建议补一句 counter 构造：`counter = absTick × 4096 + callSeq`（不改字段名/签名） |
| `design/gdd/systems/00-foundation.md §C3` | 建议把"每 streamId 的 counter"改为"streamId 注册表版本 + 调试用 counter 快照" |
| `design/gdd/systems/00-foundation.md §C7-2` | 风险已被 D3 结构性消除，建议改写为"注册表版本不匹配 → 拒绝重放" |
| `docs/architecture/control-manifest.md` | 已写入 5 条静态守卫（见 D5 表） |
| `docs/architecture/adr/ADR-003-*.md` | boundary 判定依赖 `makeSeedCtx(streamId, boundaryAbsTick)`，本 ADR 是它的前置 |

---

## 6. 验证（如何证明这条 ADR 生效）

| 验证 | 手段 | 判据 |
|---|---|---|
| V1 · 无状态性 | 单测：同一 `(seed64, counter)` 调用 1000 次，值全等；乱序调用（先 counter=100 后 counter=1）结果与顺序无关 | 100% 全等 |
| V2 · 跳步正确性 | 单测：`pcg32_at(s, n)` == 顺序推进 n 次后的输出（n ∈ {1,2,63,64,4095,4096,1e6}） | 逐位相同 |
| V3 · 10 日重放 | `simcore-cli replay --seed 20250910 --days 10 --run A/B`，B 在第 3 日做一次 save/load | `worldHash` 逐位相同；`safeDays` 差异 ≤5%（目标 0%） |
| V4 · LOD 无关性 | 强制全部 actor 走 COARSE 跑 10 日 vs 强制全部 FULL 跑 10 日 | 两条曲线差异 ≤5%，且差异只在"精度项"，面板可见 |
| V5 · 静态守卫 | CI 跑 D5 的 5 条检索 | 全部为空 / 命中数 == 1 |
| V6 · `worldHash` | 每 60 tick 计算，纳入生理 + 库存 + 位置量化 0.1m + 关系值；规范子集 ≈ 3.76 KB → FNV1a @ ~1–2 GB/s ≈ **2–4 µs/次**，摊销 <0.0001ms/tick | 两次运行逐位相同 |

---

## 7. 待主理人确认（Ask）

1. **Q1**：`counter = absTick × 4096 + callSeq`（D2）是否批准？——**这是 S0 §C2 的实现级细化，不是改名**，但不确认会有多人各自发挥。
2. **Q2**：是否批准把 S0 §C3 的"每 streamId 的 counter"改写为"注册表版本 + 调试用 counter 快照"（D3/O4）？
3. **Q3**：`raw` 用乘移取模（`(u32*20)>>32`）而非 `%20`，是否接受？（差异在 1e-9 量级，但对**逐位复现**而言必须钉死一种。）

---

## 8. 知识缺口

- `pcg32` 的 `LCG_A/LCG_C` 常量与 XSH-RR 输出函数我按 PCG 公开参考实现给出（Knuth LCG 常数 + 1442695040888963407 增量）。**若项目已有权威实现，以项目为准**；无论如何，实现一旦落定必须写进 `simcore/rng/pcg32.h` 并加 golden 测试向量（前 8 个输出值），此后不得变更。

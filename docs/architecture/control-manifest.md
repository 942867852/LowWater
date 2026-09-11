# 控制清单 · Core 七条验收的可执行验证手段

- **版本**：v0.1（Phase 3 · PHASE3-001）
- **作者**：engineering-lead（程基岩）
- **对象**：`design/gdd/game-concept.md §四-CORE` 的 **Core 七条验收标准**（唯一放行标准）
- **配套**：`architecture.md` · `adr/ADR-000` ~ `ADR-005`
- **用法**：每条验收拆成「判据 → 手段 → 阈值 → 频率 → 失败动作」五行；**自动化列标注 ✅自动 / ⚠️半自动 / ❌人工**，人工项必须给出可复现的脚本与记录表。

---

## 0. 总览：七条验收的可自动化程度

| # | 验收 | 判据类型 | 自动化 | 主要手段 | 前置工具 |
|---|---|---|---|---|---|
| 1 | 三跳连锁可复现 | 结构 + 数值 | **✅ 自动** | 归因闭合集断言 | `assert-attribution`、`export-attribution` |
| 2 | 可读性达标（预测 ≥70%） | 人类测试 | **❌ 人工** | 盲测协议 + 命中记录表 | 24h 甘特面板（校验用，不给受试看） |
| 3 | 重复感测试通过 | 人类测试 | **❌ 人工** | 45 分钟自由游玩 + 自设目标编码表 | 埋点：目标声明 + 行为轨迹 |
| 4 | 主导策略检查 | 数值 | **✅ 自动** | 固定种子 Bot 对跑 | `simcore-cli bot-run` |
| 5 | 性能预算 | 数值 | **✅ 自动** | `--perf` + CI 门禁 | 性能面板 |
| 6 | 可确定性（≤5%） | 数值 | **✅ 自动** | save/load 重放比对 | `replay --save-at` |
| 7 | 契约必然生效 | 结构 + 数值 | **⚠️ 半自动** | 违约场景脚本 + 对话文本检索 | `assert-attribution` + 文本 tag |

**结论**：七条中 **4 条全自动、1 条半自动、2 条人工**。两条人工项（2 / 3）无法用代码替代，但可以**用工具把"人为偏差"压到最小**——这是本清单对它们的主要贡献。

---

## 1. 验收 1 · 三跳连锁可复现

> **原文**：固定种子 + 脚本化玩家行为（清空 X 区 80% 物资），3 游戏日内必然观测到：某 NPC 需求链指标变化 → 日程改道 → **目标社区安全天数下降 ≥10%**；全过程可在 C8 面板逐跳导出归因链（写不出归因链的链条视为未实现）。

### 判据（机器可读）

把"三跳连锁"机械化为**闭合集**（`simcore/attribution/core_chain.h`）：

```text
S1.zone.<zoneId>.<containerId>.remaining
   →(step 1) S2.actor.<actorId>.need
   →(step 2) S2.actor.<actorId>.schedule          （须伴随 ScheduleChanged）
   →(step 3) S5.community.<communityId>.safeDays
```

| 子判据 | 内容 |
|---|---|
| 1a | 闭合集中存在**至少一条** `step == 3` 且 `effectRef` 以 `S5.community.*.safeDays` 结尾的链 |
| 1b | 该链终点 `ΔsafeDays ≤ −10%`（相对触发前一日 `DaySnapshot`） |
| 1c | 全链 `UNATTRIBUTED == 0` |
| 1d | 全链 `step ≤ 3`（越界即设计违规，报警 + 记评审，**不静默截断**） |
| 1e | 中间节点的 `ScheduleChanged.visibleSignal` **非空率 = 100%** 且可反查 `WorldSignalRegistry` 活条目（S2 A6，违反时 `assert` 中断而非降级） |

### 手段

```bash
# 触发脚本（确定性，走完整 tick 序列）
simcore-cli scenario --name drain-zone-x --seed 20250910 --zone ruins_a --ratio 0.80 --days 3

# 断言
simcore-cli assert-attribution --days 3 --closed-set core_chain.h \
            --require 'step==3 & effectRef~=/S5\.community\..*\.safeDays/ & dSafeDays<=-10%' \
            --require 'unattributed==0' --require 'maxStep<=3'
simcore-cli assert-visible-signal            # 1e
simcore-cli export-attribution --from 1 --to 3 --out evidence/   # 举证产物
```

### 阈值 / 频率 / 失败动作

| 项 | 值 |
|---|---|
| 通过阈值 | 1a–1e **全部**成立 |
| 频率 | **每次提交**（CI）；至少 3 个不同 seed × 3 个不同 zone 的矩阵每周跑一次 |
| 失败动作 | 构建失败 + 输出违规链 `.md` 摘要进设计评审队列 |

---

## 2. 验收 2 · 可读性达标（NPC 位置预测 ≥70%）

> **原文**：不给 UI 文字提示，测试者仅看画面游玩 2 个游戏日后，能预测某指定 NPC 在次日 06:00 / 12:00 / 20:00 的位置，**三选三命中率 ≥70%**。
>
> （S2 A2 的等价表述：三选三命中率 ≥70%。）

### 这条为什么难自动化

它测的是**人类从画面提取规律的能力**，本质上是可用性测试。不能伪造。但可以把**测量误差**和**样本偏差**用工具压掉。

### 判据

- **命中定义**：受试给出的位置 ∈ 该 NPC 在目标时刻实际所在 `locationId` 的**判定区域**（半径 = `OBSERVE_RADIUS` 与 20m 取大），或命中同一 `postId` / 同一 `block.intentTag` 场所。
- **三选三**：同一受试对**同一 NPC** 的三个时刻全中才算 1 个"三选三"样本。
- **命中率** = 三选三样本数 / 总样本数（受试 × NPC）。

### 手段（盲测协议）

| 步骤 | 做法 | 工具 |
|---|---|---|
| S1 | 固定 seed、固定存档；受试自由游玩 2 游戏日（不给任何 UI 文字提示、不给甘特面板） | `--seed` + 关闭一切调试 UI 的 release 构建 |
| S2 | 第 3 日 00:00 前，受试在**纸质/独立表单**上写下 3 名指定 NPC 在 06:00 / 12:00 / 20:00 的位置（从场所清单里选，或地图上点） | 独立表单（不进游戏，避免提示） |
| S3 | 游戏跑到三个时刻，**自动 dump** 实际位置 | `simcore-cli dump-actor-pos --at 360,720,1200 --out actual.json` |
| S4 | 自动比对，输出命中矩阵 | `tools/readability_score.py --guess guesses.csv --actual actual.json` |
| S5 | 24h 甘特面板**只在评分后**开给受试看，用于定性访谈"你以为他在哪 / 为什么" | 面板 ① |

### 埋点（辅助诊断，不作为判据）

- 玩家观察行为：每 `SIM_TICK` 记录 `玩家视锥内 actorId 集合` + `注视中心`（可选）
- NPC 可见性：每个 `ScheduleChanged` 的 `visibleSignal.installTick` 与玩家实际"经过该信号"的 tick 差
- 用途：**若命中率不达标，用这些埋点定位是"作息不可读"还是"信号没被看到"** —— 二者修法完全不同。

### 阈值 / 频率 / 失败动作

| 项 | 值 |
|---|---|
| 通过阈值 | 三选三命中率 **≥70%**；样本量 **≥12 受试 × 3 NPC = 36** |
| 频率 | 每次 Core 里程碑（建议 3 次：首个可玩 / 中段 / 放行前） |
| 失败动作 | 不达标 → 用埋点定位；若属 `visibleSignal` 缺失则走 S2 A6（本作可读性最低可执行保证，**不接受任何例外**） |

---

## 3. 验收 3 · 重复感测试通过（自设目标 ≥70%）

> **原文**：12 名测试玩家无任务指引自由游玩 45 分钟，**≥70% 在第 45 分钟仍有自设目标**，且人均自设目标数 ≥2（**自设，非任务列表给的**）。

### 判据

- **自设目标（self-set goal）**的操作性定义：受试**口述/书写**的目标，且能被编码到下表；**工作板 `WorkOrder` 上的条目不计入**（那是系统给的）。
- **"仍有"**：第 45 分钟时受试还能说出 ≥1 个未完成的自设目标。

### 手段（编码表 + 双盲编码）

| 编码 | 自设目标类型 | 举例 |
|---|---|---|
| G-ACCUM | 囤积某物资到某量 | "我要存够 20L 水" |
| G-ROUTE | 开发/优化某条路线 | "我想试试从北边绕过去" |
| G-SOCIAL | 影响某 NPC / 某社区 | "我要让井窖那小子欠我人情" |
| G-EXPLORE | 探明某区域/某机制 | "我想知道辐射区夜里能不能进" |
| G-ARBITRAGE | 利用价差 | "河谷的弹药便宜，我想倒一趟" |
| G-RECOVER | 挽回某个失误 | "我得把我答应他的药补上" |

```bash
# 行为侧交叉验证（不替代口述，只用于发现"说得出但没做"或"做了但说不出"）
tools/goal_probe.py --session <id> --dump-actions   # 输出：目标声明时间戳 / 对应行为轨迹 / 放弃点
```

**双盲编码**：两名编码员独立编码，Cohen's κ ≥ 0.8 方可采信；不一致由第三人裁。

### 埋点

- 每 `SIM_TICK`：玩家位置、背包、当前 `WorkOrder` 视线、最近 3 次交互
- 每 5 分钟（现实）：一次轻提示「你现在想做什么？」（不提供选项，自由文本）
- 第 45 分钟：终局提问（自由文本）

### 阈值 / 频率 / 失败动作

| 项 | 值 |
|---|---|
| 通过阈值 | **≥70%（即 ≥9/12）**第 45 分钟仍有自设目标；人均目标数 **≥2** |
| 频率 | Core 放行前**至少 2 轮**（中间轮用于调参） |
| 失败动作 | 不达标 → 用编码分布定位缺口类型：若 G-ROUTE/G-EXPLORE 缺失 → 世界信号不足（S2/S8）；若 G-SOCIAL 缺失 → 记忆/传闻耦合不足（S4/S6）；若 G-ARBITRAGE 缺失 → 价差信号不足（S3） |

> **红线关联**：概念文档规定"任何新增功能必须先证明自己能通过验收 1 与 3"。因此**新系统接入时，本测试必须重跑**。

---

## 4. 验收 4 · 主导策略检查（≤1.25×）

> **原文**：单一最优资源路线的 10 日物资净值，**不得超过最优混合路线的 1.25 倍**（固定种子 Bot 对跑测定）。

### 判据

```text
netValue(strategy) = Σ_cluster (获得的 Quantity.amount × DaySnapshot[D].price[cluster])  // FOOD 无价格，按 WATER 等价折算或单独列
dominance = netValue(best_single_route) / netValue(best_mixed_route)
通过：dominance ≤ 1.25
```

- **单一最优路线 Bot**：固定"最近邻 `minContainer`"策略，全程只刷一条最优路线。
- **最优混合路线 Bot**：允许在路线间切换的贪心/枚举策略（Core 期用手写候选集 + 枚举，不需要通用规划器）。

### 手段

```bash
simcore-cli bot-run --seed 20250910 --days 10 --strategy single-best  --out runs/single.json
simcore-cli bot-run --seed 20250910 --days 10 --strategy mixed-greedy --out runs/mixed.json
tools/dominance_check.py --single runs/single.json --mixed runs/mixed.json --threshold 1.25
```

### 附加判据（来自 S1 §8-2，P2 反重复感的量化形态）

- **边际收益非 flat**：固定种子 Bot 连跑 30 游戏日，单位 Isabel per(useful item) 的边际收益**至少 30% 相对下降**，否则判失败（"多跑一趟不能平恒单个店的单位收益"）。

### 阈值 / 频率 / 失败动作

| 项 | 值 |
|---|---|
| 通过阈值 | `dominance ≤ 1.25`；边际收益相对下降 `≥ 30%` |
| 频率 | 每次提交（CI，5 个 seed 均值）；完整 30 日版每周 |
| 失败动作 | 构建失败 + 输出两条路线的逐日净值曲线（定位是哪一日开始分叉） |

---

## 5. 验收 5 · 性能预算

> **原文**：GTX 1060 6GB / 16GB RAM / SATA SSD，1080p。场内模拟实体 ≤120（full-behavior ≤40，其余 coarse tick），**仿真（非渲染）耗时 ≤6ms/帧**。

> ⚠️ **口径冲突已记录**：概念文档写 `full-behavior ≤40`，任务硬约束与 S2 §8 取舍声明均为 **≤32**。**本清单按 32 执行**（ADR-000 F-8），待主理人确认。

### 判据（双口径，取严）

| # | 指标 | 阈值 | 测量点 |
|---|---|---|---|
| 5a | `max_per_frame`（单帧仿真墙钟） | **≤ 6.0 ms** | 每帧 |
| 5b | S2 `p95_per_tick` | **≤ 3.20 ms** | 每 `SIM_TICK` |
| 5c | S1 `p95_per_tick` | **≤ 1.20 ms** | 每 tick |
| 5d | S6 单日批处理 `p95` | **≤ 1.5 ms** | 每 `DAILY_CUTOVER` |
| 5e | 快进 8 游戏小时总墙钟 | **≤ 800 ms** | 每次 SLEEP |
| 5f | 场内实体数 | **≤ 120**（actor ≤ 77 + 容器/信号 ≤ 43） | 每 tick 断言 |
| 5g | `FULL` 并发数（含 PROTECTED） | **≤ 32** | 每 tick 断言 |
| 5h | `PROTECTED` 数 | **≤ 3** | 启动期断言 |
| 5i | 仿真侧内存 | **≤ 4 MB** | 每 60 tick 采样 |
| 5j | 存档写入 | **≤ 200 ms** | 每次 save |

### 手段

```bash
simcore-cli perf --scenario core-stress --seed 20250910 --days 3 \
                 --assert 'max_per_frame<=6.0' 's2.p95<=3.20' 's1.p95<=1.20' \
                          's6.daily.p95<=1.5' 'fastforward.8h<=800' \
                          'entities<=120' 'lod.full<=32' 'mem.sim<=4MB'
```
**压力场景 `core-stress`**（必须脚本化，固定）：玩家在两社区间往返、强制 32 个 FULL、触发一次日切、一次 DISPATCH、一次 8h 快进、一次 80% 清空 zone。

**渲染侧单独测**（不属于本验收，但必须测）：`tools/render_smoke.py --target 1060 --res 1080p`，记录总帧时间与渲染分项；GTX 1060 上目标 **≥30fps**，理想 60fps。

### 阈值 / 频率 / 失败动作

| 项 | 值 |
|---|---|
| 通过阈值 | 5a–5j **全部**成立 |
| 频率 | **每次提交**（CI 跑 headless 版 `core-stress`）；**每周**在真实 GTX 1060 机器上跑一次完整版 |
| 失败动作 | 构建失败 + 输出 per-system 分解（定位超支系统）→ **走预算复议，不得就地放宽**（S2 §8 原话：宁明示取舍，不假装装得下） |

---

## 6. 验收 6 · 可确定性（10 日差异 ≤5%）

> **原文**：同一存档 save/load 后续跑 10 日，安全天数曲线与原固定种子回放**差异 ≤5%**。
>
> S0 §C3 公式：`max_d |S_orig[d] − S_reload[d]| / max(S_orig[d], 1) ≤ 5%`，`d ∈ [d0, d0+10]`。理想值 0%；**LOD 近似是唯一合法差异来源**，且必须在调试面板可见。

### 判据

| # | 指标 | 阈值 |
|---|---|---|
| 6a | `max_d |ΔSafeDays| / max(S_orig[d],1)` | **≤ 5%** |
| 6b | `worldHash` 逐位比对（每 60 tick） | **逐位相同** |
| 6c | 全 COARSE 10 日 vs 全 FULL 10 日 | 差异 **≤5%**，且差异来源在面板「差异来源」可见 |
| 6d | `fastForward(8h)` vs 连续 `simTick × 480` | **逐位相同** |
| 6e | 恢复后首 tick | **必须是 `savedTick + 1`**（不跳号、不补跑） |
| 6f | 版本不一致（gameVersion / simSchemaVersion / streamIdRegistryVersion） | **拒绝重放 + 明确提示，不静默** |

### 手段

```bash
simcore-cli replay --seed 20250910 --days 10 --out runs/orig.jsonl
simcore-cli replay --seed 20250910 --days 10 --save-at 3 --load-and-continue --out runs/reload.jsonl
tools/determinism_diff.py --a runs/orig.jsonl --b runs/reload.jsonl --metric safeDays --threshold 0.05
# 附加
simcore-cli replay --seed 20250910 --days 10 --lod all-coarse --out runs/coarse.jsonl
simcore-cli replay --seed 20250910 --days 10 --lod all-full   --out runs/full.jsonl
simcore-cli replay --seed 20250910 --days 10 --ff-8h-vs-tick  # 6d
```

**必须纳入 `worldHash` 的量**（S0 §C3）：生理 + 库存 + 位置（量化 0.1m）+ 关系值。规范子集 ≈ **3.76 KB** → FNV1a ≈ 2–4 µs/次。

### 阈值 / 频率 / 失败动作

| 项 | 值 |
|---|---|
| 通过阈值 | 6a–6f **全部**成立 |
| 频率 | **每次提交**（CI，3 个 seed）；长跑（30 日）每周 |
| 失败动作 | 构建失败 + **首次 `worldHash` 不一致即中断并 dump 该 tick 全量状态**（S0 §C4）→ 定位是"真不确定"还是"LOD 数值精度"（后者走 ADR-003 R-B1/R-B2 量化收敛） |

---

## 7. 验收 7 · 契约必然生效

> **原文**：对话中的承诺若到期未兑现，3 游戏日内**必定**触发可观测负面后果（声望下降 + 某 NPC 后续对话提及该具体事件）。出不来这条，说明耦合没做通。

### 判据

| # | 子判据 | 机器/人工 |
|---|---|---|
| 7a | 承诺到期 → `DAILY_CUTOVER` 7.1 判定 `BREACHED` 且写 `reasonCode` | ✅ 机器 |
| 7b | 7.2 `release()` 释放 `reservedByPromise` | ✅ 机器 |
| 7c | 7.3 发 `PromiseBreached{entryId, dayKey, reasonCode}` | ✅ 机器 |
| 7d | **7.4 违约 delta 补批应用** → 进 `DaySnapshot[D]`（声望 **D+1 可见**，不是 D+2） | ✅ 机器 |
| 7e | 声望下降可观测：`ΔReputation < 0` 且分档发生变化 | ✅ 机器 |
| 7f | 某 NPC 后续对话提及**该具体事件**（文本引用该 `entryId` 或 `reasonCode`） | ⚠️ 半自动（文本 tag 检索 + 人工复核） |
| 7g | 全链 `AttributionLink.step ≤ 3`、`UNATTRIBUTED == 0` | ✅ 机器 |
| 7h | **物资侧延迟 = 1 日**：7.2 释放的预留物资在 **D+1 日切 3.3** 才回配给（S3 §7-E8） | ✅ 机器 |

### 手段

```bash
# 构造：让一个承诺在 D 日到期且必然违约
simcore-cli scenario --name promise-breach --seed 20250910 --breach-on-day 5
# 机器断言
simcore-cli assert-promise --day 5 --expect 'state==BREACHED' 'reasonCode!=null' \
            --expect 'reputation.delta<0 on D+1' 'ration.returnsOn D+1' \
            --expect 'attribution.step<=3' 'attribution.unattributed==0'
# 文本侧（7f）：对话文本按 entryId/reasonCode 打 tag，检索是否被引擎选中
simcore-cli dump-dialogue --from 6 --to 8 --grep 'breach:<entryId>' --out evidence/dialogue.json
```
**7f 的半自动判据**：对话文本资源中每条 breach 相关行带 `breach:<entryId>` tag；引擎侧记录"实际播放了哪些 tag"；两者交集非空即通过，**人工只复核交集里的文本是否真的提及了具体事件**（防"套话复用"）。

### 阈值 / 频率 / 失败动作

| 项 | 值 |
|---|---|
| 通过阈值 | 7a–7h **全部**成立；**3 游戏日内**必现（不是"可能"） |
| 频率 | 每次提交（CI 跑 7a–7e、7g、7h）；7f 每次对话内容更新后 |
| 失败动作 | 构建失败 + 输出违约链 `.md`；若 7d 缺失 → 检查日切九步是否被改序（**顺序不可调换**） |

---

## 8. 基础层门禁（七条之外，但七条全靠它们）

> 这些不直接是"验收"，但**任何一条破了，上面七条里至少一条会假通过**。必须同样进 CI。

| # | 门禁 | 判据 | 命令/手段 | 出处 |
|---|---|---|---|---|
| **B1** | 依赖方向 | `simcore` 无引擎头、无浮点；表现层无判定；业务系统无裸随机 | `rg` 静态检查（见 architecture.md §2.2） | ADR-001 R1 |
| **B2** | `d20` 唯一实现 | 全仓 `d20` 字面量命中数 **== 1** | `rg -c '\bd20\b' simcore/ --glob '!*test*'` | S0 §A8-① / ADR-002 D5 |
| **B3** | raw rng 调用数 = 0 | S2/S3/S4/S5/S6/S7 内 `pcg32_at\|makeSeedCtx` 命中数 **== 0**（只允许 `rollCheck`） | `rg` | S2 A7 / ADR-002 D5 |
| **B4** | 渲染帧禁判定 | `src/render` `src/ui` 内 `rollCheck\|makeSeedCtx` **== 0** | `rg` | S0 §C2-④ |
| **B5** | RNG 锚定 | `makeSeedCtx` 第二参永为 `e.absTick`，非当前 tick | `rg 'makeSeedCtx\([^,]+,\s*(now\|absTick\|tick)\)' simcore/` == 空 | ADR-003 I1 |
| **B6** | 判定只在 boundary | `behaviorTreeTick` / `closedFormAdvance` 内 `rollCheck` == 0 | `rg`（按 TU） | ADR-003 I3 |
| **B7** | 全局处理序 | 逐 tick 路径 ≡ 批量快进路径 | `--ff-8h-vs-tick` 逐位比对 | ADR-003 I2 |
| **B8** | 常量精确性 | S0 §D + S1 附录 A 全部常量 `round(v*1000) == v*1000` | 单测 `constants_test` | ADR-005 V1 |
| **B9** | 无浮点 / 无溢出 | `double\|float\|std::sin\|std::exp\|std::sqrt` == 空；30 日饱和计数 == 0 | `rg` + 长跑 | ADR-005 V3/V4 |
| **B10** | 归因闭合集 | `UNATTRIBUTED == 0`、`maxStep ≤ 3` | `assert-attribution` | ADR-004 V1/V2 |
| **B11** | 静态守恒 | 任一 zone `remainingByCluster` **逐日单调不增**（§5.3 两类 `zoneRegen` stub 除外） | `assert-static-conservation` | S1 §8-1 |
| **B12** | 可见信号 | 全量 `ScheduleChanged.visibleSignal` 非空率 **= 100%** 且可反查 | `assert-visible-signal`（违反时 `assert` 中断，**不降级**） | S2 A6 |
| **B13** | gossip 有界 | 链长 ≤3 跳；单 actor 单日入站 ≤2；`reachedSet` ≤7 | `assert-gossip-bounds` | S2 A5 |
| **B14** | 人口与 LOD 上限 | 有名 ≤16、匿名 ≤60、`FULL ≤ 32`，超限报警 | 每 tick 断言 | S2 A8 |
| **B15** | 写队列因果 | `enqueue` 时 `causeRef` 为空即断言失败 | 运行期断言 | ADR-004 D5 |
| **B16** | `lodStateOf` 已废弃 | 全仓 `lodStateOf` **== 0** | `rg` | C-11 |

---

## 9. 工具建设顺序（依赖关系）

七条验收里有 4 条全自动，**但这 4 条依赖的工具必须先建**。建议顺序：

```
阶段 0（阻塞一切）
  ├─ simcore L1：Fixed(Milli) · RngStream · EventBus+AttributionLog · WriteQueue · 时钟累加器
  └─ simcore-cli 骨架 + golden 测试（RNG 前 8 个输出向量 / 常量精确性）

阶段 1（解锁验收 6 与门禁 B1–B9）
  ├─ replay / --save-at / --lod / --ff-8h-vs-tick
  └─ worldHash + 断言门禁

阶段 2（解锁验收 1 与门禁 B10–B15）
  ├─ assert-attribution / export-attribution / 归因链检视器（面板 ⑥）
  └─ assert-visible-signal / assert-static-conservation / assert-gossip-bounds

阶段 3（解锁验收 4 / 5 与面板）
  ├─ bot-run + dominance_check
  ├─ perf + core-stress 场景
  └─ 24h 甘特（面板 ①）+ 性能面板（⑧）+ 种子控制（⑨）

阶段 4（解锁人工项 2 / 3）
  ├─ dump-actor-pos + readability_score（验收 2）
  └─ goal_probe + 自设目标编码表（验收 3）
```

---

## 10. 责任矩阵（建议，待主理人指派）

| 验收 | 自动化实现 | 人工执行 | 判据维护 |
|---|---|---|---|
| 1 三跳连锁 | 主程 | — | 主程 + 文策（闭合集） |
| 2 可读性 | 主程（工具） | **测试（严守真）** | 文策 + 主程 |
| 3 重复感 | 主程（埋点） | **测试（严守真）** | 文策 |
| 4 主导策略 | 主程 + Bot | — | 文策（数值） |
| 5 性能 | 主程（性能分析师） | — | 主程 |
| 6 确定性 | 主程 | — | 主程 |
| 7 契约生效 | 主程 | 文策（7f 文本复核） | 文策 + 主程 |
| 基础层门禁 | 主程 | — | 主程 |

---

## 11. 未完成 / 需先决条件

| 项 | 状态 |
|---|---|
| 验收 5 的 `full-behavior ≤40 vs ≤32` 口径 | ⏳ 待主理人确认（本清单按 **32** 执行） |
| 验收 2 / 3 的受试招募与场地 | ⏳ 未启动（Core 中段启动） |
| `ContainerDef` 完整字段、`WorldSignalRegistry` 字段级契约 | ⏳ 依赖 S8 区域内容 GDD |
| GTX 1060 真机 | ⏳ 需一台物理机或等效机型，用于每周完整版验收 5 |
| 对话文本 `breach:<entryId>` tag 规范（验收 7f） | ⏳ 需 S4 与文策确认后固化 |

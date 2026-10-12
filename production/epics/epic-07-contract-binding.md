# Epic 07 · 契约必然生效（Core 验收 7）

- **指向**：`game-concept.md §四-CORE 验收 7` ｜ `control-manifest.md §7`（7a–7h）｜ `04-dialogue-contract.md §8`（验收 1）｜ `S0 §B3.5`（日切 7.1–7.4）｜ `03-economy-barter.md §7-E8`（物资延迟 1 日）
- **一句话验收**：对话中的承诺若到期未兑现，**3 游戏日内必定**触发可观测负面后果（**声望下降** + 某 NPC 后续对话**提及该具体事件**）。出不来这条 → 说明耦合没做通。
- **涉及系统**：S4（对话/契约）· S5（账本/声望）· S3（预留/配给）· S2（记忆/gossip）· S0·B（日切 7.1–7.4 编排）· S0·C（归因）
- **涉及契约**：`LedgerEntry`（`OPEN/MET/BREACHED/VOID`）· `PromiseMade`/`PromiseBreached{entryId,dayKey,reasonCode}` · `ReserveEntry`（`reservedByPromise`：`PROMISED/FROZEN/FULFILLED/DEFAULTED/VOIDED`）· `ReputationDelta` · `GossipSeed` · `ImpressionAdded` · `DaySnapshot[D]`
- **⚠️ 防误并（C-23）**：`LedgerEntry.state` 与 `ReserveEntry.state` **以 `entryId` 外键关联，禁止合并为同一枚举**。
- **依赖**：Epic 00（E0-S5 export-attribution）· Epic 01（S2 记忆/gossip）· Epic 04（E4-S3 仓库/预留）
- **放行闸门**：**E7-S6**（对话 UI 显示文本）触碰表现层 → **需 1060 冒烟（闸门 B）**；其余纯仿真侧

---

## 目标与验收（`control-manifest §7`）

| # | 子判据 | 机器/人工 | Story |
|---|---|---|---|
| 7a | 承诺到期 → `DAILY_CUTOVER` 7.1 判定 `BREACHED` 且写 `reasonCode` | ✅ 机器 | E7-S2 |
| 7b | 7.2 `release()` 释放 `reservedByPromise` | ✅ 机器 | E7-S2 |
| 7c | 7.3 发 `PromiseBreached{entryId, dayKey, reasonCode}` | ✅ 机器 | E7-S2 |
| 7d | **7.4 违约 delta 补批应用** → 进 `DaySnapshot[D]`（声望 **D+1 可见**，非 D+2） | ✅ 机器 | E7-S2 |
| 7e | 声望下降可观测：`ΔReputation < 0` 且分档发生变化 | ✅ 机器 | E7-S3 |
| 7f | 某 NPC 后续对话提及**该具体事件**（引用该 `entryId` 或 `reasonCode`） | ⚠️ 半自动 | E7-S6 |
| 7g | 全链 `AttributionLink.step ≤ 3`、`UNATTRIBUTED == 0` | ✅ 机器 | E7-S4 |
| 7h | **物资侧延迟 = 1 日**：7.2 释放的预留物资在 **D+1 日切 3.3** 才回配给 | ✅ 机器 | E7-S5 |

> **通过阈值**：7a–7h **全部**成立；**3 游戏日内**必现（不是"可能"）。
> **失败动作**：构建失败 + 输出违约链 `.md`；若 7d 缺失 → 检查日切九步是否被改序（**顺序不可调换**）。

---

## Story 清单（按实现顺序）

### E7-S1 · S4 `LedgerEntry` + 承诺生命周期
- **判据（跑 Y 得 Z）**：`--dump-ledger` 打印 `LedgerEntry` 全量 + `reserved` 冻结明细 + 到期倒计时；`state ∈ {OPEN,MET,BREACHED,VOID}`；`--assert-dialogue-pure` 通过（S4 包内无 `tryTransfer`/`stock[`/`reputation =` 赋值；所有副作用以 `EffectExpr` 声明并出现在写队列日志）。
- **涉及契约**：`LedgerEntry` · `EffectExpr` · `PromiseMade` · `reputationHint`（本期返 0，字段照发）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E4-S3 ｜ **工作量**：3 人日 ｜ **可并行**：✓

### E7-S2 · 日切 7.1–7.4 违约四步（7a–7d）
- **判据（跑 Y 得 Z）**：承诺到期 → **7.1** 判 `BREACHED` + 写 `reasonCode`（闭集 8 值）→ **7.2** `release()` 释放 `reservedByPromise` → **7.3** 发 `PromiseBreached{entryId,dayKey,reasonCode}` → **7.4** 违约 delta 补批应用 → 进 `DaySnapshot[D]`（声望 **D+1 可见**）。**九步顺序不可调换。**
- **涉及契约**：`PromiseBreached` · `ReserveEntry` · `DaySnapshot[D]` · `WriteQueueItem.causeRef`（强制）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E7-S1 · E6-S2 ｜ **工作量**：3 人日 ｜ **可并行**：✓
- **备注**：改序 = 违约链断；`DEATH/NO_HEIR/BOTH_DEAD/TARGET_DEAD/TARGET_GONE` → `delta=0` 但**仍必发 gossip**（S4 §4.2）。

### E7-S3 · 声望 delta 可观测 + 分档（7e）
- **判据（跑 Y 得 Z）**：`--dump-reputation <cid> <D>` 显示 `ΔReputation < 0` 且**分档发生变化**；`ReputationDelta` 进 `DaySnapshot[D]`。
- **涉及契约**：`Reputation`（本期读 `DaySnapshot[D-1]`）· `ReputationDelta`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E7-S2 ｜ **工作量**：2 人日 ｜ **可并行**：✓

### E7-S4 · 违约链归因闭合（7g）
- **判据（跑 Y 得 Z）**：PromiseBreached 链 `S5.ledger → S5.reputation → S2.gossip → S2.memory`，全链 `AttributionLink.step ≤ 3`、`UNATTRIBUTED == 0`。
- **涉及契约**：`AttributionLink` · `GossipSeed`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E0-S5 · E7-S2 ｜ **工作量**：2 人日 ｜ **可并行**：✓（与 E7-S3/S5）

### E7-S5 · 物资侧延迟 1 日（7h）
- **判据（跑 Y 得 Z）**：7.2 释放的预留物资在 **D+1 日切 3.3** 才回配给；D 日 `effStock` 不含释放量；`--dump-ration` 证明 D 日 `shortfall` 仍存在（S3 §7-E8）。
- **涉及契约**：`Warehouse.reservedByPromise` · `effStock` · `RationGrant`/`RationIssued`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E7-S2 · E4-S3 ｜ **工作量**：2 人日 ｜ **可并行**：✓
- **备注**："代价由具体的人先付，再由玩家后收"——**刻意设计**，不是 bug。

### E7-S6 · 对话 breach tag 检索（7f，半自动）　【需 1060 冒烟：闸门 B】
- **判据（跑 Y 得 Z）**：对话文本每条 breach 相关行带 `breach:<entryId>` tag；`dump-dialogue --grep 'breach:<entryId>'` 输出**引擎实际播放的 tag** 与文本 tag 的**交集**（非空即通过）；**人工只复核**交集文本是否真的提及具体事件（防"套话复用"）；`--assert-impression-codes`（12 码 collapse 到 8 码闭集）。
- **涉及契约**：`ImpressionAdded` · `MemoryEvent` · 文本 tag 规范（待 S4/文策固化）
- **触碰 Godot 表现层**：**是（对话 UI 显示）** ｜ **需 1060 冒烟**：**是（闸门 B）**
- **前置**：E7-S2 · E2-S1 ｜ **工作量**：2 人日 ｜ **可并行**：✓
- **备注**：半自动 = 机器做 **tag 交集检索**，人工只做"是不是套话"的复核。

### E7-S7 · `scenario promise-breach` + `assert-promise` + CI 接线
- **判据（跑 Y 得 Z）**：`simcore_cli scenario --name promise-breach --seed 20250910 --breach-on-day 5` → `assert-promise --day 5 --expect 'state==BREACHED' 'reasonCode!=null' 'reputation.delta<0 on D+1' 'ration.returnsOn D+1' 'attribution.step<=3' 'attribution.unattributed==0'` 退出码 0；`ci.sh` 跑 7a–7e/7g/7h。
- **涉及契约**：`AttributionLink` · `LedgerEntry` · `ReserveEntry`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E7-S2..S5 ｜ **工作量**：2 人日 ｜ **可并行**：✗（收口）

---

## 依赖图（Epic 内）

```
E4-S3 ─▶ E7-S1 ─▶ E7-S2 ─┬─▶ E7-S3 ─┐
E6-S2 ───────────────────┼─▶ E7-S4 ─┼─▶ E7-S7（CI）
E0-S5 ───────────────────┼─▶ E7-S5 ─┤
E2-S1 ───────────────────└─▶ E7-S6（半自动·人工复核）
```

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 7f 只出"套话"（提及事件但泛泛） | tag 交集 + 人工复核"是否提及具体事件"（防套话复用） |
| 7d 缺失（声望 D+2 才可见） | 断言 **D+1 可见**；检查日切九步是否被改序（**顺序不可调换**） |
| `LedgerEntry`/`ReserveEntry` 枚举被误并 | `--assert-dialogue-pure` + C-23 外键关联断言 |
| 违约链断（`UNATTRIBUTED>0`） | `WriteQueueItem.causeRef` 强制（enqueue 断言）；派生效果继承上游 `effectRef` |
| 对话文本 tag 规范未固化 | 依赖 S4 与文策确认；固化前 7f **只能人工** |

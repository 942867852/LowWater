# Epic 04 · 主导策略检查（Core 验收 4）

- **指向**：`game-concept.md §四-CORE 验收 4` ｜ `control-manifest.md §4` ｜ `03-economy-barter.md §8`（A1–A8）｜ `01-scarcity-loop.md §8 验收 2/4`
- **一句话验收**：单一最优资源路线的 10 日物资净值，**不得超过最优混合路线的 1.25 倍**（固定种子 Bot 对跑测定）；且边际收益**相对下降 ≥30%**（不许"多跑一趟平恒收益"）。
- **涉及系统**：S3（易货经济，D3）· S1（稀缺，Bot 行动面）· S8（路程/风险 → `c_min`）
- **涉及契约**：`PriceTable` · `Warehouse` · `SafeDays` · `RationGrant` · `ReserveEntry` · `TradeResult`（价格影响仅展示预测）· `GapSignal`/`WorkOrder`（供 Bot 目标）
- **依赖**：Epic 01（S1/S5）· Epic 00（E0-S3/S5）
- **可并行**：价格/仓库（S3 三个 Story）互相依赖弱，可两两并行

---

## 目标与验收

```text
netValue(strategy) = Σ_cluster (获得的 Quantity.amount × DaySnapshot[D].price[cluster])   // FOOD 无价，按 WATER 等价折算
dominance = netValue(best_single_route) / netValue(best_mixed_route)
通过：dominance ≤ 1.25
附加：30 日内单位收益边际相对下降 ≥ 30%
```

**设计闭环要求（S3 A1/A2）**：`quotes` 收敛值 **≥ 0.5×c_min 且永不归零**；停手后 `--kill <water 岗>` 注入扰动 → 3 日内 `|Δ|` 回升 ≥20%。

---

## Story 清单

### E4-S1 · S3 五簇价格表 + 每日价格更新 + 快照纯净
- **判据（跑 Y 得 Z）**：`P(t+1) = P(t)·(1+0.25·(需求−库存)/(库存+ε))` clamp 至 `[0.6,1.8]×基准`；`--assert-snapshot-purity`：日切后第一笔 `quote` 的 `unitPrice == DaySnapshot[D].price`，当日多次 `quote` 返回同一价。
- **涉及契约**：`PriceTable`（5 键，**FOOD 不进**）· `DaySnapshot` · `PriceShock`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E1-S8 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E4-S3）
- **备注**：下游只读 `DaySnapshot[D-1]`，**不得直读实时值**。

### E4-S2 · S3 `quote`/`execTrade` + 保留价 + 议价 roll
- **判据（跑 Y 得 Z）**：成交价 `= P×[1+0.04×(卖家气场−买家气场)]×(1−0.03×议价等级)`，落双方保留价内**直接成交无骰**；区间外 → 破例 `rollCheck(PRESENCE,barter)`（幅度→DC 档，明示）；`--assert-no-currency` 通过（瓶盖/RU 不进支付路径）。
- **涉及契约**：`Quote`/`TradeResult` · `BARTER_OFFSET` · `rollCheck`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E4-S1 · E0-S6 ｜ **工作量**：3 人日 ｜ **可并行**：✓
- **备注**：`FOOD` 无价格牌；`safeDays<4` 且簇∈{WATER,FOOD} → 停止卖出。

### E4-S3 · S3 仓库 + `reserve/release/fulfill` + 配给
- **判据（跑 Y 得 Z）**：入仓**只经 `tryTransfer`**（全有或全无）；`--dump-warehouse` 打印 `stock(6键)/inbound/outbound/reservedByPromise`；`reserve→FROZEN（配给减少）→DEFAULTED→次日释放` 全链 `step≤3`、`UNATTRIBUTED==0`；自由路径**不能**绕过预留。
- **涉及契约**：`Warehouse` · `ReserveEntry`（`PROMISED/FROZEN/FULFILLED/DEFAULTED/VOIDED`，**禁止与 `LedgerEntry.state` 合并**）· `RationGrant`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E1-S8 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E4-S1/S2）
- **备注**：唯一写权 `Warehouse.stock` 经 S1 `tryTransfer`；解锁 Epic 07 的 `reservedByPromise`。

### E4-S4 · `bot-run`（single-best / mixed-greedy）
- **判据（跑 Y 得 Z）**：`simcore_cli bot-run --seed <N> --days 10 --strategy single-best --out runs/single.json` 与 `--strategy mixed-greedy` 各产出一份逐日净值；结果同 seed 逐位复现。
- **涉及契约**：Bot 只消费 `GapSignal`/`ContainerDef`/`PriceTable`（只读）；写入仍经 `tryTransfer`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E4-S1/S2/S3 · E1-S3 ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E4-S6）
- **备注**：Core 期用**手写候选集 + 枚举**，不需要通用规划器（`control-manifest §4`）。

### E4-S5 · `dominance_check` + 边际收益非 flat
- **判据（跑 Y 得 Z）**：`tools/dominance_check.py --single runs/single.json --mixed runs/mixed.json --threshold 1.25` 打印 `dominance` 并断言 ≤1.25；30 日版断言单位收益**相对下降 ≥30%**；失败时输出两条路线**逐日净值曲线**（定位哪天分叉）。
- **涉及契约**：无（分析脚本，消费 bot 产物）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E4-S4 ｜ **工作量**：2 人日 ｜ **可并行**：✓
- **备注**：CI 跑 5 seed 均值；完整 30 日版每周。

### E4-S6 · S8 路程常量 + `c_min/p_min` 有来源
- **判据（跑 Y 得 Z）**：断言主路 `pathLenMeters==2200`、强制遭遇点 ≥2 且 `hazardLevel≥1` → 重算 `c_min ≈ 0.059 > 0`（**往返口径**，裁决 38）；`p_min>0` 来自不可绕过的结构性风险。
- **涉及契约**：`ROAD_WINDING_MUL` · `TIME_VALUE_RU_PER_GAME_MIN` · `STAMINA_VALUE_RU_PER_POINT` · `ENCOUNTER_LOSS_RATE`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E1-S1 ｜ **工作量**：2 人日 ｜ **可并行**：✓
- **备注**：`c_min` 是 D3「收益收敛但永不归零」的数学前置。

### E4-S7 · CI 接线（5 seed 均值门禁）
- **判据（跑 Y 得 Z）**：`ci.sh` 增加一步 `dominance`：5 个 seed 的 `dominance` 均值 ≤1.25，否则退出码 1。
- **涉及契约**：无
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置**：E4-S5 ｜ **工作量**：1 人日 ｜ **可并行**：✗（收口）

---

## 依赖图（Epic 内）

```
E1-S8 ─┬─▶ E4-S1 ─▶ E4-S2 ─┐
       └─▶ E4-S3 ──────────┼─▶ E4-S4 ─▶ E4-S5 ─▶ E4-S7（CI）
E1-S1 ─▶ E4-S6 ───────────┘
E1-S3 ─▶ E4-S4
```

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 单一路线吃透（主导策略） | 有限物资池 + 不再生 + 迁移 claimant + 辐射累积（`game-concept §3.2`） |
| 收益归零（套利无意义） | `c_min>0` 来自**不可绕过的结构性风险**（E4-S6），玩法无法消除 |
| 价格递推漂移/溢出（R4） | `mulM/divM` 唯一实现 + 30 日常跑；兜底价格升 micro（×1e6）（ADR-005） |
| 快照被实时值污染 | `DaySnapshot` 不可变 + `--assert-snapshot-purity` |
| FOOD 误入价格路径 | `--assert-no-currency` + FOOD 不进 `PriceTable/quote/execTrade` 静态断言 |

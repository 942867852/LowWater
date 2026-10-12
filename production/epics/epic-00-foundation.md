# Epic 00 · 底座、工具链与 CI（enabler）

- **指向**：`control-manifest.md §8`（基础层门禁 B1–B16）· `§9`（工具建设顺序）· `architecture.md §2.2/§3/§9`
- **不是一条验收，而是七条验收的公共前提**：`control-manifest §0` 明示"4 条全自动验收依赖的工具必须先建"。
- **涉及系统**：S0·A/B/C/D/E（底座）；不含任何玩法系统。
- **涉及契约**：`AttributionLink` · `WriteQueueItem`(causeRef/sourceSystem) · `RefId` · `Milli`/mm/turn · `SimcoreVersion` 三元组 · `ViewSnapshot`/`DebugSnapshot`（调试侧）。
- **依赖**：无（`simcore` L1 已实现，见 `src/simcore/`）。
- **可并行**：Epic 00 全部 Story 与 Epic 01 的**设计梳理**可并行，但与 Epic 01 的实现存在 S0·A/工具的强前置。

---

## 目标与验收

| 验收 | 判据 | 指向 |
|---|---|---|
| A0-1 | `ci.sh` 一条命令跑完：静态门禁 → 零 warning 编译 → 全部用例 → 确定性重放 → 归因闭合 → 汇总退出码 | `control-manifest §8/§9` |
| A0-2 | 全游戏 `d20` 字面量命中数 == 1（去注释后） | `control-manifest B2` |
| A0-3 | 归因账本 `UNATTRIBUTED == 0` 且 `maxStep ≤ 3` 可一条命令断言 | `control-manifest B10 / ADR-004 V1/V2` |
| A0-4 | 新增一条 Story 级验收 = 一个函数 + 一行注册 + 一个 golden 文件（模板可跑） | 本 Epic `E0-S4` |
| A0-5 | `export-attribution` 产物头部含 `worldSeed/gameVersion/simSchemaVersion/streamIdRegistryVersion/dayKeyRange`（缺任一即无效证据） | `architecture §9.3` |

---

## Story 清单

### E0-S1 · CI 骨架（零 warning 门禁 + 静态门禁）　**✔ 已完成（PHASE4-003）**
- **判据（跑 Y 得 Z）**：`bash ci.sh` → 退出码 0，且打印 `B9/B1/B2/B3/B4/B16` 全 `[ ok ]`；任一静态门禁破坏或出现 `warning:` → 退出码 1。
- **涉及契约**：无（静态检查）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置 Story**：— ｜ **工作量**：1 人日 ｜ **可并行**：✓（与 E0-S2/S4）
- **证据**：`ci.sh` · `ci.bat`（本任务实跑：静态门禁 7 项 ok、编译干净、12 用例全过、verify/assert PASS、退出码 0）

### E0-S2 · `simcore-cli assert`（归因闭合校验）　**✔ 已完成（PHASE4-003）**
- **判据（跑 Y 得 Z）**：`./build/simcore_cli.exe assert --seed 20250910 --days 3` → 退出码 0 且打印 `unattributed=0 violations=0 maxStep≤3`；人为造断链 → 退出码 1。
- **涉及契约**：`AttributionLink` · `AttributionLog` · `RefId`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置 Story**：— ｜ **工作量**：1 人日 ｜ **可并行**：✓
- **证据**：`tools/simcore_cli.cpp::cmdAssert`；实跑输出 `links=366 unattributed=0 violations=0 maxStep=3`，`RESULT: PASS`

### E0-S3 · 确定性重放套件（`replay --save-at / --lod / --ff-8h-vs-tick`）
- **判据（跑 Y 得 Z）**：`simcore_cli replay --days 10 --save-at 3 --load-and-continue` 与参考逐位相同（退出码 0）；`--ff-8h-vs-tick` 逐位相同。**当前 `verify` 已覆盖"快进 ≡ 逐 tick"**，本 Story 补 `--save-at/--lod` 两种模式到 CLI。
- **涉及契约**：`SaveGame v1` 字段集 · `worldHash` · `StreamId` 注册表版本
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置 Story**：E0-S1 ｜ **工作量**：2 人日 ｜ **可并行**：✓（与 E0-S5/S6）
- **备注**：解锁验收 6（Epic 06）。

### E0-S4 · Story 级验收用例模板 + golden 约定　**✔ 已完成（PHASE4-003）**
- **判据（跑 Y 得 Z）**：按 `tests/STORY_CASE_TEMPLATE.md` 加一条用例 → `GOLDEN_UPDATE=1` 生成 `tests/golden/<caseId>.golden` → 复跑全 PASS；手改 golden 值 → 该用例 FAIL 并打印 `want/got + seed/days/mismatch`。
- **涉及契约**：无（测试脚手架）
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置 Story**：E0-S1 ｜ **工作量**：1 人日 ｜ **可并行**：✓
- **证据**：`tests/golden_io.h` · `tests/STORY_CASE_TEMPLATE.md` · `tests/golden/epic00__story_golden_sample.golden` · `tests/test_runner.cpp` 用例 12（实跑：命中 PASS；故意改 golden → `[FAIL] want=999 got=244`，恢复后 PASS）

### E0-S5 · `export-attribution`（.jsonl + .md 摘要）
- **判据（跑 Y 得 Z）**：`simcore_cli export-attribution --from 1 --to 3 --out evidence/` → 产出 `attribution.<seed>.<D0>-<D1>.jsonl`（一行一 link）+ `.md`（按 step 分组、闭合集命中高亮）；文件头 6 字段齐全。
- **涉及契约**：`AttributionLink` 全字段 · `RefId` · `SimcoreVersion`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置 Story**：E0-S2 ｜ **工作量**：2 人日 ｜ **可并行**：✓（与 E0-S6）
- **备注**：验收 1 的**举证产物**，Epic 01 依赖它。

### E0-S6 · S0·A `rollCheck` 唯一实现 + 4 属性 8 技能 + 派生量
- **判据（跑 Y 得 Z）**：固定 `(actor, attr, skill, dc, seedCtx)` → 结果逐位复现；`rg -c '\bd20\b' src/simcore`（去注释）== 1；`modTotal` 明细可导出。
- **涉及契约**：`rollCheck` · `CheckResult{success,margin,critFail}` · `SkillState` · `PhysioState`（读）· `CARRY_CAP/STAMINA_POOL/OBSERVE_RADIUS/BARTER_OFFSET`
- **触碰 Godot 表现层**：否 ｜ **需 1060 冒烟**：否
- **前置 Story**：— ｜ **工作量**：3 人日 ｜ **可并行**：✓（与 E0-S7）
- **备注**：`control-manifest B2`；S1/S2/S4 的唯一随机出口。**契约冻结**：判定式/DC 四档/修正表不得改。

### E0-S7 · 调试钩子骨架 + `DebugSnapshot`
- **判据（跑 Y 得 Z）**：`--tick-once` 单步推进 1 个 `absTick`；`--dump-lod` 打印每个 actor 的 `tickLevel`；`--dump-rng <streamId>` 打印最近 20 次取值；`--time-scale 30/60/120` 不改任何系统公式；`DebugSnapshot` 是 `ViewSnapshot` 的超集（含 `tickLevel`/`benefitSpec`/`resolvedBy`）。
- **涉及契约**：`DebugSnapshot`（超集）· `tickLevel` · `StreamId` 注册表
- **触碰 Godot 表现层**：否（headless dump）｜ **需 1060 冒烟**：否
- **前置 Story**：E0-S3 ｜ **工作量**：2 人日 ｜ **可并行**：✓
- **备注**：`view-contract §7` 明示 `tickLevel` 归 `DebugSnapshot`；5 个面板（E0/E2）依赖本 Story。

---

## 依赖图（Epic 内）

```
E0-S1 ─┬─▶ E0-S3 ─▶ E0-S7
       ├─▶ E0-S4
       └─▶ E0-S5
E0-S2 ─┴─▶ E0-S5
E0-S6（独立）
```

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 零 warning 门禁被"临时 pragma 压掉" | 门禁按**词**匹配 `warning:`；压警告视为回归（`build.sh` 已有先例 ≥0 warning） |
| 静态门禁误报（注释里的 `double/d20`） | 扫描前**去注释 + 去 `[CI-EXCLUDE]` 区块**（与 `build.sh` 同口径） |
| `d20` 命中数 0→2 漂移（新增判定） | `ci.sh` 每次提交断言 ==1；`control-manifest B2` |
| golden 基线被"顺手改绿" | 模板规定 golden 只准 `GOLDEN_UPDATE=1` 重生成，且需在回传说明"基线为何变" |

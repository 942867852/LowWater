# simcore · L1 确定性地基

> **状态**：L1 骨架完成（7 个头文件 + 10 条验证用例 + CLI）。
> **范围**：只做「确定性地基」——固定步长时钟、可分流无状态 RNG、milli 定点、
> 事件总线 / 归因链、跨系统写队列 + 昨日快照、最小世界 + LOD。
> **不做**（明确出界）：NPC 行为树、经济、对话、导航。

本目录是 **header-only** 库：实现全在头文件里，单个 `.cpp` 即可编译，
便于将来嵌入任意引擎层（ADR-001 方案 E：simcore 引擎无关化）。

```
src/simcore/
  simcore.h     汇总头 + 版本三元组 + selfCheck() + 裸随机 poison 钩子
  fixed.h       ADR-005 · milli 定点（Mul/Div/饱和/余数累加器）
  rng.h         ADR-002 · 可分流 RNG（pcg32_at 无状态纯函数）+ streamId 注册表
  clock.h       世界时钟与 tick 分级（固定步长累加器 + 日历）
  eventbus.h    ADR-004 · 同步事件总线 + AttributionLog（因果引用）
  writequeue.h  跨系统写队列 + 双缓冲昨日快照
  world.h       最小世界（LOD / tickOnce / fastForward / worldHash / 存档）
```

---

## 1. 怎么编

### 1.1 工具链现实约束（重要）

本机**只有** `C:\MinGW\bin\g++`（MinGW.org **GCC 6.3.0**）：
**没有 cmake / make / ninja / MSVC**，且**禁止联网装依赖**（无 Catch2 / GoogleTest / fmt）。
因此：

- 测试框架**自研**（`tests/test_runner.cpp` 里的 `CHECK_*` 宏 + 计数器 + 非零退出码）。
- **没有** `CMakeLists.txt`（写了也无法验证 = 假交付）。
- 语言按 **C++14 兼容子集**（`-std=c++14`）编写；**禁用** C++17/20 特性
  （无 `string_view`/`variant`/`optional`/`span`、无结构化绑定、无 `if constexpr`、
  无 `[[nodiscard]]`、无内联变量、无 `std::filesystem`、无 designated initializers）。
- **MSVCRT printf 口径**：GCC 6.3.0 把 `printf` 按 **MSVCRT** 语义做 `-Wformat` 检查，
  不认 `%llu/%llX/%zu`。本仓库 64 位量一律用 **`%I64u / %I64d / %I64X`**（MinGW 自有），
  32 位用 `%u/%d/%X`。这样 `-Wall -Wextra` 才能保持 **0 warning**。

### 1.2 构建

```bash
# Git Bash / Linux
./build.sh
```

```bat
rem cmd.exe
build.bat
```

或手工：

```bash
g++ -std=c++14 -O2 -Wall -Wextra -I src tests/test_runner.cpp -o build/test_runner.exe
g++ -std=c++14 -O2 -Wall -Wextra -I src tools/simcore_cli.cpp  -o build/simcore_cli.exe
```

> 目标：`-Wall -Wextra` 下 **0 warning**。出现任何 warning 视为回归。

---

## 2. 怎么跑

### 2.1 测试套件（10 条用例）

```bash
./build/test_runner.exe        # 退出码 0 = 全 PASS；非 0 = 有 FAIL
```

### 2.2 CLI

```bash
# 逐日 worldHash + 耗时
./build/simcore_cli.exe run --seed 20250910 --days 5 --mode tick
./build/simcore_cli.exe run --seed 20250910 --days 5 --mode fastforward

# 断言「快进 ≡ 逐 tick」逐位相同；相同退出码 0，不同非 0（可直接进 CI）
./build/simcore_cli.exe verify --seed 20250910 --days 5
```

---

## 3. 每个测试在验什么（10 条用例 → 契约）

| # | 用例 | 对应契约 | 判据 |
|---|---|---|---|
| 1 | RNG 分流独立性 / 无状态 / 乱序无关 / 跳步 | ADR-002 D1/D2/D3（V1/V2） | 同三元组 1000 次全等；乱序结果不变；`pcg32_at(s,n)==顺序推进 n 次` |
| 2 | 存档往返逐位相同 | ADR-002 D3/D6（V3）；S0 §C3 | 存档**不含任何 per-stream counter**；读档后继续 10 日 hash 逐位相同 |
| 3 | 定点无漂移 + 浮点漂移 | ADR-005（V1/V2/V4） | 定点终值精确；给出 vs double 最大偏差；演示 double 漂移 |
| 4 | 时间口径 | **裁决 27**；S0 §B3.1 | 1440 现实秒 → 恰好 1440 SIM_TICK；超 `MAX_STEPS_PER_FRAME` 不丢时间不跳步 |
| 5 | 视锥无关性 | **裁决 28**；ADR-003 D2 | 改相机 yaw → `worldHash` 与 `tickLevel` **完全不变**；改距离/事件 → 变 |
| 6 | **A-3 快进 ≡ 逐 tick** | S0 §B3.4 / ADR-003 D4（V5） | 两模式 worldHash 序列**逐位相同**（本项目最关键不变式） |
| 7 | 事件总线 / 归因链 | ADR-004 D2/D3/D4/D6 | 链可回放（`replayVerify`）；`step>3` 被检出并记违规；同系统不记；UNATTRIBUTED 不静默 |
| 8 | 写队列 + 昨日快照 | S0 已拍板 3；ADR-004 D5-1 | 当日写当日不可见 → 日切 `publish()` 后可见；应用序 = `(priority, actorId, seq)` |
| 9 | 性能实测 | ADR-003 D4 / §5 | 120 实体每 tick 平均耗时；8 游戏小时快进耗时（**如实测量，不要求达标**） |
| 10 | 定点取整对称性 | ADR-005 D1（修订）；PHASE3-L1-Q1 | golden 向量（484 mulM + 492 divM）全命中；`op(-x)==-op(x)`；±2.5/±1.5/±0.5 → ±3/±2/±1 |

### 关键常量（契约钉死，测试逐条断言）

```
TIME_SCALE            = 60    游戏秒 / 现实秒
SIM_TICK_COST         = 60    游戏秒 = 1 游戏分钟 → SIM_TICK = 1 Hz
MAX_STEPS_PER_FRAME   = 4     超出保留 acc，不丢时间、不跳步
1 游戏日 = 1440 游戏分钟 = 1440 现实秒 = 24 现实分钟
RNG: pcg32_at(seed64, absTick*4096 + callSeq)   无状态纯函数，存档不存 counter
tickLevel 只由「距离 + pending 事件 + PROTECTED」决定，相机朝向不得参与
LOD_FULL_MAX = 32
写队列排序键 (priority, actorId, seq)；下游读昨日快照
AttributionLink{ step, causeRef, effectRef, actorIds[], dayKey }；step>3 判违规，append-only
```

---

## 4. 发现的问题（关键）

### 4.1 已修复：`Clock::restoreAt` 的 off-by-one（会导致 save/load 不一致）

`clock.h` 原实现把日历位置设为 `lastExecutedAbsTick`，但其注释承诺
「恢复后首 tick == savedTick + 1」。由于 `advanceMs` 是**先回调 `onSimTick(absTick())`
再 `advanceOneMinute()`**，`absTick()` 的语义是「**下一个将执行的 tick**」。
把位置设为 `lastExecuted` 会让恢复后的首 tick **重复** `lastExecuted`（而非 +1），
读档后整条时间线错位一位 → 用例 2 的 240 个检查点全部不一致。

**修法**：位置设为 `lastExecutedAbsTick + 1`（见 `clock.h::restoreAt`）。
修复后用例 2 的 prefix/post mismatch 均为 0。这是 L1 中唯一一处「实现与契约不符」，
已就地修复，未绕过。

### 4.2 `world.h` 缺 `<cstdio>` 自包含 include

`world.h` 用了 `std::snprintf` 但未包含 `<cstdio>`，只是碰巧因 `simcore.h` 先吞了
`<cstdio>` 才编过。header-only 头应自包含，已补 `#include <cstdio>`。

### 4.3 已修复：`mulM` / `divM` 取整口径改为【对称取整】（PHASE3-L1-Q1 · 2025-09-11）

**原状**：`mulM` 照 ADR-005 字面实现 `((int64)a*b + 500) / 1000`，对**负积**是「向 +∞ 偏置」：
`mulM(-2500,1)` 旧值 `-2`，绝对值四舍五入期望 `-3`（正积因 C++ 除法向零截断而恰好正确，只有负侧漏）。
`divM` 的 `(a*1000 + b/2)/b` 在**除数有符号 / 异号**时同源偏置（`divM(-100000,3000)` 旧值 `-33332`，应为 `-33333`）。

**裁决**：改为 **round half away from zero（对称取整）**——负数按绝对值四舍五入后取回符号：
`+2.5 → +3`、`-2.5 → -3`。理由：非对称偏置在资源模拟里是**系统性漂移源**——经济系统存在借贷对称性
（S3 `reservedByPromise`、S4 DELIVER/违约 delta 都有正负两向），单向偏置会让长期账目持续漏损且难以归因。

**实现**：`mulM` / `divM` 均按 `|a·b|`（或 `|a·1000|`、`|b|`）做 half-away 舍入再取回符号，全整数运算、逐位确定；
恒有 `mulM(-a,b) == -mulM(a,b)`、`divM(-a,b) == -divM(a,b)`、`divM(a,-b) == -divM(a,b)`。
`roundM/floorM/ceilM` 本就分正负，未改；`divIntM`（纯整数截断，无 round 步骤）未改，**已标记待议**。

**连带**：ADR-005 D1 已同步更新（口径 + 原因 + 日期）；golden 向量按新口径**全部重算**
（`tests/golden_fixed_vectors.h`：mulM 484 条 / divM 492 条，生成器 `tools/gen_golden_fixed.py`）；
新增用例 10 逐条断言 golden 向量 + 符号对称 + ±2.5/±1.5/±0.5 边界。

### 4.4 已裁决：CI「无浮点」grep 限定【正式 TU】（PHASE3-L1-Q1 裁决）

ADR-005 V3 要求 `src/simcore/` 零浮点命中，但 `fixed.h` 末尾的**浮点参考镜像**
（`#ifdef SIMCORE_ENABLE_REFERENCE_FLOAT`）会被扫到。**裁决口径**：

1. **只统计未定义该宏的正式 TU**（`src/simcore` 下 `.h/.cpp`）；该宏仅在
   `tests/test_runner.cpp` 内、包含 simcore 头之前定义。
2. 参考镜像块**必须集中**在单个 `#ifdef` 区块内，首尾加显式标记注释
   `// [CI-EXCLUDE-BEGIN]` / `// [CI-EXCLUDE-END]`（见 `fixed.h`）。
3. CI 扫描前先剔除该区块，并剔除注释（注释里的 `double/float/std::sqrt` 是文档，不算代码）。

执行：`./build.sh check`（或随 `./build.sh` 一起跑），命中数必须为 0，否则门禁 FAIL。

---

## 5. 与契约的其余一致点（抽查）

- `TIME_SCALE/SIM_TICK_COST/MAX_STEPS_PER_FRAME/LOD_FULL_MAX/DISPATCH_MINUTE/CUTOVER_MINUTE`
  均与 S0 §B2/§D + 裁决 27/29 一致；`selfCheck()` 在 boot 期断言其中关键项。
- `pcg32_at` 的 `LCG_A/LCG_C` 与 XSH-RR 逐字取自 ADR-002 D1。
- `streamId` 静态可枚举（`enum class StreamId` + `kStreamName[]` + 版本号），
  红线②（不得把 tick/随机数/指针拼进 streamId）以 `deriveSeed64(worldSeed, id, param)`
  的接口形态约束（`param` 只应为确定性静态标识）。
- `AttributionLink` 字段集 = S0 §C2 契约 + 实现侧补充（`linkId/absTick/latencyDays`）。
- 写队列 `enqueue` 缺 `causeRef` 即断言拒绝（ADR-004 D5-1）。
- `worldHash` 不含 `renderLod / cameraYaw`（裁决 28 的代码级体现）。

---

## 6. 实测性能（本机）

**测试机 CPU**：`13th Gen Intel(R) Core(TM) i5-13400F`（`grep -m1 "model name" /proc/cpuinfo`）。
工具链 GCC 6.3.0 `-O2`；`steady_clock` 粒度约 1ms，故用足够长的积分窗口测量。

| 项 | 实测 | 架构文档推算（`architecture.md`/ADR-003 §5） |
|---|---|---|
| SIM_TICK 平均耗时（120 实体，10 游戏日 = 14400 tick） | **≈ 0.003125 ms/tick**（45.0 ms / 14400） | 单帧最坏 4.880ms、摊销 0.057ms/帧 |
| 8 游戏小时快进（480 tick，×200 取均值） | **≈ 0.77 ms/次** | 18.7ms / 上限 800ms |

> **口径说明（重要）**：L1 骨架**远快于**架构推算，因为架构预算是按"满配游戏"
> （77 actor 跑事件驱动行为树 + 逐小时结算 + 写队列 + 归因）估的，而 L1 只实现了
> 连续量积分 + boundary 判定 + 最小 LOD 抢占，**没有行为树 / 经济 / 导航**。
> 故本表数字只作"地基开销量级"参考，**不能**当作验收 5（≤6ms/帧）的达成证据——
> 那要等 S1/S2 实装后按可比的负载重测。

---

## 7. 未做的事（明确留痕）

- **未做 git 操作**（add/commit/push 一律未做，由主理人执行）。
- 未写 `CMakeLists.txt`（本机无 cmake，无法验证）。
- 未实现 NPC 行为树 / 经济 / 对话 / 导航（L1 出界）。
- 用例 8 未覆盖「`enqueue` 缺 causeRef 断言」路径：该路径在 debug（断言开启）下会
  `abort()`，与同进程跑其余用例冲突，故只在代码层保证、未在单测触发。

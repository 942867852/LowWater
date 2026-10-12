# Story 级验收用例接入模板（simcore 测试套件）

- **对象**：`tests/test_runner.cpp`（自研迷你框架，无第三方依赖）
- **配套**：`tests/golden_io.h`（golden 助手）· `tests/golden/`（golden 文件）· `ci.sh` / `ci.bat`
- **纪律**：C++14 兼容子集；`-Wall -Wextra` 下 **0 warning**；**禁 git 操作**。

---

## 0. 一句话

> 加一条 Story 级验收 = 一个 `static bool case_X()` 函数 + 注册一行 + 一个 golden 文件；
> **完成判据必须是「跑 Y 得到 Z」**，不是「实现了 X」。

---

## 1. 四条约定（照抄即可）

| # | 约定 | 值 |
|---|---|---|
| C1 | **命名** | 函数 `test<N>_<slug>()`；golden `caseId = "<epic-slug>__<story-slug>"`（双下划线分隔） |
| C2 | **seed** | 函数内 `const uint64_t seed = 20250910ULL;` —— **固定常量，禁运行时随机/系统时间** |
| C3 | **golden** | 路径 `tests/golden/<caseId>.golden`；格式 `key=value`（一行一条，`#` 注释） |
| C4 | **dump** | 失败时打印 `seed / days / mismatch`，并把 want/got 逐条打出来（`golden_io` 自动做） |

---

## 2. 加一条用例 · 三步

**第 1 步**：在 `tests/test_runner.cpp` 末尾（`main` 之前）加函数：

```cpp
// 用例 13 · <Epic id> <Story id> · <一句话判据（跑 Y 得到 Z）>
static bool test13_yourStory() {
    const uint64_t seed = 20250910ULL;      // C2：固定 seed
    const int days = 3;

    // ... 跑你的场景（走完整 tickOnce/fastForward 序列）...
    World w; EventBus b; AttributionLog a; WriteQueue q;
    w.init(seed, 120);
    for (int i = 0; i < days * 1440; ++i) w.tickOnce(1440 + (int64_t)i, &b, &a, &q);

    // ... 判据落成 golden 键值对 ...
    gold::Golden g("epic01__story_your_story");        // C1
    g.putHash("worldHash_d3_end", (unsigned long long)w.worldHash(1440 + (int64_t)(days * 1440) - 1));
    g.putU64 ("claim_displaced_count", (unsigned long long)a.size());
    g.putS   ("first_effect_ref", a.links().empty() ? "" : a.links()[0].effectRef.s);

    bool ok = true;
    const int mism = g.finish(&ok);
    CHECK_TRUE(ok, "13 <判据>");                        // C4
    if (!ok) std::printf("    [dump] seed=%I64u days=%d mismatch=%d\n",
                         (unsigned long long)seed, days, mism);
    return true;
}
```

**第 2 步**：在 `main()` 的用例清单里加一行：

```cpp
runCase("13 · <Epic id> <Story id> <短名>", test13_yourStory);
```

**第 3 步**：首次生成 golden（**唯一一次**）：

```bash
GOLDEN_UPDATE=1 ./build/ci/test_runner.exe   # 生成 tests/golden/epic01__story_your_story.golden
./build/ci/test_runner.exe                   # 复跑应 12+1 PASS
```

> golden 文件**必须一起提交**（它是判据本体）。**不要手改**——要改就 `GOLDEN_UPDATE=1` 重生成，
> 并在 Story 的 PR/回传里说明"为什么基线变了"。

---

## 3. 失败时 dump 什么（已内置）

`gold::Golden::finish()` 在比对失败时逐条打印：

```text
[golden] attribution_links        want=999 got=244
[FAIL] 13 <判据>  (line 866)
[dump] seed=20250910 days=2 mismatch=1
```

建议再补 **同源上下文**（便于定位是"真不确定"还是"行为变了"）：

| 场景 | 额外 dump |
|---|---|
| 确定性类 | 首次不一致的 `absTick` + 该 tick 全量 `worldHash` 输入 |
| 归因类 | 违规链 `.md`（`cause` → `effect` → `step`）+ `unattributedCount()` |
| 性能类 | per-system 分解 + p50/p95/max（不要只 dump 单次墙钟） |
| 场景类 | 触发脚本参数（seed / zone / ratio / days）+ 事件计数 |

---

## 4. 哪些判据**不能**进 golden（诚实边界）

| 类型 | 为什么不能 | 去处 |
|---|---|---|
| 墙钟耗时 | 机器相关，逐位不可复现 | 用**阈值断言**（`≤6ms/帧`），进 `--perf` |
| 人类测试（验收 2 / 3） | 测的是"人读不读得懂" | 盲测协议 + 评分工具，**不可代码替代** |
| 渲染帧时间（1060） | 需真机 GPU | `tools/gpu-smoke` **放行闸门**，见 `production/vertical-slice.md` |
| 未落地系统的行为 | gold 不能替实现 | 等对应 Epic Story 落地后**再**补 golden |

---

## 5. 与 CI 的关系

`ci.sh` / `ci.bat` 会：**编译（零 warning 门禁）→ 跑全部用例（含新旧 golden 比对）→ 确定性重放 → 归因闭合**。
新增用例**只要注册进 `main`** 就自动进 CI —— 不需要改 `ci.sh`。

> 若你的 Story 需要**新门禁**（例：`--assert-board-no-write`），把它加进 `ci.sh` 的静态门禁段，
> 并在对应 Epic 文档的「验收」里登记命令与期望退出码。

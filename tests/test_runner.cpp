// ============================================================================
// tests/test_runner.cpp · simcore L1 验证套件（10 条用例）
// ----------------------------------------------------------------------------
// 自研迷你测试框架（无第三方依赖）：CHECK 宏 + 计数器 + 失败时非零退出码。
// 语言约束：C++14 兼容子集（禁用 C++17/20 特性）。
// 工具链：MinGW GCC 6.3.0（C:\MinGW\bin\g++）。
//
// 编译：
//   g++ -std=c++14 -O2 -Wall -Wextra -I src tests/test_runner.cpp -o build/test_runner.exe
// 运行：
//   build/test_runner.exe        # 退出码 0 = 全 PASS
//
// 用例清单（与任务 PHASE3-L1 的 9 条一一对应 + PHASE3-L1-Q1 新增第 10 条）：
//   1 ADR-002 分流独立性 / 无状态 / 乱序无关 / 跳步正确性
//   2 ADR-002 存档往返（不存 per-stream counter）后继续 10 日逐位相同
//   3 ADR-005 定点无漂移（milli vs double 参考镜像）+ 浮点漂移演示
//   4 裁决 27 时间口径（1440 现实秒 = 1440 SIM_TICK；超限不丢时间不跳步）
//   5 裁决 28 视锥无关性（改相机朝向 → worldHash 不变；改距离/事件 → 变）
//   6 A-3 核心不变式：快进模式 ≡ 逐 tick 模式（worldHash 序列逐位相同）
//   7 事件总线：归因链可回放；step > 3 被检出
//   8 写队列：当日写不可见（读昨日快照）→ 日切发布后可见；排序键正确
//   9 性能实测（120 实体，FULL≤32）：每 tick 平均耗时 + 8 游戏小时快进耗时
//  10 ADR-005 D1（修订）mulM/divM 对称取整：golden 向量 + 符号对称 + ±2.5/±1.5/±0.5
// ============================================================================

// 打开 fixed.h 的「浮点参考镜像」（仅本测试 TU；simcore 正式构建仍零浮点）。
// 必须在包含 simcore 头之前定义。
#define SIMCORE_ENABLE_REFERENCE_FLOAT 1

// 先吞掉全部系统头，避免 simcore.h 末尾的 `rand/srand` poison 误伤。
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cerrno>
#include <vector>
#include <algorithm>
#include <chrono>

#include "simcore/simcore.h"

// 定点 golden 向量（自动生成，勿手改）：python tools/gen_golden_fixed.py
#include "golden_fixed_vectors.h"

using namespace simcore;

// ============================================================================
// 迷你框架
// ============================================================================
static long g_checks = 0;
static long g_fails  = 0;

#define CHECK_TRUE(cond, msg)                                                  \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_fails;                                                         \
            std::printf("    [FAIL] %s  (line %d)\n", (msg), __LINE__);        \
        }                                                                      \
    } while (0)

#define CHECK_EQ_U64(a, b, msg)                                                \
    do {                                                                       \
        ++g_checks;                                                            \
        const uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b);                 \
        if (_a != _b) {                                                        \
            ++g_fails;                                                         \
            std::printf("    [FAIL] %s: %016I64X != %016I64X  (line %d)\n",   \
                        (msg), (unsigned long long)_a, (unsigned long long)_b, \
                        __LINE__);                                             \
        }                                                                      \
    } while (0)

#define CHECK_EQ_I64(a, b, msg)                                                \
    do {                                                                       \
        ++g_checks;                                                            \
        const int64_t _a = (int64_t)(a), _b = (int64_t)(b);                    \
        if (_a != _b) {                                                        \
            ++g_fails;                                                         \
            std::printf("    [FAIL] %s: %I64d != %I64d  (line %d)\n", (msg),   \
                        (long long)_a, (long long)_b, __LINE__);               \
        }                                                                      \
    } while (0)

static void hex64(uint64_t v, char* out /*>=17*/) {
    std::snprintf(out, 17, "%016I64X", (unsigned long long)v);
}

static int g_casePass = 0;
static int g_caseFail = 0;
typedef bool (*TestFn)();

static void runCase(const char* name, TestFn fn) {
    const long before = g_fails;
    std::printf("CASE %s\n", name);
    fn();
    const bool pass = (g_fails == before);
    if (pass) { ++g_casePass; std::printf("  => PASS\n"); }
    else      { ++g_caseFail; std::printf("  => FAIL\n"); }
}

// ============================================================================
// 共用驱动工具
// ============================================================================
// 逐 tick 模式：用 Clock 的「1 现实秒 = 1 SIM_TICK」帧循环驱动（真实帧路径）。
// 返回本段最后一个被执行的 absTick。
static int64_t driveClockTicks(World& w, Clock& c, int nTicks,
                               std::vector<uint64_t>* hashEvery60,
                               EventBus& bus, AttributionLog& al, WriteQueue& wq) {
    int64_t lastAt = c.absTick();
    for (int i = 0; i < nTicks; ++i) {
        lastAt = 0;
        const int steps = c.advanceMs(1000, [&](int64_t t) {
            lastAt = t;
            w.tickOnce(t, &bus, &al, &wq);
        });
        if (steps != 1) { ++g_fails; std::printf("    [FAIL] advanceMs 未恰好推进 1 个 SIM_TICK\n"); }
        if (hashEvery60 && ((i + 1) % 60 == 0)) hashEvery60->push_back(w.worldHash(lastAt));
    }
    return lastAt;
}

// 快进模式：以 60 tick 为块调用 fastForward（与逐 tick 路径同一 absTick 序列）。
static void driveFastForward(World& w, int64_t startAbs, int nTicks, int chunk,
                             std::vector<uint64_t>* hashEvery60,
                             EventBus& bus, AttributionLog& al, WriteQueue& wq) {
    int done = 0;
    while (done < nTicks) {
        const int m = (nTicks - done < chunk) ? (nTicks - done) : chunk;
        w.fastForward(startAbs + (int64_t)done, m, chunk, &bus, &al, &wq);
        if (hashEvery60) hashEvery60->push_back(w.worldHash(startAbs + (int64_t)(done + m - 1)));
        done += m;
    }
}

// ============================================================================
// 用例 1 · ADR-002 分流独立性 / 无状态 / 乱序无关 / 跳步正确性
// ============================================================================
static bool test1_rngIndependence() {
    const uint64_t seed = 20250910ULL;

    // --- 1a 同一 (streamId, absTick, callSeq) → 同值（1000 次） ---------------
    {
        RngStream a(seed, StreamId::CheckPlayerScavenge, 7);
        const uint32_t v0 = a.at(1440, 0);
        bool allSame = true;
        for (int i = 0; i < 1000; ++i) if (a.at(1440, 0) != v0) allSame = false;
        CHECK_TRUE(allSame, "1a 同一三元组取值 1000 次全等");
        std::printf("    1a value(seed=%I64u, scavenge, tick=1440, seq=0) = %u\n",
                    (unsigned long long)seed, v0);
    }

    // --- 1b 不同 streamId 参数独立：先大量查询别的流，再取 A 仍同值 -----------
    {
        RngStream a(seed, StreamId::CheckPlayerScavenge, 7);
        RngStream b(seed, StreamId::CheckPlayerBarter, 7);
        RngStream c(seed, StreamId::CheckPlayerScavenge, 8);
        const uint32_t before = a.at(1440, 0);
        volatile uint32_t sink = 0;
        for (uint32_t i = 0; i < 64; ++i) {
            sink ^= b.at(1440, i);
            sink ^= c.at(1440, i);
            sink ^= rngRand(seed, StreamId::WildGlobal, 1440, i);
            sink ^= rngRand(seed, StreamId::SimBoundaryCheck, 1440, i, i);
        }
        (void)sink;
        CHECK_TRUE(a.at(1440, 0) == before, "1b 查询其它流后 A 的取值不变（分流独立）");
    }

    // --- 1c 乱序调用不影响结果（证明无状态） ---------------------------------
    {
        const uint16_t ids[8]  = {0, 1, 2, 8, 9, 10, 11, 12};
        const uint64_t tks[8]  = {1440, 1501, 2880, 4321, 1000000, 1440, 1501, 2880};
        const uint32_t sqs[8]  = {0, 3, 4095, 1, 0, 7, 2, 4095};
        uint32_t fwd[8], bwd[8];
        for (int i = 0; i < 8; ++i)
            fwd[i] = RngStream(seed, (StreamId)ids[i], 0).at(tks[i], sqs[i]);
        for (int i = 7; i >= 0; --i)
            bwd[i] = RngStream(seed, (StreamId)ids[i], 0).at(tks[i], sqs[i]);
        bool ok = true;
        for (int i = 0; i < 8; ++i) if (fwd[i] != bwd[i]) ok = false;
        CHECK_TRUE(ok, "1c 乱序调用结果与顺序无关（无状态）");
    }

    // --- 1d 跳步正确性 pcg32_at(s,n) == 顺序推进 n 次（ADR-002 V2） -----------
    {
        const uint64_t s = 0xDEADBEEF12345678ULL;
        const uint64_t ns[7] = {1, 2, 63, 64, 4095, 4096, 1000000};
        bool ok = true;
        for (int k = 0; k < 7; ++k) {
            uint64_t state = s;
            for (uint64_t i = 0; i < ns[k]; ++i) state = kLcgA * state + kLcgC;
            const uint32_t seq = pcg_xsh_rr(state);
            const uint32_t jmp = pcg32_at(s, ns[k]);
            if (seq != jmp) { ok = false; std::printf("    n=%I64u 顺序!=跳步\n", (unsigned long long)ns[k]); }
        }
        CHECK_TRUE(ok, "1d pcg32_at 跳步 == 顺序推进（n=1..1e6）");
    }

    // --- 1e callSeq × 4096 的 counter 构造（无跨流计数器） -------------------
    {
        RngStream a(seed, StreamId::CheckPlayerScavenge, 0);
        CHECK_EQ_U64(a.at(1440, 5), pcg32_at(a.seed64(), (uint64_t)1440 * 4096 + 5),
                     "1e counter = absTick*4096 + callSeq");
    }
    return true;
}

// ============================================================================
// 用例 2 · ADR-002 存档往返（不保存 per-stream counter）后继续 10 日逐位相同
// ============================================================================
static bool test2_saveLoadRoundTrip() {
    const uint64_t seed = 20250910ULL;
    const int warm = 3;     // 先跑 3 日并在此存档
    const int post = 10;    // 读档后继续 10 日

    // 参考：连续跑 warm+post 日，记录每 60 tick 的 worldHash
    std::vector<uint64_t> refH;
    {
        World w; EventBus b; AttributionLog a; WriteQueue q; Clock c;
        w.init(seed, 120); c.reset(1, 0);
        driveClockTicks(w, c, (warm + post) * 1440, &refH, b, a, q);
    }
    CHECK_EQ_I64((int64_t)refH.size(), (int64_t)(warm + post) * 24, "2 参考序列长度");

    // 分支 A：跑 warm 日 → 存档
    World wA; EventBus bA; AttributionLog aA; WriteQueue qA; Clock cA;
    wA.init(seed, 120); cA.reset(1, 0);
    std::vector<uint64_t> hA;
    const int64_t lastExecA = driveClockTicks(wA, cA, warm * 1440, &hA, bA, aA, qA);
    std::vector<uint8_t> blob;
    wA.saveTo(blob, lastExecA, (uint64_t)aA.size());
    std::printf("    2 存档字节数 = %d（不含任何 per-stream counter）\n", (int)blob.size());

    // 分支 B：读档 → 继续 post 日
    World wB; EventBus bB; AttributionLog aB; WriteQueue qB; Clock cB;
    int64_t gotLast = 0; uint64_t gotAttr = 0;
    const bool loaded = wB.loadFrom(blob, &gotLast, &gotAttr);
    CHECK_TRUE(loaded, "2 读档成功");
    CHECK_EQ_I64(gotLast, lastExecA, "2 存档 lastExecutedAbsTick 回读一致");
    cB.restoreAt(lastExecA);
    std::vector<uint64_t> hB;
    driveClockTicks(wB, cB, post * 1440, &hB, bB, aB, qB);

    // 比对：hA 应等于参考前缀；hB 应等于参考后缀
    int mismA = 0, mismB = 0;
    for (size_t i = 0; i < hA.size(); ++i) if (hA[i] != refH[i]) ++mismA;
    for (size_t i = 0; i < hB.size(); ++i) if (hB[i] != refH[(size_t)warm * 24 + i]) ++mismB;
    CHECK_EQ_I64(mismA, 0, "2 存档前序列与参考一致");
    CHECK_EQ_I64(mismB, 0, "2 读档后 10 日序列与参考逐位相同");

    char x1[17], x2[17], x3[17];
    hex64(hB.empty() ? 0 : hB[0], x1);
    hex64(refH[(size_t)warm * 24], x2);
    hex64(hB.empty() ? 0 : hB[hB.size() - 1], x3);
    std::printf("    2 读档后首块 %s == 参考 %s ；末块 %s\n", x1, x2, x3);
    std::printf("    2 逐位比对：prefix mism=%d, post mism=%d（共 %d 个检查点）\n",
                mismA, mismB, (int)(hA.size() + hB.size()));
    return true;
}

// ============================================================================
// 用例 3 · ADR-005 定点无漂移：milli vs double 参考镜像（同一累计序列 10 日）
// ============================================================================
static bool test3_fixedPointNoDrift() {
    // 只统计本用例的定点运算（selfCheck 里有一次故意的饱和探针，须先清零）
    fixedStatsReset();
    // 序列：水衰减 -8 点/游戏小时，逐分钟累加，跑 10 游戏日 = 14400 分钟。
    const Milli rate = SIMCORE_MILLI_INT(-8);   // -8000 milli
    const int minutes = 10 * 1440;

    MilliRateAccum fx(0, SIMCORE_MILLI_INT(100));   // 定点：初值 100.0
    ref::RateAccumRef rf;                            // 浮点：初值 100.0
    rf.value = 100.0;

    double maxAbsDevMilli = 0.0;
    for (int i = 0; i < minutes; ++i) {
        fx.addPerMinute(rate);             fx.takePerMinute();
        rf.addPerMinute(-8.0);             rf.takePerMinute();
        const double dev = (double)fx.value - rf.value * 1000.0;
        if (std::fabs(dev) > maxAbsDevMilli) maxAbsDevMilli = std::fabs(dev);
    }

    // 定点结果应精确等于整数运算的预期值：100 - 8 * 240 = -1820.0
    CHECK_EQ_I64(fx.value, SIMCORE_MILLI_INT(-1820),
                 "3 定点终值精确 = -1820.000（整数运算，无漂移）");
    std::printf("    3 定点终值 = %d milli (%.3f)；浮点参考 = %.9f；最大偏差 = %.6f milli\n",
                (int)fx.value, fx.value / 1000.0, rf.value, maxAbsDevMilli);
    std::printf("    3 定点零饱和：satAdd=%I64u satMul=%I64u divByZero=%I64u\n",
                (unsigned long long)fixedStats().satAdd,
                (unsigned long long)fixedStats().satMul,
                (unsigned long long)fixedStats().divByZero);

    // 浮点漂移演示：同样的「逐 step 加 0.1」序列，double 累加 ≠ 精确值
    double s = 0.0;
    for (int i = 0; i < 14400; ++i) s += 0.1;
    const bool drifted = (s != 1440.0);
    CHECK_TRUE(drifted, "3 浮点累加 0.1×14400 出现漂移（≠1440.0）");
    std::printf("    3 浮点漂移演示：Σ0.1×14400 = %.17f（精确值 1440.0，差 %.3e）\n",
                s, s - 1440.0);

    // 定点版本同序列应精确
    MilliL msum = 0;
    for (int i = 0; i < 14400; ++i) msum += 100;   // 0.1 * 1000
    CHECK_EQ_I64(msum, 1440000, "3 定点 Σ0.1×14400 精确 = 1440000 milli");
    return true;
}

// ============================================================================
// 用例 4 · 裁决 27 时间口径
// ============================================================================
static bool test4_timeSemantics() {
    // --- 4a 累计 1440 现实秒 → 恰好 1440 个 SIM_TICK -------------------------
    {
        Clock c; c.reset(1, 0);
        int64_t n = 0; int64_t lastAt = 0;
        for (int i = 0; i < 1440; ++i) {
            const int st = c.advanceMs(1000, [&](int64_t t) { ++n; lastAt = t; });
            if (st != 1) ++g_fails;
        }
        CHECK_EQ_I64(n, 1440, "4a 1440 现实秒 → 恰好 1440 个 SIM_TICK");
        CHECK_TRUE(c.ticksExecuted() == 1440, "4a ticksExecuted == 1440");
        CHECK_EQ_I64(c.dayKey(), 2, "4a 时钟推进到 dayKey=2");
        CHECK_EQ_I64(c.minuteOfDay(), 0, "4a minuteOfDay=0");
        CHECK_EQ_I64(lastAt, 1440 + 1440 - 1, "4a 最后一个 absTick = 2879");
    }

    // --- 4b MAX_STEPS_PER_FRAME 触发：不丢时间、不跳步 -----------------------
    {
        Clock c; c.reset(1, 0);
        std::vector<int64_t> ticks;
        auto cb = [&](int64_t t) { ticks.push_back(t); };
        // 一帧喂 10 现实秒（= 10 SIM_TICK 应发生），但单帧上限 4
        const int s1 = c.advanceMs(10000, cb);
        CHECK_EQ_I64(s1, MAX_STEPS_PER_FRAME, "4b 单帧最多执行 MAX_STEPS_PER_FRAME=4");
        // 用 0ms 帧排空累积的 acc（真实帧率暴跌场景）
        int guard = 0;
        while (c.acc() >= SIM_TICK_COST_MILLI && guard < 100) {
            c.advanceMs(0, cb);
            ++guard;
        }
        CHECK_EQ_I64((int64_t)ticks.size(), 10, "4b 总计执行 10 个 SIM_TICK（不丢时间）");
        bool consecutive = true;
        for (size_t i = 1; i < ticks.size(); ++i)
            if (ticks[i] != ticks[i - 1] + 1) consecutive = false;
        CHECK_TRUE(consecutive, "4b absTick 逐 1 递增（不跳步）");
        CHECK_TRUE(c.acc() < SIM_TICK_COST_MILLI, "4b 排空后 acc 归位");
        std::printf("    4b 累计 tick=%d，首 tick=%I64d，末 tick=%I64d，clampedFrames=%I64u\n",
                    (int)ticks.size(), (long long)ticks.front(), (long long)ticks.back(),
                    (unsigned long long)c.clampedFrames());
    }

    // --- 4c 一天 = 1440 现实秒 = 24 现实分钟（口径核算） ---------------------
    {
        CHECK_EQ_I64((int64_t)TIME_SCALE, 60, "4c TIME_SCALE = 60 游戏秒/现实秒");
        CHECK_EQ_I64((int64_t)SIM_TICK_COST_SEC, 60, "4c SIM_TICK_COST = 60 游戏秒 = 1 游戏分钟");
        CHECK_EQ_I64((int64_t)MAX_STEPS_PER_FRAME, 4, "4c MAX_STEPS_PER_FRAME = 4");
        CHECK_EQ_I64((int64_t)SIM_TICK_COST_MILLI, (int64_t)TIME_SCALE * 1000, "4c 1 现实秒 = 60000 毫游戏秒 = 1 SIM_TICK");
    }
    return true;
}

// ============================================================================
// 用例 5 · 裁决 28 视锥无关性
// ============================================================================
static bool test5_frustumIndependence() {
    const uint64_t seed = 20250910ULL;
    World w; w.init(seed, 120);
    const int64_t at = 1440;
    const uint64_t h0 = w.worldHash(at);
    const int32_t full0 = w.fullCount();

    // 5a 反复改动相机朝向（表现层输入）→ worldHash 完全不变
    const uint16_t yaws[6] = {0, 8192, 16384, 32768, 49152, 65535};
    bool yawStable = true;
    for (int i = 0; i < 6; ++i) {
        w.setCameraYawTurn(yaws[i]);
        if (w.worldHash(at) != h0) yawStable = false;
    }
    CHECK_TRUE(yawStable, "5a 改 yaw（表现层输入）→ worldHash 完全不变");
    // 但 renderLOD 确实被相机影响（证明输入生效、只是不影响仿真）
    bool renderChanged = false;
    {
        const RenderLod r0 = w.renderLodOf(0);
        w.setCameraYawTurn(0);      const RenderLod rA = w.renderLodOf(0);
        w.setCameraYawTurn(32768);  const RenderLod rB = w.renderLodOf(0);
        if (rA != rB) renderChanged = true;
        (void)r0;
    }
    std::printf("    5a yaw 变→hash 不变；renderLOD 受相机影响 = %s\n",
                renderChanged ? "是（符合预期）" : "否（本帧恰好无差别）");

    // 5b 改动距离（玩家位置）→ simLOD/完整度变化 → hash 变
    w.setPlayerPos(500000, 0, 0);   // 玩家远离 500m → 非 PROTECTED 全部降为 COARSE
    const uint64_t h1 = w.worldHash(at);
    const int32_t full1 = w.fullCount();
    CHECK_TRUE(h1 != h0, "5b 改距离 → worldHash 变");
    CHECK_TRUE(full1 != full0, "5b 改距离 → FULL 名额数变");
    std::printf("    5b 距离变化：fullCount %d → %d，hash %s → 变\n", full0, full1,
                h1 != h0 ? "已变" : "未变");

    // 5c 改动事件/状态 → hash 变（直接改一个实体的生理值，等价于事件效果）
    World w2; w2.init(seed, 120);
    const uint64_t g0 = w2.worldHash(at);
    w2.mutableEntity(5).needWater = SIMCORE_MILLI_INT(3);
    const uint64_t g1 = w2.worldHash(at);
    CHECK_TRUE(g1 != g0, "5c 改事件/状态 → worldHash 变");

    // 5d tickLevel 只由距离与 pending 事件决定（相机朝向不得参与）
    World w3; w3.init(seed, 120);
    std::vector<uint8_t> lvlBefore, lvlAfter;
    for (size_t i = 0; i < 120; ++i) lvlBefore.push_back((uint8_t)w3.tickLevelOf((uint32_t)i));
    w3.setCameraYawTurn(42424);
    for (size_t i = 0; i < 120; ++i) lvlAfter.push_back((uint8_t)w3.tickLevelOf((uint32_t)i));
    CHECK_TRUE(lvlBefore == lvlAfter, "5d 改相机朝向 → tickLevel 序列完全不变");
    return true;
}

// ============================================================================
// 用例 6 · A-3 核心不变式：快进模式 ≡ 逐 tick 模式（逐位相同）
// ============================================================================
static bool test6_fastForwardEqualsTick() {
    const uint64_t seed = 20250910ULL;
    const int days = 5;
    const int total = days * 1440;
    const int64_t start = 1440;   // day1 00:00

    std::vector<uint64_t> tickSeq, fastSeq;

    // 逐 tick 模式（Clock 帧循环）
    {
        World w; EventBus b; AttributionLog a; WriteQueue q; Clock c;
        w.init(seed, 120); c.reset(1, 0);
        driveClockTicks(w, c, total, &tickSeq, b, a, q);
        CHECK_EQ_I64((int64_t)tickSeq.size(), (int64_t)days * 24, "6 逐 tick 序列长度");
    }
    // 快进模式（fastForward）
    {
        World w; EventBus b; AttributionLog a; WriteQueue q;
        w.init(seed, 120);
        driveFastForward(w, start, total, 60, &fastSeq, b, a, q);
        CHECK_EQ_I64((int64_t)fastSeq.size(), (int64_t)days * 24, "6 快进序列长度");
    }

    // 逐位比对
    int mism = 0;
    const size_t n = (tickSeq.size() < fastSeq.size()) ? tickSeq.size() : fastSeq.size();
    for (size_t i = 0; i < n; ++i) if (tickSeq[i] != fastSeq[i]) ++mism;
    CHECK_EQ_I64(mism, 0, "6 快进 ≡ 逐 tick（worldHash 序列逐位相同）");
    CHECK_TRUE(tickSeq.size() == fastSeq.size(), "6 两侧序列长度相同");

    // 打印两侧序列前几个供人工核对
    std::printf("    6 逐 tick 序列(前6):");
    for (size_t i = 0; i < 6 && i < tickSeq.size(); ++i) { char x[17]; hex64(tickSeq[i], x); std::printf(" %s", x + 8); }
    std::printf("\n    6 快进  序列(前6):");
    for (size_t i = 0; i < 6 && i < fastSeq.size(); ++i) { char x[17]; hex64(fastSeq[i], x); std::printf(" %s", x + 8); }
    std::printf("\n    6 逐位比对 mism=%d / %d\n", mism, (int)n);
    return true;
}

// ============================================================================
// 用例 7 · 事件总线：归因链可回放；step > 3 被检出
// ============================================================================
static bool test7_attribution() {
    // 7a 构造一条 S1→S2→S3→S4→S5 的链（step 应为 1,2,3,4，第 4 跳违规）
    AttributionLog log;
    const uint32_t acts[1] = {3u};
    const uint64_t tick = 1440;
    const int32_t day = 1;

    const bool r1 = log.record(RefId("S1.zone.ruins_a.ctr_12.remaining"),
                               RefId("S2.actor.3.need"), acts, 1, tick, day);
    const bool r2 = log.record(RefId("S2.actor.3.need"),
                               RefId("S3.warehouse.he_valley.water"), acts, 1, tick, day);
    const bool r3 = log.record(RefId("S3.warehouse.he_valley.water"),
                               RefId("S4.ledger.e1.state"), acts, 1, tick, day);
    const bool r4 = log.record(RefId("S4.ledger.e1.state"),
                               RefId("S5.community.he_valley.safeDays"), acts, 1, tick, day);
    CHECK_TRUE(r1 && r2 && r3 && r4, "7a 跨系统链路全部被记录");
    CHECK_EQ_I64((int)log.links()[0].step, 1, "7a 第 1 跳 step=1");
    CHECK_EQ_I64((int)log.links()[1].step, 2, "7a 第 2 跳 step=2");
    CHECK_EQ_I64((int)log.links()[2].step, 3, "7a 第 3 跳 step=3");
    CHECK_EQ_I64((int)log.links()[3].step, 4, "7a 第 4 跳 step=4（违规）");
    CHECK_TRUE(log.links()[3].violation, "7a step>3 被标记 violation");
    CHECK_TRUE(log.violations().size() >= 1, "7a 违规被计入评审列表");
    std::printf("    7a 链长=%d，检出违规数=%d（step=%d）\n",
                (int)log.size(), (int)log.violations().size(),
                log.violations().empty() ? -1 : (int)log.violations()[0].step);

    // 7b 同系统不记（规则 A-BOUNDARY）
    const size_t before = log.size();
    const bool same = log.record(RefId("S2.actor.3.need"), RefId("S2.actor.3.schedule"), acts, 1, tick, day);
    CHECK_TRUE(!same, "7b 同系统调用返回 false（不记录）");
    CHECK_EQ_I64((int64_t)log.size(), (int64_t)before, "7b 同系统链条不增长");

    // 7c UNATTRIBUTED（无 causeRef）不静默
    {
        AttributionLog l2;
        const bool u = l2.record(RefId(""), RefId("S5.community.he_valley.safeDays"), acts, 1, tick, day);
        CHECK_TRUE(u, "7c 无 causeRef → 记为 UNATTRIBUTED（不静默）");
        CHECK_EQ_I64((int64_t)l2.unattributedCount(), 1, "7c UNATTRIBUTED 计数=1");
    }

    // 7d 链可回放：同一序列跑两遍 → replayVerify 通过
    {
        AttributionLog a, b;
        const char* causes[4]  = {"S1.zone.ruins_a.ctr_12.remaining", "S2.actor.3.need",
                                  "S3.warehouse.he_valley.water", "S4.ledger.e1.state"};
        const char* effects[4] = {"S2.actor.3.need", "S3.warehouse.he_valley.water",
                                  "S4.ledger.e1.state", "S5.community.he_valley.safeDays"};
        for (int i = 0; i < 4; ++i) {
            a.record(RefId(causes[i]), RefId(effects[i]), acts, 1, tick, day);
            b.record(RefId(causes[i]), RefId(effects[i]), acts, 1, tick, day);
        }
        CHECK_TRUE(AttributionLog::replayVerify(a.links(), b.links()), "7d 归因链可回放（逐条一致）");
    }
    return true;
}

// ============================================================================
// 用例 8 · 写队列：当日写不可见 → 日切发布后可见；排序键正确
// ============================================================================
static bool test8_writeQueue() {
    // 8a 当日写入不影响当日读数（读昨日快照）
    {
        SnapshotStore ss;
        const RefId k("S3.warehouse.he_valley.water");
        CHECK_EQ_I64(ss.read(k), 0, "8a 初始读昨日快照 = 0");
        ss.writeCur(k, SIMCORE_MILLI_INT(5));
        CHECK_EQ_I64(ss.read(k), 0, "8a 当日写入后下游仍读到旧值（当日不可见）");
        ss.writeCur(k, SIMCORE_MILLI_INT(9));
        CHECK_EQ_I64(ss.read(k), 0, "8a 当日多次写入仍不可见");
        ss.publish();   // 日切
        CHECK_EQ_I64(ss.read(k), SIMCORE_MILLI_INT(9), "8a 日切发布后下游读到新值");
    }

    // 8b 排序键 (priority, actorId, seq)
    {
        WriteQueue q;
        const RefId target("S3.warehouse.he_valley.water");
        const RefId cause("S2.actor.3.need");
        EventPayload p; std::memset(&p, 0, sizeof(p));
        // 入队顺序故意打乱：(prio,actor) = (3,10)(1,20)(2,5)(1,7)
        q.enqueue(3, 10, 1, SystemId::S3, target, cause, 0, p);
        q.enqueue(1, 20, 1, SystemId::S3, target, cause, 0, p);
        q.enqueue(2,  5, 1, SystemId::S3, target, cause, 0, p);
        q.enqueue(1,  7, 1, SystemId::S3, target, cause, 0, p);
        CHECK_EQ_I64((int64_t)q.pendingCount(), 4, "8b 当日 4 条入队");
        std::vector<int32_t> prioOrder;
        std::vector<uint32_t> actorOrder;
        q.applyAll([&](const WriteQueueItem& it) {
            prioOrder.push_back(it.priority);
            actorOrder.push_back(it.actorId);
        });
        const bool okOrder = (prioOrder.size() == 4 &&
                              prioOrder[0] == 1 && prioOrder[1] == 1 &&
                              prioOrder[2] == 2 && prioOrder[3] == 3 &&
                              actorOrder[0] == 7 && actorOrder[1] == 20);
        CHECK_TRUE(okOrder, "8b 应用顺序 = (priority, actorId, seq) 升序");
        CHECK_EQ_I64((int64_t)q.pendingCount(), 0, "8b 应用后队列清空");
        CHECK_EQ_I64((int64_t)q.appliedTotal(), 4, "8b 累计应用 4 条");
        std::printf("    8b 应用序 priority = [%d %d %d %d]，actorId = [%u %u %u %u]\n",
                    prioOrder[0], prioOrder[1], prioOrder[2], prioOrder[3],
                    actorOrder[0], actorOrder[1], actorOrder[2], actorOrder[3]);
    }
    return true;
}

// ============================================================================
// 用例 9 · 性能实测（120 实体，FULL≤32）
// ============================================================================
static bool test9_performance() {
    const uint64_t seed = 20250910ULL;
    typedef std::chrono::steady_clock Clk;

    const char* cpu = std::getenv("PROCESSOR_IDENTIFIER");
    std::printf("    9 CPU = %s\n", cpu ? cpu : "unknown(i5-13400F)");

    // 9a 每 SIM_TICK 平均耗时：跑 10 游戏日 = 14400 tick
    //    （本机 steady_clock 粒度约 1ms，故用足够长的积分窗口，避免单日 3~4ms 撞粒度）
    {
        const int totalTicks = 10 * 1440;
        World w; EventBus b; AttributionLog a; WriteQueue q;
        w.init(seed, 120);
        for (int i = 0; i < 120; ++i) w.tickOnce(1440 + i, &b, &a, &q);   // 预热
        const Clk::time_point t0 = Clk::now();
        for (int i = 0; i < totalTicks; ++i) w.tickOnce(1440 + 120 + i, &b, &a, &q);
        const Clk::time_point t1 = Clk::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::printf("    9a 120 实体 × %d tick = %.3f ms 总；平均 %.6f ms/tick；FULL=%d\n",
                    totalTicks, ms, ms / totalTicks, w.fullCount());
        std::printf("    9a 架构文档推算基线：单帧最坏 4.880ms、摊销 0.057ms/帧\n");
    }

    // 9b 8 游戏小时快进（480 tick）：批量跑 reps 次（每次全新世界），总耗时/次数
    {
        const int reps = 200;
        uint64_t sink = 0;
        const Clk::time_point t0 = Clk::now();
        for (int r = 0; r < reps; ++r) {
            World w; EventBus b; AttributionLog a; WriteQueue q;
            w.init(seed, 120);
            w.fastForward(1440, 480, 60, &b, &a, &q);   // 8 游戏小时
            sink ^= w.worldHash(1440 + 480);            // 消费终态，防 DCE
        }
        const Clk::time_point t1 = Clk::now();
        volatile uint64_t vs = sink; (void)vs;
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::printf("    9b 8 游戏小时（480 tick）快进 ×%d = %.3f ms 总；平均 %.3f ms/次（架构推算 18.7ms / 上限 800ms）\n",
                    reps, ms, ms / reps);
    }
    return true;
}

// ============================================================================
// 用例 10 · ADR-005 D1（修订）· mulM/divM 对称取整 + golden 向量 + 舍入边界
// ----------------------------------------------------------------------------
// 契约：ADR-005 D1（PHASE3-L1-Q1 裁决）—— round half away from zero（对称取整）。
//   ① golden 向量逐条命中（全符号组合 + 半边界，由 tools/gen_golden_fixed.py 生成）
//   ② mulM(-a,b) == -mulM(a,b)、divM(-a,b) == -divM(a,b)、divM(a,-b) == -divM(a,b)
//   ③ ±2.5 / ±1.5 / ±0.5 三个舍入边界逐位验证（−2.5→−3、+2.5→+3）
// ============================================================================
static bool test10_symmetricRounding() {
    // --- 10a golden mulM 向量逐条命中（生成于新口径） -----------------------
    int mulBad = 0;
    for (int i = 0; i < golden::kMulVectorCount; ++i) {
        const golden::FixedVec& v = golden::kMulVectors[i];
        if (mulM(v.a, v.b) != v.expect) {
            ++mulBad;
            if (mulBad <= 5)
                std::printf("    [FAIL] mulM(%d,%d)=%d 期望 %d\n",
                            (int)v.a, (int)v.b, (int)mulM(v.a, v.b), (int)v.expect);
        }
    }
    CHECK_EQ_I64(mulBad, 0, "10a golden mulM 向量全部命中");
    std::printf("    10a golden mulM 向量：%d 条，未命中 %d 条\n",
                golden::kMulVectorCount, mulBad);

    // --- 10b golden divM 向量逐条命中（含除数有符号 / 异号） -----------------
    int divBad = 0;
    for (int i = 0; i < golden::kDivVectorCount; ++i) {
        const golden::FixedVec& v = golden::kDivVectors[i];
        if (divM(v.a, v.b) != v.expect) {
            ++divBad;
            if (divBad <= 5)
                std::printf("    [FAIL] divM(%d,%d)=%d 期望 %d\n",
                            (int)v.a, (int)v.b, (int)divM(v.a, v.b), (int)v.expect);
        }
    }
    CHECK_EQ_I64(divBad, 0, "10b golden divM 向量全部命中");
    std::printf("    10b golden divM 向量：%d 条，未命中 %d 条\n",
                golden::kDivVectorCount, divBad);

    // --- 10c 符号对称性：op(-x) == -op(x)（对所有测试向量成立） --------------
    int symMul = 0, symDiv = 0;
    for (int i = 0; i < golden::kMulVectorCount; ++i) {
        const golden::FixedVec& v = golden::kMulVectors[i];
        if (mulM(-v.a, v.b) != -mulM(v.a, v.b)) ++symMul;   // 取反 a
        if (mulM(v.a, -v.b) != -mulM(v.a, v.b)) ++symMul;   // 取反 b
    }
    for (int i = 0; i < golden::kDivVectorCount; ++i) {
        const golden::FixedVec& v = golden::kDivVectors[i];
        if (divM(-v.a, v.b) != -divM(v.a, v.b)) ++symDiv;   // 取反分子
        if (divM(v.a, -v.b) != -divM(v.a, v.b)) ++symDiv;   // 取反分母（有符号）
    }
    CHECK_EQ_I64(symMul, 0, "10c mulM(-a,b) == -mulM(a,b) 对全部向量成立");
    CHECK_EQ_I64(symDiv, 0, "10c divM(-a,b) == -divM(a,b) 且 divM(a,-b) == -divM(a,b)");
    std::printf("    10c 对称性违例：mulM=%d, divM=%d（跨 %d+%d 条向量）\n",
                symMul, symDiv, golden::kMulVectorCount, golden::kDivVectorCount);

    // --- 10d 三个舍入边界值逐位验证（真值 ±2.5 / ±1.5 / ±0.5） ---------------
    //   构造：乘数 b = 1（milli），使 a×b/1000 恰好落在 x.5 上（a = 2500/1500/500）。
    //   旧口径下负分支得 -2 / -1 / 0（向 +∞ 偏置）；新口径应为 -3 / -2 / -1。
    {
        const Milli b = 1;
        const Milli av[3] = { SIMCORE_MILLI_PARTS(2, 500),   // 2.5 → 商 2.5
                              SIMCORE_MILLI_PARTS(1, 500),   // 1.5 → 商 1.5
                              SIMCORE_MILLI_PARTS(0, 500) };  // 0.5 → 商 0.5
        const Milli expPos[3] = { 3, 2, 1 };
        const Milli expNeg[3] = { -3, -2, -1 };
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            const Milli p = mulM(av[i], b);
            const Milli n = mulM(-av[i], b);
            if (p != expPos[i] || n != expNeg[i] || n != -p) ok = false;
            std::printf("    10d mulM(%+d,1)=%+d (=%+.3f)   mulM(%+d,1)=%+d (对称)\n",
                        (int)av[i], (int)p, p / 1000.0, (int)(-av[i]), (int)n);
        }
        CHECK_TRUE(ok, "10d ±2.5/±1.5/±0.5 → ±3/±2/±1（逐位，且 op(-x)==-op(x)）");
    }
    return true;
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::printf("================================================================\n");
    std::printf(" simcore L1 验证套件（10 条用例）\n");
    std::printf(" 版本: gameVersion=%s simSchema=%u streamRegistry=%u\n",
                simcoreVersion().gameVersion,
                (unsigned)simcoreVersion().simSchemaVersion,
                (unsigned)simcoreVersion().streamIdRegistryVersion);
    std::printf("================================================================\n");

    selfCheck();   // 契约级不变式（失败即 assert）

    runCase("1 · ADR-002 分流独立性/无状态/乱序无关/跳步", test1_rngIndependence);
    runCase("2 · ADR-002 存档往返逐位相同（10 日）",        test2_saveLoadRoundTrip);
    runCase("3 · ADR-005 定点无漂移 + 浮点漂移演示",         test3_fixedPointNoDrift);
    runCase("4 · 裁决 27 时间口径（1440s = 1440 tick）",     test4_timeSemantics);
    runCase("5 · 裁决 28 视锥无关性",                        test5_frustumIndependence);
    runCase("6 · A-3 快进 ≡ 逐 tick（逐位相同）",            test6_fastForwardEqualsTick);
    runCase("7 · 事件总线归因链（step>3 检出）",             test7_attribution);
    runCase("8 · 写队列快照语义 + 排序键",                   test8_writeQueue);
    runCase("9 · 性能实测（120 实体）",                      test9_performance);
    runCase("10 · ADR-005 对称取整 + golden 向量 + 舍入边界", test10_symmetricRounding);

    std::printf("================================================================\n");
    std::printf(" 用例: %d PASS / %d FAIL    检查点: %ld 个，失败 %ld 个\n",
                g_casePass, g_caseFail, g_checks, g_fails);
    std::printf(" 结果: %s\n", (g_fails == 0 && g_caseFail == 0) ? "ALL PASS" : "FAILED");
    std::printf("================================================================\n");
    return (g_fails == 0 && g_caseFail == 0) ? 0 : 1;
}

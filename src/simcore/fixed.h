// ============================================================================
// src/simcore/fixed.h · ADR-005 · milli 定点数值表示
// ----------------------------------------------------------------------------
// 契约来源：
//   - ADR-005 D1：`using Milli = int32_t;  // 语义 = 真值 × 1000`
//   - ADR-005 D1：唯一实现，禁止各自写 mulM / divM
//   - ADR-003 D7 R-B2：衰减/累积用【整数余数累加器】，不用浮点
//   - S0 §C7-3：仿真侧禁止 double 与超越函数；ADR-005 D4：float 也禁
// ----------------------------------------------------------------------------
// 语言约束（主理人裁决）：C++14 兼容子集，禁用 C++17/20 特性。
// 本文件不引入任何依赖（无第三方库、无 <cmath>、无 <string>）。
// ============================================================================
#ifndef SIMCORE_FIXED_H
#define SIMCORE_FIXED_H

#include <cstdint>
#include <cassert>
#include <cstddef>

namespace simcore {

typedef int32_t Milli;   // 主类型：真值 × 1000，范围 ±2.147e6
typedef int64_t MilliL;  // 中间运算类型，防溢出；运算后立即夹回 int32

static const MilliL kMilliScale = 1000;
static const Milli  kMilliMax   = 2147483647;          //  2147483.647
static const Milli  kMilliMin   = (Milli)(-2147483647 - 1);

// ---------------------------------------------------------------------------
// 常量构造宏（编译期整型，不引入任何浮点字面量）
//   SIMCORE_MILLI_INT(3)      -> 3000    （3.0）
//   SIMCORE_MILLI_PARTS(12,500)-> 12500  （12.5）
// ---------------------------------------------------------------------------
#define SIMCORE_MILLI_INT(whole)      ((::simcore::Milli)((whole) * 1000))
#define SIMCORE_MILLI_PARTS(w, frac)  ((::simcore::Milli)((w) * 1000 + (frac)))

// ---------------------------------------------------------------------------
// 溢出 / 饱和统计（ADR-005 V4：跑 30 日统计 mulM/divM 饱和计数，应为 0）
// 单线程仿真，故用函数内静态单例，避免 C++17 内联变量。
// ---------------------------------------------------------------------------
struct FixedStats {
    uint64_t satAdd;
    uint64_t satSub;
    uint64_t satMul;
    uint64_t divByZero;
};

inline FixedStats& fixedStats() {
    static FixedStats s;   // 静态存储期，零初始化
    return s;
}

inline void fixedStatsReset() {
    FixedStats& s = fixedStats();
    s.satAdd = 0; s.satSub = 0; s.satMul = 0; s.divByZero = 0;
}

// 饱和时是否 assert 中断（测试饱和计数逻辑时可临时关闭；默认开启）
inline bool& fixedSatAssertEnabled() {
    static bool e = true;
    return e;
}

// ---------------------------------------------------------------------------
// 加减：同量纲直接整数加减，溢出走饱和 + 计数（ADR-005 D1 / D4）
//        debug（未定义 NDEBUG）下额外 assert，便于定位；release 仅计数。
// ---------------------------------------------------------------------------
inline Milli addSat(Milli a, Milli b) {
    MilliL r = (MilliL)a + (MilliL)b;
    if (r > (MilliL)kMilliMax) {
        ++fixedStats().satAdd;
        if (fixedSatAssertEnabled()) assert(0 && "[simcore::addSat] 上溢");
        return kMilliMax;
    }
    if (r < (MilliL)kMilliMin) {
        ++fixedStats().satAdd;
        if (fixedSatAssertEnabled()) assert(0 && "[simcore::addSat] 下溢");
        return kMilliMin;
    }
    return (Milli)r;
}

inline Milli subSat(Milli a, Milli b) {
    MilliL r = (MilliL)a - (MilliL)b;
    if (r > (MilliL)kMilliMax) {
        ++fixedStats().satSub;
        if (fixedSatAssertEnabled()) assert(0 && "[simcore::subSat] 上溢");
        return kMilliMax;
    }
    if (r < (MilliL)kMilliMin) {
        ++fixedStats().satSub;
        if (fixedSatAssertEnabled()) assert(0 && "[simcore::subSat] 下溢");
        return kMilliMin;
    }
    return (Milli)r;
}

// ---------------------------------------------------------------------------
// 乘（ADR-005 D1 逐字实现）：先升 int64，除 1000 归位，四舍五入（+500）
// ---------------------------------------------------------------------------
// ⚠ 已知口径问题（待主理人裁决，见 README「发现的问题」第 3 条）：
//   ADR-005 给的 `+500` 对【负积】是"向 +∞ 方向偏置"的（例：p = -1600 →
//   (-1100)/1000 = -1，而"四舍五入"期望 -2）。本实现严格照 ADR 字面执行以
//   保证逐位可复现；如主理人偏好"绝对值四舍五入"，改一行即可，但必须重新
//   生成 golden 向量。
// ---------------------------------------------------------------------------
inline Milli mulM(Milli a, Milli b) {
    MilliL p = (MilliL)a * (MilliL)b;          // |p| ≤ ~4.6e18 < 2^63，不溢出
    MilliL q = (p + 500) / 1000;
    if (q > (MilliL)kMilliMax) { ++fixedStats().satMul; if (fixedSatAssertEnabled()) assert(0 && "[simcore::mulM] 上溢"); return kMilliMax; }
    if (q < (MilliL)kMilliMin) { ++fixedStats().satMul; if (fixedSatAssertEnabled()) assert(0 && "[simcore::mulM] 下溢"); return kMilliMin; }
    return (Milli)q;
}

// ---------------------------------------------------------------------------
// 除（ADR-005 D1 逐字实现）：先升 int64，先乘 1000 再除；b != 0 断言
// ---------------------------------------------------------------------------
inline Milli divM(Milli a, Milli b) {
    if (b == 0) {
        ++fixedStats().divByZero;
        assert(0 && "[simcore::divM] 除零");
        return 0;
    }
    MilliL p = (MilliL)a * 1000 + (b / 2);
    MilliL q = p / b;
    if (q > (MilliL)kMilliMax) { ++fixedStats().satMul; return kMilliMax; }
    if (q < (MilliL)kMilliMin) { ++fixedStats().satMul; return kMilliMin; }
    return (Milli)q;
}

// ---------------------------------------------------------------------------
// 与整数的混合运算（真值整数 × 定点 / 定点 ÷ 真值整数）
// ---------------------------------------------------------------------------
inline Milli mulIntM(Milli a, int32_t k) { return (Milli)((MilliL)a * (MilliL)k); }
inline Milli divIntM(Milli a, int32_t k) {
    if (k == 0) { ++fixedStats().divByZero; assert(0 && "[simcore::divIntM] 除零"); return 0; }
    return (Milli)((MilliL)a / (MilliL)k);
}
inline Milli addIntM(Milli a, int32_t k) { return addSat(a, SIMCORE_MILLI_INT(k)); }

// ---------------------------------------------------------------------------
// 比较 / 夹取 / 取整（比较为整数比较，无容差 —— ADR-005 D1）
// ---------------------------------------------------------------------------
inline int  cmpM(Milli a, Milli b) { return (a < b) ? -1 : ((a > b) ? 1 : 0); }

inline Milli clampM(Milli v, Milli lo, Milli hi) { return (v < lo) ? lo : ((v > hi) ? hi : v); }

inline Milli absM(Milli v) { return (v < 0) ? (Milli)(-(MilliL)v) : v; }

// 定点 → 真值整数，四舍五入（ADR-005 D5：量化到 int 的唯一出口）
inline int32_t roundM(Milli v) {
    if (v >= 0) return (int32_t)(((MilliL)v + 500) / 1000);
    return (int32_t)(((MilliL)v - 500) / 1000);
}
// 定点 → 真值整数，向下取整
inline int32_t floorM(Milli v) {
    if (v >= 0) return (int32_t)((MilliL)v / 1000);
    return (int32_t)(((MilliL)v - 999) / 1000);
}
// 定点 → 真值整数，向上取整
inline int32_t ceilM(Milli v) {
    if (v > 0) return (int32_t)(((MilliL)v + 999) / 1000);
    return (int32_t)((MilliL)v / 1000);
}

// ---------------------------------------------------------------------------
// MilliRateAccum · ADR-003 D7 R-B2「整数余数累加器」
// ---------------------------------------------------------------------------
//   ratePerHour 为每小时变化量（milli）。按分钟调用 addPerMinute + takePerMinute：
//     acc += ratePerHour;  step = acc / 60;  acc -= step * 60;  value += step
//   余数保留在 acc，故总量守恒且完全由整数运算决定 —— 不依赖任何浮点行为。
//   （C++ 整数除法向零截断，负速率下 step 亦向零截断，但 acc 守恒，确定性不受影响。）
// ---------------------------------------------------------------------------
struct MilliRateAccum {
    MilliL acc;    // 余数累加器（milli）
    Milli  value;  // 已量化的当前值（milli）

    MilliRateAccum() : acc(0), value(0) {}
    MilliRateAccum(MilliL acc0, Milli v0) : acc(acc0), value(v0) {}

    void addPerMinute(Milli ratePerHour) { acc += (MilliL)ratePerHour; }

    // 返回本分钟的量化增量；同时累加进 value
    Milli takePerMinute() {
        MilliL step = acc / 60;
        acc -= step * 60;
        value = addSat(value, (Milli)step);
        return (Milli)step;
    }
};

// ---------------------------------------------------------------------------
// 设计常量精确性静态断言（ADR-005 V1：失败数必须为 0）
// ---------------------------------------------------------------------------
static_assert(sizeof(Milli) == 4, "Milli 必须是 32 位");
static_assert(sizeof(MilliL) == 8, "MilliL 必须是 64 位");
static_assert(SIMCORE_MILLI_INT(3) == 3000,        "WATER_L_PER_PERSON_DAY 3.0 必须精确表示");
static_assert(SIMCORE_MILLI_INT(1) == 1000,        "FOOD_UNIT_PER_PERSON_DAY 1.0 必须精确表示");
static_assert(SIMCORE_MILLI_INT(64) == 64000,      "PHYSIO_POINTS_PER_LITER 64");
static_assert(SIMCORE_MILLI_INT(96) == 96000,      "PHYSIO_POINTS_PER_FOOD_UNIT 96");
static_assert(SIMCORE_MILLI_INT(-8) == -8000,      "PHYSIO_DECAY_WATER -8");
static_assert(SIMCORE_MILLI_PARTS(12, 500) == 12500, "SLEEP_RECOVER_PER_HOUR 12.5 必须精确表示");
static_assert(SIMCORE_MILLI_PARTS(0, 970) == 970,  "REPUTATION_DECAY 0.97 必须精确表示");
static_assert(SIMCORE_MILLI_PARTS(1, 150) == 1150, "SPEED_MUL 上界 1.15");
static_assert(SIMCORE_MILLI_PARTS(0, 500) == 500,  "SPEED_MUL 系数 0.5");
static_assert(SIMCORE_MILLI_PARTS(0, 450) == 450,  "SPEED_MUL 下界 0.45");
static_assert(SIMCORE_MILLI_PARTS(1, 400) == 1400, "BASE_WALK_SPEED 1.4");
static_assert(SIMCORE_MILLI_PARTS(0, 40) == 40,    "BARTER_OFFSET 4%");
static_assert(SIMCORE_MILLI_PARTS(0, 600) == 600,  "Quality 下界 0.6");
static_assert(SIMCORE_MILLI_PARTS(0, 400) == 400,  "Quality 下界 0.4");
static_assert(SIMCORE_MILLI_INT(10) == 10000,      "DC_TRIVIAL 10");
static_assert(SIMCORE_MILLI_INT(5) == 5000,        "CRIT_MARGIN 5");

}  // namespace simcore

// ---------------------------------------------------------------------------
// 【浮点对照版】仅在显式定义 SIMCORE_ENABLE_REFERENCE_FLOAT 时启用
// ---------------------------------------------------------------------------
// 用途：ADR-005 V2「10 日递推：milli vs double 参考实现」的漂移测试。
// 默认【关闭】，因此正式构建下 src/simcore/ 仍然是零浮点（满足 ADR-005 V3
// 的 CI 静态检查）。启用后，CI 的「无浮点」grep 需排除本段 —— 见 README
// 「发现的问题」第 1 条。
// ---------------------------------------------------------------------------
#ifdef SIMCORE_ENABLE_REFERENCE_FLOAT

namespace simcore {
namespace ref {

typedef double Real;

inline Real fromMilli(Milli m) { return (Real)m / 1000.0; }
inline Milli toMilliRound(Real v) { return (Milli)(v * 1000.0 + (v >= 0 ? 0.5 : -0.5)); }

// MilliRateAccum 的浮点镜像：同一算法，仅表示换为 double
struct RateAccumRef {
    Real acc;
    Real value;
    RateAccumRef() : acc(0.0), value(0.0) {}
    void addPerMinute(Real ratePerHour) { acc += ratePerHour; }
    Real takePerMinute() {
        Real step = acc / 60.0;
        acc -= step * 60.0;
        value += step;
        return step;
    }
};

}  // namespace ref
}  // namespace simcore

#endif  // SIMCORE_ENABLE_REFERENCE_FLOAT

#endif  // SIMCORE_FIXED_H

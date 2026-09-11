// ============================================================================
// src/simcore/simcore.h · 汇总头（L1 骨架）
// ----------------------------------------------------------------------------
// 这是一个【header-only】库：所有实现都在头文件里，单个 .cpp 即可编译，
// 便于将来嵌入任意引擎层（ADR-001 方案 E：simcore 引擎无关化）。
// ----------------------------------------------------------------------------
// 依赖方向（ADR-001 R1 / control-manifest B1）：
//   simcore → 无引擎头、无第三方库、无浮点（仿真路径）、无裸随机。
//   表现层（render/ui）→ 只读快照，禁止调用 rollCheck / makeSeedCtx / 直接写 actor。
// ============================================================================
#ifndef SIMCORE_SIMCORE_H
#define SIMCORE_SIMCORE_H

// ---------------------------------------------------------------------------
// 先吞掉全部系统头，再加载 simcore —— 这样末尾的 `rand` poison 不会误伤
// <cstdlib> / <algorithm> 里对 rand 的声明（见文件末尾）。
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <utility>
#include <vector>
#include <algorithm>

#include "fixed.h"
#include "rng.h"
#include "clock.h"
#include "eventbus.h"
#include "writequeue.h"
#include "world.h"

namespace simcore {

// ---------------------------------------------------------------------------
// 版本三元组（ADR-002 D6 / S0 §C3：三者任一不匹配 → 拒绝重放并明确提示）
// ---------------------------------------------------------------------------
static const uint32_t kSimSchemaVersion      = 1;
static const uint32_t kStreamRegistryVersion = SIMCORE_STREAM_REGISTRY_VERSION;
static const char* const kGameVersion        = "0.1.0-l1";

struct SimcoreVersion {
    const char* gameVersion;
    uint32_t    simSchemaVersion;
    uint32_t    streamIdRegistryVersion;
};

inline SimcoreVersion simcoreVersion() {
    SimcoreVersion v;
    v.gameVersion = kGameVersion;
    v.simSchemaVersion = kSimSchemaVersion;
    v.streamIdRegistryVersion = kStreamRegistryVersion;
    return v;
}

// ---------------------------------------------------------------------------
// 启动自检：把"契约级"的不变式在 boot 期跑一遍（失败即 assert，不静默）
// ---------------------------------------------------------------------------
inline void selfCheck() {
    // ADR-005：常量精确性 + 类型宽度
    fixedStatsReset();
    assert(mulM(SIMCORE_MILLI_PARTS(1, 500), SIMCORE_MILLI_PARTS(0, 970)) == 1455);  // 1.5 × 0.97 = 1.455
    assert(divM(SIMCORE_MILLI_INT(100), SIMCORE_MILLI_INT(3)) == 33333);             // 100 / 3
    {   // 饱和（临时关闭 assert，只验返回值与计数）
        const bool old = fixedSatAssertEnabled();
        fixedSatAssertEnabled() = false;
        assert(addSat(kMilliMax, 1) == kMilliMax);
        assert(subSat(kMilliMin, 1) == kMilliMin);
        fixedSatAssertEnabled() = old;
    }

    // ADR-002：pcg32_at 的跳步正确性 —— n 次顺序推进 == 一次跳步
    {
        uint64_t s = 0x1234567890ABCDEFULL;
        uint64_t state = s;
        for (int i = 1; i <= 64; ++i) {
            state = kLcgA * state + kLcgC;
            assert(pcg32_at(s, (uint64_t)i) == pcg_xsh_rr(state));
        }
    }

    // 裁决 27：SIM_TICK = 1 Hz —— 1440 现实秒 → 1440 SIM_TICK
    {
        Clock c;
        uint64_t n = 0;
        for (int i = 0; i < 1440; ++i) {
            int steps = c.advanceMs(1000, [&](int64_t) { ++n; });
            assert(steps == 1);
        }
        assert(n == 1440);
        assert(c.dayKey() == 2 && c.minuteOfDay() == 0);
    }

    // 裁决 29 / 裁决 28：注册表版本与 LOD 上限
    assert(kStreamRegistryVersion >= 1);
    assert(LOD_FULL_MAX == 32);
}

}  // namespace simcore

// ---------------------------------------------------------------------------
// 静态检查钩子：禁止裸随机（ADR-002 D5 / S0 §C2 红线①）
// ---------------------------------------------------------------------------
// GCC/Clang 的 `#pragma GCC poison` 使其后出现的 `rand` / `srand` 标识符变成
// 【硬编译错误】—— 比 grep 更强：任何包含本头文件的翻译单元里写裸 rand 都编不过。
//
// 使用约束（重要）：
//   1) 必须先包含全部系统头（本文件顶部已吞掉 <cstdlib>/<algorithm> 等）再包含
//      本头；否则系统头里对 rand 的声明会被误伤。
//   2) 若某第三方头必须声明 rand，请在该 TU 定义 SIMCORE_NO_POISON_RAND。
// ---------------------------------------------------------------------------
#if defined(__GNUC__) && !defined(SIMCORE_NO_POISON_RAND)
#pragma GCC poison rand srand
#endif

#endif  // SIMCORE_SIMCORE_H

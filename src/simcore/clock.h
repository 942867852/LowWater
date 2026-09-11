// ============================================================================
// src/simcore/clock.h · 世界时钟与 tick 分级
// ----------------------------------------------------------------------------
// 契约来源（逐字实现，不重新设计）：
//   裁决 27 / ADR-003 §2.1 修 A：
//       TIME_SCALE    = 60      【游戏秒 / 现实秒】（不是"游戏分钟/现实秒"）
//       SIM_TICK_COST = 60      【游戏秒】= 1 游戏分钟 → SIM_TICK = 1 Hz
//       MAX_STEPS_PER_FRAME = 4；超出上限保留 acc，不丢时间、不跳步
//       1 游戏日 = 1440 游戏分钟 = 1440 现实秒 = 24 现实分钟
//   ADR-003 D1：SIM_TICK / HOURLY_TICK / DISPATCH(06:00) / DAILY_CUTOVER(1439 后)
//   ADR-002 D7-3：dtReal 唯一合法用途是喂累加器，禁止进入任何仿真公式
//   ADR-005 D4：simcore 内禁止浮点 → 累加器以【毫游戏秒】为单位的整数实现
// ----------------------------------------------------------------------------
// 累加器单位口径（本实现的关键工程决定）：
//   acc 的单位 = 毫游戏秒（1/1000 游戏秒）。
//   dtReal 以【现实毫秒】整数喂入：acc += dtRealMs * TIME_SCALE。
//   1 现实秒 = 1000 ms → +60000 毫游戏秒 → 恰好 1 个 SIM_TICK。
//   这样全程整数运算，逐位可复现，且不引入任何浮点（符合 ADR-005）。
// ============================================================================
#ifndef SIMCORE_CLOCK_H
#define SIMCORE_CLOCK_H

#include <cstdint>
#include <cassert>

#include "fixed.h"

namespace simcore {

// --- 常量（S0 §D + 裁决 27/29） ---------------------------------------------
static const int32_t TIME_SCALE           = 60;    // 游戏秒 / 现实秒
static const int64_t SIM_TICK_COST_MILLI  = 60000; // 60 游戏秒 = 60000 毫游戏秒
static const int32_t SIM_TICK_COST_SEC    = 60;    // 60 游戏秒 = 1 游戏分钟
static const int32_t MAX_STEPS_PER_FRAME  = 4;
static const int32_t MINUTES_PER_DAY      = 1440;
static const int32_t DISPATCH_MINUTE      = 360;   // 06:00 发令
static const int32_t CUTOVER_MINUTE       = 1439;  // 当日最后一分钟
static const int32_t SNAPSHOT_PERIOD      = 60;    // 每 60 tick 计算 worldHash（S0 §C3）

// tick 种类（ADR-003 D1）
enum class TickKind : uint8_t {
    SIM_TICK      = 0,
    HOURLY_TICK   = 1,
    DISPATCH      = 2,
    DAILY_CUTOVER = 3
};

// ---------------------------------------------------------------------------
// 时间地址（S0 §B2：absTick = dayKey × 1440 + minuteOfDay，唯一时间地址）
// ---------------------------------------------------------------------------
inline int64_t makeAbsTick(int32_t dayKey, int32_t minuteOfDay) {
    assert(dayKey >= 1 && "[simcore::makeAbsTick] dayKey 从 1 起");
    assert(minuteOfDay >= 0 && minuteOfDay < MINUTES_PER_DAY);
    return (int64_t)dayKey * (int64_t)MINUTES_PER_DAY + (int64_t)minuteOfDay;
}
inline int32_t dayKeyOf(int64_t absTick)      { return (int32_t)(absTick / MINUTES_PER_DAY); }
inline int32_t minuteOfDayOf(int64_t absTick) { return (int32_t)(absTick % MINUTES_PER_DAY); }

// ---------------------------------------------------------------------------
// Clock · 固定步长累加器 + 日历
// ---------------------------------------------------------------------------
class Clock {
public:
    Clock()
        : dayKey_(1), minuteOfDay_(0), accMilliGameSec_(0),
          stepsLastFrame_(0), ticksExecuted_(0), clampedFrames_(0), leftoverMilli_(0) {}

    void reset(int32_t dayKey = 1, int32_t minuteOfDay = 0) {
        dayKey_ = dayKey; minuteOfDay_ = minuteOfDay;
        accMilliGameSec_ = 0; stepsLastFrame_ = 0;
        ticksExecuted_ = 0; clampedFrames_ = 0; leftoverMilli_ = 0;
    }

    int32_t dayKey() const      { return dayKey_; }
    int32_t minuteOfDay() const { return minuteOfDay_; }
    int64_t absTick() const     { return makeAbsTick(dayKey_, minuteOfDay_); }
    int64_t acc() const         { return accMilliGameSec_; }
    int32_t stepsLastFrame() const { return stepsLastFrame_; }
    uint64_t ticksExecuted() const { return ticksExecuted_; }
    uint64_t clampedFrames() const { return clampedFrames_; }

    // -------------------------------------------------------------------------
    // advanceMs：喂累加器（dtReal 的唯一合法入口）
    //   dtRealMs —— 现实毫秒（整数）。禁止传浮点。
    //   onSimTick(absTick) —— 每推进 1 个 SIM_TICK 回调一次，按 tick 升序。
    //   返回本帧实际执行的 SIM_TICK 数（0..MAX_STEPS_PER_FRAME）。
    // -------------------------------------------------------------------------
    template <class F>
    int advanceMs(uint64_t dtRealMs, F onSimTick) {
        accMilliGameSec_ += (int64_t)dtRealMs * (int64_t)TIME_SCALE;
        int steps = 0;
        while (accMilliGameSec_ >= SIM_TICK_COST_MILLI && steps < MAX_STEPS_PER_FRAME) {
            onSimTick(absTick());
            advanceOneMinute();
            accMilliGameSec_ -= SIM_TICK_COST_MILLI;
            ++steps;
            ++ticksExecuted_;
        }
        // 裁决 27：超出上限 → 保留 acc（不丢时间），世界变慢，绝不跳步
        if (accMilliGameSec_ >= SIM_TICK_COST_MILLI) {
            ++clampedFrames_;
            leftoverMilli_ = accMilliGameSec_;
        } else {
            leftoverMilli_ = 0;
        }
        stepsLastFrame_ = steps;
        return steps;
    }

    // 快进：直接推进 n 个 SIM_TICK（不经过累加器）。
    // 语义要求（S0 §B3.4 / ADR-003 D4）：【必须走同一 tick 序列】——
    // 与 advanceMs 调用的是同一个 onSimTick(absTick)，absTick 逐 1 递增，
    // 不跳过任何一分钟、不更换 RNG 流。本函数只改变"谁在驱动"，不改变"跑什么"。
    template <class F>
    void advanceTicks(uint64_t n, F onSimTick) {
        for (uint64_t i = 0; i < n; ++i) {
            onSimTick(absTick());
            advanceOneMinute();
            ++ticksExecuted_;
        }
    }

    // 显式恢复（save/load 用）：S0 §C3 要求恢复后首 tick == savedTick + 1。
    // 注意 absTick() 的语义是【下一个将执行的 tick】（advanceMs 先回调 onSimTick(absTick())
    // 再 advanceOneMinute），故须把日历位置设为 lastExecutedAbsTick + 1，
    // 这样恢复后的首个 onSimTick 才恰好是 savedTick + 1（不跳号、不重复、不补跑）。
    void restoreAt(int64_t lastExecutedAbsTick) {
        const int64_t nextAbsTick = lastExecutedAbsTick + 1;
        dayKey_      = dayKeyOf(nextAbsTick);
        minuteOfDay_ = minuteOfDayOf(nextAbsTick);
        accMilliGameSec_ = 0;
        stepsLastFrame_ = 0;
    }

private:
    void advanceOneMinute() {
        if (minuteOfDay_ >= CUTOVER_MINUTE) { minuteOfDay_ = 0; ++dayKey_; }
        else                                { ++minuteOfDay_; }
    }

    int32_t dayKey_;
    int32_t minuteOfDay_;
    int64_t accMilliGameSec_;
    int32_t stepsLastFrame_;
    uint64_t ticksExecuted_;
    uint64_t clampedFrames_;
    int64_t leftoverMilli_;
};

// ---------------------------------------------------------------------------
// 一帧内的一分钟该触发哪些 tick（ADR-003 D1 / D3 的顺序）
// ---------------------------------------------------------------------------
// 约定（本骨架的显式口径，写死以便逐位复现）：
//   对当前 absTick t：
//     1) SIM_TICK      —— 总是执行（实体推进 + boundary 补做）
//     2) HOURLY_TICK   —— t.minuteOfDay % 60 == 0
//     3) DISPATCH      —— t.minuteOfDay == 360（06:00，只发令）
//     4) DAILY_CUTOVER —— t.minuteOfDay == 1439（日切九步）；随后日历进位
//   DISPATCH 与 CUTOVER 分钟互斥（360 != 1439），不存在同 tick 争用。
// ---------------------------------------------------------------------------
struct TickSchedule {
    bool hourly;
    bool dispatch;
    bool cutover;
};

inline TickSchedule scheduleFor(int64_t absTick) {
    const int32_t m = minuteOfDayOf(absTick);
    TickSchedule s;
    s.hourly   = (m % 60 == 0);
    s.dispatch = (m == DISPATCH_MINUTE);
    s.cutover  = (m == CUTOVER_MINUTE);
    return s;
}

}  // namespace simcore

#endif  // SIMCORE_CLOCK_H

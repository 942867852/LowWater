// ============================================================================
// src/simcore/rng.h · ADR-002 · 可分流 RNG（无状态纯函数）
// ----------------------------------------------------------------------------
// 契约来源（逐字实现，不重新设计）：
//   ADR-002 D1：`pcg32_at(seed64, counter)` 无状态、可跳步 —— 全项目唯一随机原语
//   ADR-002 D2：counter = absTick × 4096 + callSeq；callSeq < 4096，越界即断言
//   ADR-002 D3：无状态 → 存档不需要存 per-stream counter
//   ADR-002 D4：streamId 静态注册表，命名 <domain>.<sub>.<qualifier>
//   S0 §C2   ：seed64 = FNV1a64(worldSeed ‖ streamId)
//   红线②    ：禁止把 tick / 随机数 / 指针 / 遍历序号拼进 streamId
// ----------------------------------------------------------------------------
// 语言约束：C++14 兼容子集。
// ============================================================================
#ifndef SIMCORE_RNG_H
#define SIMCORE_RNG_H

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstring>

#include "fixed.h"

namespace simcore {

// ---------------------------------------------------------------------------
// FNV-1a 64（S0 §C2 指定的 seed 派生 / §C3 指定的 worldHash）
// ---------------------------------------------------------------------------
static const uint64_t kFnvOffset64 = 14695981039346656037ULL;
static const uint64_t kFnvPrime64  = 1099511628211ULL;

inline uint64_t fnv1a64(const void* data, size_t len, uint64_t seed = kFnvOffset64) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = seed;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint64_t)p[i];
        h *= kFnvPrime64;
    }
    return h;
}

// 增量哈希器（worldHash 用，ADR-002 V6 / S0 §C3）
struct Hasher {
    uint64_t h;
    Hasher() : h(kFnvOffset64) {}
    explicit Hasher(uint64_t seed) : h(seed) {}
    void bytes(const void* p, size_t n) {
        const unsigned char* b = (const unsigned char*)p;
        for (size_t i = 0; i < n; ++i) { h ^= (uint64_t)b[i]; h *= kFnvPrime64; }
    }
    void u8(uint8_t v)   { bytes(&v, 1); }
    void u16(uint16_t v) { bytes(&v, 2); }
    void u32(uint32_t v) { bytes(&v, 4); }
    void i32(int32_t v)  { bytes(&v, 4); }
    void u64(uint64_t v) { bytes(&v, 8); }
    void i64(int64_t v)  { bytes(&v, 8); }
    void str(const char* s) { if (s) bytes(s, std::strlen(s)); }
    uint64_t value() const { return h; }
};

// ---------------------------------------------------------------------------
// PCG32 · ADR-002 D1 给出的常量与输出函数（golden 向量由本实现产生后不得变更）
// ---------------------------------------------------------------------------
static const uint64_t kLcgA = 6364136223846793005ULL;
static const uint64_t kLcgC = 1442695040888963407ULL;

struct LcgFn { uint64_t a, c; };   // f(x) = a*x + c  (mod 2^64)

// 复合：先 f 后 g
inline LcgFn compose_then(LcgFn f, LcgFn g) {
    LcgFn r;
    r.a = g.a * f.a;
    r.c = g.a * f.c + g.c;
    return r;
}

// (LCG_A, LCG_C)^n —— O(log n)，纯函数
inline LcgFn lcg_pow(uint64_t n) {
    LcgFn r; r.a = 1; r.c = 0;
    LcgFn b; b.a = kLcgA; b.c = kLcgC;
    while (n) {
        if (n & 1ULL) r = compose_then(r, b);
        b = compose_then(b, b);
        n >>= 1;
    }
    return r;
}

inline uint32_t pcg_xsh_rr(uint64_t s) {
    uint32_t xs  = (uint32_t)(((s >> 18u) ^ s) >> 27u);
    uint32_t rot = (uint32_t)(s >> 59u);
    return (xs >> rot) | (xs << ((-rot) & 31u));
}

// 唯一随机原语：只依赖 (seed64, counter)，与调用顺序 / 调用次数 / 何时调用无关
inline uint32_t pcg32_at(uint64_t seed64, uint64_t counter) {
    LcgFn f = lcg_pow(counter);
    return pcg_xsh_rr(f.a * seed64 + f.c);
}

// ---------------------------------------------------------------------------
// StreamId 静态注册表（ADR-002 D4）
// ---------------------------------------------------------------------------
// 说明（L1 骨架口径）：
//   - 带 `<...>` 的是【参数化模板】，运行时用确定性参数实例化；模板本身静态。
//   - 实例化参数只允许 ActorId / ClusterId / zoneId / containerId / communityId
//     （均来自确定性数据）。禁止传 tick、随机数、指针、容器遍历序号。
//   - 新增 streamId 必须改这里 + 提升 STREAM_REGISTRY_VERSION。
// ---------------------------------------------------------------------------
#define SIMCORE_STREAM_REGISTRY_VERSION 1

enum class StreamId : uint16_t {
    CheckPlayerScavenge = 0,   // check.player.scavenge
    CheckPlayerBarter   = 1,   // check.player.barter
    CheckPlayerDialogue = 2,   // check.player.dialogue
    AiNpcRoute          = 3,   // ai.npc.<community>.<role>_<nn>.route   【模板】
    LootZoneContainer   = 4,   // loot.zone.<zoneId>.<containerId>       【模板】
    PriceCluster        = 5,   // price.<community>.<cluster>            【模板】
    GossipCommunity     = 6,   // gossip.<community>                     【模板】
    EstateGossip        = 7,   // estate.<community>.gossip              【模板】
    WeatherGlobal       = 8,   // weather.global
    WildGlobal          = 9,   // wild.global
    // ↓↓↓ L1 simcore 骨架内部专用（boundaries / LOD 抢占），归属 S0·B
    SimBoundaryCheck    = 10,  // sim.boundary.<actorIndex>.check        【模板】
    SimLodPreempt       = 11,  // sim.lod.preempt
    SimWorldInit        = 12,  // sim.world.init.<actorIndex>            【模板】
    kCount              = 13
};

static const char* const kStreamName[] = {
    "check.player.scavenge",
    "check.player.barter",
    "check.player.dialogue",
    "ai.npc.<community>.<role>_<nn>.route",
    "loot.zone.<zoneId>.<containerId>",
    "price.<community>.<cluster>",
    "gossip.<community>",
    "estate.<community>.gossip",
    "weather.global",
    "wild.global",
    "sim.boundary.<actorIndex>.check",
    "sim.lod.preempt",
    "sim.world.init.<actorIndex>"
};

// 注册表自检：枚举数量必须与名字表一致（防"加了枚举忘加名字"）
// （C++14 无法在 static_assert 里用 constexpr 数组长度以外的复杂表达，故用简单方式）
static_assert((int)StreamId::kCount == 13, "StreamId 数量变更时必须同步 kStreamName");

inline const char* streamName(StreamId id) {
    uint16_t i = (uint16_t)id;
    assert(i < (uint16_t)StreamId::kCount && "[simcore::streamName] streamId 越界（不在注册表内）");
    return kStreamName[i];
}

// streamId → streamIndex（静态注册表的序号，进存档对账用）
inline uint16_t streamIndex(StreamId id) {
    uint16_t i = (uint16_t)id;
    assert(i < (uint16_t)StreamId::kCount && "[simcore::streamIndex] streamId 越界");
    return i;
}

// ---------------------------------------------------------------------------
// seed64 派生：FNV1a64(worldSeed ‖ streamId名字 ‖ 实例化参数)
// ---------------------------------------------------------------------------
// 注意：这里的 `param` 只允许是确定性静态标识（actorIndex / zoneId / cluster 等）。
//      传 tick 或随机数即违反 ADR-002 红线②。
inline uint64_t deriveSeed64(uint64_t worldSeed, StreamId id, uint32_t param) {
    Hasher h;
    h.u64(worldSeed);
    h.str(streamName(id));
    h.u8(0x1F);          // 分隔符，防止 ("a"‖"bc") 与 ("ab"‖"c") 碰撞
    h.u32(param);
    return h.value();
}

// ---------------------------------------------------------------------------
// RngStream · 无状态随机源
// ---------------------------------------------------------------------------
// counter = absTick × 4096 + callSeq   （ADR-002 D2）
//   absTick : 全局单调递增 = dayKey × 1440 + minuteOfDay
//   callSeq : 同一 (streamId, absTick) 内的调用序号，从 0 起，< 4096
// 因为 pcg32_at 是纯函数，本对象【不含任何可变状态】—— 存档无需存 counter。
// ---------------------------------------------------------------------------
static const uint64_t kCallSeqSpan = 4096;

class RngStream {
public:
    RngStream() : seed64_(0), id_(StreamId::kCount), param_(0) {}
    RngStream(uint64_t worldSeed, StreamId id, uint32_t param = 0)
        : seed64_(deriveSeed64(worldSeed, id, param)), id_(id), param_(param) {}

    uint64_t seed64() const { return seed64_; }
    StreamId streamId() const { return id_; }
    uint32_t param() const { return param_; }

    // 核心：无状态取值（纯函数）
    uint32_t at(uint64_t absTick, uint32_t callSeq) const {
        assert(callSeq < kCallSeqSpan && "[simcore::RngStream::at] callSeq 越界（≥4096），ADR-002 D2 硬约束");
        return pcg32_at(seed64_, absTick * kCallSeqSpan + (uint64_t)callSeq);
    }

    // 便捷：取一次并推进 callSeq（callSeq 由调用方持有，不藏在对象里 —— 保证无状态）
    uint32_t next(uint64_t absTick, uint32_t& callSeq) const {
        uint32_t v = at(absTick, callSeq);
        ++callSeq;
        return v;
    }

    // [0, n) 均匀整数（乘移取模，无取模偏置 —— ADR-002 D5 Q3 钉死的手法）
    uint32_t below(uint64_t absTick, uint32_t callSeq, uint32_t n) const {
        assert(n > 0);
        return (uint32_t)(((uint64_t)at(absTick, callSeq) * (uint64_t)n) >> 32);
    }

    // d20：1..20（ADR-002 D5：raw = 1 + (u32*20)>>32）
    uint32_t d20(uint64_t absTick, uint32_t callSeq) const {
        return 1u + (uint32_t)(((uint64_t)at(absTick, callSeq) * 20u) >> 32);
    }

    // [0, 1000) 的 milli 概率 → 用于定点概率判定
    Milli chanceMilli(uint64_t absTick, uint32_t callSeq) const {
        return (Milli)(((uint64_t)at(absTick, callSeq) * 1000u) >> 32);
    }

    bool chance(uint64_t absTick, uint32_t callSeq, Milli pMilli) const {
        return chanceMilli(absTick, callSeq) < pMilli;
    }

private:
    uint64_t seed64_;
    StreamId id_;
    uint32_t param_;
};

// 自由函数形态：rand(streamId, absTick, callSeq) —— 供系统层直接使用
// （不使用裸 rand，见文件末尾的 poison 静态检查钩子）
inline uint32_t rngRand(uint64_t worldSeed, StreamId id, uint64_t absTick, uint32_t callSeq, uint32_t param = 0) {
    return RngStream(worldSeed, id, param).at(absTick, callSeq);
}

}  // namespace simcore

// ---------------------------------------------------------------------------
// 静态检查钩子：禁止裸随机（ADR-002 D5 / S0 §C2 红线①）
// ---------------------------------------------------------------------------
// 真正的 poison 具名在 simcore.h 末尾（必须排在所有系统头之后，否则
// <cstdlib>/<algorithm> 里对 rand 的声明会被误伤）。见 simcore.h 末尾说明。
// ---------------------------------------------------------------------------

#endif  // SIMCORE_RNG_H

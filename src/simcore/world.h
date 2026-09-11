// ============================================================================
// src/simcore/world.h · 最小世界（L1 骨架）
// ----------------------------------------------------------------------------
// 契约来源（逐字实现，不重新设计）：
//   裁决 28 / ADR-003 D2：tickLevel（FULL/COARSE/PROTECTED）【只由距离与 pending
//       事件决定】，视锥【不得参与】；视锥只驱动表现层 renderLOD（不进存档、
//       不进 worldHash、不影响仿真）。
//   裁决 29：LOD_FULL_MAX = 32。
//   S0 §B3.2：升级 = 距离 < 120m；降级 = 距离 > 150m（滞回）；
//             FULL 名额超限按【有 pending 事件 → 有名字 → 距离倒数】抢占。
//   ADR-003 D3：boundary 补做判定 —— 锚定【boundary 自己的 absTick】，
//       排序键 (absTick, actorIndex, seqInActor)；FULL/COARSE 共用同一段代码。
//   ADR-003 I3：连续推进路径（fullStep / coarseStep）【不得发起随机判定】。
//   S0 §C3   ：worldHash = FNV1a(生理 + 库存 + 位置量化到 0.1m + 关系值)，每 60 tick。
// ----------------------------------------------------------------------------
// 范围声明：L1 只做"确定性地基"。未实现行为树 / 经济 / 对话 / 导航。
// 语言约束：C++14 兼容子集。
// ============================================================================
#ifndef SIMCORE_WORLD_H
#define SIMCORE_WORLD_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>

#include "fixed.h"
#include "rng.h"
#include "clock.h"
#include "eventbus.h"
#include "writequeue.h"

namespace simcore {

// --- 常量 -------------------------------------------------------------------
static const int32_t  LOD_FULL_MAX       = 32;      // 裁决 29（S0 §D 的 40 已废止）
static const int32_t  LOD_FULL_RADIUS_M  = 120;     // 升级
static const int32_t  LOD_DROP_RADIUS_M  = 150;     // 降级
static const int32_t  HASH_QUANTUM_MM    = 100;     // 位置量化到 0.1m（S0 §C3）
static const int32_t  DEFAULT_ENTITY_COUNT = 120;   // 实体上限（S0 §D 附录 B-14）
static const uint32_t kSaveMagic = 0x53494D31u;     // "SIM1"
static const uint32_t kSaveVersion = 1;

// --- 档位 -------------------------------------------------------------------
enum class TickLevel : uint8_t { FULL = 0, COARSE = 1, PROTECTED = 2 };
enum class RenderLod : uint8_t { kHigh = 0, kMid = 1, kLow = 2 };   // 表现层私有

// --- 边界事件（ADR-003 D3） --------------------------------------------------
struct Boundary {
    uint64_t absTick;      // 锚定时刻（闭形式预计算，运行时不重算）
    uint32_t actorIndex;   // 固定 actorsOrder 下标（不是指针、不是哈希）
    uint16_t seqInActor;   // 同一 actor 内的边界序号
    uint8_t  kind;         // 0=ARRIVE 1=PICKUP 2=FAIL（L1 只用 0）
};

inline bool boundaryLess(const Boundary& a, const Boundary& b) {
    if (a.absTick    != b.absTick)    return a.absTick < b.absTick;
    if (a.actorIndex != b.actorIndex) return a.actorIndex < b.actorIndex;
    return a.seqInActor < b.seqInActor;
}

// --- 实体 -------------------------------------------------------------------
static const uint8_t kFlagNamed     = 0x01;
static const uint8_t kFlagProtected = 0x02;
static const uint8_t kFlagAlive     = 0x04;

struct Entity {
    uint32_t   actorId;
    uint32_t   actorIndex;
    uint8_t    flags;
    int32_t    distMilli;                 // 与玩家的距离（mm）—— simLOD 输入
    int32_t    posX, posY, posZ;          // mm
    int32_t    velPerMinX, velPerMinZ;    // mm / 游戏分钟（整数）
    TickLevel  level;                     // simLOD（进存档、参与仿真）
    RenderLod  renderLod;                 // 表现层私有（不进 worldHash）
    Milli      needWater;                 // 0..100000 (milli)
    Milli      needFood;
    Milli      needSleep;
    MilliL     accWater, accFood, accSleep;   // 整数余数累加器（R-B2）
    uint16_t   pendingEvents;
    uint32_t   nextBoundaryIdx;
    uint16_t   lodSwitches;
    std::vector<Boundary> boundaries;

    Entity()
        : actorId(0), actorIndex(0), flags(0), distMilli(0),
          posX(0), posY(0), posZ(0), velPerMinX(0), velPerMinZ(0),
          level(TickLevel::COARSE), renderLod(RenderLod::kLow),
          needWater(0), needFood(0), needSleep(0),
          accWater(0), accFood(0), accSleep(0),
          pendingEvents(0), nextBoundaryIdx(0), lodSwitches(0) {}
};

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
class World {
public:
    World() : seed_(0), playerX_(0), playerY_(0), playerZ_(0), cameraYawTurn_(0),
              lodSwitchTotal_(0), hashRingCount_(0), hashRingPos_(0) {}

    // -------------------------------------------------------------------------
    // init：确定性初始化（同一 seed → 逐位相同的初始世界）
    // -------------------------------------------------------------------------
    void init(uint64_t seed, int32_t entityCount = DEFAULT_ENTITY_COUNT) {
        seed_ = seed;
        entities_.clear();
        hashRing_.assign(24, 0);
        hashRingCount_ = 0; hashRingPos_ = 0;
        lodSwitchTotal_ = 0;
        playerX_ = 0; playerY_ = 0; playerZ_ = 0;
        cameraYawTurn_ = 0;

        const int32_t n = (entityCount < 1) ? 1 : entityCount;
        entities_.resize((size_t)n);
        for (int32_t i = 0; i < n; ++i) {
            Entity& e = entities_[(size_t)i];
            e.actorIndex = (uint32_t)i;
            // 编号：前 16 个为有名的 npc.he_valley.<role>_<nn>（S0 §D 命名规范），
            // 其余为 anon.<nnn>（最高位置 1 → isAnonymousActor）
            if (i < 16) {
                e.actorId = (uint32_t)i;                    // 有名
                e.flags |= kFlagNamed;
            } else {
                e.actorId = kAnonymousFlag | (uint32_t)i;   // 匿名
            }
            if (i < 3) e.flags |= kFlagProtected;           // PROTECTED ≤ 3（S0 §B3）
            e.flags |= kFlagAlive;

            RngStream rs(seed_, StreamId::SimWorldInit, (uint32_t)i);
            const uint32_t v0 = rs.at(0, 0);
            const uint32_t v1 = rs.at(0, 1);
            const uint32_t v2 = rs.at(0, 2);
            const uint32_t v3 = rs.at(0, 3);

            // 位置均匀撒在 ±180 m 的方形内 → 约 1/3 落在 120m 内（名额 32 会真正生效）
            e.posX = (int32_t)(v0 % 360000u) - 180000;
            e.posZ = (int32_t)(v1 % 360000u) - 180000;
            e.posY = 0;
            e.velPerMinX = (int32_t)(v2 % 201u) - 100;      // ±100 mm/min（≈6 m/h）
            e.velPerMinZ = (int32_t)(v3 % 201u) - 100;

            e.needWater = SIMCORE_MILLI_INT(100);
            e.needFood  = SIMCORE_MILLI_INT(100);
            e.needSleep = SIMCORE_MILLI_INT(100);
            e.accWater = 0; e.accFood = 0; e.accSleep = 0;
            e.pendingEvents = 0;
            e.nextBoundaryIdx = 0;
            e.lodSwitches = 0;
            e.boundaries.clear();
            e.level = TickLevel::COARSE;
            e.renderLod = RenderLod::kLow;
        }
        updateDistances();
        assignLod();
        applyCameraToRenderLod();
    }

    uint64_t seed() const { return seed_; }
    size_t entityCount() const { return entities_.size(); }
    const Entity& entity(size_t i) const { return entities_[i]; }
    Entity& mutableEntity(size_t i) { return entities_[i]; }

    // -------------------------------------------------------------------------
    // 表现层输入（裁决 28：只影响 renderLOD，绝不触碰 level / 仿真状态）
    // -------------------------------------------------------------------------
    void setPlayerPos(int32_t x, int32_t y, int32_t z) {
        playerX_ = x; playerY_ = y; playerZ_ = z;
        updateDistances();
        assignLod();          // 距离变了 → simLOD 可能变（这是允许的）
        applyCameraToRenderLod();
    }

    void setCameraYawTurn(uint16_t yawTurn) {
        cameraYawTurn_ = yawTurn;
        applyCameraToRenderLod();   // 【只】更新 renderLOD
    }
    uint16_t cameraYawTurn() const { return cameraYawTurn_; }

    TickLevel tickLevelOf(uint32_t actorIndex) const { return entities_[(size_t)actorIndex].level; }
    RenderLod renderLodOf(uint32_t actorIndex) const { return entities_[(size_t)actorIndex].renderLod; }

    int32_t fullCount() const {
        int32_t c = 0;
        for (size_t i = 0; i < entities_.size(); ++i)
            if (entities_[i].level == TickLevel::FULL || entities_[i].level == TickLevel::PROTECTED) ++c;
        return c;
    }
    uint64_t lodSwitchTotal() const { return lodSwitchTotal_; }

    // -------------------------------------------------------------------------
    // tickOnce：推进 absTick 这一分钟（ADR-003 D3 / 本文件顶部顺序约定）
    // -------------------------------------------------------------------------
    void tickOnce(int64_t absTick, EventBus* bus, AttributionLog* alog, WriteQueue* wq) {
        if (bus) bus->beginTick((uint64_t)absTick);

        const TickSchedule sch = scheduleFor(absTick);
        const bool isHourly = sch.hourly;

        for (size_t i = 0; i < entities_.size(); ++i) {
            Entity& e = entities_[i];
            if ((e.flags & kFlagAlive) == 0) continue;
            if (e.level == TickLevel::COARSE) {
                if (isHourly) coarseStepHourly(e);       // 按小时闭形式积分
            } else {
                fullStepPerMinute(e);                    // 逐分钟细化
            }
            processBoundaries(e, absTick, bus);          // 两条路径【共用】；唯一随机点
        }

        if (sch.hourly)   hourlyTick(absTick, bus, alog, wq);
        if (sch.dispatch) dispatch(absTick, bus);
        if (sch.cutover)  dailyCutover(absTick, bus, alog, wq);

        // S0 §C3：每 60 tick 计算一次 worldHash
        if (absTick % SNAPSHOT_PERIOD == 0) pushHash(worldHash(absTick));
    }

    // -------------------------------------------------------------------------
    // 快进：与逐 tick 模式【同一 tick 序列】，只是驱动方式不同（S0 §B3.4 / ADR-003 D4）
    //   · 不跳过任何一分钟
    //   · 不更换 RNG 流（boundary 仍锚定自己的 absTick）
    //   · chunk 只是外层切分，不影响任何 tick 内部行为
    // -------------------------------------------------------------------------
    void fastForward(int64_t fromAbsTick, int32_t nTicks, int32_t chunk,
                     EventBus* bus, AttributionLog* alog, WriteQueue* wq) {
        const int32_t c = (chunk <= 0) ? 60 : chunk;
        int32_t done = 0;
        while (done < nTicks) {
            const int32_t m = (nTicks - done < c) ? (nTicks - done) : c;
            for (int32_t k = 0; k < m; ++k)
                tickOnce(fromAbsTick + (int64_t)(done + k), bus, alog, wq);
            done += m;
        }
    }

    // -------------------------------------------------------------------------
    // worldHash（S0 §C3：生理 + 位置量化 0.1m + LOD 档位 + 时间地址）
    //   注意：renderLod / cameraYaw 【不参与】—— 裁决 28 的代码级体现。
    // -------------------------------------------------------------------------
    uint64_t worldHash(int64_t absTick) const {
        Hasher h;
        h.i64(absTick);
        h.u32((uint32_t)entities_.size());
        for (size_t i = 0; i < entities_.size(); ++i) {
            const Entity& e = entities_[i];
            h.u32(e.actorId);
            h.u8((uint8_t)e.level);
            h.u8(e.flags);
            h.i32(e.posX / HASH_QUANTUM_MM);     // 量化到 0.1m
            h.i32(e.posY / HASH_QUANTUM_MM);
            h.i32(e.posZ / HASH_QUANTUM_MM);
            h.i32(e.needWater);
            h.i32(e.needFood);
            h.i32(e.needSleep);
        }
        return h.value();
    }

    uint64_t worldHashNow(int64_t absTick) const { return worldHash(absTick); }

    const std::vector<uint64_t>& hashRing() const { return hashRing_; }
    uint32_t hashRingCount() const { return hashRingCount_; }

    // -------------------------------------------------------------------------
    // boundary 批量收集（ADR-003 D4 / I2 排序键）—— 供批量路径与对账测试使用
    // -------------------------------------------------------------------------
    std::vector<Boundary> collectBoundariesIn(int64_t t0, int64_t t1) const {
        std::vector<Boundary> out;
        for (size_t i = 0; i < entities_.size(); ++i) {
            const Entity& e = entities_[i];
            for (size_t k = e.nextBoundaryIdx; k < e.boundaries.size(); ++k) {
                const Boundary& b = e.boundaries[k];
                if (b.absTick >= (uint64_t)t0 && b.absTick <= (uint64_t)t1) out.push_back(b);
            }
        }
        std::stable_sort(out.begin(), out.end(), boundaryLess);
        return out;
    }

    // 只按 (absTick, actorIndex, seqInActor) 批量【取值】，不改状态 —— 用于验证
    // "批量路径与逐 tick 路径取到同一个随机数"（ADR-003 I1 + I2）
    void evalBoundariesBatch(const std::vector<Boundary>& bs, std::vector<uint32_t>& out) const {
        out.clear();
        out.reserve(bs.size());
        for (size_t i = 0; i < bs.size(); ++i) {
            const Boundary& b = bs[i];
            RngStream rs(seed_, StreamId::SimBoundaryCheck, b.actorIndex);
            out.push_back(rs.at(b.absTick, b.seqInActor));   // 锚定 boundary 的 absTick
        }
    }

    // -------------------------------------------------------------------------
    // 存档（ADR-002 D6 字段集；注意：【不保存任何 per-stream counter】—— ADR-002 D3）
    // -------------------------------------------------------------------------
    struct SaveHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t registryVersion;
        uint64_t seed;
        int32_t  dayKey;
        int32_t  minuteOfDay;
        int64_t  lastExecutedAbsTick;
        uint32_t entityCount;
        uint64_t attributionCount;    // 仅对账用，不参与取值
        uint64_t padding;
    };

    void saveTo(std::vector<uint8_t>& out, int64_t lastExecutedAbsTick, uint64_t attributionCount) const {
        out.clear();
        SaveHeader hd;
        std::memset(&hd, 0, sizeof(hd));
        hd.magic = kSaveMagic;
        hd.version = kSaveVersion;
        hd.registryVersion = SIMCORE_STREAM_REGISTRY_VERSION;
        hd.seed = seed_;
        hd.dayKey = dayKeyOf(lastExecutedAbsTick);
        hd.minuteOfDay = minuteOfDayOf(lastExecutedAbsTick);
        hd.lastExecutedAbsTick = lastExecutedAbsTick;
        hd.entityCount = (uint32_t)entities_.size();
        hd.attributionCount = attributionCount;
        hd.padding = 0;
        putBytes(out, &hd, sizeof(hd));

        for (size_t i = 0; i < entities_.size(); ++i) {
            const Entity& e = entities_[i];
            putU32(out, e.actorId);
            putU32(out, e.actorIndex);
            putU8 (out, e.flags);
            putI32(out, e.distMilli);
            putI32(out, e.posX); putI32(out, e.posY); putI32(out, e.posZ);
            putI32(out, e.velPerMinX); putI32(out, e.velPerMinZ);
            putU8 (out, (uint8_t)e.level);
            putI32(out, e.needWater); putI32(out, e.needFood); putI32(out, e.needSleep);
            putI64(out, e.accWater); putI64(out, e.accFood); putI64(out, e.accSleep);
            putU32(out, e.pendingEvents);
            putU32(out, e.nextBoundaryIdx);
            putU32(out, (uint32_t)e.boundaries.size());
            for (size_t k = 0; k < e.boundaries.size(); ++k) {
                const Boundary& b = e.boundaries[k];
                putU64(out, b.absTick); putU32(out, b.actorIndex);
                putU32(out, b.seqInActor); putU8(out, b.kind);
            }
        }
    }

    // 返回 false 表示存档不合法（版本/注册表不匹配 → 拒绝重放，不静默，S0 §C3）
    bool loadFrom(const std::vector<uint8_t>& in, int64_t* outLastExecutedAbsTick,
                  uint64_t* outAttributionCount) {
        size_t pos = 0;
        SaveHeader hd;
        if (!getBytes(in, pos, &hd, sizeof(hd))) return false;
        if (hd.magic != kSaveMagic) return false;
        if (hd.version != kSaveVersion) return false;
        if (hd.registryVersion != (uint32_t)SIMCORE_STREAM_REGISTRY_VERSION) return false;   // 拒绝重放

        seed_ = hd.seed;
        entities_.assign((size_t)hd.entityCount, Entity());
        for (uint32_t i = 0; i < hd.entityCount; ++i) {
            Entity& e = entities_[(size_t)i];
            e.actorId        = getU32(in, pos);
            e.actorIndex     = getU32(in, pos);
            e.flags          = getU8(in, pos);
            e.distMilli      = getI32(in, pos);
            e.posX           = getI32(in, pos);
            e.posY           = getI32(in, pos);
            e.posZ           = getI32(in, pos);
            e.velPerMinX     = getI32(in, pos);
            e.velPerMinZ     = getI32(in, pos);
            e.level          = (TickLevel)getU8(in, pos);
            e.needWater      = getI32(in, pos);
            e.needFood       = getI32(in, pos);
            e.needSleep      = getI32(in, pos);
            e.accWater       = getI64(in, pos);
            e.accFood        = getI64(in, pos);
            e.accSleep       = getI64(in, pos);
            e.pendingEvents  = (uint16_t)getU32(in, pos);
            e.nextBoundaryIdx= getU32(in, pos);
            const uint32_t nb = getU32(in, pos);
            e.boundaries.clear();
            e.boundaries.reserve(nb);
            for (uint32_t k = 0; k < nb; ++k) {
                Boundary b;
                b.absTick    = getU64(in, pos);
                b.actorIndex = getU32(in, pos);
                b.seqInActor = (uint16_t)getU32(in, pos);
                b.kind       = getU8(in, pos);
                e.boundaries.push_back(b);
            }
            e.renderLod = RenderLod::kLow;   // 表现层状态不进存档（裁决 28）
            e.lodSwitches = 0;
        }
        hashRing_.assign(24, 0);
        hashRingCount_ = 0; hashRingPos_ = 0;
        applyCameraToRenderLod();
        if (outLastExecutedAbsTick) *outLastExecutedAbsTick = hd.lastExecutedAbsTick;
        if (outAttributionCount) *outAttributionCount = hd.attributionCount;
        return true;
    }

private:
    // --- 连续推进（ADR-003 I3：这两条路径【禁止】发起随机判定） -----------------
    static void fullStepPerMinute(Entity& e) {
        e.posX += e.velPerMinX;                 // 逐分钟细化
        e.posZ += e.velPerMinZ;
        e.accWater += kDecayWaterPerHour;  e.needWater = addSat(e.needWater, takeStep(e.accWater));
        e.accFood  += kDecayFoodPerHour;   e.needFood  = addSat(e.needFood,  takeStep(e.accFood));
        e.accSleep += kDecaySleepPerHour;  e.needSleep = addSat(e.needSleep, takeStep(e.accSleep));
        clampNeeds(e);
    }

    static void coarseStepHourly(Entity& e) {
        e.posX += e.velPerMinX * 60;            // 按小时闭形式积分（不逐分钟）
        e.posZ += e.velPerMinZ * 60;
        e.accWater += kDecayWaterPerHour * 60; e.needWater = addSat(e.needWater, takeStep(e.accWater));
        e.accFood  += kDecayFoodPerHour  * 60; e.needFood  = addSat(e.needFood,  takeStep(e.accFood));
        e.accSleep += kDecaySleepPerHour * 60; e.needSleep = addSat(e.needSleep, takeStep(e.accSleep));
        clampNeeds(e);
    }

    static Milli takeStep(MilliL& acc) {
        const MilliL step = acc / 60;
        acc -= step * 60;
        return (Milli)step;
    }
    static void clampNeeds(Entity& e) {
        e.needWater = clampM(e.needWater, 0, SIMCORE_MILLI_INT(100));
        e.needFood  = clampM(e.needFood,  0, SIMCORE_MILLI_INT(100));
        e.needSleep = clampM(e.needSleep, 0, SIMCORE_MILLI_INT(100));
    }

    // --- boundary 补做判定（ADR-003 D3；FULL 与 COARSE 共用） --------------------
    void processBoundaries(Entity& e, int64_t absTick, EventBus* bus) {
        while (e.nextBoundaryIdx < e.boundaries.size()
               && e.boundaries[(size_t)e.nextBoundaryIdx].absTick <= (uint64_t)absTick) {
            const Boundary& b = e.boundaries[(size_t)e.nextBoundaryIdx];
            // I1：锚定【boundary 的 absTick】，绝不传当前 absTick
            RngStream rs(seed_, StreamId::SimBoundaryCheck, b.actorIndex);
            const uint32_t v = rs.at(b.absTick, b.seqInActor);
            const Milli delta = -(Milli)(v % 7u) * 1000;    // -0.0 .. -6.0 点
            e.needWater = clampM(addSat(e.needWater, delta), 0, SIMCORE_MILLI_INT(100));
            if (e.pendingEvents > 0) --e.pendingEvents;
            if (bus) {
                EventPayload p; std::memset(&p, 0, sizeof(p));
                p.i2.a = (int32_t)e.actorIndex; p.i2.b = e.needWater;
                RefId subj = makeActorNeedRef(e.actorId);
                RefId cause = makeContainerRef(b.actorIndex);
                uint32_t acts[1]; acts[0] = e.actorId;
                bus->emit(EventType::kNeedChanged, TickKind::SIM_TICK, subj, cause, acts, 1, 0, p);
            }
            ++e.nextBoundaryIdx;
        }
    }

    // --- HOURLY_TICK -----------------------------------------------------------
    void hourlyTick(int64_t absTick, EventBus* bus, AttributionLog* alog, WriteQueue* wq) {
        (void)absTick; (void)bus; (void)alog; (void)wq;
        // COARSE 实体的按小时积分已在 tickOnce 主循环里完成（isHourly 分支），
        // 这里只做"整点批量应用写队列"的占位（S0 §B4：QUEUED → APPLIED(整点/日切)）。
    }

    // --- DISPATCH（06:00 只发令） ----------------------------------------------
    void dispatch(int64_t absTick, EventBus* bus) {
        const int32_t m = minuteOfDayOf(absTick);
        for (size_t i = 0; i < entities_.size(); ++i) {
            Entity& e = entities_[i];
            RngStream rs(seed_, StreamId::SimWorldInit, (uint32_t)i);
            const uint32_t v0 = rs.at((uint64_t)absTick, 0);
            const uint16_t n = (uint16_t)(1u + (v0 % 4u));       // 1..4 个 boundary
            e.boundaries.clear();
            e.nextBoundaryIdx = 0;
            e.pendingEvents = n;
            for (uint16_t k = 0; k < n; ++k) {
                const uint32_t vk = rs.at((uint64_t)absTick, 1u + k);
                // 边界落在本日 [m+1, 1439]
                const int32_t span = CUTOVER_MINUTE - m;          // 360 → 1079
                const int32_t off  = (span > 0) ? (int32_t)(vk % (uint32_t)span) + 1 : 1;
                Boundary b;
                b.absTick = (uint64_t)absTick + (uint64_t)off;
                b.actorIndex = (uint32_t)i;
                b.seqInActor = k;
                b.kind = 0;
                e.boundaries.push_back(b);
            }
            std::stable_sort(e.boundaries.begin(), e.boundaries.end(), boundaryLess);
        }
        // FULL 名额抢占依赖 pending 事件 → 重新分配 LOD
        assignLod();
        if (bus) {
            EventPayload p; std::memset(&p, 0, sizeof(p));
            RefId subj("S0.world.dispatch");
            RefId cause("S0.clock.dispatch");
            bus->emit(EventType::kScheduleChanged, TickKind::DISPATCH, subj, cause, NULL, 0, 0, p);
        }
    }

    // --- DAILY_CUTOVER（日切） -------------------------------------------------
    void dailyCutover(int64_t absTick, EventBus* bus, AttributionLog* alog, WriteQueue* wq) {
        (void)bus;
        const int32_t dayKey = dayKeyOf(absTick);

        // ADR-003 V8：当日 boundary 必须全部消费，否则 assert
        for (size_t i = 0; i < entities_.size(); ++i) {
            const Entity& e = entities_[i];
            if ((e.flags & kFlagAlive) == 0) continue;
            assert(e.nextBoundaryIdx == e.boundaries.size()
                   && "[simcore::World] 日切时仍有未消费的 boundary（ADR-003 V8）");
        }

        // --- 归因三跳链（Core 验收 1 的机器判据，ADR-004 D8 闭合集） -------------
        // 注意：必须在"发配给"【之前】判定，否则配给会把低水位抬回去，链条永远不触发。
        if (alog) {
            int firstLow = -1;
            for (size_t i = 0; i < entities_.size(); ++i) {
                const Entity& e = entities_[i];
                if ((e.flags & kFlagAlive) == 0) continue;
                if (e.needWater < SIMCORE_MILLI_INT(20)) {
                    uint32_t acts[1]; acts[0] = e.actorId;
                    RefId cause("S1.zone.ruins_a.ctr_12.remaining");
                    RefId effect = makeActorNeedRef(e.actorId);
                    alog->record(cause, effect, acts, 1, (uint64_t)absTick, dayKey);  // step 1
                    if (firstLow < 0) firstLow = (int)i;
                }
            }
            if (firstLow >= 0) {
                uint32_t acts[1]; acts[0] = entities_[(size_t)firstLow].actorId;
                RefId h2cause = makeActorNeedRef(entities_[(size_t)firstLow].actorId);
                RefId h2effect("S3.warehouse.he_valley.water");
                alog->record(h2cause, h2effect, acts, 1, (uint64_t)absTick, dayKey);  // step 2
                RefId h3cause("S3.warehouse.he_valley.water");
                RefId h3effect("S5.community.he_valley.safeDays");
                alog->record(h3cause, h3effect, acts, 1, (uint64_t)absTick, dayKey);  // step 3
            }
        }

        // 日切第 3 步（简化）：发配给 + 回升
        for (size_t i = 0; i < entities_.size(); ++i) {
            Entity& e = entities_[i];
            if ((e.flags & kFlagAlive) == 0) continue;
            e.needWater = clampM(addSat(e.needWater, SIMCORE_MILLI_INT(20)), 0, SIMCORE_MILLI_INT(100));
            e.needFood  = clampM(addSat(e.needFood,  SIMCORE_MILLI_INT(15)), 0, SIMCORE_MILLI_INT(100));
            e.needSleep = clampM(addSat(e.needSleep, SIMCORE_MILLI_INT(50)), 0, SIMCORE_MILLI_INT(100));
        }
        (void)wq;
    }

    // --- LOD 分配（裁决 28：只看距离 + pending 事件 + PROTECTED） ----------------
    struct Candidate {
        uint32_t actorIndex;
        uint8_t  prot;
        uint8_t  hasPending;
        uint8_t  named;
        int32_t  dist;
    };
    static bool candidateLess(const Candidate& a, const Candidate& b) {
        if (a.prot       != b.prot)       return a.prot > b.prot;        // PROTECTED 优先
        if (a.hasPending != b.hasPending) return a.hasPending > b.hasPending;
        if (a.named      != b.named)      return a.named > b.named;
        if (a.dist       != b.dist)       return a.dist < b.dist;        // 距离倒数
        return a.actorIndex < b.actorIndex;                              // 稳定全序
    }

    void assignLod() {
        std::vector<Candidate> cands;
        cands.reserve(entities_.size());
        for (size_t i = 0; i < entities_.size(); ++i) {
            Entity& e = entities_[i];
            if ((e.flags & kFlagAlive) == 0) continue;
            const bool prot = (e.flags & kFlagProtected) != 0;
            bool wantFull;
            if (prot) wantFull = true;
            else if (e.level == TickLevel::FULL) wantFull = (e.distMilli <= LOD_DROP_RADIUS_M * 1000);  // 滞回落
            else wantFull = (e.distMilli <  LOD_FULL_RADIUS_M * 1000);                                  // 滞回升
            if (!wantFull) continue;
            Candidate c;
            c.actorIndex = (uint32_t)i;
            c.prot       = prot ? 1u : 0u;
            c.hasPending = (e.pendingEvents > 0) ? 1u : 0u;
            c.named      = (e.flags & kFlagNamed) ? 1u : 0u;
            c.dist       = e.distMilli;
            cands.push_back(c);
        }
        std::stable_sort(cands.begin(), cands.end(), candidateLess);

        const size_t nFull = (cands.size() < (size_t)LOD_FULL_MAX) ? cands.size() : (size_t)LOD_FULL_MAX;
        std::vector<uint8_t> isFull(entities_.size(), 0);
        for (size_t k = 0; k < nFull; ++k) isFull[(size_t)cands[k].actorIndex] = 1;

        for (size_t i = 0; i < entities_.size(); ++i) {
            Entity& e = entities_[i];
            if ((e.flags & kFlagAlive) == 0) continue;
            TickLevel want;
            if (isFull[i]) want = ((e.flags & kFlagProtected) != 0) ? TickLevel::PROTECTED : TickLevel::FULL;
            else           want = TickLevel::COARSE;
            if (want != e.level) {
                e.level = want;
                ++e.lodSwitches;
                ++lodSwitchTotal_;
            }
        }
    }

    // --- 视锥 → renderLOD（裁决 28：只动表现层，绝不改 level） -------------------
    void applyCameraToRenderLod() {
        for (size_t i = 0; i < entities_.size(); ++i) {
            Entity& e = entities_[i];
            const uint16_t bearing = bearingTurn(e.posX - playerX_, e.posZ - playerZ_);
            const int32_t diff = angleDiffTurn(bearing, cameraYawTurn_);   // ±32768
            const bool inFrustum = (diff >= -10922 && diff <= 10922);      // ±60°
            if (!inFrustum)                     e.renderLod = RenderLod::kLow;
            else if (e.distMilli < 120 * 1000)  e.renderLod = RenderLod::kHigh;
            else                                e.renderLod = RenderLod::kMid;
        }
    }

    void updateDistances() {
        for (size_t i = 0; i < entities_.size(); ++i) {
            Entity& e = entities_[i];
            const int64_t dx = (int64_t)e.posX - (int64_t)playerX_;
            const int64_t dz = (int64_t)e.posZ - (int64_t)playerZ_;
            // 整数欧氏距离（不开根号以保持定点纯度：用 |dx|+|dz| 的 Chebyshev 近似
            // 会破坏距离语义，故这里用整数牛顿法开平方 —— ADR-005 D3 允许）
            e.distMilli = (int32_t)isqrt64(dx * dx + dz * dz);
        }
    }

    // 整数开平方（牛顿/位运算，禁 std::sqrt —— ADR-005 D3）
    static uint64_t isqrt64(uint64_t x) {
        if (x == 0) return 0;
        uint64_t r = 1u;
        while (r <= x / r) r <<= 1;                 // 上界
        uint64_t lo = r >> 1, hi = r;
        while (lo + 1 < hi) {
            const uint64_t mid = (lo + hi) >> 1;
            if (mid <= x / mid) lo = mid; else hi = mid;
        }
        return lo;
    }

    // 方位角：8 象限 + 段内线性插值的整数近似（无三角函数、无浮点；架构 stub）
    static uint16_t bearingTurn(int32_t dx, int32_t dz) {
        const int32_t ax = (dx < 0) ? -dx : dx;
        const int32_t az = (dz < 0) ? -dz : dz;
        uint32_t oct;
        if (ax >= az) {
            const int32_t r = (ax == 0) ? 0 : (int32_t)((int64_t)az * 8192 / ax);
            oct = (uint32_t)r;
        } else {
            const int32_t r = (az == 0) ? 0 : (int32_t)((int64_t)ax * 8192 / az);
            oct = (uint32_t)(16384 - r);
        }
        uint32_t base;
        if (dx >= 0 && dz >= 0)      base = oct;
        else if (dx < 0 && dz >= 0)  base = 32768u - oct;
        else if (dx < 0 && dz < 0)   base = 32768u + oct;
        else                         base = 65536u - oct;
        return (uint16_t)(base & 0xFFFFu);
    }
    static int32_t angleDiffTurn(uint16_t a, uint16_t b) {
        int32_t d = (int32_t)a - (int32_t)b;
        if (d >  32768) d -= 65536;
        if (d < -32768) d += 65536;
        return d;
    }

    static RefId makeActorNeedRef(uint32_t actorId) {
        char buf[kRefIdCap];
        std::snprintf(buf, kRefIdCap, "S2.actor.%u.need", (unsigned)(actorId & ~kAnonymousFlag));
        return RefId(buf);
    }
    static RefId makeContainerRef(uint32_t actorIndex) {
        char buf[kRefIdCap];
        std::snprintf(buf, kRefIdCap, "S1.zone.ruins_a.ctr_%02u.remaining", (unsigned)(actorIndex % 40u));
        return RefId(buf);
    }

    void pushHash(uint64_t h) {
        hashRing_[(size_t)hashRingPos_] = h;
        hashRingPos_ = (hashRingPos_ + 1) % 24u;
        if (hashRingCount_ < 24u) ++hashRingCount_;
    }

    // --- 序列化小工具 ---
    static void putBytes(std::vector<uint8_t>& o, const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        o.insert(o.end(), b, b + n);
    }
    static void putU8 (std::vector<uint8_t>& o, uint8_t  v) { o.push_back(v); }
    static void putU32(std::vector<uint8_t>& o, uint32_t v) { putBytes(o, &v, 4); }
    static void putI32(std::vector<uint8_t>& o, int32_t  v) { putBytes(o, &v, 4); }
    static void putU64(std::vector<uint8_t>& o, uint64_t v) { putBytes(o, &v, 8); }
    static void putI64(std::vector<uint8_t>& o, int64_t  v) { putBytes(o, &v, 8); }

    static bool getBytes(const std::vector<uint8_t>& in, size_t& pos, void* p, size_t n) {
        if (pos + n > in.size()) return false;
        std::memcpy(p, &in[pos], n);
        pos += n;
        return true;
    }
    static uint8_t  getU8 (const std::vector<uint8_t>& in, size_t& pos) { uint8_t v = 0;  getBytes(in, pos, &v, 1); return v; }
    static uint32_t getU32(const std::vector<uint8_t>& in, size_t& pos) { uint32_t v = 0; getBytes(in, pos, &v, 4); return v; }
    static int32_t  getI32(const std::vector<uint8_t>& in, size_t& pos) { int32_t v = 0;  getBytes(in, pos, &v, 4); return v; }
    static uint64_t getU64(const std::vector<uint8_t>& in, size_t& pos) { uint64_t v = 0; getBytes(in, pos, &v, 8); return v; }
    static int64_t  getI64(const std::vector<uint8_t>& in, size_t& pos) { int64_t v = 0;  getBytes(in, pos, &v, 8); return v; }

    // 生理衰减率（S0 §D：水 -8 / 食 -4 / 睡 -5 点每小时），单位 milli
    static const MilliL kDecayWaterPerHour = -8000;
    static const MilliL kDecayFoodPerHour  = -4000;
    static const MilliL kDecaySleepPerHour = -5000;

    uint64_t seed_;
    std::vector<Entity> entities_;
    int32_t playerX_, playerY_, playerZ_;
    uint16_t cameraYawTurn_;
    uint64_t lodSwitchTotal_;
    std::vector<uint64_t> hashRing_;
    uint32_t hashRingCount_;
    uint32_t hashRingPos_;
};

}  // namespace simcore

#endif  // SIMCORE_WORLD_H

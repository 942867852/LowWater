// ============================================================================
// src/simcore/view.h · 表现层只读投影契约（ViewSnapshot + WorldSignalRegistry）
// ----------------------------------------------------------------------------
// 本文件是 docs/architecture/view-contract.md 的【唯一实现载体】。
//   文档与代码的字段必须【逐字一致】：改名 / 增删字段 = 变更契约 = 三路并行
//   （UX 规格 / 资产规格 / Epic 拆分）全部返工。改字段前先改 view-contract.md。
//
// 硬边界（结构性保证，逐条见 view-contract.md §5）：
//   · 只读：本文件只放【结构体 + 只读访问器】，不放任何写入口；
//     WorldSignalRegistry 的安装 / 撤销路径在仿真侧，不经本文件。
//   · 值拷贝：ViewSnapshot 是 POD 缓冲，不含指向仿真内存的指针，
//     表现层无法经快照别名回写仿真状态。
//   · 不进存档、不参与 worldHash（S0 §C3）。
//   · 相机派生字段（SimViewCamera）只允许 renderLOD / 可读性保持锥消费，
//     绝不回喂 simLOD（ADR-003 D2 / 裁决 28）。
//   · 仿真成本恒为 0：本契约的一切消费都发生在表现层（accessibility.md §0）。
//
// 语言：C++14 兼容子集（MinGW GCC 6.3 为验证下限）；C ABI 兼容。
//   DLL 级导出在桥接 / ABI TU 用 extern "C" 包一层即可（形态见
//   tools/spike/abi-probe/simcore_abi.cpp）；本文件已把 ABI 布局钉死。
// ============================================================================
#ifndef SIMCORE_VIEW_H
#define SIMCORE_VIEW_H

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// C ABI 只读契约：容量常量 / 枚举 / 结构体
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

// ---- 容量常量（数组定长：零堆分配、ABI 定长） -----------------------------
enum {
    SIMVIEW_MAX_ACTORS        = 120,   // 场内实体上限（S0 §D 附录 B-14）
    SIMVIEW_MAX_SIGNALS       = 48,    // 活跃 WorldSignal 上限（4 类 × ≤12）
    SIMVIEW_MAX_BOARDS        = 2,     // 社区数（he_valley / jing_cell）
    SIMVIEW_MAX_BOARD_ENTRIES = 3,     // 每社区板 ≤3（S5 GAP_SIGNAL_MAX / S7 §3.4）
    SIMVIEW_MAX_LEDGER        = 3,     // 欠条页 ≤3（S4 §6-3）
    SIMVIEW_NEED_COUNT        = 6,     // needSeverity[6]
    SIMVIEW_REFID_CAP         = 64,    // 与 simcore::kRefIdCap 一致（ADR-004 D7）
    SIMVIEW_PHYSIO_COUNT      = 4,     // water / food / sleep / rad（S1 PhysioState）
    SIMVIEW_ACTOR_POS_AXES    = 3,     // posMm[X, Y, Z]
    // 契约 G-SIG-LEAD：信号先于行为 ≥6 游戏小时（S2 §4.1 / §6-5 / A6）。
    //   6 游戏小时 = 360 游戏分钟 = 360 absTick。
    SIMVIEW_SIGNAL_LEAD_MIN_TICKS = 360
};

// ---- 枚举（uint8；值域封闭，顺序冻结，禁止重排） ---------------------------
// 顺序与 docs/architecture/view-contract.md §3 逐字一致。

// S2 §2.2 闭集 ActivityId（14 值；含 DEAD / FLED）
typedef uint8_t SimViewActivityId;
enum {
    SIMVIEW_ACT_SLEEP = 0, SIMVIEW_ACT_RITUAL, SIMVIEW_ACT_COMMUTE, SIMVIEW_ACT_WORK,
    SIMVIEW_ACT_HAUL, SIMVIEW_ACT_SOCIAL, SIMVIEW_ACT_WATCH, SIMVIEW_ACT_PATROL,
    SIMVIEW_ACT_TREAT, SIMVIEW_ACT_EAT, SIMVIEW_ACT_SEEK, SIMVIEW_ACT_ALERT,
    SIMVIEW_ACT_DEAD, SIMVIEW_ACT_FLED,
    SIMVIEW_ACT_COUNT = 14
};

// 携带物剪影（accessibility §10.1 / §7.1：桶 / 灯 / 工具 / 袋 / 无）
typedef uint8_t SimViewCarrier;
enum {
    SIMVIEW_CARRY_NONE = 0, SIMVIEW_CARRY_BUCKET, SIMVIEW_CARRY_LAMP,
    SIMVIEW_CARRY_TOOL, SIMVIEW_CARRY_BAG, SIMVIEW_CARRY_COUNT = 5
};

// WorldSignal.changeKind（S2 §3.5：四种，没有第五种）
typedef uint8_t SimViewSignalKind;
enum {
    SIMVIEW_SIG_REROUTE = 0, SIMVIEW_SIG_ADD_WATCH,
    SIMVIEW_SIG_DEEPEN_STASH, SIMVIEW_SIG_SET_TRAP,
    SIMVIEW_SIG_COUNT = 4
};

// WorldSignal 生命周期状态（expired := state != ACTIVE）
typedef uint8_t SimViewSignalState;
enum { SIMVIEW_SIG_ACTIVE = 0, SIMVIEW_SIG_EXPIRED = 1, SIMVIEW_SIG_REVOKED = 2 };

// needSeverity 档（S2 §3.1：v<40 → 1，v<20 → 2，v<5 → 3）
typedef uint8_t SimViewSeverity;
enum { SIMVIEW_SEV_NONE = 0, SIMVIEW_SEV_LOW = 1, SIMVIEW_SEV_MID = 2, SIMVIEW_SEV_HIGH = 3 };

// needSeverity[6] 的下标顺序（冻结）：生理三项在前，自有需求三项在后
typedef uint8_t SimViewNeedIndex;
enum {
    SIMVIEW_NEED_WATER = 0, SIMVIEW_NEED_FOOD, SIMVIEW_NEED_SLEEP,
    SIMVIEW_NEED_SAFETY, SIMVIEW_NEED_SOCIAL, SIMVIEW_NEED_CREED
};

// renderHint 位域：只承载"距离分档"所必需的 sim 派生提示（非 renderLOD！）
typedef uint8_t SimViewRenderHint;
enum { SIMVIEW_HINT_MOVING = 1 << 0, SIMVIEW_HINT_CARRIES_LIGHT = 1 << 1 };

// CommunityId（S0 §D）
typedef uint8_t SimViewCommunityId;
enum {
    SIMVIEW_COMMUNITY_HE_VALLEY = 0, SIMVIEW_COMMUNITY_JING_CELL = 1,
    SIMVIEW_COMMUNITY_COUNT = 2
};

// ClusterId（S0 §D 六键闭集；FOOD 为第 6 键）
typedef uint8_t SimViewClusterId;
enum {
    SIMVIEW_CLUSTER_WATER = 0, SIMVIEW_CLUSTER_FUEL, SIMVIEW_CLUSTER_AMMO,
    SIMVIEW_CLUSTER_MEDS, SIMVIEW_CLUSTER_SEED, SIMVIEW_CLUSTER_FOOD,
    SIMVIEW_CLUSTER_COUNT = 6
};

// WorkOrder.kind（S7 §2.1 闭集四值）
typedef uint8_t SimViewWorkOrderKind;
enum { SIMVIEW_WO_POST = 0, SIMVIEW_WO_CREED, SIMVIEW_WO_PERSON, SIMVIEW_WO_STOCK };

// WorkOrder.state（S7 §2.1）
typedef uint8_t SimViewWorkOrderState;
enum { SIMVIEW_WO_OPEN = 0, SIMVIEW_WO_NOTED, SIMVIEW_WO_RESOLVED, SIMVIEW_WO_EXPIRED };

// haulFit（S7 §3.2；OVER 降权但不隐藏）
typedef uint8_t SimViewHaulFit;
enum { SIMVIEW_HAUL_FITS = 0, SIMVIEW_HAUL_TIGHT, SIMVIEW_HAUL_OVER };

// ---- 结构体（值拷贝契约；标准布局 POD，ABI 定长） --------------------------

// per actor（accessibility §10.1 + S9 §0）
typedef struct SimViewActor {
    uint32_t actorId;                            // S0 §D ActorId 的内部 uint32 编码
    uint8_t  isNamed;                            // 1 = 有名（≤16）
    uint8_t  activityId;                         // SimViewActivityId
    uint8_t  needSeverity[SIMVIEW_NEED_COUNT];   // 0..3，顺序见 SimViewNeedIndex
    uint8_t  carryingSilhouette;                 // SimViewCarrier
    int32_t  posMm[SIMVIEW_ACTOR_POS_AXES];      // 世界坐标 mm（ADR-005：真值 ×1000）
    uint16_t facingTurn;                         // 朝向，1/65536 圈（ADR-005 角度表示）
    uint8_t  renderHint;                         // 位域 SimViewRenderHint
    uint8_t  _pad0;                              // 显式对齐（ABI 稳定）
} SimViewActor;

// per signal —— 同一结构体同时是 WorldSignalRegistry 条目与快照投影
//   expired 由只读访问器 simview_signal_expired() 派生（state != ACTIVE）。
typedef struct SimWorldSignal {
    uint32_t signalId;                           // 单调递增（同 seed → 逐位同）
    uint8_t  kind;                               // SimViewSignalKind
    uint8_t  state;                              // SimViewSignalState
    uint8_t  _pad0[2];
    char     refId[SIMVIEW_REFID_CAP];           // "<signalKind>:<refId>"（S2 §5.1）
    uint64_t installTick;                        // 信号装进世界的 absTick（写队列整点应用）
    uint64_t applyTick;                          // 行为变更生效 absTick（次日 DISPATCH）
                                                 // 契约 G-SIG-LEAD：applyTick - installTick ≥ 360
    uint64_t expireTick;                         // 0 = 不急；降级 / 撤销时置
} SimWorldSignal;

// per board entry（S7 §2.1 契约 7 字段 + S9 渲染所需的派生量）
typedef struct SimViewWorkOrder {
    char     orderId[SIMVIEW_REFID_CAP];         // "wo.<community>.<mergeKeySlug>"（S7 §3.5）
    uint8_t  communityId;                        // SimViewCommunityId
    uint8_t  kind;                               // SimViewWorkOrderKind
    uint8_t  needCluster;                        // SimViewClusterId（≡ GapSignal.cluster）
    uint8_t  urgency;                            // 1..5（仅用于选介质；数字【不渲染】）
    int32_t  qtyMilli;                           // ≡ deficitUnits 合并后 Σ（Milli）
    int32_t  deadlineDay;                        // ≡ expiresDayKey
    uint8_t  haulFit;                            // SimViewHaulFit
    uint8_t  state;                              // SimViewWorkOrderState
    uint8_t  _pad0[2];
    int32_t  travelMinQuantized;                 // 距玩家行程档，5 分钟量化（S7 §3.2）
    char     headlineCode[SIMVIEW_REFID_CAP];    // UI 取那句话的键（禁自由文本）
    char     targetLocationId[SIMVIEW_REFID_CAP];// 目的地（S8 locationId；供画方位）
} SimViewWorkOrder;

// per board（S9 §0：WorkOrder[] + urgency + safeDays + trendDelta7d）
typedef struct SimViewBoard {
    uint8_t  communityId;
    uint8_t  entryCount;                         // ≤ SIMVIEW_MAX_BOARD_ENTRIES
    uint8_t  _pad0[2];
    int32_t  safeDaysMilli;                      // SAFE_DAYS 木桶值（Milli；不美化、不缩水）
    int32_t  trendDelta7dMilli;                  // 7 日趋势（Milli；板头箭头）
    SimViewWorkOrder entries[SIMVIEW_MAX_BOARD_ENTRIES];
    char     sedimentLine[SIMVIEW_REFID_CAP];    // 沉降小字（S7 §2.2；不占配额）
} SimViewBoard;

// HUD（S9 §0 / §6.2：只有"玩家自身状态 + 时间"）
typedef struct SimViewHud {
    int32_t  dayKey;                             // S0 §B2
    int32_t  minuteOfDay;                        // 0..1439
    int32_t  physio[SIMVIEW_PHYSIO_COUNT];       // water / food / sleep / rad（Milli，S1）
    int32_t  loadCurrentKgMilli;                 // 当前负重（Milli）
    int32_t  speedMulMilli;                      // SPEED_MUL（Milli，S0 §D）
    int32_t  staminaMilli;                       // 体力（Milli）
    uint32_t ledgerCount;                        // ≤ SIMVIEW_MAX_LEDGER
    char     ledgerHeadline[SIMVIEW_MAX_LEDGER][SIMVIEW_REFID_CAP];  // 欠条页短句
} SimViewHud;

// 相机派生字段（renderLOD / 可读性保持锥专用；见 view-contract.md §5.3）
//   —— 由表现层每帧写入（相机是表现层自有输入），不参与仿真、不进存档、不进 worldHash。
typedef struct SimViewCamera {
    uint16_t centerDirTurn;   // 屏幕中心方向，1/65536 圈（可读性保持锥 ±25° 的轴）
    uint16_t fovHalfTurnH;    // 水平半视锥角；默认 10922 ≈ 半角 60 度
    uint16_t fovHalfTurnV;    // 垂直半视锥角
    uint16_t playerYawTurn;   // 相机水平朝向（与 centerDir 同源）
} SimViewCamera;

// 表现层唯一的正式数据源（架构 §3.2 simcore_view_snapshot() 的载荷）
typedef struct SimViewSnapshot {
    int64_t  publishedAbsTick;                   // 快照发布对应的 absTick（双缓冲，只在 tick 边界发布）
    uint32_t publishSeq;                         // 发布序号（对账用；不进 worldHash）
    uint32_t actorCount;                         // ≤ SIMVIEW_MAX_ACTORS
    uint32_t signalCount;                        // ≤ SIMVIEW_MAX_SIGNALS
    uint32_t boardCount;                         // ≤ SIMVIEW_MAX_BOARDS
    SimViewActor   actors[SIMVIEW_MAX_ACTORS];
    SimWorldSignal signals[SIMVIEW_MAX_SIGNALS];
    SimViewBoard   boards[SIMVIEW_MAX_BOARDS];
    SimViewHud     hud;
    SimViewCamera  camera;                       // 【相机派生】只允许 renderLOD 侧消费
} SimViewSnapshot;

// WorldSignalRegistry（S2 §2.2 WorldSignal 的注册表；唯一定义处）
//   注意：安装 / 撤销是【仿真侧写路径】，不在本文件；本文件只给只读视图。
typedef struct SimWorldSignalRegistry {
    uint32_t       count;
    uint32_t       nextSignalId;
    SimWorldSignal entries[SIMVIEW_MAX_SIGNALS];
} SimWorldSignalRegistry;

#ifdef __cplusplus
}  // extern "C"
#endif

// ---------------------------------------------------------------------------
// 只读访问器（C 兼容 static inline；无写入口）
// ---------------------------------------------------------------------------
static inline uint8_t simview_signal_expired(const SimWorldSignal* s) {
    return (s->state != SIMVIEW_SIG_ACTIVE) ? 1u : 0u;
}
static inline int64_t simview_signal_lead_ticks(const SimWorldSignal* s) {
    return (int64_t)s->applyTick - (int64_t)s->installTick;
}
// 契约 G-SIG-LEAD：信号先于行为 ≥ 6 游戏小时
static inline int simview_signal_lead_ok(const SimWorldSignal* s) {
    return (simview_signal_lead_ticks(s) >= (int64_t)SIMVIEW_SIGNAL_LEAD_MIN_TICKS) ? 1 : 0;
}
static inline uint32_t simview_actor_count(const SimViewSnapshot* v) { return v->actorCount; }
static inline const SimViewActor* simview_actor_at(const SimViewSnapshot* v, uint32_t i) {
    return (i < v->actorCount) ? &v->actors[i] : (const SimViewActor*)0;
}
static inline uint32_t simview_signal_count(const SimViewSnapshot* v) { return v->signalCount; }
static inline const SimWorldSignal* simview_signal_at(const SimViewSnapshot* v, uint32_t i) {
    return (i < v->signalCount) ? &v->signals[i] : (const SimWorldSignal*)0;
}
static inline int simview_contract_version(void) { return 1; }

// ---------------------------------------------------------------------------
// C++ 侧：只读投影实现（需 World；仅 C++ TU 可见）
// ---------------------------------------------------------------------------
#ifdef __cplusplus
#include "world.h"

namespace simcore {

// need 值（Milli，0..100000）→ severity 档（S2 §3.1）
static inline uint8_t simview_severity_of_need(Milli v) {
    if (v < SIMCORE_MILLI_INT(5))  return (uint8_t)SIMVIEW_SEV_HIGH;
    if (v < SIMCORE_MILLI_INT(20)) return (uint8_t)SIMVIEW_SEV_MID;
    if (v < SIMCORE_MILLI_INT(40)) return (uint8_t)SIMVIEW_SEV_LOW;
    return (uint8_t)SIMVIEW_SEV_NONE;
}

// 占位朝向：按速度四象限（TBD：S2 落地后回填精确 facing）
static inline uint16_t simview_facing_of_vel(int32_t vx, int32_t vz) {
    if (vx == 0 && vz == 0) return (uint16_t)0u;
    const int32_t ax = (vx < 0) ? -vx : vx;
    const int32_t az = (vz < 0) ? -vz : vz;
    if (ax >= az) return (vx >= 0) ? (uint16_t)0u     : (uint16_t)32768u;
    return              (vz >= 0) ? (uint16_t)16384u  : (uint16_t)49152u;
}

// ---------------------------------------------------------------------------
// projectView：只读投影。形参 const World& —— 结构性保证不修改任何仿真状态。
//   同一个 (world, absTick, registry, camera) → 逐位相同的快照。
//   【不】参与 worldHash、【不】进存档、【不】产生任何仿真开销。
// ---------------------------------------------------------------------------
inline void projectView(const World& w,
                        int64_t absTick,
                        const ::SimWorldSignalRegistry* reg,
                        const ::SimViewCamera& cam,
                        ::SimViewSnapshot& out) {
    out.publishedAbsTick = absTick;
    out.publishSeq = (uint32_t)((uint64_t)absTick & 0xFFFFFFFFu);

    // ---- actors --------------------------------------------------------
    out.actorCount = 0;
    const size_t n = w.entityCount();
    for (size_t i = 0; i < n && out.actorCount < (uint32_t)SIMVIEW_MAX_ACTORS; ++i) {
        const Entity& e = w.entity(i);
        const bool alive = (e.flags & kFlagAlive) != 0;
        ::SimViewActor& a = out.actors[out.actorCount++];
        a.actorId  = e.actorId;
        a.isNamed  = ((e.flags & kFlagNamed) != 0) ? 1u : 0u;
        // 占位 activity（TBD：S2 §2.2 回填真实 activityId）
        if (!alive)                                                        a.activityId = (uint8_t)SIMVIEW_ACT_DEAD;
        else if (e.needWater < SIMCORE_MILLI_INT(20) || e.needFood < SIMCORE_MILLI_INT(20))
                                                                           a.activityId = (uint8_t)SIMVIEW_ACT_SEEK;
        else if (e.level == TickLevel::COARSE)                             a.activityId = (uint8_t)SIMVIEW_ACT_COMMUTE;
        else                                                               a.activityId = (uint8_t)SIMVIEW_ACT_WORK;
        a.needSeverity[SIMVIEW_NEED_WATER]  = simview_severity_of_need(e.needWater);
        a.needSeverity[SIMVIEW_NEED_FOOD]   = simview_severity_of_need(e.needFood);
        a.needSeverity[SIMVIEW_NEED_SLEEP]  = simview_severity_of_need(e.needSleep);
        a.needSeverity[SIMVIEW_NEED_SAFETY] = (uint8_t)SIMVIEW_SEV_NONE;   // TBD：S2 回填
        a.needSeverity[SIMVIEW_NEED_SOCIAL] = (uint8_t)SIMVIEW_SEV_NONE;   // TBD：S2 回填
        a.needSeverity[SIMVIEW_NEED_CREED]  = (uint8_t)SIMVIEW_SEV_NONE;   // TBD：S2 回填
        a.carryingSilhouette = (uint8_t)SIMVIEW_CARRY_NONE;               // TBD：S2 回填
        a.posMm[0] = e.posX; a.posMm[1] = e.posY; a.posMm[2] = e.posZ;
        a.facingTurn = simview_facing_of_vel(e.velPerMinX, e.velPerMinZ);
        a.renderHint = 0;
        if (e.velPerMinX != 0 || e.velPerMinZ != 0) a.renderHint |= (uint8_t)SIMVIEW_HINT_MOVING;
        a._pad0 = 0;
    }

    // ---- signals（只读镜像 registry） ---------------------------------
    out.signalCount = 0;
    if (reg != 0) {
        for (uint32_t i = 0; i < reg->count && out.signalCount < (uint32_t)SIMVIEW_MAX_SIGNALS; ++i)
            out.signals[out.signalCount++] = reg->entries[i];
    }

    // ---- boards（TBD：S7 落地后回填；当前占位为空） --------------------
    out.boardCount = 0;

    // ---- HUD -----------------------------------------------------------
    out.hud.dayKey      = dayKeyOf(absTick);
    out.hud.minuteOfDay = minuteOfDayOf(absTick);
    if (n > 0) {
        const Entity& p = w.entity(0);            // 占位：玩家 actor（TBD）
        out.hud.physio[0] = p.needWater;
        out.hud.physio[1] = p.needFood;
        out.hud.physio[2] = p.needSleep;
    } else {
        out.hud.physio[0] = out.hud.physio[1] = out.hud.physio[2] = 0;
    }
    out.hud.physio[3] = 0;                        // rad（TBD：S1 回填）
    out.hud.loadCurrentKgMilli = 0;               // TBD：S1 getLoad
    out.hud.speedMulMilli      = SIMCORE_MILLI_PARTS(1, 150);  // SPEED_MUL 上界占位
    out.hud.staminaMilli       = 0;               // TBD：S0 §D STAMINA_POOL
    out.hud.ledgerCount        = 0;               // TBD：S4

    // ---- camera（相机派生：原样回读；只允许 renderLOD 侧消费） ---------
    out.camera = cam;
}

}  // namespace simcore
#endif  // __cplusplus

#endif  // SIMCORE_VIEW_H

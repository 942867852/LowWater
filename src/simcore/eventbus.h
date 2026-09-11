// ============================================================================
// src/simcore/eventbus.h · 事件总线 + 因果引用（AttributionLink）
// ----------------------------------------------------------------------------
// 契约来源（逐字实现，不重新设计）：
//   ADR-004 D1：总线形态 = 同步、有序、单线程、按 tick 分代；不做异步/优先级/
//               跨线程/合并/延迟；订阅表启动时静态构建，运行期不可增删。
//   ADR-004 D2：规则 A-BOUNDARY —— 只有【跨系统】因果才记 AttributionLink，
//               机械标准 = causeRef 与 effectRef 的 <system> 段不同。
//   ADR-004 D3：step 沿 causeRef 上溯 = parent.step + 1；step > 3 报设计违规，
//               不静默截断；跨日另计 latencyDays，不重置 step。
//   ADR-004 D4：causeRef 空 → UNATTRIBUTED（step 0 + 告警 + 计数），不静默。
//   ADR-004 D6：append-only，永不修改；撤销靠追加反向 link。
//   ADR-004 D7：RefId = "<system>.<entityType>.<id>[.<field>]"；S0 §C2 契约。
//   S0 §C7-4  ：step > 3 即设计违规，报警 + 记入评审。
// ----------------------------------------------------------------------------
// 语言约束：C++14 兼容子集。不引入 <string>（RefId 用定长栈缓冲，禁堆分配）。
// ============================================================================
#ifndef SIMCORE_EVENTBUS_H
#define SIMCORE_EVENTBUS_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <vector>

#include "fixed.h"
#include "clock.h"

namespace simcore {

// ---------------------------------------------------------------------------
// RefId（ADR-004 D7）：定长 64B 栈上缓冲 + 小字符串优化，禁堆分配
// ---------------------------------------------------------------------------
static const size_t kRefIdCap = 64;

struct RefId {
    char s[kRefIdCap];

    RefId() { s[0] = '\0'; }
    explicit RefId(const char* v) { s[0] = '\0'; set(v); }

    void set(const char* v) {
        size_t i = 0;
        if (v) { for (; v[i] != '\0' && i + 1 < kRefIdCap; ++i) s[i] = v[i]; }
        s[i] = '\0';
    }
    bool empty() const { return s[0] == '\0'; }
    bool operator==(const RefId& o) const { return std::strcmp(s, o.s) == 0; }
    bool operator!=(const RefId& o) const { return std::strcmp(s, o.s) != 0; }
    bool operator<(const RefId& o)  const { return std::strcmp(s, o.s) < 0; }
};

// <system> 段（第一个 '.' 之前）
inline void systemOfRef(const RefId& r, char out[8]) {
    size_t i = 0;
    for (; r.s[i] != '\0' && r.s[i] != '.' && i + 1 < 8; ++i) out[i] = r.s[i];
    out[i] = '\0';
}

// 规则 A-BOUNDARY 的机械判据
inline bool isCrossSystem(const RefId& cause, const RefId& effect) {
    char a[8], b[8];
    systemOfRef(cause, a);
    systemOfRef(effect, b);
    return std::strcmp(a, b) != 0;
}

// RefId 校验（ADR-004 D7，debug 下每次 recordAttribution 都校验两端）
inline bool validateRefId(const RefId& r) {
    if (r.empty()) return false;
    // system ∈ {S0..S7}（闭集）
    if (!(r.s[0] == 'S' && r.s[1] >= '0' && r.s[1] <= '7' && r.s[2] == '.')) return false;
    // 至少还要有 entityType 段
    const char* p = std::strchr(r.s + 3, '.');
    if (p == NULL) return false;
    return true;
}

// ---------------------------------------------------------------------------
// 匿名 actor：ActorId 最高位（S0 §D：anon.<nnn>）
// ADR-004 D3：匿名 actor 仅 step ≤ 2 时记入 actorIds[]
// ---------------------------------------------------------------------------
static const uint32_t kAnonymousFlag = 0x80000000u;
inline bool isAnonymousActor(uint32_t actorId) { return (actorId & kAnonymousFlag) != 0; }

// ---------------------------------------------------------------------------
// 事件（ADR-004 D1）
// ---------------------------------------------------------------------------
static const uint32_t kMaxActorIdsPerEvent = 4;

union EventPayload {
    Milli   m;          // 定点量
    int32_t i;
    uint32_t u;
    struct { int32_t a; int32_t b; } i2;
    char    raw[16];
};

enum class EventType : uint16_t {
    kUnspecified     = 0,
    kContainerDrained = 1,   // S1：容器被清空
    kNeedChanged      = 2,   // S2：NPC 需求变化
    kScheduleChanged  = 3,   // S2：日程改道
    kStockChanged     = 4,   // S3：库存变化
    kSafeDaysChanged  = 5,   // S5：社区安全天数变化
    kLodSwitch        = 6,   // S0·B：LOD 切换（裁决 28 相关）
    kWriteQueued      = 7,   // S0·B：写队列入队
    kWriteApplied     = 8    // S0·B：写队列应用
};

struct SimEvent {
    uint64_t    eventId;     // 全局单调递增 = 已发出事件总数，确定性
    uint64_t    absTick;
    uint32_t    emitSeq;     // 同一 tick 内的发出序号
    EventType   type;
    TickKind    kind;
    RefId       subjectRef;  // 事件主体
    RefId       causeRef;    // 可为空 → UNATTRIBUTED（ADR-004 D4）
    uint32_t    actorIds[kMaxActorIdsPerEvent];
    uint8_t     actorCount;
    uint8_t     actorOverflow;   // 超出定长数组的部分只记数（ADR-004 D1）
    uint16_t    payloadKind;
    EventPayload payload;
};

// ---------------------------------------------------------------------------
// EventBus（同步、有序、单线程）
// ---------------------------------------------------------------------------
typedef void (*EventHandler)(const SimEvent& ev, void* user);

class EventBus {
public:
    EventBus() : sealed_(false), curAbsTick_(0), emitSeq_(0), nextEventId_(0), handler_(NULL), handlerUser_(NULL) {}

    // 订阅：只允许在 boot 期调用（ADR-004 D1）
    void subscribe(EventHandler fn, void* user) {
        assert(!sealed_ && "[simcore::EventBus] subscribe 只允许在 boot 期调用");
        handler_ = fn;
        handlerUser_ = user;
    }
    void seal() { sealed_ = true; }

    void beginTick(uint64_t absTick) { curAbsTick_ = absTick; emitSeq_ = 0; }   // 每 tick 归零
    uint64_t curAbsTick() const { return curAbsTick_; }
    uint32_t emitSeq() const { return emitSeq_; }
    uint64_t nextEventId() const { return nextEventId_; }

    // 同步投递（深度优先）+ 记入事件日志
    void emit(EventType type, TickKind kind,
              const RefId& subject, const RefId& cause,
              const uint32_t* actors, uint8_t actorCount,
              uint16_t payloadKind, const EventPayload& payload) {
        SimEvent ev;
        ev.eventId   = nextEventId_++;
        ev.absTick   = curAbsTick_;
        ev.emitSeq   = emitSeq_++;
        ev.type      = type;
        ev.kind      = kind;
        ev.subjectRef = subject;
        ev.causeRef  = cause;
        ev.actorCount = 0;
        ev.actorOverflow = 0;
        for (uint8_t i = 0; i < actorCount; ++i) {
            if (i < kMaxActorIdsPerEvent) ev.actorIds[ev.actorCount++] = actors[i];
            else ++ev.actorOverflow;
        }
        ev.payloadKind = payloadKind;
        ev.payload = payload;
        log_.push_back(ev);
        if (handler_) handler_(ev, handlerUser_);   // 同步投递；handler 只许写写队列
    }

    const std::vector<SimEvent>& log() const { return log_; }
    size_t eventCount() const { return log_.size(); }
    void clearLog() { log_.clear(); }

private:
    bool sealed_;
    uint64_t curAbsTick_;
    uint32_t emitSeq_;
    uint64_t nextEventId_;
    EventHandler handler_;
    void* handlerUser_;
    std::vector<SimEvent> log_;
};

// ---------------------------------------------------------------------------
// AttributionLink（S0 §C2 契约字段，逐字保留）
//   { step, causeRef, effectRef, actorIds[], dayKey }
//   + 实现侧补：absTick / linkId / latencyDays（ADR-004 D3 跨日另计）
// ---------------------------------------------------------------------------
struct AttributionLink {
    uint64_t linkId;      // 实现侧：append-only 序号
    uint8_t  step;        // 从 1 起；0 表示 UNATTRIBUTED
    int32_t  dayKey;      // 效果发生日（S0 §B7-4）
    int32_t  latencyDays; // 跨日另计，不重置 step
    uint64_t absTick;
    RefId    causeRef;
    RefId    effectRef;
    uint32_t actorIds[kMaxActorIdsPerEvent];
    uint8_t  actorCount;
    bool     unattributed;
    bool     violation;   // step > 3（设计违规）

    AttributionLink()
        : linkId(0), step(0), dayKey(0), latencyDays(0), absTick(0),
          actorCount(0), unattributed(false), violation(false) {}
};

// step > 3 的设计违规记录（ADR-004 D3：报警 + 记入评审，不静默截断）
struct AttributionViolation {
    uint64_t linkId;
    uint8_t  step;
    int32_t  dayKey;
    RefId    cause;
    RefId    effect;
};

// ---------------------------------------------------------------------------
// AttributionLog · append-only 账本
// ---------------------------------------------------------------------------
class AttributionLog {
public:
    AttributionLog() : unattributedCount_(0), sameSystemSkipped_(0), nextLinkId_(0) {}

    // 返回 true 表示【已记录】一条跨系统链路；false 表示未记录（同系统，规则 A-BOUNDARY）
    bool record(const RefId& cause, const RefId& effect,
                const uint32_t* actors, uint8_t actorCount,
                uint64_t absTick, int32_t dayKey) {
        assert(validateRefId(effect) && "[simcore::AttributionLog] effectRef 非法");

        AttributionLink link;
        link.linkId     = nextLinkId_++;
        link.absTick    = absTick;
        link.dayKey     = dayKey;
        link.effectRef  = effect;
        link.actorCount = 0;

        // --- D4：无 causeRef → UNATTRIBUTED，立即记 + 计数（不静默） -----------
        if (cause.empty()) {
            link.step = 0;
            link.unattributed = true;
            link.causeRef.set("UNATTRIBUTED");
            ++unattributedCount_;
            pushActors(link, actors, actorCount, 0);
            appendLink(link);
            return true;
        }

        assert(validateRefId(cause) && "[simcore::AttributionLog] causeRef 非法");
        link.causeRef = cause;

        // --- D2 规则 A-BOUNDARY：同系统不记 -------------------------------------
        if (!isCrossSystem(cause, effect)) {
            ++sameSystemSkipped_;
            --nextLinkId_;   // 未记录，回收序号
            return false;
        }

        // --- D3：step 沿 causeRef 上溯 ------------------------------------------
        int parentIdx = findLastByEffect(cause);
        uint8_t step = (parentIdx >= 0) ? (uint8_t)(links_[parentIdx].step + 1) : (uint8_t)1;
        if (parentIdx >= 0 && links_[parentIdx].step == 0) step = 1;  // 父链是 UNATTRIBUTED
        link.step = step;

        // --- 跨日：latencyDays 另计，不重置 step（S0 §B7-4） --------------------
        int prevIdx = findLastByEffect(effect);
        if (prevIdx >= 0) {
            int32_t d = dayKey - links_[prevIdx].dayKey;
            link.latencyDays = (d > 0) ? d : 0;
        }

        // --- step > 3：设计违规，报警 + 记入评审，不静默截断（S0 §C7-4） --------
        if (step > 3) {
            link.violation = true;
            AttributionViolation v;
            v.linkId = link.linkId; v.step = step; v.dayKey = dayKey;
            v.cause = cause; v.effect = effect;
            violations_.push_back(v);
        }

        pushActors(link, actors, actorCount, step);
        appendLink(link);
        return true;
    }

    // 追加反向 link 实现"撤销/回滚"（D6：永不修改旧记录）
    void recordCompensation(const RefId& originEffect, const RefId& newEffect,
                            uint64_t absTick, int32_t dayKey) {
        record(originEffect, newEffect, NULL, 0, absTick, dayKey);
    }

    const std::vector<AttributionLink>& links() const { return links_; }
    const std::vector<AttributionViolation>& violations() const { return violations_; }
    uint64_t unattributedCount() const { return unattributedCount_; }
    uint64_t sameSystemSkipped() const { return sameSystemSkipped_; }
    size_t size() const { return links_.size(); }

    // 重放校验：逐条比对（eventId → 字段），用于 ADR-004 V4
    static bool replayVerify(const std::vector<AttributionLink>& a,
                             const std::vector<AttributionLink>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            const AttributionLink& x = a[i];
            const AttributionLink& y = b[i];
            if (x.linkId != y.linkId || x.step != y.step || x.dayKey != y.dayKey ||
                x.absTick != y.absTick || x.actorCount != y.actorCount ||
                x.unattributed != y.unattributed || x.violation != y.violation ||
                !(x.causeRef == y.causeRef) || !(x.effectRef == y.effectRef)) return false;
            for (uint8_t k = 0; k < x.actorCount; ++k)
                if (x.actorIds[k] != y.actorIds[k]) return false;
        }
        return true;
    }

    void clear() { links_.clear(); violations_.clear(); unattributedCount_ = 0; sameSystemSkipped_ = 0; nextLinkId_ = 0; }

private:
    void pushActors(AttributionLink& link, const uint32_t* actors, uint8_t n, uint8_t step) {
        for (uint8_t i = 0; i < n && link.actorCount < kMaxActorIdsPerEvent; ++i) {
            // 匿名 actor 仅 step ≤ 2 时记入（ADR-004 D3 / S0 §C3）
            if (isAnonymousActor(actors[i]) && step > 2) continue;
            link.actorIds[link.actorCount++] = actors[i];
        }
    }
    void appendLink(const AttributionLink& link) { links_.push_back(link); }   // append-only
    int findLastByEffect(const RefId& effect) const {
        for (size_t i = links_.size(); i > 0; --i)
            if (links_[i - 1].effectRef == effect) return (int)(i - 1);
        return -1;
    }

    std::vector<AttributionLink> links_;
    std::vector<AttributionViolation> violations_;
    uint64_t unattributedCount_;
    uint64_t sameSystemSkipped_;
    uint64_t nextLinkId_;
};

}  // namespace simcore

#endif  // SIMCORE_EVENTBUS_H

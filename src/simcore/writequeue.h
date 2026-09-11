// ============================================================================
// src/simcore/writequeue.h · 跨系统写队列 + 昨日快照
// ----------------------------------------------------------------------------
// 契约来源（逐字实现，不重新设计）：
//   S0 已拍板 3：跨系统回写一律【当日只写队列 → 日切/整点批量应用 →
//                下游读昨日快照】
//   S0 §B3.5 第 1 步：冻结当日写队列 → 按 (priority, actorId, seq) 排序 → 应用
//   S0 §B4     ：QUEUED → APPLIED(整点/日切) → SNAPSHOTTED(日切后不可变)
//   ADR-004 D5-1：WriteQueueItem 自带 causeRef / sourceSystem；
//                 没有 causeRef 的条目【不允许入队】（enqueue 断言）
// ----------------------------------------------------------------------------
// 语言约束：C++14 兼容子集。
// ============================================================================
#ifndef SIMCORE_WRITEQUEUE_H
#define SIMCORE_WRITEQUEUE_H

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstring>
#include <utility>
#include <vector>
#include <algorithm>

#include "fixed.h"
#include "clock.h"
#include "eventbus.h"

namespace simcore {

// 规范系统编号（S0 §〇·〇 对照表，闭集）
enum class SystemId : uint8_t {
    S0 = 0, S1 = 1, S2 = 2, S3 = 3, S4 = 4, S5 = 5, S6 = 6, S7 = 7
};

// ---------------------------------------------------------------------------
// WriteQueueItem（S0 §B4 + ADR-004 D5-1 增补 causeRef / sourceSystem）
// ---------------------------------------------------------------------------
struct WriteQueueItem {
    uint64_t  seq;          // 入队序号，全局单调递增（确定性）
    int32_t   priority;     // 小者先应用（S0 §B3.5：排序键首位）
    uint32_t  actorId;      // 排序键次位
    int32_t   dayKey;       // 入队日
    SystemId  sourceSystem;
    RefId     targetRef;    // 写入目标
    RefId     causeRef;     // 归因（ADR-004 D5-1：必填）
    uint16_t  payloadKind;
    EventPayload payload;

    WriteQueueItem()
        : seq(0), priority(0), actorId(0), dayKey(0),
          sourceSystem(SystemId::S0), payloadKind(0) {
        std::memset(&payload, 0, sizeof(payload));
    }
};

// 排序键：(priority, actorId, seq) —— S0 §B3.5 第 1 步，逐字实现
inline bool writeQueueLess(const WriteQueueItem& a, const WriteQueueItem& b) {
    if (a.priority != b.priority) return a.priority < b.priority;
    if (a.actorId  != b.actorId)  return a.actorId  < b.actorId;
    return a.seq < b.seq;
}

// ---------------------------------------------------------------------------
// WriteQueue
// ---------------------------------------------------------------------------
class WriteQueue {
public:
    WriteQueue() : nextSeq_(0), appliedTotal_(0), rejectedNoCause_(0) {}

    // 入队（当日只入队，不直接改世界）
    // 返回 seq；(uint64)-1 表示被拒（无 causeRef）
    uint64_t enqueue(int32_t priority, uint32_t actorId, int32_t dayKey,
                     SystemId src, const RefId& target, const RefId& cause,
                     uint16_t payloadKind, const EventPayload& payload) {
        // ADR-004 D5-1：没有 causeRef 的条目不允许入队
        if (cause.empty()) {
            ++rejectedNoCause_;
            assert(0 && "[simcore::WriteQueue::enqueue] 缺少 causeRef（ADR-004 D5-1）");
            return (uint64_t)-1;
        }
        WriteQueueItem it;
        it.seq          = nextSeq_++;
        it.priority     = priority;
        it.actorId      = actorId;
        it.dayKey       = dayKey;
        it.sourceSystem = src;
        it.targetRef    = target;
        it.causeRef     = cause;
        it.payloadKind  = payloadKind;
        it.payload      = payload;
        pending_.push_back(it);
        return it.seq;
    }

    // 批量应用（整点 / 日切调用）：先按 (priority, actorId, seq) 排序，再逐个应用
    // Applier: void operator()(const WriteQueueItem&)
    template <class Applier>
    size_t applyAll(Applier ap) {
        if (pending_.empty()) return 0;
        // stable_sort 保证严格全序 —— (priority, actorId, seq) 已唯一，但仍用稳定排序
        std::stable_sort(pending_.begin(), pending_.end(), writeQueueLess);
        const size_t n = pending_.size();
        for (size_t i = 0; i < n; ++i) ap(pending_[i]);
        appliedTotal_ += n;
        pending_.clear();      // 应用后清空（S0 §B3.5 第 9 步）
        return n;
    }

    // 应用顺序快照（供测试断言排序键，不改变队列）
    std::vector<WriteQueueItem> sortedView() const {
        std::vector<WriteQueueItem> v = pending_;
        std::stable_sort(v.begin(), v.end(), writeQueueLess);
        return v;
    }

    size_t pendingCount() const { return pending_.size(); }
    uint64_t appliedTotal() const { return appliedTotal_; }
    uint64_t rejectedNoCauseCount() const { return rejectedNoCause_; }
    const std::vector<WriteQueueItem>& pending() const { return pending_; }

    void clear() { pending_.clear(); }

private:
    std::vector<WriteQueueItem> pending_;
    uint64_t nextSeq_;
    uint64_t appliedTotal_;
    uint64_t rejectedNoCause_;
};

// ---------------------------------------------------------------------------
// SnapshotStore · 双缓冲快照（当日写 cur，下游读 prev = 昨日快照）
// ---------------------------------------------------------------------------
// 语义（S0 §B4 / 已拍板 3）：
//   · 整点/日切批量应用 → 写 cur_
//   · 下游任何读跨系统量 → 读 prev_（昨日快照），【不得直读实时值】
//   · 日切 publish() → prev_ = cur_，此后当日写入对下游可见
// 用有序 vector + 线性查找（L1 规模小、零依赖、遍历顺序完全确定）
// ---------------------------------------------------------------------------
class SnapshotStore {
public:
    void writeCur(const RefId& key, Milli v) {
        const size_t i = findIndex(cur_, key);
        if (i != (size_t)-1) cur_[i].second = v;
        else cur_.push_back(Entry(key, v));
    }

    // 下游唯一读口：读昨日快照
    Milli read(const RefId& key) const {
        const size_t i = findIndex(prev_, key);
        return (i != (size_t)-1) ? prev_[i].second : (Milli)0;
    }

    bool containsPrev(const RefId& key) const { return findIndex(prev_, key) != (size_t)-1; }

    // 日切发布（S0 §B3.5 第 9 步）：发布后当日快照不可变
    void publish() { prev_ = cur_; }

    size_t curSize() const { return cur_.size(); }
    size_t prevSize() const { return prev_.size(); }
    void clear() { cur_.clear(); prev_.clear(); }

private:
    typedef std::pair<RefId, Milli> Entry;
    static size_t findIndex(const std::vector<Entry>& v, const RefId& key) {
        for (size_t i = 0; i < v.size(); ++i) if (v[i].first == key) return i;
        return (size_t)-1;
    }
    std::vector<Entry> cur_;
    std::vector<Entry> prev_;
};

}  // namespace simcore

#endif  // SIMCORE_WRITEQUEUE_H

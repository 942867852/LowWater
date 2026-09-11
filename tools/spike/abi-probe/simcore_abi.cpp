// 方案 E 的最小 ABI 证明：simcore(headers) -> C ABI(extern "C") -> 纯 C 消费者
#include "simcore/simcore.h"
extern "C" {
unsigned long long sc_abi_seed(unsigned long long worldSeed, unsigned streamIdx, unsigned param){
    return (unsigned long long)simcore::deriveSeed64(worldSeed, (simcore::StreamId)streamIdx, param);
}
unsigned sc_abi_roll(unsigned long long worldSeed, unsigned streamIdx, unsigned param,
                     unsigned long long absTick, unsigned callSeq){
    uint64_t seed64 = simcore::deriveSeed64(worldSeed, (simcore::StreamId)streamIdx, param);
    uint64_t counter = absTick * 4096ull + (uint64_t)callSeq;   // ADR-002 D2
    return (unsigned)simcore::pcg32_at(seed64, counter);
}
// R2 证明：把"渲染帧 dt"喂进来，也绝不改变判定结果
unsigned sc_abi_roll_with_frame_dt(unsigned long long ws, unsigned sid, unsigned param,
                                   unsigned long long tick, unsigned seq, double frame_dt){
    (void)frame_dt;   // 结构性忽略：引擎时间不得进入仿真
    return sc_abi_roll(ws, sid, param, tick, seq);
}
unsigned sc_abi_schema_version(void){ return simcore::kSimSchemaVersion; }
}

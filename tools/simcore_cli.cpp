// ============================================================================
// tools/simcore_cli.cpp · simcore L1 命令行工具
// ----------------------------------------------------------------------------
// 子命令：
//   simcore_cli run    --seed <N> --days <D> --mode tick|fastforward
//       输出逐日 worldHash 与耗时。
//   simcore_cli verify --seed <N> --days <D>
//       断言「快进模式 ≡ 逐 tick 模式」逐位相同；相同退出码 0，不同非 0（供 CI）。
//
// 语言约束：C++14 兼容子集。工具链：MinGW GCC 6.3.0（MSVCRT printf → 用 %I64）。
// 编译：g++ -std=c++14 -O2 -Wall -Wextra -I src tools/simcore_cli.cpp -o build/simcore_cli.exe
// ============================================================================

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <chrono>

#include "simcore/simcore.h"

using namespace simcore;

typedef std::chrono::steady_clock Clk;

static const int64_t kStartAbsTick = 1440;   // day1 00:00
static const int kEntityCount = 120;

// ---------------------------------------------------------------------------
// 以 60 tick 为块推进（tick 模式走 Clock 帧循环；fastforward 走 fastForward）。
// 每块结束记录一次 worldHash → 返回逐块（每 60 tick）序列，长度 = days*24。
// ---------------------------------------------------------------------------
static void runMode(uint64_t seed, int days, bool fast,
                    std::vector<uint64_t>& seqEvery60, double* outMs) {
    World w; EventBus bus; AttributionLog al; WriteQueue wq; Clock c;
    w.init(seed, kEntityCount);
    c.reset(1, 0);

    const int blocks = days * 24;
    seqEvery60.clear();
    seqEvery60.reserve((size_t)blocks);

    const Clk::time_point t0 = Clk::now();
    for (int blk = 0; blk < blocks; ++blk) {
        const int64_t from = kStartAbsTick + (int64_t)blk * 60;
        if (fast) {
            w.fastForward(from, 60, 60, &bus, &al, &wq);
        } else {
            for (int k = 0; k < 60; ++k)
                c.advanceMs(1000, [&](int64_t t) { w.tickOnce(t, &bus, &al, &wq); });
        }
        seqEvery60.push_back(w.worldHash(from + 59));
    }
    const Clk::time_point t1 = Clk::now();
    if (outMs) *outMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// ---------------------------------------------------------------------------
// run：逐日 worldHash + 耗时
// ---------------------------------------------------------------------------
static int cmdRun(uint64_t seed, int days, bool fast) {
    std::printf("simcore_cli run  seed=%I64u days=%d mode=%s entities=%d\n",
                (unsigned long long)seed, days, fast ? "fastforward" : "tick", kEntityCount);

    World w; EventBus bus; AttributionLog al; WriteQueue wq; Clock c;
    w.init(seed, kEntityCount);
    c.reset(1, 0);

    double total = 0.0;
    for (int d = 0; d < days; ++d) {
        const int64_t dayStart = kStartAbsTick + (int64_t)d * 1440;
        const Clk::time_point t0 = Clk::now();
        for (int blk = 0; blk < 24; ++blk) {
            const int64_t from = dayStart + (int64_t)blk * 60;
            if (fast) {
                w.fastForward(from, 60, 60, &bus, &al, &wq);
            } else {
                for (int k = 0; k < 60; ++k)
                    c.advanceMs(1000, [&](int64_t t) { w.tickOnce(t, &bus, &al, &wq); });
            }
        }
        const Clk::time_point t1 = Clk::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total += ms;
        const uint64_t h = w.worldHash(dayStart + 1439);
        std::printf("  day %2d  worldHash=%016I64X   %8.3f ms\n",
                    d + 1, (unsigned long long)h, ms);
    }
    std::printf("  total   %8.3f ms   events=%I64u links=%I64u violations=%I64u full=%d\n",
                total, (unsigned long long)bus.eventCount(),
                (unsigned long long)al.size(),
                (unsigned long long)al.violations().size(), w.fullCount());
    return 0;
}

// ---------------------------------------------------------------------------
// verify：断言两模式逐位相同（逐日 + 每 60 tick）
// ---------------------------------------------------------------------------
// injectMismatch：仅用于 CI 自检「退出码非 0 路径」——让 fastforward 模式用
// seed^1 跑，制造必然不一致；正式使用不要带该开关。
static int cmdVerify(uint64_t seed, int days, bool injectMismatch) {
    std::printf("simcore_cli verify  seed=%I64u days=%d%s\n", (unsigned long long)seed, days,
                injectMismatch ? "  [inject-mismatch 自检]" : "");

    std::vector<uint64_t> tickSeq, fastSeq;
    double msTick = 0.0, msFast = 0.0;
    runMode(seed, days, false, tickSeq, &msTick);
    runMode(injectMismatch ? (seed ^ 1ULL) : seed, days, true, fastSeq, &msFast);

    const size_t n = (tickSeq.size() < fastSeq.size()) ? tickSeq.size() : fastSeq.size();
    size_t mismatches = 0;
    size_t firstMismatch = (size_t)-1;
    for (size_t i = 0; i < n; ++i) {
        if (tickSeq[i] != fastSeq[i]) {
            if (firstMismatch == (size_t)-1) firstMismatch = i;
            ++mismatches;
        }
    }
    const bool lengthOk = (tickSeq.size() == fastSeq.size());

    // 打印逐日 worldHash（每日最后一个 60-tick 块）供人工核对
    const int dayBlocks = 24;
    std::printf("  %-6s  %-18s  %-18s\n", "day", "tick-mode", "fastforward");
    for (int d = 0; d < days; ++d) {
        const size_t idx = (size_t)d * dayBlocks + (dayBlocks - 1);
        if (idx < n)
            std::printf("  %-6d  %016I64X      %016I64X\n",
                        d + 1, (unsigned long long)tickSeq[idx], (unsigned long long)fastSeq[idx]);
    }
    std::printf("  逐 60-tick 检查点比对：%d 个，mismatch=%d%s\n",
                (int)n, (int)mismatches, lengthOk ? "" : "（长度不一致）");
    std::printf("  耗时：tick=%.3f ms  fastforward=%.3f ms\n", msTick, msFast);

    const bool pass = (mismatches == 0) && lengthOk;
    std::printf("  RESULT: %s\n", pass ? "PASS（快进 ≡ 逐 tick，逐位相同）" : "FAIL（存在不一致）");
    return pass ? 0 : 1;
}

static void usage() {
    std::printf(
        "用法:\n"
        "  simcore_cli run    --seed <N> --days <D> --mode tick|fastforward\n"
        "  simcore_cli verify --seed <N> --days <D> [--inject-mismatch]\n"
        "  （--inject-mismatch 仅供 CI 自检「失败退出码」路径）\n");
}

int main(int argc, char** argv) {
    selfCheck();
    if (argc < 2) { usage(); return 2; }

    const char* cmd = argv[1];
    uint64_t seed = 20250910ULL;
    int days = 1;
    const char* mode = "tick";
    bool injectMismatch = false;

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint64_t)std::strtoull(argv[++i], NULL, 10);
        } else if (std::strcmp(argv[i], "--days") == 0 && i + 1 < argc) {
            days = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else if (std::strcmp(argv[i], "--inject-mismatch") == 0) {
            injectMismatch = true;
        } else {
            std::printf("未知参数: %s\n", argv[i]);
            usage();
            return 2;
        }
    }
    if (days < 1) { std::printf("--days 必须 >= 1\n"); return 2; }

    if (std::strcmp(cmd, "run") == 0) {
        const bool fast = (std::strcmp(mode, "fastforward") == 0);
        return cmdRun(seed, days, fast);
    }
    if (std::strcmp(cmd, "verify") == 0) {
        return cmdVerify(seed, days, injectMismatch);
    }
    usage();
    return 2;
}

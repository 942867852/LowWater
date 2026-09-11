#!/usr/bin/env bash
# ============================================================================
# build.sh · 构建 simcore L1（CI 门禁 + 测试套件 + CLI）
# ----------------------------------------------------------------------------
# 工具链现实约束：本机只有 MinGW GCC 6.3.0（无 cmake / make / ninja / MSVC）。
# 故用最朴素的直接编译，不用任何构建系统。
#   - 语言：C++14 兼容子集（-std=c++14）
#   - 警告视为红线：-Wall -Wextra 下必须 0 warning
#
# 用法：
#   ./build.sh          # CI 门禁 + 编译测试套件与 CLI
#   ./build.sh check    # 只跑 CI 门禁（ADR-005 V3 无浮点静态检查），不编译
#
# ----------------------------------------------------------------------------
# CI 门禁 · ADR-005 V3「无浮点」口径
#   （PHASE3-L1-Q1 裁决；权威说明见 src/simcore/README.md §4.4）
#
#   只统计【未定义 SIMCORE_ENABLE_REFERENCE_FLOAT 的正式 TU】（src/simcore 下 .h/.cpp）。
#   扫描前做两步归一：
#     (1) 剔除 [CI-EXCLUDE-BEGIN] .. [CI-EXCLUDE-END] 区块 —— 浮点参考镜像
#         （fixed.h 中 SIMCORE_ENABLE_REFERENCE_FLOAT 段，必须集中于此区块内）；
#     (2) 剔除注释（注释里写 "double / float / std::sqrt" 是文档，不算仿真代码）。
#   若正式 TU 定义该宏、或残留浮点命中 → 门禁 FAIL（退出码 1）。
# ============================================================================
set -e

CXX="${CXX:-g++}"
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

FLAGS="-std=c++14 -O2 -Wall -Wextra -I src"
# 与 ADR-005 D4 一致：double / float / 超越函数 一律禁（正式 TU）
FLOAT_RE='\bdouble\b|\bfloat\b|std::sin|std::cos|std::exp|std::pow|std::log|std::sqrt'

# ---------------------------------------------------------------------------
# CI 门禁：ADR-005 V3 无浮点（口径见文件头 / README §4.4）
# ---------------------------------------------------------------------------
check_no_float() {
    # ① 正式 TU 不得启用浮点参考镜像
    if grep -rn "define[[:space:]]*SIMCORE_ENABLE_REFERENCE_FLOAT" src/ >/dev/null 2>&1; then
        echo "[gate] FAIL：src/ 定义了 SIMCORE_ENABLE_REFERENCE_FLOAT（正式 TU 必须零浮点）"
        return 1
    fi
    # ② 剔除 CI-EXCLUDE 区块与注释后扫描正式 TU 源码
    local tmp f hits
    tmp="$(mktemp)"
    for f in $(find src/simcore -type f \( -name '*.h' -o -name '*.cpp' \) | sort); do
        awk -v FN="$f" '
            /\[CI-EXCLUDE-BEGIN\]/ { ex = 1; next }
            /\[CI-EXCLUDE-END\]/   { ex = 0; next }
            ex { next }
            { line = $0; sub(/\/\/.*/, "", line); print FN ":" FNR ":" line }
        ' "$f"
    done > "$tmp"
    hits="$(grep -E "$FLOAT_RE" "$tmp" || true)"
    rm -f "$tmp"
    if [ -n "$hits" ]; then
        echo "[gate] FAIL：src/simcore 正式 TU 检测到浮点 / 超越函数（ADR-005 V3）："
        echo "$hits"
        return 1
    fi
    echo "[gate] PASS：src/simcore 正式 TU 零浮点（ADR-005 V3；已剔除 CI-EXCLUDE 区块与注释）"
    return 0
}

mkdir -p build

if [ "${1:-}" = "check" ]; then
    check_no_float
    exit $?
fi

check_no_float

echo "[build] tests/test_runner.cpp -> build/test_runner.exe"
$CXX $FLAGS tests/test_runner.cpp -o build/test_runner.exe

echo "[build] tools/simcore_cli.cpp -> build/simcore_cli.exe"
$CXX $FLAGS tools/simcore_cli.cpp -o build/simcore_cli.exe

echo "[build] 完成：build/test_runner.exe  build/simcore_cli.exe"

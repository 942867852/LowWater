#!/usr/bin/env bash
# ============================================================================
# ci.sh · 《枯水期》本机 CI 骨架（无 CI 服务器 / 无 CMake / 无网络）
# ----------------------------------------------------------------------------
# 串起：编译（零 warning 门禁）→ 静态门禁 → 全部用例 → 确定性重放校验
#       → 归因链闭合校验 → 汇总与退出码。
#
# 本机现实约束：只有 MinGW GCC 6.3.0（无 cmake/make/ninja/MSVC），禁联网。
# 用法：
#   ./ci.sh              # 全流程
#   ./ci.sh quick        # 跳过 golden 更新提示，只跑一遍
#
# 退出码：0 = 全绿；非 0 = 任一步 FAIL（脚本会跑完所有步骤再汇总，不早退）。
# 纪律：不做任何 git 操作。
# ============================================================================
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT" || exit 2

CXX="${CXX:-g++}"
FLAGS="-std=c++14 -O2 -Wall -Wextra -I src"
WORK="build/ci"
mkdir -p build "$WORK"

RC=0
say()  { printf '[ci] %s\n' "$*"; }
hdr()  { printf '\n==== %s ====\n' "$*"; }
bad()  { printf '[ci][FAIL] %s\n' "$*"; RC=1; }
ok()   { printf '[ci][ ok ] %s\n' "$*"; }

# 去掉注释 + [CI-EXCLUDE-BEGIN..END] 区块后输出 <file>:<line>:<code>（供 rg/grep）
emit_src() {
    local d="$1" f
    for f in $(find "$d" -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null | sort); do
        awk -v FN="$f" '
            /\[CI-EXCLUDE-BEGIN\]/ { ex = 1; next }
            /\[CI-EXCLUDE-END\]/   { ex = 0; next }
            ex { next }
            { line = $0; sub(/\/\/.*/, "", line); print FN ":" FNR ":" line }
        ' "$f"
    done
}

# ============================================================================
hdr "静态门禁（control-manifest §8；architecture §2.2）"
# ============================================================================

# --- B9（部分）：simcore 正式 TU 零浮点 / 零超越函数 --------------------------
if grep -rn "define[[:space:]]*SIMCORE_ENABLE_REFERENCE_FLOAT" src/simcore >/dev/null 2>&1; then
    bad "B9：src/simcore 定义了 SIMCORE_ENABLE_REFERENCE_FLOAT（正式 TU 必须零浮点）"
else
    HIT="$(emit_src src/simcore | grep -E '\bdouble\b|\bfloat\b|std::sin|std::cos|std::exp|std::pow|std::log|std::sqrt' || true)"
    if [ -n "$HIT" ]; then bad "B9：simcore 正式 TU 检测到浮点/超越函数："; printf '%s\n' "$HIT"; else ok "B9：simcore 正式 TU 零浮点（ADR-005 V3）"; fi
fi

# --- B1：simcore 无引擎头 -----------------------------------------------------
HIT="$(emit_src src/simcore | grep -E '#include[[:space:]]*<(godot|unity|unreal)' || true)"
if [ -n "$HIT" ]; then bad "B1：simcore 引入了引擎头："; printf '%s\n' "$HIT"; else ok "B1：simcore 无引擎头（ADR-001 R1）"; fi

# --- B2：d20 字面量唯一（== 1，去注释后） ------------------------------------
N_D20="$(emit_src src/simcore | grep -Ec '\bd20\b' || true)"
if [ "$N_D20" = "1" ]; then ok "B2：d20 字面量命中数 == 1（唯一判定实现）"
else bad "B2：d20 命中数 = $N_D20（期望 1；S0·A rollCheck 落地后须复核）"; fi

# --- B3：业务系统 raw rng 调用数 == 0（只允许 rollCheck） --------------------
if [ -d src/simcore/systems ]; then
    N_RAW="$(emit_src src/simcore/systems | grep -Ec 'pcg32_at|makeSeedCtx' || true)"
    if [ "$N_RAW" = "0" ]; then ok "B3：S1..S7 raw rng == 0"; else bad "B3：业务系统 raw rng = $N_RAW"; fi
else
    ok "B3：N/A（src/simcore/systems 尚未建立；Epic 1 落地后启用）"
fi

# --- B4：表现层无判定（render/ui）-------------------------------------------
if [ -d src/render ] || [ -d src/ui ]; then
    HIT="$( { emit_src src/render 2>/dev/null; emit_src src/ui 2>/dev/null; } | grep -E 'rollCheck|makeSeedCtx' || true)"
    if [ -n "$HIT" ]; then bad "B4：表现层出现判定："; printf '%s\n' "$HIT"; else ok "B4：表现层零判定"; fi
else
    ok "B4：N/A（src/render、src/ui 尚未建立；Epic 2 落地后启用）"
fi

# --- B9（部分）：simcore 无裸 rand/srand 调用 --------------------------------
HIT="$(emit_src src/simcore | grep -E '\brand[[:space:]]*\(|\bsrand[[:space:]]*\(' || true)"
if [ -n "$HIT" ]; then bad "B9：simcore 出现裸 rand/srand 调用："; printf '%s\n' "$HIT"; else ok "B9：simcore 无裸随机（另有 #pragma GCC poison 静态兜底）"; fi

# --- B16：lodStateOf 已废弃 --------------------------------------------------
HIT="$(grep -rn 'lodStateOf' src 2>/dev/null || true)"
if [ -n "$HIT" ]; then bad "B16：仍存在 lodStateOf："; printf '%s\n' "$HIT"; else ok "B16：lodStateOf 全仓 0 命中（裁决 C-11）"; fi

# ============================================================================
hdr "编译（零 warning 门禁）"
# ============================================================================
compile_one() {  # $1=源文件 $2=输出名
    local src="$1"
    local out="$2"
    local log="$WORK/cc_$2.log"
    $CXX $FLAGS "$src" -o "$WORK/$out" > "$log" 2>&1
    local rc=$?
    if [ $rc -ne 0 ]; then bad "编译失败：$src"; sed -n '1,40p' "$log"; return 1; fi
    if grep -qE 'warning:' "$log"; then bad "警告即红线：$src"; grep -nE 'warning:' "$log"; return 1; fi
    ok "编译干净：$src -> $WORK/$out"
    return 0
}
compile_one tests/test_runner.cpp test_runner.exe || true
compile_one tools/simcore_cli.cpp simcore_cli.exe || true

TR="$WORK/test_runner.exe"
CLI="$WORK/simcore_cli.exe"

# ============================================================================
hdr "全部用例（test_runner）"
# ============================================================================
if [ -x "$TR" ]; then
    "$TR"; rc=$?
    if [ $rc -eq 0 ]; then ok "test_runner：ALL PASS"; else bad "test_runner：FAIL（退出码 $rc，见上方 CASE）"; fi
else
    bad "test_runner 未构建，跳过"
fi

# ============================================================================
hdr "确定性重放校验（快进 ≡ 逐 tick）"
# ============================================================================
if [ -x "$CLI" ]; then
    "$CLI" verify --seed 20250910 --days 5; rc=$?
    if [ $rc -eq 0 ]; then ok "verify：快进 ≡ 逐 tick（逐位相同）"; else bad "verify：存在不一致"; fi
else
    bad "simcore_cli 未构建，跳过"
fi

# ============================================================================
hdr "归因链闭合校验（UNATTRIBUTED==0 / maxStep<=3）"
# ============================================================================
if [ -x "$CLI" ]; then
    "$CLI" assert --seed 20250910 --days 3; rc=$?
    if [ $rc -eq 0 ]; then ok "assert：归因账本自洽"; else bad "assert：存在断链或越界跳"; fi
else
    bad "simcore_cli 未构建，跳过"
fi

# ============================================================================
hdr "汇总"
# ============================================================================
if [ $RC -eq 0 ]; then
    say "结果：ALL GREEN（退出码 0）"
else
    say "结果：FAILED（退出码 1）—— 见上方 [FAIL] 行"
fi
exit $RC

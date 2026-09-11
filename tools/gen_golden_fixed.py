# -*- coding: utf-8 -*-
"""golden 定点向量生成器 · PHASE3-L1-Q1

用途
----
按 [ADR-005 D1 修订] 的【对称取整（round half away from zero）】口径，重算 mulM/divM
的 golden 向量，写入 `tests/golden_fixed_vectors.h`。该头文件被 `tests/test_runner.cpp`
包含并逐条断言。

口径（唯一权威 → ADR-005 D1）：
    mulM(a,b) = round_half_away( a*b / 1000 )
    divM(a,b) = round_half_away( a*1000 / b )
    即：对绝对值四舍五入后取回符号 ⇒ 恒有  op(-x) == -op(x)（符号对称）。

本脚本同时用【旧口径】(p+500)/1000（向 +∞ 偏置）复算一遍，打印差异统计，供回归报告。

用法：
    python tools/gen_golden_fixed.py            # 生成头文件并打印差异
    python tools/gen_golden_fixed.py --check    # 只打印差异，不写文件
"""
import io
import os
import sys

MILLI_MAX = 2147483647
MILLI_MIN = -2147483648

# ---------------------------------------------------------------------------
# 取整原语
# ---------------------------------------------------------------------------
def c_trunc_div(x, y):
    """C 风格整数除法：向零截断（Python 的 // 是向下取整，故需模拟）。"""
    q = abs(x) // abs(y)
    return q if (x < 0) == (y < 0) else -q


def old_mul(a, b):
    """旧口径：((a*b) + 500) / 1000，向 +∞ 偏置。"""
    return c_trunc_div(a * b + 500, 1000)


def new_mul(a, b):
    """新口径：round half away from zero。"""
    p = a * b
    if p >= 0:
        return (p + 500) // 1000
    return -(((-p) + 500) // 1000)


def old_div(a, b):
    """旧口径：(a*1000 + b/2) / b（b/2 向零截断）。"""
    p = a * 1000 + c_trunc_div(b, 2)
    return c_trunc_div(p, b)


def new_div(a, b):
    """新口径：round half away from zero。"""
    n = a * 1000
    nb = abs(n)
    db = abs(b)
    mag = (nb + db // 2) // db
    return mag if (n >= 0) == (b >= 0) else -mag


def in_range(v):
    return MILLI_MIN <= v <= MILLI_MAX


# ---------------------------------------------------------------------------
# 向量集合
# ---------------------------------------------------------------------------
# 基准量（milli）：覆盖精确值、半边界（x.5）、十进制循环、大数
_BASE = [1, 333, 500, 970, 1455, 1500, 2500, 3000, 12345, 100000, 1000000]
MAGS = _BASE + [-m for m in _BASE]

# 补充的显式向量：舍入半边界（乘积/1000 或商恰好落在 .5）与自检锚点
MUL_EXTRAS = [
    (2500, 1), (-2500, 1),   # 2.5  半边界
    (1500, 1), (-1500, 1),   # 1.5  半边界
    (500, 1),  (-500, 1),    # 0.5  半边界
    (1500, 333), (-1500, 333),  # 499.5 半边界（499500/1000）
    (1500, 970), (-1500, 970),  # 1.5×0.97=1.455（自检锚点，非边界）
]
DIV_EXTRAS = [
    (100000, 3000), (-100000, 3000),   # 100/3=33.333（自检锚点）
    (1, 2000), (-1, 2000),             # 0.5 milli 半边界
    (1, 400), (-1, 400),               # 2.5 milli 半边界
    (3, 2000), (-3, 2000),             # 1.5 milli 半边界
    (1000, -3000), (-1000, -3000),     # 除数有符号
]


def build_mul_vectors():
    seen = set()
    out = []
    for a in MAGS:
        for b in MAGS:
            v = new_mul(a, b)
            if not in_range(v) or v == MILLI_MAX or v == MILLI_MIN:
                continue  # 跳过会触发饱和的向量（避免 debug 下 assert 中断）
            key = (a, b)
            if key in seen:
                continue
            seen.add(key)
            out.append((a, b, v))
    for a, b in MUL_EXTRAS:
        v = new_mul(a, b)
        if (a, b) not in seen and in_range(v):
            seen.add((a, b))
            out.append((a, b, v))
    return out


def build_div_vectors():
    seen = set()
    out = []
    for a in MAGS:
        for b in MAGS:
            if b == 0:
                continue
            v = new_div(a, b)
            if not in_range(v):
                continue
            key = (a, b)
            if key in seen:
                continue
            seen.add(key)
            out.append((a, b, v))
    for a, b in DIV_EXTRAS:
        v = new_div(a, b)
        if (a, b) not in seen and in_range(v):
            seen.add((a, b))
            out.append((a, b, v))
    return out


HEADER = """\
// ============================================================================
// tests/golden_fixed_vectors.h · 定点运算 golden 向量（【自动生成，请勿手改】）
// ----------------------------------------------------------------------------
// 生成器：tools/gen_golden_fixed.py    重新生成：python tools/gen_golden_fixed.py
// 口径  ：ADR-005 D1（修订）—— round half away from zero（对称取整）
//             mulM(a,b) = round_half_away( a*b / 1000 )
//             divM(a,b) = round_half_away( a*1000 / b )
//         含全符号组合 + 舍入半边界（x.5）。断言恒有 op(-x) == -op(x)。
// 变更历史：PHASE3-L1-Q1（2025-09-11）由「向 +∞ 偏置」改为对称取整后重算。
// ============================================================================
#ifndef SIMCORE_GOLDEN_FIXED_VECTORS_H
#define SIMCORE_GOLDEN_FIXED_VECTORS_H

#include "simcore/fixed.h"

namespace simcore {
namespace golden {

struct FixedVec { Milli a; Milli b; Milli expect; };

static const FixedVec kMulVectors[] = {
"""


def emit():
    mul = build_mul_vectors()
    div = build_div_vectors()

    buf = io.StringIO()
    buf.write(HEADER)
    for a, b, v in mul:
        buf.write("    { %d, %d, %d },\n" % (a, b, v))
    buf.write("};\n")
    buf.write("static const int kMulVectorCount = "
              "sizeof(kMulVectors) / sizeof(kMulVectors[0]);\n\n")
    buf.write("static const FixedVec kDivVectors[] = {\n")
    for a, b, v in div:
        buf.write("    { %d, %d, %d },\n" % (a, b, v))
    buf.write("};\n")
    buf.write("static const int kDivVectorCount = "
              "sizeof(kDivVectors) / sizeof(kDivVectors[0]);\n\n")
    buf.write("}  // namespace golden\n}  // namespace simcore\n\n")
    buf.write("#endif  // SIMCORE_GOLDEN_FIXED_VECTORS_H\n")
    return buf.getvalue(), mul, div


def diff_report(mul, div):
    mul_changed = [t for t in mul if old_mul(t[0], t[1]) != t[2]]
    div_changed = [t for t in div if old_div(t[0], t[1]) != t[2]]
    print("== golden 向量差异（旧口径 (p+500)/1000  →  新口径 half-away）==")
    print("mulM：共 %d 条，口径变化 %d 条" % (len(mul), len(mul_changed)))
    for a, b, v in mul_changed[:12]:
        print("    mulM(%d, %d): 旧 %d → 新 %d" % (a, b, old_mul(a, b), v))
    if len(mul_changed) > 12:
        print("    ...（其余 %d 条同类，均为负半边界/负舍入）" % (len(mul_changed) - 12))
    print("divM：共 %d 条，口径变化 %d 条" % (len(div), len(div_changed)))
    for a, b, v in div_changed[:12]:
        print("    divM(%d, %d): 旧 %d → 新 %d" % (a, b, old_div(a, b), v))
    if len(div_changed) > 12:
        print("    ...（其余 %d 条同类）" % (len(div_changed) - 12))
    # 对称性自检（生成期即验证口径）
    bad = [t for t in mul if new_mul(-t[0], t[1]) != -t[2]]
    print("生成期对称性自检：mulM 违例 %d 条" % len(bad))
    bad2 = [t for t in div if new_div(-t[0], t[1]) != -t[2]]
    print("生成期对称性自检：divM 违例 %d 条" % len(bad2))
    return len(mul_changed), len(div_changed)


def main():
    check_only = "--check" in sys.argv[1:]
    text, mul, div = emit()
    diff_report(mul, div)
    if check_only:
        print("[gen] --check：未写文件")
        return 0
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "tests", "golden_fixed_vectors.h")
    with io.open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("[gen] 写入 %s（mulM %d 条 / divM %d 条）" % (out, len(mul), len(div)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

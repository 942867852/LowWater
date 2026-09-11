#!/usr/bin/env bash
# ============================================================================
# tools/spike/abi-probe/build.sh
# 方案 E 最小 ABI 证明：simcore(头文件) -> C++ 静态库 -> extern "C" -> 纯 C 消费者
# 实测工具链：MinGW.org GCC 6.3.0（-std=c++14）
# 预期输出：ABI_PROBE_OK / repeat_identical=1 / frame_dt_irrelevant=1
# ============================================================================
set -e
CXX="${CXX:-C:/MinGW/bin/g++}"
CC="${CC:-C:/MinGW/bin/gcc}"
AR="${AR:-C:/MinGW/bin/ar}"
# 仓库根（本文件在 tools/spike/abi-probe/ 下）
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
INC="$(cygpath -m "$ROOT/src")"        # 给 MinGW 工具链用的 Windows 形式路径

cd "$(dirname "$0")"

echo "[1/4] 编译 simcore ABI 静态库 TU (C++14)"
"$CXX" -std=c++14 -O2 -I "$INC" -c simcore_abi.cpp -o simcore_abi.o

echo "[2/4] 归档 libsimcore_abi.a"
"$AR" rcs libsimcore_abi.a simcore_abi.o

echo "[3/4] 编译纯 C 消费者并链接 C++ 静态库"
"$CC" -O2 consumer.c -o consumer.exe -L. -lsimcore_abi -lstdc++

echo "[4/4] 运行"
./consumer.exe

echo
echo "通过判据：输出包含 ABI_PROBE_OK（repeat_identical=1 且 frame_dt_irrelevant=1）"

#!/usr/bin/env bash
# ============================================================================
# build.sh · 构建 simcore L1（测试套件 + CLI）
# ----------------------------------------------------------------------------
# 工具链现实约束：本机只有 MinGW GCC 6.3.0（无 cmake / make / ninja / MSVC）。
# 故用最朴素的直接编译，不用任何构建系统。
#   - 语言：C++14 兼容子集（-std=c++14）
#   - 警告视为红线：-Wall -Wextra 下必须 0 warning
# ============================================================================
set -e

CXX="${CXX:-g++}"
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

FLAGS="-std=c++14 -O2 -Wall -Wextra -I src"
mkdir -p build

echo "[build] tests/test_runner.cpp -> build/test_runner.exe"
$CXX $FLAGS tests/test_runner.cpp -o build/test_runner.exe

echo "[build] tools/simcore_cli.cpp -> build/simcore_cli.exe"
$CXX $FLAGS tools/simcore_cli.cpp -o build/simcore_cli.exe

echo "[build] 完成：build/test_runner.exe  build/simcore_cli.exe"

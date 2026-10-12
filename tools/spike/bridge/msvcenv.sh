#!/usr/bin/env bash
# ============================================================================
# msvcenv.sh · 在 bash 里手工搭出 MSVC x64 命令行环境
# ----------------------------------------------------------------------------
# 用途：本会话沙箱禁止经 cmd.exe 派生子进程，因此不能跑 vcvarsall.bat。
#       这里把 vcvarsall.bat 的效果手工还原（PATH / INCLUDE / LIB）。
# 正常使用（普通终端 / VS Developer Command Prompt）时【不需要】本脚本。
# ----------------------------------------------------------------------------
# 实测版本：MSVC 19.44.35221 for x64（D:\VS2026），Windows SDK 10.0.26100.0
# ============================================================================
set -u
MSVC_WIN='D:\VS2026\VC\Tools\MSVC\14.44.35207'
MSVC_POSIX='/d/VS2026/VC/Tools/MSVC/14.44.35207'
SDK_WIN='D:\Windows Kits\10'
SDK_VER='10.0.26100.0'
CMAKE_POSIX='/d/VS2026/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
NINJA_POSIX='/d/VS2026/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'

export PATH="$MSVC_POSIX/bin/Hostx64/x64:$CMAKE_POSIX:$NINJA_POSIX:$PATH"
export INCLUDE="$MSVC_WIN\include;$SDK_WIN\Include\$SDK_VER\ucrt;$SDK_WIN\Include\$SDK_VER\um;$SDK_WIN\Include\$SDK_VER\shared;$SDK_WIN\Include\$SDK_VER\winrt;"
export LIB="$MSVC_WIN\lib\x64;$SDK_WIN\Lib\$SDK_VER\ucrt\x64;$SDK_WIN\Lib\$SDK_VER\um\x64;"

if [ "${1:-}" = "--check" ]; then
	echo "== cl ==";    cl 2>&1 | head -2
	echo "== cmake =="; cmake --version 2>&1 | head -1
	echo "== ninja =="; ninja --version 2>&1 | head -1
fi

#!/usr/bin/env bash
# ============================================================================
# build-bridge.sh · 【不降级路径】一键构建 godot-cpp + 最小 GDExtension 桥接
# ----------------------------------------------------------------------------
# 做的事（任一步失败即停）：
#   1. 取 godot-cpp（tag: godot-4.5-stable，与 Godot 4.5-stable 匹配）
#   2. CMake + Ninja + MSVC 编出 godot-cpp 静态库
#   3. cl / link 编出 lowwater_bridge DLL
#   4. 打印如何用 Godot 4.5 验证
#
# 已实测通过（2026-09-13，本会话）：
#   godot-cpp 1024/1025 目标全部构建成功（末步是打静态库）；
#   Godot 4.5-stable 加载 DLL 成功，GDScript 调到 simcore 函数：
#     LOWWATER_BRIDGE_OK / game_version=0.1.0-l1 / schema_version=1
#     world_hash(20250913,1440)=334314409100154065 / repeat_identical=true
#   证据：tools/spike/logs/{godot-cpp-build-cmake-success.txt, bridge-godot45-call.txt}
#
# 用法：
#   在【VS Developer Command Prompt】里最省事；
#   若在 Git Bash 里跑且没有 vcvarsall，本脚本会 source msvcenv.sh 兜底。
# ============================================================================
set -u
cd "$(dirname "$0")"

BRIDGE_DIR="$(pwd)"
GCPP_TAG="godot-4.5-stable"
GCPP_DIR="$BRIDGE_DIR/_godot-cpp"
GCPP_ZIP="$BRIDGE_DIR/_godot-cpp.zip"
GCPP_URL="https://github.com/godotengine/godot-cpp/archive/refs/tags/$GCPP_TAG.zip"
BUILD_DIR="$GCPP_DIR/build-cmake"
SYSLIBS="kernel32.lib user32.lib gdi32.lib winspool.lib shell32.lib ole32.lib oleaut32.lib uuid.lib comdlg32.lib advapi32.lib"
OUT="godot-test/bin/lowwater_bridge.windows.template_debug.x86_64.dll"

# ---- 0. 编译器环境 ----------------------------------------------------------
if ! command -v cl >/dev/null 2>&1; then
	echo "[0/4] 未检测到 cl.exe，尝试用 msvcenv.sh 兜底 ..."
	. "$BRIDGE_DIR/msvcenv.sh"
fi
if ! command -v cl >/dev/null 2>&1; then
	echo "[X] 找不到 cl.exe。"
	echo "    请在【Developer Command Prompt for VS】里运行，或先执行："
	echo "      \"D:\\VS2026\\VC\\Auxiliary\\Build\\vcvarsall.bat\" x64"
	exit 1
fi
echo "[0/4] cl    : $(cl 2>&1 | head -1)"
echo "[0/4] cmake : $(cmake --version 2>&1 | head -1)"
echo "[0/4] ninja : $(ninja --version 2>&1 | head -1)"

# ---- 1. 取 godot-cpp --------------------------------------------------------
if [ ! -f "$GCPP_DIR/CMakeLists.txt" ]; then
	echo "[1/4] 下载 godot-cpp ($GCPP_TAG) ..."
	curl -fsSL --max-time 300 -o "$GCPP_ZIP" "$GCPP_URL" \
		|| curl -fsSL --max-time 300 -o "$GCPP_ZIP" "https://gh-proxy.com/$GCPP_URL"
	if [ $? -ne 0 ]; then echo "[X] 下载失败：$GCPP_URL"; exit 1; fi
	mkdir -p "$GCPP_DIR" "$GCPP_DIR.tmp"
	unzip -o -q "$GCPP_ZIP" -d "$GCPP_DIR.tmp"
	cp -r "$GCPP_DIR.tmp"/godot-cpp-*/. "$GCPP_DIR"/
	rm -rf "$GCPP_DIR.tmp" "$GCPP_ZIP"
fi
if [ ! -f "$GCPP_DIR/CMakeLists.txt" ]; then echo "[X] godot-cpp 源码不完整"; exit 1; fi
echo "[1/4] godot-cpp 源码 OK: $GCPP_DIR"

# cl / link 只认 Windows 路径（MSYS 的 /d/... 不行）
GCPP_WIN="$(cd "$GCPP_DIR" && pwd -W 2>/dev/null || echo "$GCPP_DIR")"
ROOT_WIN="$(cd "$BRIDGE_DIR/../.." && pwd -W 2>/dev/null || echo "$BRIDGE_DIR/../..")"

# ---- 2. 构建 godot-cpp ------------------------------------------------------
GCPPLIB="$(ls "$BUILD_DIR"/bin/*.lib 2>/dev/null | head -1)"
if [ -z "$GCPPLIB" ]; then
	echo "[2/4] CMake 配置 + 构建 godot-cpp（约 5-10 分钟）..."
	cmake -B "$BUILD_DIR" -S "$GCPP_DIR" -G Ninja \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		|| { echo "[X] cmake 配置失败"; exit 1; }
	ninja -C "$BUILD_DIR" || { echo "[X] ninja 构建失败"; exit 1; }
	GCPPLIB="$(ls "$BUILD_DIR"/bin/*.lib 2>/dev/null | head -1)"
fi
if [ -z "$GCPPLIB" ]; then echo "[X] 没找到 godot-cpp 静态库"; exit 1; fi
# 原名带两个连续的点（libgodot-cpp.windows.template_debug..lib），link.exe 处理异常 → 拷成干净名字
GCPPLIB_CLEAN="$BUILD_DIR/bin/godot-cpp-d.lib"
[ -f "$GCPPLIB_CLEAN" ] || cp -f "$GCPPLIB" "$GCPPLIB_CLEAN"
GCPPLIB_CLEAN_WIN="$GCPP_WIN\\build-cmake\\bin\\godot-cpp-d.lib"
echo "[2/4] godot-cpp 静态库 OK: $GCPPLIB_CLEAN"

# ---- 3. 编译 + 链接桥接 DLL --------------------------------------------------
# 开关与 godot-cpp 的 CMake 构建【逐字一致】（抄自 build-cmake/compile_commands.json）
mkdir -p build godot-test/bin
echo "[3/4] 编译 lowwater_bridge.cpp ..."
cl /nologo /TP -DDEBUG_ENABLED -DGDEXTENSION -DHOT_RELOAD_ENABLED -DNOMINMAX -DTHREADS_ENABLED -DTYPED_METHOD_BIND -DWINDOWS_ENABLED -D_HAS_EXCEPTIONS=0 -I"$GCPP_WIN\\include" -I"$GCPP_WIN\\build-cmake\\gen\\include" -I"$GCPP_WIN\\gdextension" -I"$ROOT_WIN\\src" /DWIN32 /D_WINDOWS /GR /EHsc /Zi /Ob0 /Od /RTC1 -std:c++17 -MT /utf-8 /W3 /Fo:build\\lowwater_bridge.obj /Fd:build\\lowwater_bridge.pdb /FS -c lowwater_bridge.cpp
if [ $? -ne 0 ]; then echo "[X] 编译失败"; exit 1; fi

echo "[3/4] 链接 DLL ..."
link /nologo /DLL /machine:x64 /DEBUG /OUT:"$OUT" build\\lowwater_bridge.obj "$GCPPLIB_CLEAN_WIN" $SYSLIBS
if [ $? -ne 0 ]; then echo "[X] 链接失败"; exit 1; fi
echo "[3/4] 产物: $OUT"

# ---- 4. 怎么用 ---------------------------------------------------------------
cat <<EOF

[4/4] 完成。验证方式（Godot 4.5-stable）：

  <godot4.5> --headless --path "<本目录>\\godot-test" --script res://test.gd

期望输出：
  LOWWATER_BRIDGE_OK
  engine=4.5-stable (official)
  game_version=0.1.0-l1
  schema_version=1
  stream_registry_version=1
  world_hash(20250913,1440)=334314409100154065
  repeat_identical=true
EOF

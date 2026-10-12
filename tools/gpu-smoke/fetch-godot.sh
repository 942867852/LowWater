#!/usr/bin/env bash
# ============================================================================
# fetch-godot.sh · 取 Godot 4.5-stable 到 _bin/（bash / Git Bash / WSL）
# ----------------------------------------------------------------------------
# 手动方式（目标机无外网时）：把下面【两个】文件放到本目录的 _bin/ 下
#   Godot_v4.5-stable_win64.exe
#   Godot_v4.5-stable_win64_console.exe
# 放好后本脚本会自动识别并跳过下载。
# ============================================================================
set -u
cd "$(dirname "$0")"

EXPECTED_SHA512="e8ef4867466bb01a2b762c4ec404f54266b40a5cbba0efa95e3ab325cd7d349b5dc11259e1c1525195080dc82294ff28044f7ac4acad833e4b054b4257e9aa1a"
ZIP="Godot_v4.5-stable_win64.exe.zip"
URL1="https://github.com/godotengine/godot/releases/download/4.5-stable/${ZIP}"
URL2="https://gh-proxy.com/https://github.com/godotengine/godot/releases/download/4.5-stable/${ZIP}"

mkdir -p _bin

if [ -x "_bin/Godot_v4.5-stable_win64_console.exe" ] && [ -f "_bin/Godot_v4.5-stable_win64.exe" ]; then
	echo "[=] _bin 下已存在 Godot 4.5-stable，跳过下载。"
	./_bin/Godot_v4.5-stable_win64_console.exe --headless --version
	exit 0
fi

echo "[1/3] 下载 Godot 4.5-stable ..."
ok=0
for u in "$URL1" "$URL2"; do
	echo "  尝试 $u"
	if curl -fsSL --max-time 600 -o "$ZIP" "$u"; then ok=1; break; fi
	echo "  失败"
done
if [ "$ok" -ne 1 ]; then
	echo "[X] 下载失败。请改用手动方式（见本文件头部注释）。"
	exit 1
fi

echo "[2/3] 校验 SHA512 ..."
ACTUAL=$(sha512sum "$ZIP" | awk '{print $1}')
if [ "$ACTUAL" != "$EXPECTED_SHA512" ]; then
	echo "[X] SHA512 不一致，文件不可信，已停止。"
	echo "    期望: $EXPECTED_SHA512"
	echo "    实际: $ACTUAL"
	exit 1
fi
echo "    OK: $ACTUAL"

echo "[3/3] 解压到 _bin/ ..."
unzip -o -q "$ZIP" -d _bin
rm -f "$ZIP"

./_bin/Godot_v4.5-stable_win64_console.exe --headless --version
echo "[=] 完成。现在可以运行 ./run-smoke.sh"

#!/usr/bin/env bash
# ============================================================================
# run-smoke.sh · 一键运行 GPU 渲染冒烟测试（bash / Git Bash / WSL / Linux）
#   用法：./run-smoke.sh [额外的 Godot 参数]
#   例 ：./run-smoke.sh -- --frames=120
# ============================================================================
set -u
cd "$(dirname "$0")"

GODOT="${GODOT_BIN:-}"
if [ -z "$GODOT" ] && [ -x "_bin/Godot_v4.5-stable_win64_console.exe" ]; then
	GODOT="./_bin/Godot_v4.5-stable_win64_console.exe"
fi
if [ -z "$GODOT" ] && command -v godot >/dev/null 2>&1; then
	GODOT="godot"
fi
if [ -z "$GODOT" ]; then
	echo "[X] 没找到 Godot 4.5-stable。"
	echo "    请先运行 ./fetch-godot.sh ，"
	echo "    或把 Godot_v4.5-stable_win64_console.exe 放到本目录的 _bin/ 下（两个 exe 都要），"
	echo "    或设置环境变量 GODOT_BIN 指向任意 Godot 4.5 可执行文件。"
	exit 1
fi

mkdir -p results
echo "[=] Godot : $GODOT"
echo "[=] 工程  : $PWD"
echo "[=] 开始冒烟测试（约 1-2 分钟，期间请勿操作鼠标键盘）..."
"$GODOT" --path . "$@"
rc=$?
echo ""
echo "[=] 退出码: $rc"
[ -f results/result.json ] && echo "[=] 机器可读结果: $PWD/results/result.json"
[ -f results/report.md ]  && echo "[=] 人可读报告  : $PWD/results/report.md"
exit $rc

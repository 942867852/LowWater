@echo off
REM ============================================================================
REM run-smoke.bat · 一键运行 GPU 渲染冒烟测试（Windows）
REM   用法：run-smoke.bat [额外的 Godot 参数]
REM   例 ：run-smoke.bat -- --frames=120        （快速试跑，每阶段只采 120 帧）
REM   例 ：run-smoke.bat --rendering-driver vulkan
REM ----------------------------------------------------------------------------
REM 结果输出到 results\result.json 与 results\report.md
REM ============================================================================
setlocal
cd /d "%~dp0"

set "GODOT="
if defined GODOT_BIN set "GODOT=%GODOT_BIN%"
if not defined GODOT (
    if exist "_bin\Godot_v4.5-stable_win64_console.exe" set "GODOT=_bin\Godot_v4.5-stable_win64_console.exe"
)
if not defined GODOT (
    echo [X] 没找到 Godot 4.5-stable。
    echo     请先双击运行 fetch-godot.bat ，
    echo     或把 Godot_v4.5-stable_win64_console.exe 放到本目录的 _bin\ 下（两个 exe 都要）。
    echo     也可以设置环境变量 GODOT_BIN 指向任意 Godot 4.5 可执行文件。
    exit /b 1
)

if not exist "results" mkdir "results"

echo [=] Godot : %GODOT%
echo [=] 工程  : %CD%
echo [=] 开始冒烟测试（约 1-2 分钟，期间请勿操作鼠标键盘）...
"%GODOT%" --path . %*
set "RC=%ERRORLEVEL%"

echo.
echo [=] 退出码: %RC%
if exist "results\result.json" echo [=] 机器可读结果: %CD%\results\result.json
if exist "results\report.md"  echo [=] 人可读报告  : %CD%\results\report.md
endlocal & exit /b %RC%

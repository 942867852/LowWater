@echo off
REM ============================================================================
REM fetch-godot.bat · 取 Godot 4.5-stable 到 _bin\（离线机器请用「手动方式」，见下）
REM ----------------------------------------------------------------------------
REM 手动方式（目标机无外网时）：
REM   1. 在能上网的机器上打开
REM      https://github.com/godotengine/godot/releases/download/4.5-stable/Godot_v4.5-stable_win64.exe.zip
REM   2. 下载后核对 SHA512（必须与 EXPECTED_SHA512 完全一致）：
REM      certutil -hashfile Godot_v4.5-stable_win64.exe.zip SHA512
REM   3. 解压，把下面【两个】文件放到本目录的 _bin\ 下（两个都要，缺一不可）：
REM         Godot_v4.5-stable_win64.exe
REM         Godot_v4.5-stable_win64_console.exe
REM      放好后本脚本会自动识别并跳过下载。
REM ============================================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "EXPECTED_SHA512=E8EF4867466BB01A2B762C4EC404F54266B40A5CBBA0EFA95E3AB325CD7D349B5DC11259E1C1525195080DC82294FF28044F7AC4ACAD833E4B054B4257E9AA1A"
set "ZIP=Godot_v4.5-stable_win64.exe.zip"
set "URL1=https://github.com/godotengine/godot/releases/download/4.5-stable/%ZIP%"
set "URL2=https://gh-proxy.com/https://github.com/godotengine/godot/releases/download/4.5-stable/%ZIP%"

if not exist "_bin" mkdir "_bin"

REM --- 已存在就直接跳过 ---
if exist "_bin\Godot_v4.5-stable_win64_console.exe" if exist "_bin\Godot_v4.5-stable_win64.exe" (
    echo [=] _bin 下已存在 Godot 4.5-stable，跳过下载。
    "_bin\Godot_v4.5-stable_win64_console.exe" --headless --version
    goto :eof
)

echo [1/3] 下载 Godot 4.5-stable ...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ProgressPreference='SilentlyContinue'; $ok=$false; foreach($u in @('%URL1%','%URL2%')){ try { Write-Host ('  尝试 ' + $u); Invoke-WebRequest -Uri $u -OutFile '%ZIP%' -UseBasicParsing; $ok=$true; break } catch { Write-Host ('  失败：' + $_.Exception.Message) } }; if(-not $ok){ exit 1 }"
if errorlevel 1 (
    echo [X] 下载失败。请改用手动方式（见本文件头部注释）。
    exit /b 1
)

echo [2/3] 校验 SHA512 ...
for /f "delims=" %%H in ('powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA512 '%ZIP%').Hash"') do set "ACTUAL=%%H"
if /i not "!ACTUAL!"=="!EXPECTED_SHA512!" (
    echo [X] SHA512 不一致，文件不可信，已停止。
    echo     期望: !EXPECTED_SHA512!
    echo     实际: !ACTUAL!
    exit /b 1
)
echo     OK: !ACTUAL!

echo [3/3] 解压到 _bin\ ...
powershell -NoProfile -Command "Expand-Archive -Path '%ZIP%' -DestinationPath '_bin' -Force"
del /q "%ZIP%" 2>nul

"_bin\Godot_v4.5-stable_win64_console.exe" --headless --version
echo [=] 完成。现在可以运行 run-smoke.bat
endlocal

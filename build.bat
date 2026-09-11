@echo off
rem ==========================================================================
rem build.bat · 构建 simcore L1（测试套件 + CLI）
rem --------------------------------------------------------------------------
rem 工具链：MinGW GCC 6.3.0（无 cmake / make / ninja / MSVC）。
rem 语言：C++14 兼容子集；-Wall -Wextra 必须 0 warning。
rem ADR-005 V3 无浮点门禁见 build.sh 的 check_no_float（Git Bash: ./build.sh check）。
rem ==========================================================================
setlocal

rem 若 g++ 不在 PATH，尝试本机已知位置 C:\MinGW\bin
where g++ >nul 2>nul
if errorlevel 1 set "PATH=C:\MinGW\bin;%PATH%"

set "FLAGS=-std=c++14 -O2 -Wall -Wextra -I src"
if not exist build mkdir build

echo [build] tests/test_runner.cpp -^> build/test_runner.exe
g++ %FLAGS% tests/test_runner.cpp -o build/test_runner.exe
if errorlevel 1 goto fail

echo [build] tools/simcore_cli.cpp -^> build/simcore_cli.exe
g++ %FLAGS% tools/simcore_cli.cpp -o build/simcore_cli.exe
if errorlevel 1 goto fail

echo [build] 完成：build/test_runner.exe  build/simcore_cli.exe
exit /b 0

:fail
echo [build] 失败。
exit /b 1

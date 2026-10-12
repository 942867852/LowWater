@echo off
rem ==========================================================================
rem ci.bat . LowWater local CI (cmd.exe variant of ci.sh)
rem --------------------------------------------------------------------------
rem Pipeline: compile (zero-warning gate) -> all tests -> determinism replay
rem           -> attribution closure -> summary + exit code.
rem Toolchain reality: MinGW GCC 6.3.0 only (no cmake/make/ninja/MSVC, offline).
rem Usage:  ci.bat
rem Exit:   0 = all green; 1 = any step FAILED.
rem NOTE: the static-gate set (B1..B16, float block-strip) lives in ci.sh (bash)
rem       and is authoritative; this .bat covers the compile/test/replay part.
rem ==========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

if not defined CXX set CXX=g++
set FLAGS=-std=c++14 -O2 -Wall -Wextra -I src
set WORK=build\ci
if not exist build mkdir build
if not exist "%WORK%" mkdir "%WORK%"
set RC=0

echo.
echo ==== compile (zero-warning gate) ====
"%CXX%" %FLAGS% tests\test_runner.cpp -o "%WORK%\test_runner.exe" > "%WORK%\cc_tr.log" 2>&1
if errorlevel 1 (echo [ci][FAIL] compile test_runner & type "%WORK%\cc_tr.log" & set RC=1) else (
  findstr /C:"warning:" "%WORK%\cc_tr.log" >nul 2>&1
  if errorlevel 1 (echo [ci][ ok ] test_runner compiled clean) else (echo [ci][FAIL] test_runner warnings & set RC=1)
)
"%CXX%" %FLAGS% tools\simcore_cli.cpp -o "%WORK%\simcore_cli.exe" > "%WORK%\cc_cli.log" 2>&1
if errorlevel 1 (echo [ci][FAIL] compile simcore_cli & type "%WORK%\cc_cli.log" & set RC=1) else (
  findstr /C:"warning:" "%WORK%\cc_cli.log" >nul 2>&1
  if errorlevel 1 (echo [ci][ ok ] simcore_cli compiled clean) else (echo [ci][FAIL] simcore_cli warnings & set RC=1)
)

echo.
echo ==== all tests ====
if exist "%WORK%\test_runner.exe" (
  "%WORK%\test_runner.exe"
  if errorlevel 1 (echo [ci][FAIL] test_runner & set RC=1) else (echo [ci][ ok ] test_runner ALL PASS)
) else (echo [ci][FAIL] test_runner not built & set RC=1)

echo.
echo ==== determinism replay (fastforward == tick) ====
if exist "%WORK%\simcore_cli.exe" (
  "%WORK%\simcore_cli.exe" verify --seed 20250910 --days 5
  if errorlevel 1 (echo [ci][FAIL] verify & set RC=1) else (echo [ci][ ok ] verify PASS)
) else (echo [ci][FAIL] simcore_cli not built & set RC=1)

echo.
echo ==== attribution closure (UNATTRIBUTED==0 / maxStep^<=3) ====
if exist "%WORK%\simcore_cli.exe" (
  "%WORK%\simcore_cli.exe" assert --seed 20250910 --days 3
  if errorlevel 1 (echo [ci][FAIL] assert & set RC=1) else (echo [ci][ ok ] assert PASS)
) else (echo [ci][FAIL] simcore_cli not built & set RC=1)

echo.
echo ==== summary ====
if "%RC%"=="0" (echo [ci] ALL GREEN) else (echo [ci] FAILED)
exit /b %RC%

@echo off
REM run_stress.bat - End-to-end stress test runner.
REM
REM Prerequisite:
REM   - LoginServer.exe and GameServer.exe are already running (start them by hand).
REM   - This script starts DummyClient in stress mode and generates HTML report.
REM
REM Usage:
REM   run_stress.bat [config]
REM     config = debug (default) | release
REM
REM Tunables: edit BOTS / RAMP / HOLD below.

setlocal enabledelayedexpansion

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug
if /i "%CONFIG%"=="debug"   set CONFIG=Debug
if /i "%CONFIG%"=="release" set CONFIG=Release

set ROOT=%~dp0..\..
set BIN=%ROOT%\bin\%CONFIG%\x64
set METRICS_DIR=%ROOT%\metrics
set REPORT=%METRICS_DIR%\report.html

set BOTS=300
set RAMP=60
set HOLD=120

if not exist "%BIN%\DummyClient.exe" (
    echo [ERROR] DummyClient.exe not found at %BIN%
    echo Build first: msbuild %ROOT%\mmosvr.sln /p:Configuration=%CONFIG% /p:Platform=x64
    exit /b 1
)

if not exist "%METRICS_DIR%" mkdir "%METRICS_DIR%"

echo =================================================================
echo Stress run: bots=%BOTS% ramp=%RAMP%s hold=%HOLD%s config=%CONFIG%
echo Make sure LoginServer and GameServer are already running.
echo =================================================================

pushd "%BIN%"
DummyClient.exe --stress --bots=%BOTS% --ramp-sec=%RAMP% --hold-sec=%HOLD% --csv="%METRICS_DIR%\client_metrics.csv"
set RC=%ERRORLEVEL%
popd

if not %RC%==0 (
    echo [WARN] DummyClient exited with code %RC%
)

where node >nul 2>nul
if errorlevel 1 (
    echo [WARN] node not found in PATH - skipping HTML report generation
    exit /b 0
)

node "%~dp0generate_report.js" ^
  --server="%METRICS_DIR%\gameserver_metrics.csv" ^
  --client="%METRICS_DIR%\client_metrics.csv" ^
  --out="%REPORT%"

echo Report: %REPORT%
endlocal

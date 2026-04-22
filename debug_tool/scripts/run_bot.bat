@echo off
rem ---------------------------------------------------------------------
rem  Launcher for debug_tool/bot.py (headless bot spawner).
rem
rem  Usage:
rem    run_bot.bat               -> default 50 bots
rem    run_bot.bat 100           -> 100 bots
rem    run_bot.bat 30 loader     -> 30 bots with prefix "loader_"
rem
rem  _setup_python.bat handles python/deps setup.
rem ---------------------------------------------------------------------

setlocal enabledelayedexpansion

cd /d "%~dp0.."

call "%~dp0_setup_python.bat"
if errorlevel 1 exit /b 1

if "%~1"=="" (
    %PY_INVOKE% bot.py 50
) else (
    %PY_INVOKE% bot.py %*
)

if errorlevel 1 (
    echo.
    echo [bot.py exited with error !errorlevel!]
    pause
)
endlocal
exit /b 0

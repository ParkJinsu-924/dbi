@echo off
rem ---------------------------------------------------------------------
rem  Launcher for debug_tool/client.py.
rem
rem  - Python discovery / install / dep install delegated to
rem    _setup_python.bat in the same directory.
rem  - cd's to debug_tool/ (parent of this script) before launching.
rem  - Forwards all CLI args to client.py (e.g. username + password).
rem ---------------------------------------------------------------------

setlocal enabledelayedexpansion

cd /d "%~dp0.."

call "%~dp0_setup_python.bat"
if errorlevel 1 exit /b 1

%PY_INVOKE% client.py %*

if errorlevel 1 (
    echo.
    echo [client.py exited with error !errorlevel!]
    pause
)
endlocal
exit /b 0

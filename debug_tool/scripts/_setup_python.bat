@echo off
rem ---------------------------------------------------------------------
rem  Shared helper for run_client.bat / run_bot.bat.
rem
rem  Caller contract:
rem    * setlocal enabledelayedexpansion already active.
rem    * Current dir must be debug_tool/ (where requirements.txt lives).
rem
rem  Behavior:
rem    1. Locate python (PATH, py launcher, known install dirs).
rem    2. If missing, offer winget install (user scope, no admin).
rem    3. Install deps via pip if pygame or google.protobuf missing.
rem    4. Export PY_INVOKE to caller and exit /b 0.
rem
rem  On failure: exit /b 1. Caller checks errorlevel.
rem ---------------------------------------------------------------------

rem === 1. Locate python ===
call :_setup_find_python
if defined PY_INVOKE goto :_setup_have_python

rem === 2. Not found - offer winget install ===
echo [WARN] Python not found in PATH or known install locations.
echo         (If you just installed Python, close this window and open a new cmd.)
echo.

where winget >nul 2>&1
if errorlevel 1 (
    echo winget is not available on this system.
    echo Install Python manually: https://www.python.org/downloads/
    echo  (check "Add Python to PATH" during install)
    pause
    exit /b 1
)

set "INSTALL_NOW="
set /p INSTALL_NOW="Install Python 3.12 now via winget (user scope, no admin)? [Y/N] "
if /i not "!INSTALL_NOW!"=="Y" (
    echo Aborted. Manual install: https://www.python.org/downloads/
    pause
    exit /b 1
)

echo.
echo Running: winget install --id Python.Python.3.12 -e --scope user --accept-source-agreements --accept-package-agreements
winget install --id Python.Python.3.12 -e --scope user --accept-source-agreements --accept-package-agreements
if errorlevel 1 (
    echo [ERROR] winget install failed.
    echo Try manual install: https://www.python.org/downloads/
    pause
    exit /b 1
)

rem === 3. Re-locate after install (session PATH may be stale) ===
call :_setup_find_python
if defined PY_INVOKE (
    echo [OK] Using !PY_INVOKE!
    echo.
    goto :_setup_have_python
)

echo.
echo [OK] Python was installed, but it couldn't be located automatically.
echo      Close this window, open a new terminal, and run this batch again.
pause
exit /b 1

:_setup_have_python
rem === 4. Sanity check ===
%PY_INVOKE% --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] !PY_INVOKE! was found but failed to run.
    pause
    exit /b 1
)

rem === 5. Check deps, install if missing ===
%PY_INVOKE% -c "import pygame, google.protobuf" >nul 2>&1
if errorlevel 1 (
    echo Installing dependencies from requirements.txt ...
    %PY_INVOKE% -m pip install -r requirements.txt
    if errorlevel 1 (
        echo [ERROR] pip install failed.
        pause
        exit /b 1
    )
    echo.
)

goto :eof

rem =========================================================================
rem  Subroutines
rem =========================================================================
:_setup_find_python
set "PY_INVOKE="

rem --- Strategy 1: python.exe in PATH (reject MS Store WindowsApps stubs) ---
for /f "delims=" %%P in ('where python 2^>nul') do (
    echo %%P | findstr /i "WindowsApps" >nul
    if !errorlevel! neq 0 if not defined PY_INVOKE set PY_INVOKE="%%P"
)

rem --- Strategy 2: py launcher in PATH ---
if not defined PY_INVOKE (
    where py >nul 2>&1
    if !errorlevel! equ 0 set "PY_INVOKE=py -3"
)

rem --- Strategy 3: user-scope install dir ---
if not defined PY_INVOKE (
    for %%V in (313 312 311 310 39) do (
        if not defined PY_INVOKE if exist "%LOCALAPPDATA%\Programs\Python\Python%%V\python.exe" set PY_INVOKE="%LOCALAPPDATA%\Programs\Python\Python%%V\python.exe"
    )
)

rem --- Strategy 4: system-wide install dir ---
if not defined PY_INVOKE (
    for %%V in (313 312 311 310 39) do (
        if not defined PY_INVOKE if exist "%ProgramFiles%\Python%%V\python.exe" set PY_INVOKE="%ProgramFiles%\Python%%V\python.exe"
    )
)

goto :eof

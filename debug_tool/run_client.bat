@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

rem === 1. Locate a real Python (reject Microsoft Store WindowsApps stub) ===
call :find_python
if defined PY_INVOKE goto :have_python

rem === 2. Python not found — offer to install via winget ===
echo [WARN] Python not found in PATH.
echo         ^(Microsoft Store aliases under WindowsApps\ are not real installs.^)
echo.

where winget >nul 2>&1
if errorlevel 1 (
    echo winget is not available on this system.
    echo Install Python manually: https://www.python.org/downloads/
    echo  ^(check "Add Python to PATH" during install^)
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

rem === 3. Locate freshly installed Python (user-scope default path) ===
rem    PATH in the current session is NOT refreshed by winget, so probe the
rem    well-known install directory directly.
for %%V in (313 312 311 310) do (
    if not defined PY_INVOKE if exist "%LOCALAPPDATA%\Programs\Python\Python%%V\python.exe" (
        set PY_INVOKE="%LOCALAPPDATA%\Programs\Python\Python%%V\python.exe"
    )
)

if not defined PY_INVOKE (
    echo.
    echo [OK] Python was installed, but this terminal's PATH isn't refreshed yet.
    echo      Close this window, open a new terminal, and run this batch again.
    pause
    exit /b 0
)

echo [OK] Using !PY_INVOKE!
echo.

:have_python

rem === 4. Sanity check it actually runs ===
%PY_INVOKE% --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] !PY_INVOKE! was found but failed to run.
    pause
    exit /b 1
)

rem === 5. Check dependencies, install via "python -m pip" if missing ===
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

rem === 6. Run client (pass through CLI args) ===
%PY_INVOKE% client.py %*

if errorlevel 1 (
    echo.
    echo [client.py exited with error !errorlevel!]
    pause
)
exit /b 0

rem =========================================================================
rem  Subroutines
rem =========================================================================
:find_python
set "PY_INVOKE="
for /f "delims=" %%P in ('where python 2^>nul') do (
    echo %%P | findstr /i "WindowsApps" >nul
    if !errorlevel! neq 0 if not defined PY_INVOKE set PY_INVOKE="%%P"
)
if not defined PY_INVOKE (
    where py >nul 2>&1 && set "PY_INVOKE=py -3"
)
goto :eof

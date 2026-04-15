@echo off
setlocal
cd /d "%~dp0"

rem === 1. Check Python availability ===
where python >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Python not found in PATH.
    echo Please install Python 3.9+ from https://www.python.org/downloads/
    pause
    exit /b 1
)

rem === 2. Check dependencies: pygame, protobuf ===
python -c "import pygame, google.protobuf" >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Installing dependencies from requirements.txt ...
    pip install -r requirements.txt
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] pip install failed.
        pause
        exit /b 1
    )
    echo.
)

rem === 3. Run client (pass through CLI args) ===
python client.py %*

if %ERRORLEVEL% neq 0 (
    echo.
    echo [client.py exited with error %ERRORLEVEL%]
    pause
)

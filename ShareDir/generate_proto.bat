@echo off
setlocal

set SCRIPT_DIR=%~dp0
set PROTO_DIR=%SCRIPT_DIR%proto
set CPP_OUT=%SCRIPT_DIR%..\mmosvr\proto\generated
set CS_OUT=%SCRIPT_DIR%..\ClaudeProject\Assets\Scripts\Proto
set PY_OUT=%SCRIPT_DIR%..\debug_tool

rem === Find protoc (vcpkg first, then PATH) ===
set PROTOC=%SCRIPT_DIR%..\mmosvr\ThirdParty\protobuf\tools\protoc.exe
if not exist "%PROTOC%" (
    where protoc >nul 2>&1
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] protoc not found in vcpkg or PATH
        exit /b 1
    )
    set PROTOC=protoc
)

rem === Create output dirs if needed ===
if not exist "%CPP_OUT%" mkdir "%CPP_OUT%"
if not exist "%CS_OUT%" mkdir "%CS_OUT%"
if not exist "%PY_OUT%" mkdir "%PY_OUT%"

rem === Generate C++ (all protos) ===
echo Generating C++ protobuf files...
"%PROTOC%" --proto_path="%PROTO_DIR%" --cpp_out="%CPP_OUT%" "%PROTO_DIR%\common.proto" "%PROTO_DIR%\login.proto" "%PROTO_DIR%\game.proto" "%PROTO_DIR%\server.proto"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] C++ generation failed
    exit /b 1
)

rem === Generate C# (client protos only, excluding server.proto) ===
echo Generating C# protobuf files...
"%PROTOC%" --proto_path="%PROTO_DIR%" --csharp_out="%CS_OUT%" "%PROTO_DIR%\common.proto" "%PROTO_DIR%\login.proto" "%PROTO_DIR%\game.proto"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] C# generation failed
    exit /b 1
)

rem === Generate Python (client protos only, excluding server.proto) ===
echo Generating Python protobuf files...
"%PROTOC%" --proto_path="%PROTO_DIR%" --python_out="%PY_OUT%" "%PROTO_DIR%\common.proto" "%PROTO_DIR%\login.proto" "%PROTO_DIR%\game.proto"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Python generation failed
    exit /b 1
)

rem === Generate PacketId files (C++ PacketUtils/Traits, C# PacketIds) ===
echo Generating PacketId files...
where node >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [WARN] node not found, skipping PacketId generation
) else (
    node "%SCRIPT_DIR%generate_packet_ids.js"
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] PacketId generation failed
        exit /b 1
    )
)

echo [OK] All generation complete.

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MMOSVR is a distributed MMORPG game server written in C++20. It uses Boost.Asio (Windows IOCP) for networking, Protocol Buffers for packet serialization, and nanodbc (ODBC) for database access. Designed for 5,000~20,000 concurrent connections. Uses C++20 features: `std::format`, `std::source_location`, `std::jthread`, `std::scoped_lock`, concepts, `[[unlikely]]`, and fold expressions.

## Build Commands

MSBuild solution (`mmosvr.sln`) with vcpkg global integration. Open in VS 2022 or build from Developer Command Prompt:

```bash
# From VS Developer Command Prompt (x64):
msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m

# Release (enables LTCG/WholeProgramOptimization):
msbuild mmosvr.sln /p:Configuration=Release /p:Platform=x64 /m
```

**Output binaries**: `bin\Debug\x64\` (or `bin\Release\x64\`)
- `GameServer.exe` (port 7777)
- `LoginServer.exe` (port 9999)
- `DummyClient.exe` (test client)

### vcpkg Setup
vcpkg is at `C:\vcpkg\vcpkg\` with global MSBuild integration (`vcpkg integrate install`).
Packages: `boost-asio:x64-windows`, `protobuf:x64-windows`, `nanodbc:x64-windows`.

### Protobuf Code Generation
Proto source files are in `../ShareDir/proto/` (shared with the Unity client project). ProtoLib's Pre-Build Event calls `ShareDir/generate_proto.bat` which runs vcpkg-installed protoc (`C:\vcpkg\vcpkg\installed\x64-windows\tools\protobuf\protoc.exe`). C++ generated files go to `proto/generated/`, C# generated files go to `../ClaudeProject/Assets/Scripts/Proto/`.

## Architecture

```
ProtoLib (static lib)          ← Protobuf generated code (.pb.h/.pb.cc)
ServerCore (static lib)        ← Shared network/DB framework
  ├── Network/                 ← Boost.Asio IOCP, TCP sessions, buffers
  ├── Packet/                  ← Packet header, handler dispatch, ID registry
  ├── Server/                  ← ServerBase, SessionManager, ServiceLocator
  ├── Database/                ← DbManager, DbConnection, DbResult, SpParam
  └── Utils/                   ← Types, Logger

proto/                         ← generated files (source .proto in ../ShareDir/proto/)
GameServer (exe)               ← Game logic, services, packet handlers
LoginServer (exe)              ← Authentication, token generation
DummyClient (exe)              ← Test client
docs/superpowers/              ← Design specs and implementation plans
```

### Project Dependencies
```
GameServer   → ServerCore → ProtoLib
LoginServer  → ServerCore → ProtoLib
DummyClient  → ServerCore → ProtoLib
```

### Class Hierarchy
```
Session → PacketSession → GameSession / LoginSession / DummyClientSession / ServerSession
ServerBase → GameServer / LoginServer
GameService → PlayerService (extensible)
```

`ServerSession` is used for server-to-server connections (e.g., GameServer→LoginServer). `Connector` initiates outbound TCP connections for these links.

### Packet Format
`[uint16 size][uint32 id][protobuf payload]` — 6-byte header (`#pragma pack(1)`). `size` is total including header. IDs defined in `ServerCore/Packet/PacketUtils.h` as `enum class PacketId : uint32`:
- 1–999: Login (C_LOGIN=1, S_LOGIN=2, S_LOGIN_FAIL=3)
- 1000–1999: Game (C_ENTER_GAME=1000 through S_CHAT=1006)
- 2000–2999: Server-to-Server (SS_VALIDATE_TOKEN=2000, SS_VALIDATE_TOKEN_RESULT=2001)

`PacketIdTraits.h` provides compile-time mapping from protobuf message type → PacketId via `PACKET_ID_TRAIT` macro, enabling `PacketSession::Send<T>()` without explicit ID.

### Thread Model
- **Main thread**: Acceptor `io_context` — handles accept callbacks only
- **IoContextPool**: N `io_context` objects (round-robin session assignment), each on its own `std::jthread`. One session is bound to one I/O context at accept time, so per-session callbacks are naturally serialized without strands
- **DB thread pool**: Dedicated `io_context` + worker threads inside `DbManager` for async database work
- Packet handlers execute on I/O threads (can be extended with JobQueue for game tick thread)

### Database Layer (nanodbc/ODBC)
- **DbConnection**: Wraps `nanodbc::connection` with `inUse_` pooling flag and auto-reconnect
- **DbResult**: Result wrapper with index-based and name-based column access, `Next()` iteration, `NextResultSet()` for multi-resultset stored procedures
- **DbManager**: Connection pool (`condition_variable` wait when exhausted) + dedicated I/O thread pool. Two API tiers:
  - **Sync** (`Execute`, `CallProcedure`): blocks calling thread, for server init
  - **Async** (`AsyncExecute`, `AsyncCallProcedure`): posts to DB thread pool, callback re-dispatched to caller's I/O context (keeps result processing on the session's thread)
- **SpParam.h**: `Out<T>()`/`InOut<T>()` wrappers for stored procedure parameters. `AllInputParams` concept guards `AsyncCallProcedure` from output params (unsafe async pattern)

### Server-to-Server Communication
GameServer connects to LoginServer on startup via `Connector`. The token validation flow:
1. Client sends `C_EnterGame` with token to GameServer
2. GameServer stores `(token → weak_ptr<GameSession>)` in `sPendingValidations`, forwards `SS_ValidateToken` to LoginServer via the `ServerSession`
3. LoginServer validates token against `TokenStore`, returns `SS_ValidateTokenResult`
4. GameServer retrieves pending session by token, completes or rejects the login

### ServiceLocator Pattern
`ServiceLocator` is type-indexed (`std::type_index → shared_ptr<void>`), owned per-`ServerBase` instance (not a global singleton). Register during `Init()` before I/O threads start; read-only during operation — no locking needed.

## Adding New Content

1. Add protobuf messages in `proto/*.proto`
2. Add packet IDs in `ServerCore/Packet/PacketUtils.h` (enum + `PACKET_ID_TRAIT` in `PacketIdTraits.h`)
3. Write handler functions (receive typed session + parsed protobuf)
4. Register handlers in the server's `Init()` method via `PacketHandler::Instance().Register<MsgType, SessionType>()`
5. Optionally create a `GameService` subclass and register it in `ServiceLocator`

No network code changes needed.

## Key Conventions

- All project types/aliases in `ServerCore/Utils/Types.h` (global scope, no namespace)
- LOG macros (`LOG_INFO`, `LOG_ERROR`, etc.) are global — synchronous, mutex-guarded stdout
- Sessions use `shared_from_this()` for async lifetime management
- `SendBufferChunkPtr` (shared_ptr) must stay alive during `async_write` — broadcast shares one chunk across all sessions (O(1) alloc regardless of session count)
- ServerCore uses PCH (`pch.h`); exe projects force-include it via `<ForcedIncludeFiles>`
- `PacketHandler` registration and `ServiceLocator` writes must happen at startup before I/O threads start (no locking on these maps)
- `GamePacketHandler` and `LoginPacketHandler` are static-only classes with static member pointers (safe because server object outlives all handlers)

## Known Limitations

- No reconnect logic for GameServer→LoginServer link; if it drops, `C_EnterGame` fails until restart
- Token auth is a stub: any non-empty username+password accepted, tokens never expire
- `PlayerService::FindPlayer()` returns a raw pointer into the map — pointer may dangle if another thread removes the player concurrently

# MMO Project

GitHub: https://github.com/ParkJinsu-924/dbi

## Project Structure

```
C:\claude-project\
├── mmosvr/          ← C++20 MMO 게임 서버 (VS 2022, MSBuild)
├── mmoclient/       ← Unity 6 MMO 클라이언트 (URP)
│   └── Assets/Scripts/Network/  ← MMO Network Layer (TcpSession, PacketRouter, NetworkManager)
│   └── Assets/Scripts/Proto/    ← protoc 자동생성 C# (asmdef: MMO.Proto)
│   └── Assets/Plugins/          ← Google.Protobuf.dll
├── ShareDir/        ← 서버/클라이언트 공유 파일 (proto, 생성 스크립트)
└── debug_tool/      ← Python 디버그/테스트 클라이언트
```

## ShareDir — 공유 리소스

```
ShareDir/
├── proto/                    ← Protobuf 원본 파일 (Single Source of Truth)
│   ├── common.proto          ← Vector3, Timestamp
│   ├── login.proto           ← C_Login, S_Login, S_LoginFail
│   ├── game.proto            ← C_EnterGame, S_EnterGame, C_PlayerMove, ...
│   └── server.proto          ← SS_ValidateToken, SS_ValidateTokenResult (서버 간 전용)
├── generate_proto.bat        ← protoc + PacketId 자동 생성 통합 스크립트
└── generate_packet_ids.js    ← proto 파싱 → PacketId 자동 할당 (Node.js)
```

### 코드 생성 (`generate_proto.bat`)

한 번 실행으로 모든 코드 자동 생성:
1. **protoc**: proto → C++ (.pb.h/.pb.cc) + C# (.cs)
2. **generate_packet_ids.js**: proto에서 메시지 이름 파싱 → 순서대로 PacketId 자동 할당
   - C++: `PacketUtils.h` (enum) + `PacketIdTraits.h` (컴파일 타임 매핑)
   - C#: `PacketIds.cs` (partial class로 각 메시지에 `public const uint PacketId` 추가)

## 서버 빌드 (mmosvr)

```bash
# VS 2022 Developer Command Prompt에서:
msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m
```

- vcpkg 불필요 — 모든 서드파티가 `ThirdParty/`에 포함
- Output: `bin\Debug\x64\` → `GameServer.exe` (7777), `LoginServer.exe` (9999), `DummyClient.exe`

## 클라이언트 빌드 (mmoclient)

Unity Hub → Add project from disk → `mmoclient/` 선택 (Unity 6).
표준 `File > Build Settings` 로 빌드.

### Network Layer (Assets/Scripts/Network/)
- `TcpSession`: 비동기 send/recv 스레드 + ArrayPool 버퍼 풀링 + 상태머신 + DisconnectReason 분리
- `ReceiveBuffer`: TCP 프레이밍(append-and-compact) + 6byte 헤더 검증
- `PacketIdMap`: 리플렉션 기반 Type↔uint 캐시 (1회만 reflect, 이후 dict lookup)
- `PacketRouter.Register<T>(handler)`: 백그라운드에서 protobuf 파싱 → MainThreadDispatcher로 dispatch
- `MainThreadDispatcher`: 프레임당 처리 budget 제한, 백그라운드-안전 TryEnqueue
- `LoginSession` / `GameSession`: 도메인별 typed event API
- `NetworkManager` (싱글턴 MonoBehaviour): 로그인 → S_Login → GameServer → C_EnterGame 플로우 오케스트레이션
- `NetworkConfig` (ScriptableObject): 환경별 endpoint/timeout/buffer 설정

## 새 패킷 추가 workflow

1. `ShareDir/proto/`에서 `.proto` 파일에 메시지 정의
2. `generate_proto.bat` 실행 → protobuf 코드 + PacketId 자동 생성
3. 서버: 핸들러 함수 작성 + `PacketHandler::Instance().Register<MsgType, SessionType>()` 등록
4. 클라이언트: `PacketRouter.Register<T>(handler)` 등록
5. **PacketId 수동 관리 불필요** — 스크립트가 자동 할당

## 패킷 프로토콜

`[uint16 size][uint32 id][protobuf payload]` — 6바이트 헤더
- `size`: 헤더 포함 전체 패킷 크기
- `id`: 자동 생성된 PacketId (proto 메시지 순서 기반)

## Key Conventions

- 서버: `session->Send(msg)` — PacketIdTraits로 ID 자동 결정
- 클라이언트: `client.Send(msg)` — 리플렉션으로 `T.PacketId` 자동 추출
- `PacketRouter.Register<T>(handler)` — T에서 PacketId 자동 추출, 외부 ID 지정 불필요

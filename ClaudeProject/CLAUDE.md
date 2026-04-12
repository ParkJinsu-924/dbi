# ClaudeProject — Unity MMO 클라이언트

## Project Overview

Hack & Slash MMO 클라이언트. URP (Universal Render Pipeline) 기반 Unity 프로젝트. C++ 서버(`mmosvr`)와 Protobuf 기반 TCP 통신.

## Build

- Unity 에디터에서 `Build > Build And Run (Relative Path)` (커스텀 메뉴)
- 빌드 산출물: `Build/` 폴더 (프로젝트 루트 상대 경로)
- 또는 표준 `File > Build Settings` 사용

## Project Settings

- **Input System**: Both (New + Old) — `activeInputHandler: 1`
- **Run In Background**: 켜짐 — 멀티 클라이언트 테스트용
- **Fullscreen Mode**: Windowed (3) — 창 모드
- **Resizable Window**: 켜짐
- **Target Frame Rate**: 60fps (`AppSettings.cs`에서 런타임 설정)

## Architecture

```
Assets/Scripts/
├── Network/
│   ├── NetworkClient.cs        ← TCP 클라이언트 (6바이트 헤더, Send<T> 자동 PacketId)
│   ├── NetworkManager.cs       ← 로그인/게임서버 연결 관리 (싱글톤)
│   ├── PacketRouter.cs         ← PacketId 기반 핸들러 디스패치
│   ├── PlayerManager.cs        ← 원격 플레이어 스폰/동기화
│   ├── NetworkPlayerSync.cs    ← 로컬 플레이어 위치 전송 (0.1초 주기)
│   ├── RemotePlayerController.cs ← 원격 플레이어 위치 보간
│   └── MainThreadDispatcher.cs ← 네트워크 스레드 → 메인 스레드 디스패치
├── Proto/
│   ├── Common.cs, Login.cs, Game.cs  ← protoc 생성 (수동 편집 금지)
│   └── PacketIds.cs            ← 자동 생성 (partial class, PacketId 상수)
├── UI/
│   └── LoginUI.cs              ← 로그인 화면 + EventSystem 자동 생성
├── Editor/
│   ├── BuildScript.cs          ← Build 메뉴 (상대 경로 빌드)
│   └── HackAndSlashSceneSetup.cs ← 씬 자동 구성 도구
├── AppSettings.cs              ← 프레임레이트, vSync 설정 (RuntimeInitializeOnLoadMethod)
├── PlayerController.cs         ← WASD 이동, 스프린트, 닷지
├── PlayerCombat.cs             ← 공격 시스템
├── CameraFollow.cs             ← 3인칭 카메라
├── HealthSystem.cs             ← HP 시스템
├── EnemyAI.cs                  ← NavMesh 기반 적 AI
└── GameManager.cs              ← 웨이브 스폰, UI 관리
```

## Packet System

- 헤더: `[uint16 size][uint32 id][protobuf payload]` — 6바이트
- `NetworkClient.Send<T>(msg)` — 리플렉션으로 `T.PacketId` 자동 추출
- `PacketRouter.Register<T>(handler)` — T에서 PacketId 자동 추출, ID 수동 지정 불필요
- PacketId는 `PacketIds.cs`에 partial class로 정의됨 (자동 생성)

## Code Generation

`../ShareDir/generate_proto.bat` 실행 시:
1. protoc → `Proto/Common.cs`, `Login.cs`, `Game.cs` 생성
2. generate_packet_ids.js → `Proto/PacketIds.cs` 생성

**Proto/ 폴더의 파일을 수동으로 편집하지 말 것** — 재생성 시 덮어씌워짐.

## Key Conventions

- `NetworkManager`는 싱글톤 (`DontDestroyOnLoad`)
- `LoginUI`는 로그인 전 `PlayerInput` 비활성화, 게임 진입 시 활성화
- `LoginUI`는 `EventSystem`이 없으면 런타임에 자동 생성
- `AppSettings`는 `[RuntimeInitializeOnLoadMethod]`로 씬에 수동 추가 불필요
- Protobuf DLL: `Assets/Plugins/Google.Protobuf.dll`

## Server Connection

- LoginServer: 127.0.0.1:9999 (NetworkManager Inspector에서 변경 가능)
- GameServer: 127.0.0.1:7777 (LoginServer 응답에서 자동 설정)
- 흐름: 로그인 → 토큰 발급 → GameServer 자동 접속 → 토큰 검증 → 게임 입장

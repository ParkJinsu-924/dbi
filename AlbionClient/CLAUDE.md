# AlbionClient — 알비온 스타일 탑다운 MMO 클라이언트

Unity Technologies **Open Project #1 ("Chop Chop")** 을 베이스로 Unity 6 에 적응시킨 MMO 스타터.

- 원본: https://github.com/UnityTechnologies/open-project-1 (shallow clone, .git 제거됨)
- 원본 Unity: 2020.3.17f1 / 원본 라이선스: UCL (LICENSE 파일 참조)

## Unity 프로젝트 경로

실제 Unity 프로젝트 루트는 `AlbionClient/UOP1_Project/`.
Unity Hub → Add project from disk → `AlbionClient/UOP1_Project` 선택.

Unity 에디터 버전: **6000.4.2f1**.

## MMO 적응 내역

### 신규 스크립트 (`Assets/Scripts/MMO/`)
- `NetworkBootstrap.cs` — 씬에 붙여서 NetworkManager 부팅 + 디버그 자동 로그인
- `TopDownCameraRig.cs` — Cinemachine 없는 탑다운 카메라 (Q/E 회전, 휠 줌)
- `ClickToMoveController.cs` — NavMeshAgent 기반 좌클릭 이동 (UI 클릭 무시)
- `Network/` — TCP/Protobuf 레이어 (NetworkManager, NetworkClient, PacketRouter, PlayerManager, NetworkPlayerSync, RemotePlayerController, MainThreadDispatcher)
- `Proto/` — protoc 생성 C# (Common, Login, Game, PacketIds) — `../ShareDir/generate_proto.bat` 가 직접 이 폴더에 출력
- `Editor/MapExporter.cs` — 씬 지오메트리 → `ShareDir/maps/default.scene.bin` Export (Recast NavMesh 입력)
- `../Plugins/Google.Protobuf.dll`

### 중립화 (compile 만 되도록 본문 제거)
- `Scripts/SaveSystem/SaveSystem.cs` — 메서드 no-op. 서버가 권위 소스이므로 로컬 세이브 불필요
- `Scripts/Camera/CameraManager.cs` — Cinemachine 2.x API (`CinemachineFreeLook`) 제거, shell 만 유지
- `Scripts/SceneManagement/LocationEntrance.cs` — `CinemachineVirtualCamera` 제거

### 패키지 업그레이드 (`Packages/manifest.json`)
Unity 6 호환으로 bump. 재resolve 유도를 위해 `packages-lock.json` 삭제.
- URP 10.6 → 17.0.3
- Cinemachine 2.7 → 3.1.2 (**major breaking**)
- Input System 1.0 → 1.11.2
- Addressables 1.19 → 2.2.2
- ProBuilder 4.5 → 6.0.4
- 제거: `textmeshpro` (ugui 2.0 에 통합), `learn.iet-framework`, `ui.builder`, `modules.unityanalytics`

## 서버 연결 흐름

1. LoginServer (127.0.0.1:9999) 로 C_Login 전송
2. 응답으로 token + GameServer IP/port 수신
3. GameServer (응답값) 로 C_EnterGame 전송 (token 포함)
4. S_EnterGame 수신 → spawn position 확보 → 플레이어 스폰

패킷 프로토콜 (부모 `CLAUDE.md` 참조): `[uint16 size][uint32 id][protobuf payload]` 6바이트 헤더.

## 새 패킷 추가 workflow

`../ShareDir/generate_proto.bat` 실행 시 C# Proto 가 `Assets/Scripts/MMO/Proto/` 로 자동 생성됨.

## 에디터 첫 오픈 시 예상 이슈

1. **API Updater prompt** — 수락. 일부 deprecated API 자동 변환.
2. **Cinemachine 2→3 호환 에러** — 원본 Chop Chop VCam 프리팹 (`Assets/Prefabs/CinemachineFreeLookRig.prefab` 등) 가 깨짐. 사용하지 않으므로 무시하거나 프리팹 삭제.
3. **씬 missing script 경고** — `LocationEntrance`, `CameraManager` 의 Cinemachine 필드가 None 으로 표시됨. 무시 가능.
4. **Addressables 재빌드** — Window > Asset Management > Addressables > Groups > Build > New Build.

## 다음 단계 (수작업)

1. Unity Hub 에서 `UOP1_Project` 오픈 → 패키지 resolve → 컴파일 에러 목록 확인
2. 빈 씬에 GameObject 하나 만들고 `NetworkBootstrap` 추가 → autoLogin 체크 → Play
3. 플레이어 프리팹: Capsule + CharacterController 또는 NavMeshAgent + `ClickToMoveController` + `NetworkPlayerSync`, Tag "Player"
4. 카메라: MainCamera 에 `TopDownCameraRig` 추가, target 을 비워두고 `TransformAnchor` 연결 (Chop Chop 의 `ProtagonistTransformAnchor` 재사용)
5. 빈 GameObject 에 `PlayerManager` 추가, remotePlayerPrefab 은 None 이어도 fallback 으로 초록 캡슐이 생성됨

## 원본 Chop Chop 자산 재활용 우선순위

- **유지 권장**: SO 이벤트 채널 시스템 (`Events/`), `TransformAnchor`, `GameStateSO`, `InputReader`, UI 메뉴 프레임워크, 씬 매니지먼트
- **교체 예정**: `Characters/Protagonist.cs` (WASD 기반) → `ClickToMoveController` 로 이미 대체 가능
- **보류**: `Quests/`, `Dialogues/`, `Inventory/` — 싱글플레이 기반 설계. 서버 권위로 바꾸려면 별도 리팩터 필요

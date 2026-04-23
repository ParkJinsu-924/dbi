# 맵 제작 & 서버 동기화 가이드

이 문서는 Unity에서 게임 맵을 제작하고, C++ 서버가 해당 맵을 인식하여 플레이어 이동을 검증하게 만드는 전체 과정을 설명합니다.

---

## 1. 왜 이 시스템이 필요한가?

클라이언트만 맵을 아는 경우의 문제:
- 해킹된 클라이언트가 "벽 안에 있다", "건물 내부로 텔레포트" 같은 위치를 전송해도 서버가 막을 수 없음
- 다른 플레이어의 시야에서 캐릭터가 허공이나 장애물 안에 있는 것처럼 보임

따라서 **서버도 맵을 알고 있어야** 합니다. 그런데 서버는 C++이고 클라이언트는 Unity인데, 어떻게 같은 맵을 공유할까요?

→ **Unity에서 씬의 지오메트리를 파일로 Export하면, 서버가 같은 파일을 읽어 자체적으로 NavMesh를 빌드**합니다.

---

## 2. 전체 원리

```
┌──────────────────── Unity ────────────────────┐
│                                                │
│  씬 제작 (Primitive 배치, Static 마킹)          │
│         │                                      │
│         │  NavMesh 베이크 (클라이언트 AI용)       │
│         │                                      │
│         │  MapExporter 실행                     │
│         ▼                                      │
│  [Static 오브젝트의 메시 정점/삼각형 수집]        │
│         │                                      │
└─────────┼──────────────────────────────────────┘
          │
          ▼
   ┌──────────────────────────────────────┐
   │  ShareDir/maps/default.scene.bin     │  ← 공유 바이너리 파일
   │  (월드 좌표의 정점 + 삼각형 인덱스)      │
   └──────────────────────────────────────┘
          │
┌─────────┼──────────────── C++ 서버 ─────────────┐
│         ▼                                      │
│  MapService::Init() 시 파일 로드                │
│         │                                      │
│         │  Recast 파이프라인으로 NavMesh 빌드    │
│         │  (voxelize → contour → polymesh)     │
│         ▼                                      │
│  Detour NavMesh (런타임 쿼리 가능)              │
│         │                                      │
│         │  C_PlayerMove 수신 시                 │
│         ▼                                      │
│  IsOnNavMesh(x,y,z)? ──No──► S_MoveCorrection │
│         │                                      │
│        Yes                                     │
│         ▼                                      │
│  PlayerService 업데이트 + 전체 브로드캐스트        │
└────────────────────────────────────────────────┘
```

### 핵심 개념

**왜 Unity NavMesh를 그대로 안 쓰고 원본 지오메트리를 Export하나?**

- **NavMesh ≠ 지형**: NavMesh는 "걸을 수 있는 영역"을 계산한 **결과물**. 지형은 "세상의 물리적 형태" 그 자체.
- Unity NavMesh와 Detour NavMesh는 **내부 포맷이 완전히 다릅니다**. 변환하려면 내부 구조를 하나하나 매핑해야 하는데 오히려 더 복잡.
- 대신 **원본 지오메트리(큐브/평면의 메시)**를 주면, Unity와 서버가 각자의 엔진으로 같은 입력을 처리하여 동등한 NavMesh를 얻습니다.
- 서버는 Recast 파라미터(`agentRadius`, `agentHeight` 등)를 자체 설정할 수 있어 서버 로직에 맞춘 NavMesh를 만들 수 있습니다.

---

## 3. 맵 제작하기 (Unity)

### 3.1 어떤 도구로 맵을 만드나?

프로젝트는 **어떤 방식이든** 허용합니다 — 중요한 건 "씬에 Collider가 있는 Static GameObject가 배치되어 있는가"입니다.

| 도구 | 용도 | 난이도 |
|------|------|--------|
| **Primitive (Cube, Plane, Sphere)** | 간단한 블록 맵, 프로토타입 | ★ |
| **Unity Terrain** | 야외 지형 (높낮이, 텍스처) | ★★ |
| **ProBuilder** (Package Manager 설치) | 던전/실내 맵 모델링 | ★★ |
| **외부 3D 모델 임포트** (.fbx, .obj) | 전문적인 레벨 디자인 | ★★★ |

### 3.2 맵 제작 규칙 (필수)

어떤 도구로 만들든 다음 규칙을 지켜야 서버에 반영됩니다:

1. **GameObject를 `Static`으로 마킹**
   - Inspector 우상단 "Static" 체크박스 → 전체 Static 선택
   - 또는 최소한 `Navigation Static` + `BatchingStatic`

2. **MeshFilter + MeshRenderer가 있어야 함**
   - Primitive Cube/Plane은 기본 포함
   - 빈 GameObject(Empty)는 Export 대상이 아님

3. **Ground와 Obstacle 구분** (관례)
   - 이름을 `Ground`, `Obstacle` 등으로 지어두면 나중에 필터링/디버깅 편리
   - 현재 Export는 이름으로 구분하지 않고 모든 Static MeshFilter를 수집함

### 3.3 샘플 맵 예시

현재 프로젝트의 기본 맵 (`HackAndSlashSceneSetup.cs` 자동 생성):

```
Ground:   Cube, 위치 (0,0,0),   Scale (40, 0.5, 40)   ← 40x40 바닥
Obstacle: Cube, 위치 (5, 0.75, 5),    Scale (2, 1.5, 2)
Obstacle: Cube, 위치 (-7, 0.75, 3),   Scale (3, 1.5, 1.5)
Obstacle: Cube, 위치 (3, 0.75, -8),   Scale (1.5, 1.5, 3)
Obstacle: Cube, 위치 (-5, 0.75, -6),  Scale (2, 1.5, 2)
```

더 복잡한 맵을 만들려면:
- 벽을 길게 배치하여 방 구조 만들기
- 높낮이가 다른 플랫폼 (점프/계단)
- 경사면 (최대 45도까지 걸을 수 있음 — Recast 기본 설정)

### 3.4 NavMesh 베이크 (선택적)

서버는 NavMesh 파일을 읽지 않습니다. 서버가 Recast로 자체 NavMesh를 빌드하기 때문입니다.

하지만 **클라이언트(특히 EnemyAI)가 NavMeshAgent를 사용**하므로, Unity에서는 NavMesh 베이크가 필요합니다:

1. `Window > AI > Navigation` 열기
2. Static 오브젝트들이 `Navigation Static`인지 확인
3. `Bake` 탭 → `Bake` 버튼 클릭
4. Scene 뷰에서 파란색 NavMesh 오버레이 확인

> **주의**: NavMesh 베이크는 **클라이언트용**입니다. 서버에 영향 없음.

---

## 4. 서버용 지오메트리 Export하기

### 4.1 실행 방법

Unity 메뉴: `Hack And Slash > Export Scene Geometry`

### 4.2 무슨 일이 일어나는가?

`MapExporter.cs`가 다음을 수행:

1. 씬의 모든 `MeshFilter` 탐색
2. `isStatic == true`이고 `sharedMesh != null`인 것만 필터링
3. 각 메시의 정점을 `transform.TransformPoint()`로 **월드 좌표**로 변환
4. 삼각형 인덱스는 `vertexOffset`을 적용하여 모든 메시를 하나의 배열로 병합
5. `ShareDir/maps/default.scene.bin` 파일로 저장

### 4.3 파일 포맷

```
바이너리 (리틀엔디안):

HEADER (20 bytes)
  [4B] Magic:        "GSCN" (0x4E435347)
  [4B] Version:      uint32 = 1
  [4B] vertexCount:  uint32
  [4B] triangleCount: uint32
  [4B] reserved:     uint32 = 0

VERTICES (vertexCount × 12 bytes)
  반복: float x, float y, float z   ← 월드 좌표

TRIANGLES (triangleCount × 12 bytes)
  반복: int32 i0, int32 i1, int32 i2  ← 정점 인덱스
```

### 4.4 Export 결과 확인

Unity Console에서 로그 확인:

```
[MapExporter] Ground: 24 verts, 12 tris
[MapExporter] Obstacle: 24 verts, 12 tris
...
[MapExporter] Exported: 120 vertices, 60 triangles, 1460 bytes
[MapExporter] Saved to: C:\...\ShareDir\maps\default.scene.bin
```

파일이 생성되지 않는다면:
- 씬에 Static MeshFilter가 없음 (로그에 "No static MeshFilter objects found" 표시)
- `ShareDir/maps/` 디렉토리 권한 문제

---

## 5. 서버에 반영하기

### 5.1 서버 시작 시 자동 로드

서버(`GameServer.exe`)는 시작 시 `MapService::Init()`에서 다음 경로들을 순서대로 탐색:

```
../../../../ShareDir/maps/default.scene.bin
../../../ShareDir/maps/default.scene.bin
../../ShareDir/maps/default.scene.bin
../ShareDir/maps/default.scene.bin
ShareDir/maps/default.scene.bin
```

`bin/Debug/x64/GameServer.exe`에서 실행하면 `../../../../ShareDir/maps/...`가 매칭됩니다.

### 5.2 서버 로그로 확인

정상 로드 시:
```
[INFO] MapService: Loading C:\...\ShareDir\maps\default.scene.bin
[INFO] MapService: Loaded 120 vertices, 60 triangles
[INFO] MapService: NavMesh ready (48 polys)
[INFO] MapService: NavMesh built successfully
```

맵 파일이 없을 때:
```
[INFO] MapService: No map file found, movement validation disabled
```
→ 이 경우 서버는 정상 실행되지만 이동 검증이 비활성화됩니다 (개발 편의성).

### 5.3 맵 갱신 워크플로

```
1. Unity에서 씬 수정 (장애물 추가/삭제)
2. Hack And Slash > Export Scene Geometry 실행
3. 서버 종료
4. 서버 재시작 (파일을 다시 읽음)
5. 클라이언트 접속 → 새 맵으로 검증
```

> **핫리로드 미지원**: 현재 서버는 실행 중 맵을 다시 읽지 않습니다. 맵 변경 시 서버 재시작이 필요합니다.

---

## 6. 검증 (테스트 방법)

### 6.1 정상 케이스

1. 서버 실행 → "NavMesh ready" 로그 확인
2. 클라이언트 접속 → 장애물 **없는** 영역에서 자유롭게 이동
3. 다른 클라이언트로 2번째 접속 → 두 플레이어가 서로의 이동을 정상 확인

### 6.2 차단 케이스

1. 클라이언트에서 **장애물 방향으로 이동 시도**
2. Unity Console 로그:
   ```
   [NetworkPlayerSync] Position corrected to (X, Y, Z)
   ```
3. 캐릭터가 장애물 직전으로 스냅됨
4. 다른 클라이언트에서도 해당 플레이어가 장애물 내부로 들어가지 않음

### 6.3 경계 케이스

- **맵 바깥 좌표**: 서버가 가장 가까운 NavMesh 위의 점으로 보정
- **공중 좌표** (Y가 비정상): NavMesh에서 가장 가까운 지면으로 보정
- **완전히 유효한 위치 없음** (3미터 내 NavMesh 없음): 이동 완전히 무시 (브로드캐스트 안 함)

---

## 7. Recast 파라미터 튜닝 (심화)

`mmosvr/GameServer/Services/MapService.cpp` 상단에 정의:

```cpp
constexpr float CELL_SIZE = 0.3f;          // Voxel 가로/세로 (작을수록 정밀, 빌드 느림)
constexpr float CELL_HEIGHT = 0.2f;        // Voxel 높이
constexpr float AGENT_HEIGHT = 2.0f;       // 플레이어 키
constexpr float AGENT_RADIUS = 0.5f;       // 플레이어 반지름 (장애물과 최소 거리)
constexpr float AGENT_MAX_CLIMB = 0.5f;    // 오를 수 있는 최대 높이 (계단)
constexpr float AGENT_MAX_SLOPE = 45.0f;   // 걸을 수 있는 최대 경사각
```

### 언제 조정하나?

| 문제 | 조정 대상 |
|------|-----------|
| NavMesh가 너무 거칠다 (좁은 틈이 막힘) | `CELL_SIZE` 감소 (0.15 등) |
| 플레이어가 장애물에 바짝 붙을 수 있다 | `AGENT_RADIUS` 증가 |
| 높은 계단을 못 올라간다 | `AGENT_MAX_CLIMB` 증가 |
| 가파른 언덕을 못 걷는다 | `AGENT_MAX_SLOPE` 증가 |
| NavMesh 빌드가 너무 느리다 | `CELL_SIZE` 증가 (0.5 등) |

파라미터 변경 후 서버 재빌드 필요.

---

## 8. 문제 해결 FAQ

**Q1. Export는 됐는데 서버가 NavMesh를 빌드 못한다.**
- 로그 확인: "rcRasterizeTriangles failed" 등 특정 단계 실패 메시지
- 원인: 메시 데이터가 비어있거나 퇴화된 삼각형 (면적 0) 포함
- 해결: 씬에서 잘못된 메시 제거, Export 다시 실행

**Q2. 플레이어가 장애물 근처에서 계속 튕긴다.**
- 원인: `AGENT_RADIUS` 확장으로 인해 장애물 주변에 진입 불가 영역 생김. 클라는 해당 영역에 들어가려 하지만 서버가 계속 보정.
- 해결: 클라이언트의 이동 제한을 서버와 맞추거나, `AGENT_RADIUS`를 감소시킴

**Q3. 맵을 바꿨는데 서버에 반영이 안 된다.**
- Export를 했는지 확인 (`ShareDir/maps/default.scene.bin`의 수정 시간 확인)
- 서버를 재시작했는지 확인 (핫리로드 미지원)

**Q4. Y좌표(높이)가 이상하게 보정된다.**
- 현재 NavMesh는 2.5D (XZ 평면 + 높이). 계단이나 다층 구조에서는 잘못된 층으로 보정될 수 있음
- 해결: 층을 더 멀리 떨어뜨리거나, `CELL_HEIGHT`를 줄여 해상도 향상

**Q5. 특정 오브젝트를 NavMesh에서 제외하고 싶다 (예: 장식물).**
- 현재 Export는 **모든 Static MeshFilter**를 수집함
- 해결 방법 1: 해당 오브젝트의 `isStatic`을 false로 설정
- 해결 방법 2: `MapExporter.cs`에 태그/레이어 필터 추가 (코드 수정 필요)

**Q6. 여러 맵을 지원하고 싶다.**
- 현재는 `default.scene.bin` 하나만 지원. 맵별로 파일명을 다르게 하고 `MapService::Init()`에서 로드 경로를 설정 가능하게 수정 필요
- `C_EnterGame`에 맵 ID를 추가하여 서버가 분기 처리하는 방식 등 확장 가능

---

## 9. 관련 파일 레퍼런스

### 클라이언트 (Unity)
- `AlbionClient/UOP1_Project/Assets/Scripts/MMO/Editor/MapExporter.cs` — 씬 지오메트리 Export
- `AlbionClient/UOP1_Project/Assets/Scripts/MMO/Network/NetworkPlayerSync.cs` — S_MoveCorrection 수신 처리

### 공유 (ShareDir)
- `ShareDir/maps/default.scene.bin` — Export된 바이너리 맵 데이터
- `ShareDir/proto/game.proto` — `S_MoveCorrection` 등 프로토 메시지

### 서버 (C++)
- `mmosvr/GameServer/Services/MapService.h/.cpp` — 맵 로드 + Recast NavMesh 빌드 + Detour 쿼리
- `mmosvr/GameServer/GamePacketHandler.cpp` — `C_PlayerMove`에서 이동 검증
- `mmosvr/RecastDetour/RecastDetour.vcxproj` — Recast/Detour 정적 라이브러리
- `mmosvr/ThirdParty/recastnavigation/` — Recast/Detour 소스 코드

---

## 10. 추가 학습 자료

- **Recast Navigation 공식 문서**: https://recastnav.com
- **Detour API**: `ThirdParty/recastnavigation/include/DetourNavMeshQuery.h` 참고 (경로탐색 등 추가 기능)
- **Unity NavMesh**: https://docs.unity3d.com/Manual/nav-BuildingNavMesh.html

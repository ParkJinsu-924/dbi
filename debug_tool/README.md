# debug_tool

MMO 서버 테스트용 Python 기반 2D 디버그 클라이언트.
Unity 클라이언트 대신 빠르게 서버와 상호작용하며 패킷 흐름을 눈으로 확인하기 위한 도구.

> 이 디렉토리는 **Claude가 주 수정자**라는 전제로 유지됩니다. 파일을 수정하기 전 아래
> "AI가 수정할 때 주의할 점" 섹션을 반드시 읽어주세요.

## 실행

```bash
# 의존성 설치 (최초 1회)
pip install -r requirements.txt

# 실행 (기본 계정: test / test)
python client.py

# 또는 계정 지정
python client.py <username> <password>
```

Windows에서는 `scripts/run_client.bat` 이 Python 설치 및 의존성까지 자동 확인해 줍니다
(처음 실행 시 Python 미설치라면 winget 으로 user 스코프 설치 제안 → 의존성 pip install
→ client.py 실행).

## 봇 투입기 (`bot.py`)

많은 유저가 동시에 맵을 누비는 풍경을 보거나 서버 부하 테스트를 할 때 사용합니다.
헤드리스(pygame 창 없음)로 한 프로세스가 N마리의 봇을 쓰레드로 띄웁니다.

```bash
# 기본 10마리 (bot_0001 ~ bot_0010)
python bot.py

# 개수 지정
python bot.py 50

# 개수 + 접두어 지정 → loader_0001 ~ loader_0030
python bot.py 30 loader
```

Windows 에서는 `scripts/run_bot.bat` 을 더블클릭하거나 인자 전달:

```bat
scripts\run_bot.bat
scripts\run_bot.bat 50
scripts\run_bot.bat 30 loader
```

각 봇은 무작위 지점으로 이동하고 주기적으로 스킬을 시전합니다. 행동 간격과 이동
범위는 `bot.py` 상단의 `# ── 봇 행동 파라미터 ──` 섹션에서 조정. Ctrl+C 로 정지.

**주의**: 봇은 LoginServer/GameServer 에 실제로 접속하므로 서버가 먼저 떠있어야 합니다.
스킬 쿨다운이 0 (`skill_templates.csv` 의 플레이어 스킬) 인 상태에서는 쿨다운 없이
난사하므로 서버 부하에 유의하세요.

## 테스트

```bash
pip install -r requirements.txt   # pytest 포함
python -m pytest tests/ -v
```

- `tests/test_config.py`: 설정 값의 타입/범위 검증
- `tests/test_network.py`: 패킷 프레이밍, ID 매핑, 실제 로컬 TCP 라운드트립
- `tests/test_feedback.py`: damage popup/파티클/카메라 shake/오디오 fallback 동작
- `tests/test_cc_visuals.py`: CC 레지스트리 완전성, `_apply_cc_to_target` 라우팅
- `tests/test_bot.py`: Bot 인스턴스화, 이동 범위, 스킬 요청 빌드
- `tests/test_skill_data.py`: 스킬 CSV 파싱

## 파일 구성

| 파일                | 역할                                      | 수정 가능? |
| ------------------- | ----------------------------------------- | :--------: |
| `client.py`         | 메인 루프, 입력, 패킷 디스패치            |     O      |
| `bot.py`            | 헤드리스 봇 N마리 투입기 (부하/시각 데모용) |     O      |
| `scripts/`          | Windows 실행 런처 모음 (`run_client.bat` / `run_bot.bat` / `_setup_python.bat`) | O |
| `network.py`        | TCP 소켓 + 패킷 프레이밍                  |     O      |
| `renderer.py`       | pygame 기반 2D 렌더링                     |     O      |
| `feedback.py`       | damage popup/파티클/카메라 shake/오디오   |     O      |
| `cc_visuals.py`     | CC(Stun/Root/Slow/…) 시각화 레지스트리    |     O      |
| `skill_data.py`     | 스킬 CSV 로더 (ShareDir CSV 파싱)         |     O      |
| `effect_data.py`    | 이펙트 CSV 로더                           |     O      |
| `config.py`         | 네트워크/게임플레이/레이아웃/피드백 상수  |     O      |
| `log_setup.py`      | 로깅 초기화                               |     O      |
| `assets/sounds/`    | 선택적 `.wav` 오버라이드 (없으면 절차 합성) |     O      |
| `requirements.txt`  | 파이썬 의존성                             |     O      |
| `common_pb2.py`     | **자동 생성** (protobuf)                  |     X      |
| `login_pb2.py`      | **자동 생성** (protobuf)                  |     X      |
| `game_pb2.py`       | **자동 생성** (protobuf)                  |     X      |
| `packet_ids.py`     | **자동 생성** (PacketId)                  |     X      |

자동 생성 파일(`*_pb2.py`, `packet_ids.py`)은 `.gitignore`에 등록되어 있으며,
직접 수정하지 않습니다. (아래 "Protobuf / PacketId 재생성" 참고)

## Protobuf / PacketId 재생성

패킷 스키마(프로토)의 Single Source of Truth는 `ShareDir/proto/`입니다.
메시지 추가/변경 후에는 반드시 재생성 스크립트를 돌려야 합니다.

```bash
# 프로젝트 루트에서
cd ShareDir
generate_proto.bat
```

이 스크립트가 한 번에 처리하는 것:

1. `protoc` 실행 → `debug_tool/common_pb2.py`, `login_pb2.py`, `game_pb2.py` 재생성
2. `generate_packet_ids.js` 실행 → `debug_tool/packet_ids.py` 재생성 (C++/C# 쪽도 동시 갱신)

**스크립트 실행 경로**:
- Python 출력 → `debug_tool/` (이 디렉토리)
- C++ 출력  → `mmosvr/proto/generated/`
- C# 출력   → `ClaudeProject/Assets/Scripts/Proto/`

즉 서버/클라(C++), Unity(C#), debug_tool(Python) 이 **한 번의 배치 실행**으로
모두 동기화됩니다. debug_tool에서만 별도로 돌리는 경로는 없습니다.

## 새 패킷을 다루기 위한 흐름

1. `ShareDir/proto/*.proto` 에 메시지 추가
2. `ShareDir/generate_proto.bat` 실행
3. `network.py`의 `_MSG_CLASS_MAP` (수신용) 또는 `_ID_MAP` (송신용)에 등록
4. `client.py` 에 `_h_xxx(state, msg)` 핸들러 추가 + `PACKET_HANDLERS` 딕셔너리에 등록
5. 필요 시 다음 중 하나 선택:
   - **상태 변경만** → `GameState` 필드 업데이트 (기존 `_h_*` 패턴 따름)
   - **정적 시각화** (체력바, 위치 등) → `renderer.py`
   - **순간 시각/청각 피드백** (데미지 숫자, 파티클, 카메라 흔들림, 사운드)
     → `_h_*` 핸들러 안에서 `state.feedback.on_xxx(...)` 호출.
     필요하면 `feedback.py` 의 `FeedbackSystem` 에 새 `on_event()` 메서드 추가.

## AI가 수정할 때 주의할 점

- **설정값은 `config.py`에 모아둔다.** 새 하드코딩 상수를 추가하지 말고 config에 추가 후 import.
- **자동 생성 파일을 수정하지 않는다.** `*_pb2.py`, `packet_ids.py`는 `.gitignore` 대상이며,
  수정하면 다음 `generate_proto.bat` 실행에서 무조건 덮어써집니다.
- **서버가 authoritative.** 클라이언트는 예측/보간만 하며, 충돌/히트 판정은 서버 결과를 신뢰합니다.
  클라이언트 단독으로 게임 규칙을 추가하지 마세요.
- **렌더 좌표계**: 월드는 `(x, y, z)`이며 top-down으로 `(x, z)` 평면만 화면에 투영합니다.
  `y`는 표시에 사용되지 않습니다 (높이는 현재 무시).
- **패킷 핸들링 추가 시**: `client.py` 의 `_h_xxx(state, msg)` 함수로 작성 후
  `PACKET_HANDLERS` 딕셔너리에 `packet_ids.S_XXX: _h_xxx` 로 등록.
- **시각/청각 피드백 추가 시**: `FeedbackSystem` 파사드 경유로 일관된다.
  - 호출부는 항상 `if state.feedback is not None:` 가드 (테스트/headless 호환).
  - 피드백 상태(popups/particles/shake) 는 world 좌표 `(wx, wz)` 로 저장 → 카메라가 움직여도 자연스럽게 따라간다.
  - 오디오는 `assets/sounds/*.wav` 없으면 절차적 합성으로 대체되므로 **파일 의존성을 코드에 심지 않는다**.
  - 새 이벤트 추가 시 `FeedbackSystem.on_xxx()` 메서드로 노출하고, 내부에서 파티클/팝업/셰이크/사운드 조합.
- **새 CC 타입 추가 시** (`cc_visuals.py`):
  1. `effects.csv` 의 `cc_flag` 컬럼에 새 문자열 추가 (예: `"Fear"`).
  2. `cc_visuals.py` 에 `_draw_fear(screen, sx, sy, t)` 함수 추가 — 시그니처 고정.
  3. `CC_VISUALIZERS` 딕셔너리에 `"Fear": _draw_fear` 한 줄 추가.
  - 렌더러/클라이언트 본문은 **수정 불필요**. 라우팅은 `client._apply_cc_to_target` 이 자동 처리.
- **Renderer 시그니처 확장**: `draw_frame(...)` 에 새 파라미터 추가 시 **반드시 default 값**
  을 두어 기존 호출부(테스트 포함)가 깨지지 않도록 한다.

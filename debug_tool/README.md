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

Windows에서는 `run_client.bat`이 Python 설치 및 의존성까지 자동 확인해 줍니다.

## 테스트

```bash
pip install -r requirements.txt   # pytest 포함
python -m pytest tests/ -v
```

- `tests/test_config.py`: 설정 값의 타입/범위 검증
- `tests/test_network.py`: 패킷 프레이밍, ID 매핑, 실제 로컬 TCP 라운드트립

## 파일 구성

| 파일              | 역할                             | 수정 가능? |
| ----------------- | -------------------------------- | :--------: |
| `client.py`       | 메인 루프, 입력, 패킷 디스패치   |     O      |
| `network.py`      | TCP 소켓 + 패킷 프레이밍         |     O      |
| `renderer.py`     | pygame 기반 2D 렌더링            |     O      |
| `config.py`       | 네트워크/게임플레이/레이아웃 상수 |     O      |
| `requirements.txt`| 파이썬 의존성                    |     O      |
| `run_client.bat`  | Windows 실행 스크립트            |     O      |
| `common_pb2.py`   | **자동 생성** (protobuf)         |     X      |
| `login_pb2.py`    | **자동 생성** (protobuf)         |     X      |
| `game_pb2.py`     | **자동 생성** (protobuf)         |     X      |
| `packet_ids.py`   | **자동 생성** (PacketId)         |     X      |

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
4. `client.py` 메인 루프에 패킷 핸들러 분기 추가
5. 필요 시 `renderer.py`에 시각화 코드 추가

## AI가 수정할 때 주의할 점

- **설정값은 `config.py`에 모아둔다.** 새 하드코딩 상수를 추가하지 말고 config에 추가 후 import.
- **자동 생성 파일을 수정하지 않는다.** `*_pb2.py`, `packet_ids.py`는 `.gitignore` 대상이며,
  수정하면 다음 `generate_proto.bat` 실행에서 무조건 덮어써집니다.
- **서버가 authoritative.** 클라이언트는 예측/보간만 하며, 충돌/히트 판정은 서버 결과를 신뢰합니다.
  클라이언트 단독으로 게임 규칙을 추가하지 마세요.
- **렌더 좌표계**: 월드는 `(x, y, z)`이며 top-down으로 `(x, z)` 평면만 화면에 투영합니다.
  `y`는 표시에 사용되지 않습니다 (높이는 현재 무시).
- **패킷 핸들링 추가 시**: `client.py`의 `elif pkt_id == packet_ids.S_XXX` 체인에 분기 추가.
  핸들러가 많아져 가독성이 떨어지면 별도 함수로 추출하는 것도 고려.

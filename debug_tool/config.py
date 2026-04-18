"""
Debug tool 전역 설정값.

- 네트워크 주소, 입력 타이밍, 보간 속도 등 런타임에 조절하고 싶은 값은
  전부 여기서 정의하고 다른 모듈은 이 값을 import 한다.
- 색상 팔레트처럼 renderer 내부에서만 의미 있는 값은 renderer.py에 남겨둔다.
"""

# ── Network ─────────────────────────────────────────────────────────
LOGIN_HOST = "127.0.0.1"
LOGIN_PORT = 9999
CONNECT_TIMEOUT = 5.0       # seconds
LOGIN_RESPONSE_TIMEOUT = 5.0  # seconds to wait for S_Login after sending C_Login

# ── Gameplay / input ────────────────────────────────────────────────
MOVE_SPEED = 5.0            # local prediction speed (world units / sec)
SEND_INTERVAL = 0.05        # C_PlayerMove send cadence (20 Hz)
SKILL_THROTTLE = 0.25       # client-side skill key rate limit (sec)
TARGET_FPS = 60             # used to derive dt_frame for interpolation

# ── Skills ──────────────────────────────────────────────────────────
# sid, cooldown, targeting 등 스킬 메타는 ShareDir/data/skill_templates.csv 가 SSOT.
# 여기서는 (1) 키 → 스킬 sid 매핑 + (2) debug_tool 전용 입력 수집 방식 override 만 선언.
# 키도 값도 PK(sid) 기준 — name 리네이밍에 휘둘리지 않는다. 옆 주석으로 사람 읽기용 이름 표기.

# QWER 키 → CSV 의 skill sid.
SKILL_BINDINGS = {
    "q": 2002,   # bolt        — Skillshot 기본 (cursor 방향)
    "w": 2003,   # strike      — Homing 기본을 point_click 으로 덮어씀
    "e": 2001,   # auto_attack — Homing 기본 (auto_homing)
    "r": 2004,   # nuke        — Skillshot 기본을 ground_target 으로 덮어씀
}

# CSV 의 `targeting` 만으로는 Point-click vs Homing(auto), Ground-target vs Skillshot(dir) 을
# 구분할 수 없으므로, 특수 입력 방식이 필요한 스킬은 여기에 sid 로 선언한다.
# 값: "skillshot_dir" | "point_click" | "auto_homing" | "ground_target"
SKILL_INPUT_MODE_OVERRIDES = {
    2003: "point_click",     # strike — 커서 아래 적을 target_guid 로 명시
    2004: "ground_target",   # nuke   — 커서 월드좌표를 target_pos 로 송신
}

# ── Click-to-move ───────────────────────────────────────────────────
CLICK_MARKER_LIFETIME = 0.4   # 우클릭 피드백 원의 화면 표시 시간 (초)
POSITION_CORRECTION_EPSILON = 0.3  # 서버 교정값이 로컬 예측과 이 거리 이상 차이나면 스냅

# ── Interpolation ───────────────────────────────────────────────────
PLAYER_LERP_SPEED = 15.0    # other-player display position smoothing
MONSTER_LERP_SPEED = 12.0   # monster display position smoothing

# ── Visual effects ──────────────────────────────────────────────────
HITSCAN_LINE_LIFETIME = 0.4       # seconds a hitscan tracer stays on screen
PROJECTILE_GRACE_LIFETIME = 0.5   # extra seconds past server max_lifetime before
                                  # client-side cleanup (server is authoritative)

# ── Rendering (window / layout) ─────────────────────────────────────
WINDOW_WIDTH = 900
WINDOW_HEIGHT = 700
PIXELS_PER_UNIT = 20        # world unit -> pixel scale
GRID_STEP_UNITS = 5         # grid line spacing in world units
CHAR_SCALE = 1.0            # global multiplier for character sprite sizes

# ── Logging ─────────────────────────────────────────────────────────
LOG_LEVEL = "INFO"          # DEBUG / INFO / WARNING / ERROR
LOG_FILE = None             # set to e.g. "debug_tool.log" to also write logs to a file

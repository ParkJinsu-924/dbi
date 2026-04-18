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

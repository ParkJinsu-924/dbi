"""config.py 값의 타입과 합리적 범위 검증."""

import config


def test_network_values():
    assert isinstance(config.LOGIN_HOST, str) and config.LOGIN_HOST
    assert isinstance(config.LOGIN_PORT, int)
    assert 0 < config.LOGIN_PORT < 65536
    assert config.CONNECT_TIMEOUT > 0
    assert config.LOGIN_RESPONSE_TIMEOUT > 0


def test_gameplay_values():
    assert config.MOVE_SPEED > 0
    assert config.SEND_INTERVAL > 0
    assert config.SKILL_THROTTLE >= 0
    assert config.TARGET_FPS > 0
    # 프레임 레이트가 1000을 넘으면 1/FPS 연산에서 비정상
    assert config.TARGET_FPS <= 1000


def test_interpolation_values():
    # lerp 계수가 0 이하면 보간이 멈춰 화면에 아무것도 안 움직인다
    assert config.PLAYER_LERP_SPEED > 0
    assert config.MONSTER_LERP_SPEED > 0


def test_visual_effect_values():
    assert config.HITSCAN_LINE_LIFETIME > 0
    assert config.PROJECTILE_GRACE_LIFETIME >= 0


def test_rendering_values():
    assert config.WINDOW_WIDTH > 0
    assert config.WINDOW_HEIGHT > 0
    assert config.PIXELS_PER_UNIT > 0
    assert config.GRID_STEP_UNITS > 0
    assert config.CHAR_SCALE > 0


def test_log_level_valid():
    assert config.LOG_LEVEL in {"DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"}
    # LOG_FILE은 None이거나 str이어야 한다
    assert config.LOG_FILE is None or isinstance(config.LOG_FILE, str)

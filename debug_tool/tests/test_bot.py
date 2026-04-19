"""
bot.py 단위 테스트. 네트워크/서버 의존 없이 검증 가능한 것만:
  - Bot 인스턴스화 + 필드 기본값
  - _pick_destination() 가 MAP_* 범위 내
  - _build_skill_request() 가 유효한 C_UseSkill 빌드
  - PLAYER_SKILL_IDS 가 실제 SKILL_TABLE 과 일치
"""

import math

import pytest

import bot
import game_pb2


def test_bot_default_state():
    b = bot.Bot(username="t_1")
    assert b.username == "t_1"
    assert b.password == "bot"
    assert b.in_game is False
    assert b.guid == 0
    assert b.player_id == 0
    assert b.client is None
    assert not b.stop_event.is_set()


def test_pick_destination_in_map_bounds():
    b = bot.Bot(username="t_2")
    for _ in range(100):
        x, z = b._pick_destination()
        assert bot.MAP_MIN_X <= x <= bot.MAP_MAX_X
        assert bot.MAP_MIN_Z <= z <= bot.MAP_MAX_Z


def test_build_skill_request_has_unit_direction():
    """Vector2.y 는 월드 z 축. dir 는 단위 벡터여야 하고 target_pos 는 봇 위치 기준 8m."""
    b = bot.Bot(username="t_3")
    b.x = 1.0
    b.z = 2.0
    req = b._build_skill_request(2002)
    assert isinstance(req, game_pb2.C_UseSkill)
    assert req.skill_id == 2002
    length = math.sqrt(req.dir.x ** 2 + req.dir.y ** 2)
    assert length == pytest.approx(1.0, abs=1e-6)
    dx = req.target_pos.x - b.x
    dz = req.target_pos.y - b.z
    assert math.sqrt(dx * dx + dz * dz) == pytest.approx(8.0, rel=1e-4)
    assert req.target_guid == 0


def test_player_skill_ids_all_in_skill_table():
    """bot 이 서버에 존재하지 않는 sid 를 보내지 않도록 CSV 와 일치해야 한다."""
    for sid in bot.PLAYER_SKILL_IDS:
        assert sid in bot.SKILL_TABLE, f"sid {sid} missing from SKILL_TABLE"


def test_stop_event_stops_run(monkeypatch):
    """stop_event 가 set 되면 run() 이 login 단계에서 빠져나와야 한다."""
    b = bot.Bot(username="t_stop")

    # _login 이 바로 None 을 반환하도록 해서 run() 이 login 분기에서 종료.
    monkeypatch.setattr(b, "_login", lambda: None)
    b.stop_event.set()
    b.run()
    # 예외 없이 반환하고 in_game 상태로 진입하지 않아야 함.
    assert b.in_game is False
    assert b.client is None

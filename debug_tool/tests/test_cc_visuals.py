"""
CC(행동 제약) 시각화 파이프라인 단위 테스트.

두 층을 따로 검증한다:
  1. cc_visuals 레지스트리 — 기대한 CC 타입이 등록돼있고, 미지 플래그는
     예외 없이 noop 이어야 한다.
  2. client._apply_cc_to_target / _remove_cc_from_target — effects.csv 의
     cc_flag 가 있는 eid 는 target 의 active_ccs 에 기록되고, 없는 eid 는
     기록되지 않아야 한다. 만료 정리(메인 루프 로직)는 wall-clock 기반.

렌더러 자체의 픽셀 출력은 smoke test 수준으로만 — 예외 없이 호출되면 OK.
"""

import os
import time

import pytest

import cc_visuals
import client as client_mod
import effect_data


# ── 레지스트리 완전성 ────────────────────────────────────────────────
def test_registry_covers_known_cc_flags():
    expected = {"Stun", "Root", "Slow", "Silence", "Invulnerable"}
    assert expected.issubset(set(cc_visuals.CC_VISUALIZERS.keys()))


def test_draw_unknown_flag_is_noop():
    """새 CC 가 CSV 에 추가됐지만 아직 비주얼이 등록되지 않았을 때 크래시 대신 무시."""
    class _FakeScreen:
        def blit(self, *a, **kw):
            raise AssertionError("screen should not be touched for unknown flag")
    cc_visuals.draw(_FakeScreen(), 10, 10, "UnknownFlag", 0.0)


def test_draw_none_flag_is_noop():
    cc_visuals.draw(None, 0, 0, "None", 0.0)
    cc_visuals.draw(None, 0, 0, "", 0.0)


# ── client 측 CC 라우팅 ──────────────────────────────────────────────
def _find_cc_eid() -> tuple[int, str]:
    """effects.csv 에서 cc_flag 가 'None' 이 아닌 첫 eid 를 찾아 반환.
    프로덕션 데이터를 건드리지 않고 실제 경로로 검증하기 위함."""
    table = effect_data.load_from_csv()
    for eid, eff in table._by_eid.items():
        if eff.cc_flag and eff.cc_flag != "None":
            return eid, eff.cc_flag
    pytest.skip("no CC effect in effects.csv to test against")


def test_apply_cc_routes_to_monster_active_ccs():
    eid, flag = _find_cc_eid()
    # EFFECT_TABLE 은 client 모듈이 로드 시점에 캐시한다. 재로드해서 테스트가
    # CSV 실제 내용을 사용하는지 확실히.
    client_mod.EFFECT_TABLE = effect_data.load_from_csv()

    state = client_mod.GameState(me=client_mod.PlayerState(guid=123))
    state.monsters[999] = client_mod.MonsterState(guid=999, name="Goblin")
    client_mod._apply_cc_to_target(state, target_guid=999, eid=eid, duration=3.0)
    assert flag in state.monsters[999].active_ccs
    expire, applied = state.monsters[999].active_ccs[flag]
    assert expire > applied
    assert expire - applied == pytest.approx(3.0, rel=1e-2)


def test_apply_cc_routes_to_me_when_target_matches_my_guid():
    eid, flag = _find_cc_eid()
    client_mod.EFFECT_TABLE = effect_data.load_from_csv()

    state = client_mod.GameState(me=client_mod.PlayerState(guid=42))
    client_mod._apply_cc_to_target(state, target_guid=42, eid=eid, duration=1.0)
    assert flag in state.me.active_ccs


def test_apply_cc_ignores_effect_without_cc_flag():
    """cc_flag 가 None 인 순수 데미지/힐 이펙트는 active_ccs 에 들어가지 않아야 한다."""
    table = effect_data.load_from_csv()
    non_cc_eid = None
    for eid, eff in table._by_eid.items():
        if not eff.cc_flag or eff.cc_flag == "None":
            non_cc_eid = eid
            break
    if non_cc_eid is None:
        pytest.skip("no non-CC effect in effects.csv")
    client_mod.EFFECT_TABLE = table

    state = client_mod.GameState(me=client_mod.PlayerState(guid=1))
    state.monsters[5] = client_mod.MonsterState(guid=5, name="M")
    client_mod._apply_cc_to_target(state, target_guid=5, eid=non_cc_eid, duration=1.0)
    assert state.monsters[5].active_ccs == {}


def test_remove_cc_clears_flag():
    eid, flag = _find_cc_eid()
    client_mod.EFFECT_TABLE = effect_data.load_from_csv()
    state = client_mod.GameState(me=client_mod.PlayerState(guid=1))
    state.monsters[7] = client_mod.MonsterState(guid=7, name="M")
    client_mod._apply_cc_to_target(state, 7, eid, 5.0)
    assert flag in state.monsters[7].active_ccs
    client_mod._remove_cc_from_target(state, 7, eid)
    assert flag not in state.monsters[7].active_ccs


def test_apply_cc_unknown_target_is_noop():
    """target_guid 가 me/monster/other 어디에도 없으면 조용히 무시."""
    eid, _flag = _find_cc_eid()
    client_mod.EFFECT_TABLE = effect_data.load_from_csv()
    state = client_mod.GameState(me=client_mod.PlayerState(guid=1))
    # GUID 99999 는 등록 안 된 상태.
    client_mod._apply_cc_to_target(state, 99999, eid, 1.0)
    assert state.me.active_ccs == {}
    assert state.monsters == {}

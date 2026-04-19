"""
feedback.py 단위 테스트. pygame 창/mixer 실제 초기화를 피하기 위해
AudioManager 는 enable_audio=False 로 무력화하거나 monkeypatch 로 init 실패를 주입한다.
"""

import time

import pytest

from feedback import (
    AudioManager,
    CameraShake,
    DamagePopup,
    FeedbackSystem,
    Particle,
)


# ── CameraShake ──────────────────────────────────────────────────────
def test_camera_shake_bump_then_decays_to_zero():
    shake = CameraShake(decay_rate=20.0, max_intensity=15.0)
    shake.bump(10.0)
    assert shake.intensity == 10.0
    shake.tick(0.1)
    assert shake.intensity == pytest.approx(10.0 - 2.0, rel=1e-3)
    shake.tick(10.0)
    assert shake.intensity == 0.0


def test_camera_shake_capped_at_max():
    shake = CameraShake(max_intensity=5.0)
    shake.bump(100.0)
    assert shake.intensity == 5.0


def test_camera_shake_bump_takes_max_not_sum():
    shake = CameraShake()
    shake.bump(3.0)
    shake.bump(1.0)
    assert shake.intensity == 3.0


def test_camera_shake_offset_zero_when_idle():
    shake = CameraShake()
    assert shake.offset() == (0, 0)


def test_camera_shake_offset_in_range_when_active():
    shake = CameraShake()
    shake.bump(10.0)
    ox, oy = shake.offset()
    assert -10 <= ox <= 10
    assert -10 <= oy <= 10


# ── Particle ─────────────────────────────────────────────────────────
def test_particle_tick_moves_position():
    now = time.time()
    p = Particle(wx=0.0, wz=0.0, vx=2.0, vz=0.0, size=3.0,
                 color=(255, 0, 0), born=now, lifetime=1.0, drag=0.0)
    p.tick(0.5)
    assert p.wx == pytest.approx(1.0)
    assert p.wz == pytest.approx(0.0)


def test_particle_drag_reduces_velocity():
    now = time.time()
    p = Particle(wx=0.0, wz=0.0, vx=10.0, vz=0.0, size=3.0,
                 color=(255, 0, 0), born=now, lifetime=1.0, drag=4.0)
    p.tick(0.1)
    assert 0.0 < p.vx < 10.0


def test_particle_alive_flag_flips_after_lifetime():
    now = time.time()
    p = Particle(wx=0, wz=0, vx=0, vz=0, size=1,
                 color=(0, 0, 0), born=now - 2.0, lifetime=1.0)
    assert not p.alive


# ── DamagePopup ──────────────────────────────────────────────────────
def test_damage_popup_alive_until_lifetime():
    now = time.time()
    d = DamagePopup(wx=0, wz=0, text="5", color=(255, 0, 0),
                    born=now, lifetime=1.0, scale=1.0)
    assert d.alive


def test_damage_popup_dead_after_lifetime():
    now = time.time()
    d = DamagePopup(wx=0, wz=0, text="5", color=(255, 0, 0),
                    born=now - 2.0, lifetime=1.0, scale=1.0)
    assert not d.alive


# ── FeedbackSystem ───────────────────────────────────────────────────
def _make_fb() -> FeedbackSystem:
    """Audio 를 완전히 우회한 인스턴스 (mixer/pygame 의존성 제거)."""
    return FeedbackSystem(enable_audio=False)


def test_on_hit_spawns_popup_and_particles():
    fb = _make_fb()
    fb.on_hit(1.0, 2.0, damage=15, target_is_me=False)
    assert len(fb.damage_popups) == 1
    assert fb.damage_popups[0].text == "15"
    assert len(fb.particles) > 0


def test_on_hit_zero_damage_is_noop():
    fb = _make_fb()
    fb.on_hit(0, 0, damage=0, target_is_me=False)
    assert fb.damage_popups == []
    assert fb.particles == []
    assert fb.shake.intensity == 0.0


def test_on_hit_self_triggers_shake():
    fb = _make_fb()
    fb.on_hit(0, 0, damage=10, target_is_me=True)
    assert fb.shake.intensity > 0.0


def test_on_hit_other_does_not_shake():
    """다른 유닛 피격은 화면을 흔들지 않아야 한다 (시각 노이즈 방지)."""
    fb = _make_fb()
    fb.on_hit(0, 0, damage=10, target_is_me=False)
    assert fb.shake.intensity == 0.0


def test_on_hit_heavy_damage_uses_big_scale():
    fb = _make_fb()
    fb.on_hit(0, 0, damage=50, target_is_me=False)
    assert fb.damage_popups[0].scale >= 1.4


def test_on_skill_cast_spawns_ring_particles():
    fb = _make_fb()
    fb.on_skill_cast(0, 0, caster_is_me=True)
    assert len(fb.particles) > 0


def test_tick_removes_expired_items():
    """DamagePopup/Particle 의 alive 는 wall-clock 기준이므로
    born 을 과거로 당겨 만료 상태를 강제한 뒤 tick 이 실제로 청소하는지 검증한다."""
    fb = _make_fb()
    fb.on_hit(0, 0, damage=10, target_is_me=True)
    assert len(fb.particles) > 0
    # 모든 popup/particle 의 born 을 수명보다 먼 과거로 이동.
    for p in fb.particles:
        p.born -= 10.0
    for d in fb.damage_popups:
        d.born -= 10.0
    fb.tick(10.0)   # shake 는 dt 기반이라 동시에 0 까지 decay
    assert fb.damage_popups == []
    assert fb.particles == []
    assert fb.shake.intensity == 0.0


# ── AudioManager silent fallback ─────────────────────────────────────
def test_audio_manager_silent_when_mixer_fails(monkeypatch):
    """mixer.init 이 실패해도 AudioManager 는 예외 없이 noop 이 돼야 한다."""
    import pygame

    def _raise(*a, **kw):
        raise pygame.error("simulated mixer failure")

    monkeypatch.setattr(pygame.mixer, "get_init", lambda: None)
    monkeypatch.setattr(pygame.mixer, "init", _raise)

    am = AudioManager(master_volume=0.3)
    assert am._ok is False
    # 존재하든 존재하지 않든 play 호출이 예외로 번지지 않아야 한다.
    am.play("hit")
    am.play("nonexistent_event")


def test_feedback_system_no_audio_path_works():
    """enable_audio=False 시 mixer 에 전혀 접근하지 않고도 동작해야 한다."""
    fb = FeedbackSystem(enable_audio=False)
    assert fb.audio is None
    fb.on_hit(0, 0, damage=10, target_is_me=True)
    fb.on_skill_cast(0, 0, caster_is_me=False)
    fb.on_monster_spawn(0, 0)
    fb.tick(0.1)   # 예외 없이 tick 가능해야 함

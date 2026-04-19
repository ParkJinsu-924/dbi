"""
Visual/audio feedback layer for debug_tool — damage popups, particles,
camera shake, and procedurally-generated impact sounds.

Design notes:
  - World-space particles/popups so camera (me) movement keeps them attached
    to the world, not the screen.
  - Audio uses pygame.mixer with *procedurally synthesized* sounds. No asset
    files required; users can later drop .wav files into assets/sounds/ and
    AudioManager will prefer them.
  - All systems degrade silently: if pygame.mixer fails to init, audio is
    a no-op. If any individual synth fails, the fallback is silence, never
    a crash.
  - FeedbackSystem is the single facade client.py interacts with.
"""

import array
import math
import os
import random
import time
from dataclasses import dataclass


SAMPLE_RATE = 22050
_SOUNDS_DIR = os.path.join(os.path.dirname(__file__), "assets", "sounds")

# Special skill sids — 일반 평타 캐스트와 시각/청각적으로 구분해 강조한다.
# 새 스페셜 스킬이 추가되면 여기에 sid 만 등록하면 된다 (skill_templates.csv 의 PK).
SPECIAL_SKILL_IDS: set[int] = {
    1101,   # goblin_bomb — Goblin 의 간헐 폭탄 투척
}


# ── Data types ──────────────────────────────────────────────────────
@dataclass
class DamagePopup:
    """Floating damage number that rises and fades over its lifetime."""
    wx: float
    wz: float
    text: str
    color: tuple
    born: float
    lifetime: float
    scale: float          # 1.0 = normal, >1 = emphasized (crits, me as target)

    @property
    def age(self) -> float:
        return time.time() - self.born

    @property
    def alive(self) -> bool:
        return self.age < self.lifetime


@dataclass
class Particle:
    """Short-lived world-space particle (spark, dust, ring segment)."""
    wx: float
    wz: float
    vx: float        # world units / sec in x
    vz: float        # world units / sec in z
    size: float      # initial radius in pixels
    color: tuple
    born: float
    lifetime: float
    drag: float = 4.0   # velocity damping rate

    def tick(self, dt: float) -> None:
        self.wx += self.vx * dt
        self.wz += self.vz * dt
        damp = math.exp(-self.drag * dt)
        self.vx *= damp
        self.vz *= damp

    @property
    def age(self) -> float:
        return time.time() - self.born

    @property
    def alive(self) -> bool:
        return self.age < self.lifetime


# ── Camera shake ────────────────────────────────────────────────────
class CameraShake:
    """Decaying per-frame random offset. Call bump() on impactful events."""

    def __init__(self, decay_rate: float = 12.0, max_intensity: float = 18.0):
        self.intensity = 0.0
        self._decay_rate = decay_rate
        self._cap = max_intensity

    def bump(self, intensity: float) -> None:
        self.intensity = min(self._cap, max(self.intensity, intensity))

    def tick(self, dt: float) -> None:
        self.intensity = max(0.0, self.intensity - self._decay_rate * dt)

    def offset(self) -> tuple[int, int]:
        if self.intensity <= 0.25:
            return (0, 0)
        return (
            int(random.uniform(-self.intensity, self.intensity)),
            int(random.uniform(-self.intensity, self.intensity)),
        )


# ── Audio ───────────────────────────────────────────────────────────
class AudioManager:
    """Thin pygame.mixer wrapper with procedural tone synthesis.

    Uses mono 16-bit signed audio. If mixer.init fails (no audio device,
    driver issue, headless test host, etc.), all play() calls become no-ops.
    """

    def __init__(self, master_volume: float = 0.3):
        self._ok = False
        self._master = max(0.0, min(1.0, master_volume))
        self._sounds: dict = {}
        self._init_mixer()
        if self._ok:
            self._load_or_synth()

    def _init_mixer(self) -> None:
        try:
            import pygame
            # pre_init is no-op if mixer was already initialized elsewhere.
            if not pygame.mixer.get_init():
                pygame.mixer.init(frequency=SAMPLE_RATE, size=-16, channels=1)
            self._ok = True
        except Exception:
            self._ok = False

    def _load_or_synth(self) -> None:
        # Event names must match what FeedbackSystem.play_sound dispatches.
        events = {
            "hit":           lambda: self._synth_noise(140, 0.08, amp=0.40),
            "crit":          lambda: self._synth_sweep(900, 320, 0.18, amp=0.50),
            "skill_cast":    lambda: self._synth_sweep(220, 720, 0.20, amp=0.28),
            # 스페셜 스킬: 더 낮은 시작음 + 긴 sweep + noise burst → 무거운 "쿵" 느낌.
            "special_cast":  lambda: self._synth_sweep(80, 380, 0.45, amp=0.55),
            "spawn":         lambda: self._synth_tone(160, 0.18, amp=0.25),
            "me_hit":        lambda: self._synth_noise(90, 0.14, amp=0.55),
        }
        for name, synth in events.items():
            wav_path = os.path.join(_SOUNDS_DIR, f"{name}.wav")
            snd = None
            if os.path.isfile(wav_path):
                try:
                    import pygame
                    snd = pygame.mixer.Sound(wav_path)
                except Exception:
                    snd = None
            if snd is None:
                try:
                    snd = synth()
                except Exception:
                    snd = None
            if snd is not None:
                self._sounds[name] = snd

    # ── synth primitives ────────────────────────────────────────────
    def _make_sound(self, samples: array.array):
        import pygame
        return pygame.mixer.Sound(buffer=samples.tobytes())

    def _synth_tone(self, freq: float, duration_s: float, amp: float = 0.3):
        n = int(SAMPLE_RATE * duration_s)
        buf = array.array('h')
        for i in range(n):
            t = i / SAMPLE_RATE
            env = max(0.0, 1.0 - i / n)
            s = int(amp * env * 32767 * math.sin(2 * math.pi * freq * t))
            buf.append(max(-32767, min(32767, s)))
        return self._make_sound(buf)

    def _synth_sweep(self, f_start: float, f_end: float,
                     duration_s: float, amp: float = 0.3):
        n = int(SAMPLE_RATE * duration_s)
        buf = array.array('h')
        phase = 0.0
        for i in range(n):
            t = i / n
            freq = f_start + (f_end - f_start) * t
            env = max(0.0, 1.0 - t)
            phase += 2 * math.pi * freq / SAMPLE_RATE
            s = int(amp * env * 32767 * math.sin(phase))
            buf.append(max(-32767, min(32767, s)))
        return self._make_sound(buf)

    def _synth_noise(self, freq: float, duration_s: float, amp: float = 0.3):
        n = int(SAMPLE_RATE * duration_s)
        buf = array.array('h')
        for i in range(n):
            t = i / SAMPLE_RATE
            env = max(0.0, (1.0 - i / n)) ** 2
            base = math.sin(2 * math.pi * freq * t)
            noise = random.uniform(-0.8, 0.8)
            s = int(amp * env * 32767 * (base * 0.35 + noise * 0.65))
            buf.append(max(-32767, min(32767, s)))
        return self._make_sound(buf)

    # ── playback ────────────────────────────────────────────────────
    def play(self, name: str, volume: float = 1.0) -> None:
        if not self._ok:
            return
        snd = self._sounds.get(name)
        if snd is None:
            return
        try:
            ch = snd.play()
            if ch is not None:
                ch.set_volume(self._master * max(0.0, min(1.0, volume)))
        except Exception:
            pass


# ── Facade ──────────────────────────────────────────────────────────
class FeedbackSystem:
    """Single entry point for client.py. Owns particles, popups, shake, audio."""

    def __init__(self, enable_audio: bool = True, audio_volume: float = 0.3):
        self.damage_popups: list[DamagePopup] = []
        self.particles: list[Particle] = []
        self.shake = CameraShake()
        self.audio = AudioManager(master_volume=audio_volume) if enable_audio else None

    # ── per-frame tick ──────────────────────────────────────────────
    def tick(self, dt: float) -> None:
        for p in self.particles:
            p.tick(dt)
        self.particles = [p for p in self.particles if p.alive]
        self.damage_popups = [d for d in self.damage_popups if d.alive]
        self.shake.tick(dt)

    # ── event helpers called from packet handlers ───────────────────
    def on_hit(self, wx: float, wz: float, damage: int,
               target_is_me: bool) -> None:
        # Damage popup — bright yellow for me (so player notices own damage),
        # red for others; large scale for heavy hits.
        if damage <= 0:
            return
        scale = 1.0
        color = (255, 90, 80)
        if target_is_me:
            color = (255, 220, 80)
            scale = 1.35
        if damage >= 30:
            color = (255, 140, 40)
            scale = max(scale, 1.5)
        self.damage_popups.append(DamagePopup(
            wx=wx, wz=wz, text=str(int(damage)),
            color=color, born=time.time(),
            lifetime=1.0, scale=scale))

        # Impact particles — sparks radiating outward.
        self._spawn_burst(wx, wz, color=(255, 210, 120),
                          count=14, speed=(2.5, 6.5),
                          size=(2.0, 4.0), lifetime=(0.25, 0.55))

        # Shake: only when I'm hit (avoid visual noise from distant monster fights).
        if target_is_me:
            self.shake.bump(10.0 + min(8.0, damage * 0.2))
            self._play("me_hit")
        else:
            # Softer feedback for others' hits — crit sound if big.
            self._play("crit" if damage >= 30 else "hit", volume=0.6)

    def on_skill_cast(self, wx: float, wz: float, caster_is_me: bool,
                      skill_id: int = 0) -> None:
        is_special = skill_id in SPECIAL_SKILL_IDS
        if is_special:
            # 시전 시점부터 "특수기" 임을 인지할 수 있게: 큰 적색 이중 링 + 강한 흔들림 + 무거운 사운드.
            self._spawn_ring(wx, wz, color=(255, 90, 60),
                             count=24, speed=6.5, lifetime=0.7)
            self._spawn_ring(wx, wz, color=(255, 200, 90),
                             count=18, speed=3.2, lifetime=0.55)
            self._spawn_burst(wx, wz, color=(255, 140, 60),
                              count=20, speed=(1.5, 4.5),
                              size=(2.5, 4.5), lifetime=(0.4, 0.8))
            self.shake.bump(8.0 if caster_is_me else 5.0)
            self._play("special_cast", volume=1.0 if caster_is_me else 0.75)
        else:
            self._spawn_ring(wx, wz, color=(130, 190, 255),
                             count=16, speed=3.8, lifetime=0.45)
            if caster_is_me:
                self.shake.bump(3.5)
            self._play("skill_cast", volume=0.8 if caster_is_me else 0.5)

    def on_monster_spawn(self, wx: float, wz: float) -> None:
        self._spawn_burst(wx, wz, color=(180, 120, 220),
                          count=10, speed=(1.2, 3.0),
                          size=(2.0, 3.5), lifetime=(0.4, 0.7))
        self._play("spawn", volume=0.4)

    # ── internal spawn primitives ───────────────────────────────────
    def _spawn_burst(self, wx: float, wz: float, color: tuple,
                     count: int, speed: tuple, size: tuple,
                     lifetime: tuple) -> None:
        now = time.time()
        for _ in range(count):
            a = random.uniform(0, 2 * math.pi)
            s = random.uniform(*speed)
            self.particles.append(Particle(
                wx=wx, wz=wz,
                vx=math.cos(a) * s, vz=math.sin(a) * s,
                size=random.uniform(*size),
                color=color, born=now,
                lifetime=random.uniform(*lifetime)))

    def _spawn_ring(self, wx: float, wz: float, color: tuple,
                    count: int, speed: float, lifetime: float) -> None:
        now = time.time()
        for i in range(count):
            a = i * (2 * math.pi / count)
            self.particles.append(Particle(
                wx=wx, wz=wz,
                vx=math.cos(a) * speed, vz=math.sin(a) * speed,
                size=3.0, color=color,
                born=now, lifetime=lifetime,
                drag=2.0))

    def _play(self, name: str, volume: float = 1.0) -> None:
        if self.audio is not None:
            self.audio.play(name, volume)

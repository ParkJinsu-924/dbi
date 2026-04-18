"""
2D top-down renderer for the debug tool.

Draws procedural sprites:
  - Local player: armored knight with breathing animation
  - Other players: blue warriors with idle sway
  - Monsters: species-specific shapes (Goblin, Orc, Slime, ...) with state animations

Coordinate transform: world (x, z) on GameServer maps to screen (x, y) with
the local player centered. A fixed pixels-per-unit scale keeps distances readable.

─────────────────────────────────────────────────────────────────────
Table of Contents (rough line numbers — grep the marker comments):
─────────────────────────────────────────────────────────────────────
  # ── Palette ──          player/monster color constants (BG, ME_*,
                           OTHER_*, GOBLIN_*, ORC_*, SLIME_*, ...)
  # ── Layout constants ── PIXELS_PER_UNIT, GRID_STEP_UNITS, CHAR_SCALE
                           (values come from config.py)
  # ── State names ──      monster AI state id -> name/color

  class Renderer:
    world_to_screen()      world (x,z) -> screen (x,y)
    draw_grid()
    _draw_shadow(), _draw_hp_bar(), _label()

    # ─── PLAYER DRAWING ───────
    _draw_player()         used for both local player and others

    # ─── MONSTER DRAWING (one method per species) ───────
    _draw_goblin(), _draw_orc(), _draw_slime(), _draw_skeleton(),
    _draw_sniper(), _draw_homing_archer(), _draw_bolt_caster(),
    _draw_mushroom(), _draw_wolf(), _draw_drake(),
    _draw_generic_monster()    # fallback
    _draw_monster()            # dispatch by monster.name substring
    _draw_state_indicator()    # small dot above monster showing AI state

    draw_frame()           top-level per-frame draw (called from client.py)
    _draw_status()         HUD text overlay
    close()

Adding a new monster species:
    1. Add its color constants under "# Monsters" in the palette block.
    2. Add a new _draw_xxx(self, sx, sy, state, anim_id) method.
    3. Register it in _draw_monster() name dispatch.
"""

import math
import time

import pygame

import config

# ── Palette ──────────────────────────────────────────────────────────
BG_COLOR = (22, 22, 28)
GRID_COLOR = (42, 42, 52)
TEXT_COLOR = (210, 210, 210)
SHADOW_COLOR = (10, 10, 15, 100)

# Players
ME_BODY = (200, 55, 55)
ME_ARMOR = (160, 40, 40)
ME_SKIN = (240, 200, 170)
ME_SHIELD = (180, 140, 60)

OTHER_BODY = (50, 100, 200)
OTHER_ARMOR = (40, 80, 170)
OTHER_SKIN = (220, 190, 160)

# Monsters
GOBLIN_BODY = (80, 160, 60)
GOBLIN_SKIN = (120, 180, 80)
GOBLIN_EYE = (255, 50, 30)

ORC_BODY = (100, 70, 50)
ORC_SKIN = (80, 140, 60)
ORC_EYE = (255, 200, 30)

SLIME_BODY = (60, 180, 220)
SLIME_HIGHLIGHT = (120, 220, 255)
SLIME_EYE = (20, 20, 40)

GENERIC_MONSTER_BODY = (140, 60, 180)
GENERIC_MONSTER_EYE = (255, 255, 100)

SKELETON_BONE = (220, 210, 190)
SKELETON_DARK = (60, 55, 45)
SKELETON_EYE = (255, 80, 50)

SNIPER_CLOAK = (50, 55, 50)
SNIPER_HOOD = (35, 40, 35)
SNIPER_SKIN = (200, 180, 160)
SNIPER_GUN = (120, 115, 110)

MUSHROOM_CAP = (200, 50, 50)
MUSHROOM_SPOT = (240, 230, 220)
MUSHROOM_STEM = (220, 200, 170)
MUSHROOM_EYE = (30, 30, 40)

WOLF_FUR = (120, 110, 100)
WOLF_BELLY = (180, 170, 155)
WOLF_EYE = (220, 200, 50)

DRAKE_BODY = (180, 50, 40)
DRAKE_WING = (200, 70, 50)
DRAKE_BELLY = (220, 180, 80)
DRAKE_EYE = (255, 220, 50)

ARCHER_CLOAK = (45, 80, 55)
ARCHER_TUNIC = (90, 140, 80)
ARCHER_SKIN = (220, 200, 170)
ARCHER_BOW = (120, 80, 40)
ARCHER_ARROW = (240, 200, 80)
ARCHER_ARROW_GLOW = (255, 240, 160)

CASTER_ROBE = (70, 45, 110)
CASTER_ROBE_DARK = (45, 25, 80)
CASTER_HOOD = (35, 20, 60)
CASTER_STAFF = (90, 70, 50)
CASTER_CRYSTAL = (120, 180, 255)
CASTER_BOLT = (180, 220, 255)
CASTER_EYE = (150, 220, 255)

# State indicator colors
STATE_IDLE_COLOR = (60, 200, 90)
STATE_CHASE_COLOR = (220, 180, 40)
STATE_ATTACK_COLOR = (255, 50, 50)
STATE_RETURN_COLOR = (120, 120, 140)

# HP bars
HP_BAR_BG = (60, 15, 15)
HP_BAR_FG = (220, 40, 40)
HP_BAR_FG_OTHER = (50, 140, 220)
MONSTER_HP_BAR_FG = (60, 200, 90)

# Detect range
DETECT_RANGE_COLOR = (60, 200, 90, 35)

# ── Layout constants ─────────────────────────────────────────────────
# 레이아웃/창 크기는 config에서 관리한다. 이 파일에서는 별칭만 두어 기존 코드와 호환.
PIXELS_PER_UNIT = config.PIXELS_PER_UNIT
GRID_STEP_UNITS = config.GRID_STEP_UNITS
CHAR_SCALE = config.CHAR_SCALE

# ── State names ──────────────────────────────────────────────────────
_STATE_NAMES = {0: "Idle", 1: "Patrol", 2: "Chase", 3: "ATK", 4: "Return"}
_STATE_COLORS = {
    0: STATE_IDLE_COLOR,
    1: STATE_IDLE_COLOR,
    2: STATE_CHASE_COLOR,
    3: STATE_ATTACK_COLOR,
    4: STATE_RETURN_COLOR,
}


def _lerp_color(c1, c2, t):
    """Linearly interpolate between two RGB colors."""
    t = max(0.0, min(1.0, t))
    return (
        int(c1[0] + (c2[0] - c1[0]) * t),
        int(c1[1] + (c2[1] - c1[1]) * t),
        int(c1[2] + (c2[2] - c1[2]) * t),
    )


def _pulse(speed=2.0, lo=0.85, hi=1.0):
    """Return a value oscillating between lo and hi."""
    t = (math.sin(time.time() * speed) + 1.0) * 0.5
    return lo + (hi - lo) * t


class Renderer:
    def __init__(self, width=config.WINDOW_WIDTH, height=config.WINDOW_HEIGHT,
                 title="MMO Debug Tool"):
        pygame.init()
        self.screen = pygame.display.set_mode((width, height))
        pygame.display.set_caption(title)
        self.font = pygame.font.SysFont("consolas", 13)
        self.font_small = pygame.font.SysFont("consolas", 11)
        self.font_name = pygame.font.SysFont("consolas", 12, bold=True)
        self.width = width
        self.height = height
        self.clock = pygame.time.Clock()
        self._t0 = time.time()

    @property
    def _t(self):
        return time.time() - self._t0

    # ── coordinate conversion ────────────────────────────────────────
    def world_to_screen(self, wx: float, wz: float, cx: float, cz: float):
        sx = self.width / 2 + (wx - cx) * PIXELS_PER_UNIT
        sy = self.height / 2 - (wz - cz) * PIXELS_PER_UNIT
        return int(sx), int(sy)

    # ── grid ─────────────────────────────────────────────────────────
    def draw_grid(self, cx: float, cz: float):
        step = GRID_STEP_UNITS * PIXELS_PER_UNIT
        offset_x = int(self.width / 2 - cx * PIXELS_PER_UNIT) % step
        offset_y = int(self.height / 2 + cz * PIXELS_PER_UNIT) % step
        for x in range(offset_x, self.width, step):
            pygame.draw.line(self.screen, GRID_COLOR, (x, 0), (x, self.height))
        for y in range(offset_y, self.height, step):
            pygame.draw.line(self.screen, GRID_COLOR, (0, y), (self.width, y))

    # ── shadow under entities ────────────────────────────────────────
    def _draw_shadow(self, sx, sy, rx=10, ry=4):
        shadow = pygame.Surface((rx * 2, ry * 2), pygame.SRCALPHA)
        pygame.draw.ellipse(shadow, SHADOW_COLOR, (0, 0, rx * 2, ry * 2))
        self.screen.blit(shadow, (sx - rx, sy - ry + 2))

    # ── HP bar ───────────────────────────────────────────────────────
    def _draw_hp_bar(self, sx, sy, hp, max_hp, bar_color, y_offset=-22, width=32):
        if max_hp <= 0:
            return
        bar_h = 4
        bar_x = sx - width // 2
        bar_y = sy + y_offset
        pygame.draw.rect(self.screen, HP_BAR_BG, (bar_x, bar_y, width, bar_h), border_radius=2)
        fill = int(width * max(0, hp) / max_hp)
        if fill > 0:
            pygame.draw.rect(self.screen, bar_color, (bar_x, bar_y, fill, bar_h), border_radius=2)

    # ── name label ───────────────────────────────────────────────────
    def _label(self, text: str, x: int, y: int, font=None, color=None):
        f = font or self.font
        c = color or TEXT_COLOR
        surf = f.render(text, True, c)
        rect = surf.get_rect(center=(x, y))
        self.screen.blit(surf, rect)

    # ═══════════════════════════════════════════════════════════════════
    #  PLAYER DRAWING
    # ═══════════════════════════════════════════════════════════════════

    def _draw_player(self, sx, sy, body_color, armor_color, skin_color,
                     is_local=False, name="", anim_offset=0.0):
        """Draw a humanoid character from basic shapes."""
        t = self._t + anim_offset
        s = CHAR_SCALE

        # Breathing animation
        breathe = math.sin(t * 2.5) * 1.2 * s

        # Shadow
        self._draw_shadow(sx, int(sy + 10 * s), int(9 * s), int(4 * s))

        # ── Legs (two small rectangles) ──
        leg_y = int(sy + 5 * s + breathe * 0.3)
        leg_w = int(3 * s)
        leg_h = int(6 * s)
        # Walk animation
        walk_offset = math.sin(t * 6) * 1.5 * s
        pygame.draw.rect(self.screen, armor_color,
                         (sx - int(4 * s), leg_y + int(walk_offset * 0.3), leg_w, leg_h),
                         border_radius=1)
        pygame.draw.rect(self.screen, armor_color,
                         (sx + int(1 * s), leg_y - int(walk_offset * 0.3), leg_w, leg_h),
                         border_radius=1)

        # ── Body / Torso (rounded rect) ──
        body_w = int(12 * s)
        body_h = int(10 * s)
        body_x = sx - body_w // 2
        body_y = int(sy - 3 * s + breathe * 0.5)
        pygame.draw.rect(self.screen, body_color,
                         (body_x, body_y, body_w, body_h),
                         border_radius=int(3 * s))

        # Belt
        belt_y = body_y + body_h - int(3 * s)
        pygame.draw.rect(self.screen, (80, 60, 30),
                         (body_x + 1, belt_y, body_w - 2, int(2 * s)))

        # ── Arms ──
        arm_w = int(3 * s)
        arm_h = int(7 * s)
        arm_y = int(body_y + 2 * s)
        arm_swing = math.sin(t * 3) * 1.0 * s
        # Left arm
        pygame.draw.rect(self.screen, skin_color,
                         (body_x - arm_w, int(arm_y + arm_swing), arm_w, arm_h),
                         border_radius=int(1 * s))
        # Right arm
        pygame.draw.rect(self.screen, skin_color,
                         (body_x + body_w, int(arm_y - arm_swing), arm_w, arm_h),
                         border_radius=int(1 * s))

        # ── Shield (local player only) ──
        if is_local:
            shield_x = body_x - arm_w - int(2 * s)
            shield_y = int(arm_y + arm_swing + 1 * s)
            pygame.draw.ellipse(self.screen, ME_SHIELD,
                                (shield_x, shield_y, int(5 * s), int(6 * s)))
            pygame.draw.ellipse(self.screen, (200, 170, 80),
                                (shield_x, shield_y, int(5 * s), int(6 * s)), 1)

        # ── Head (circle) ──
        head_r = int(5 * s)
        head_y = int(body_y - head_r + 1 * s + breathe * 0.3)
        pygame.draw.circle(self.screen, skin_color, (sx, head_y), head_r)

        # Hair/helmet
        if is_local:
            # Red helmet
            pygame.draw.arc(self.screen, (170, 40, 40),
                            (sx - head_r, head_y - head_r, head_r * 2, head_r * 2),
                            0.3, math.pi - 0.3, int(3 * s))
            # Helmet crest
            pygame.draw.line(self.screen, (200, 50, 50),
                             (sx, head_y - head_r), (sx, head_y - head_r - int(3 * s)), 2)
        else:
            # Blue hat
            pygame.draw.arc(self.screen, (40, 70, 160),
                            (sx - head_r, head_y - head_r, head_r * 2, head_r * 2),
                            0.3, math.pi - 0.3, int(3 * s))

        # Eyes (two dots)
        eye_y = head_y
        eye_sep = int(2.5 * s)
        pygame.draw.circle(self.screen, (30, 30, 40), (sx - eye_sep, eye_y), int(1.2 * s))
        pygame.draw.circle(self.screen, (30, 30, 40), (sx + eye_sep, eye_y), int(1.2 * s))

    # ═══════════════════════════════════════════════════════════════════
    #  MONSTER DRAWING
    # ═══════════════════════════════════════════════════════════════════

    def _draw_goblin(self, sx, sy, state, anim_id):
        """Small, hunched goblin with pointy ears."""
        t = self._t + anim_id * 1.7
        s = CHAR_SCALE
        state_pulse = 0.0

        if state == 3:  # Attack
            state_pulse = math.sin(t * 12) * 2.0
        elif state == 2:  # Chase
            state_pulse = math.sin(t * 6) * 1.5

        self._draw_shadow(sx, int(sy + 8 * s), int(7 * s), int(3 * s))

        # Body (hunched oval)
        body_w = int(10 * s)
        body_h = int(8 * s)
        body_x = sx - body_w // 2
        body_y = int(sy - 2 * s + math.sin(t * 3) * 0.8)
        pygame.draw.ellipse(self.screen, GOBLIN_BODY,
                            (body_x, body_y, body_w, body_h))

        # Belly highlight
        pygame.draw.ellipse(self.screen, GOBLIN_SKIN,
                            (body_x + int(2 * s), body_y + int(2 * s),
                             body_w - int(4 * s), body_h - int(3 * s)))

        # Head (slightly large for body)
        head_r = int(5 * s)
        head_y = int(body_y - head_r + 2 * s)
        pygame.draw.circle(self.screen, GOBLIN_SKIN, (sx, head_y), head_r)

        # Pointy ears
        ear_w = int(5 * s)
        ear_h = int(3 * s)
        # Left ear
        pygame.draw.polygon(self.screen, GOBLIN_SKIN, [
            (sx - head_r + 1, head_y - int(1 * s)),
            (sx - head_r - ear_w, head_y - ear_h - int(2 * s)),
            (sx - head_r + 2, head_y - int(3 * s)),
        ])
        # Right ear
        pygame.draw.polygon(self.screen, GOBLIN_SKIN, [
            (sx + head_r - 1, head_y - int(1 * s)),
            (sx + head_r + ear_w, head_y - ear_h - int(2 * s)),
            (sx + head_r - 2, head_y - int(3 * s)),
        ])

        # Eyes (angry slant)
        eye_y = head_y
        eye_sep = int(2.5 * s)
        er = int(1.5 * s)
        pygame.draw.circle(self.screen, GOBLIN_EYE, (sx - eye_sep, eye_y), er)
        pygame.draw.circle(self.screen, GOBLIN_EYE, (sx + eye_sep, eye_y), er)
        # Pupils
        pr = int(0.7 * s)
        pygame.draw.circle(self.screen, (20, 10, 5), (sx - eye_sep, eye_y), pr)
        pygame.draw.circle(self.screen, (20, 10, 5), (sx + eye_sep, eye_y), pr)

        # Mouth (toothy grin)
        mouth_y = head_y + int(2.5 * s)
        pygame.draw.line(self.screen, (40, 20, 10),
                         (sx - int(2 * s), mouth_y), (sx + int(2 * s), mouth_y), 1)

        # Legs
        leg_y = body_y + body_h - int(2 * s)
        walk = math.sin(t * 7) * 1.5 * s if state in (1, 2, 4) else 0
        pygame.draw.rect(self.screen, GOBLIN_BODY,
                         (sx - int(3 * s), int(leg_y + walk * 0.5), int(3 * s), int(5 * s)),
                         border_radius=1)
        pygame.draw.rect(self.screen, GOBLIN_BODY,
                         (sx + int(0 * s), int(leg_y - walk * 0.5), int(3 * s), int(5 * s)),
                         border_radius=1)

        # Weapon (small dagger) when chasing/attacking
        if state in (2, 3):
            dagger_x = sx + body_w // 2 + int(1 * s)
            dagger_y = int(body_y + 2 * s + state_pulse)
            pygame.draw.line(self.screen, (180, 180, 190),
                             (dagger_x, dagger_y),
                             (dagger_x + int(4 * s), dagger_y - int(3 * s)), 2)

    def _draw_orc(self, sx, sy, state, anim_id):
        """Big, muscular orc with tusks."""
        t = self._t + anim_id * 2.3
        s = CHAR_SCALE * 1.2  # Orcs are bigger

        if state == 3:
            shake = math.sin(t * 15) * 2.0
        elif state == 2:
            shake = math.sin(t * 5) * 1.0
        else:
            shake = 0

        self._draw_shadow(sx, int(sy + 10 * s), int(10 * s), int(4 * s))

        # Body (wide barrel)
        body_w = int(14 * s)
        body_h = int(12 * s)
        body_x = sx - body_w // 2
        breathe = math.sin(t * 2) * 1.0
        body_y = int(sy - 4 * s + breathe)
        pygame.draw.rect(self.screen, ORC_BODY,
                         (body_x, body_y, body_w, body_h),
                         border_radius=int(3 * s))
        # Chest armor plate
        pygame.draw.rect(self.screen, (70, 55, 40),
                         (body_x + int(2 * s), body_y + int(1 * s),
                          body_w - int(4 * s), body_h - int(4 * s)),
                         border_radius=int(2 * s))

        # Arms (thick)
        arm_w = int(4 * s)
        arm_h = int(9 * s)
        arm_y = int(body_y + 2 * s)
        arm_swing = math.sin(t * 3.5) * 1.5 * s
        pygame.draw.rect(self.screen, ORC_SKIN,
                         (body_x - arm_w, int(arm_y + arm_swing + shake), arm_w, arm_h),
                         border_radius=int(2 * s))
        pygame.draw.rect(self.screen, ORC_SKIN,
                         (body_x + body_w, int(arm_y - arm_swing + shake), arm_w, arm_h),
                         border_radius=int(2 * s))

        # Club weapon when attacking/chasing
        if state in (2, 3):
            club_x = body_x + body_w + arm_w
            club_y = int(arm_y - arm_swing + shake)
            angle = math.sin(t * 8) * 0.3 if state == 3 else 0
            end_x = club_x + int(math.cos(-0.5 + angle) * 8 * s)
            end_y = club_y + int(math.sin(-0.5 + angle) * 8 * s)
            pygame.draw.line(self.screen, (100, 70, 40),
                             (club_x, club_y), (end_x, end_y), int(3 * s))
            pygame.draw.circle(self.screen, (80, 60, 35), (end_x, end_y), int(2.5 * s))

        # Legs
        leg_y = body_y + body_h - int(2 * s)
        walk = math.sin(t * 5) * 2.0 * s if state in (1, 2, 4) else 0
        pygame.draw.rect(self.screen, ORC_SKIN,
                         (sx - int(4 * s), int(leg_y + walk * 0.4), int(4 * s), int(6 * s)),
                         border_radius=2)
        pygame.draw.rect(self.screen, ORC_SKIN,
                         (sx + int(0 * s), int(leg_y - walk * 0.4), int(4 * s), int(6 * s)),
                         border_radius=2)

        # Head
        head_r = int(6 * s)
        head_y = int(body_y - head_r + 3 * s + breathe * 0.5)
        pygame.draw.circle(self.screen, ORC_SKIN, (int(sx + shake), head_y), head_r)

        # Jaw / lower face darker
        pygame.draw.arc(self.screen, (60, 110, 45),
                        (int(sx + shake) - head_r, head_y - head_r,
                         head_r * 2, head_r * 2),
                        -0.5, math.pi + 0.5, int(2 * s))

        # Eyes (fierce)
        eye_y = head_y - int(1 * s)
        eye_sep = int(3 * s)
        pygame.draw.circle(self.screen, ORC_EYE,
                           (int(sx + shake) - eye_sep, eye_y), int(1.8 * s))
        pygame.draw.circle(self.screen, ORC_EYE,
                           (int(sx + shake) + eye_sep, eye_y), int(1.8 * s))
        # Brow line
        pygame.draw.line(self.screen, (50, 40, 30),
                         (int(sx + shake) - eye_sep - int(2 * s), eye_y - int(2 * s)),
                         (int(sx + shake) - eye_sep + int(2 * s), eye_y - int(1 * s)), 2)
        pygame.draw.line(self.screen, (50, 40, 30),
                         (int(sx + shake) + eye_sep - int(2 * s), eye_y - int(1 * s)),
                         (int(sx + shake) + eye_sep + int(2 * s), eye_y - int(2 * s)), 2)

        # Tusks
        tusk_y = head_y + int(2 * s)
        pygame.draw.line(self.screen, (230, 220, 200),
                         (int(sx + shake) - int(2.5 * s), tusk_y),
                         (int(sx + shake) - int(3.5 * s), tusk_y - int(3 * s)), 2)
        pygame.draw.line(self.screen, (230, 220, 200),
                         (int(sx + shake) + int(2.5 * s), tusk_y),
                         (int(sx + shake) + int(3.5 * s), tusk_y - int(3 * s)), 2)

    def _draw_slime(self, sx, sy, state, anim_id):
        """Bouncy, blobby slime."""
        t = self._t + anim_id * 3.1
        s = CHAR_SCALE

        # Bounce animation
        bounce = abs(math.sin(t * 3)) * 3.0 * s
        squash_x = 1.0 + math.sin(t * 3) * 0.1
        squash_y = 1.0 - math.sin(t * 3) * 0.1

        if state == 3:  # Attack - rapid bounce
            bounce = abs(math.sin(t * 10)) * 5.0 * s
            squash_x = 1.0 + math.sin(t * 10) * 0.15
            squash_y = 1.0 - math.sin(t * 10) * 0.15
        elif state == 2:  # Chase - faster bounce
            bounce = abs(math.sin(t * 5)) * 4.0 * s
            squash_x = 1.0 + math.sin(t * 5) * 0.12
            squash_y = 1.0 - math.sin(t * 5) * 0.12

        self._draw_shadow(sx, int(sy + 6 * s), int(8 * squash_x * s), int(3 * s))

        # Main blob body
        blob_w = int(16 * squash_x * s)
        blob_h = int(12 * squash_y * s)
        blob_x = sx - blob_w // 2
        blob_y = int(sy - blob_h // 2 - bounce)
        pygame.draw.ellipse(self.screen, SLIME_BODY,
                            (blob_x, blob_y, blob_w, blob_h))

        # Inner highlight (makes it look glossy)
        hl_w = int(10 * squash_x * s)
        hl_h = int(6 * squash_y * s)
        hl_x = sx - hl_w // 2 - int(1 * s)
        hl_y = int(blob_y + 1 * s)
        hl_surf = pygame.Surface((hl_w, hl_h), pygame.SRCALPHA)
        pygame.draw.ellipse(hl_surf, (*SLIME_HIGHLIGHT, 80),
                            (0, 0, hl_w, hl_h))
        self.screen.blit(hl_surf, (hl_x, hl_y))

        # Shine spot (top-left)
        shine_r = int(2 * s)
        shine_x = sx - int(3 * s)
        shine_y = int(blob_y + 3 * s)
        pygame.draw.circle(self.screen, (200, 240, 255), (shine_x, shine_y), shine_r)

        # Eyes
        eye_y = int(blob_y + blob_h * 0.4)
        eye_sep = int(3 * s)
        eye_r = int(2.2 * s)
        # White part
        pygame.draw.circle(self.screen, (220, 240, 255), (sx - eye_sep, eye_y), eye_r)
        pygame.draw.circle(self.screen, (220, 240, 255), (sx + eye_sep, eye_y), eye_r)
        # Pupils (look toward target direction when chasing)
        pupil_r = int(1.2 * s)
        px_off = 0
        if state in (2, 3):
            px_off = int(0.8 * s)
        pygame.draw.circle(self.screen, SLIME_EYE,
                           (sx - eye_sep + px_off, eye_y), pupil_r)
        pygame.draw.circle(self.screen, SLIME_EYE,
                           (sx + eye_sep + px_off, eye_y), pupil_r)

        # Mouth
        mouth_y = int(blob_y + blob_h * 0.65)
        if state == 3:
            # Open mouth when attacking
            pygame.draw.ellipse(self.screen, (30, 100, 140),
                                (sx - int(2 * s), mouth_y, int(4 * s), int(3 * s)))
        else:
            # Cute smile
            pygame.draw.arc(self.screen, (30, 100, 140),
                            (sx - int(2 * s), mouth_y - int(1 * s),
                             int(4 * s), int(3 * s)),
                            math.pi + 0.3, 2 * math.pi - 0.3, 1)

    def _draw_skeleton(self, sx, sy, state, anim_id):
        """Bony undead warrior with rattling animation."""
        t = self._t + anim_id * 2.1
        s = CHAR_SCALE

        rattle = 0.0
        if state == 3:
            rattle = math.sin(t * 20) * 1.5
        elif state == 2:
            rattle = math.sin(t * 8) * 1.0

        self._draw_shadow(sx, int(sy + 9 * s), int(7 * s), int(3 * s))

        # Legs (thin bones)
        leg_y = int(sy + 4 * s)
        walk = math.sin(t * 6) * 2.0 * s if state in (1, 2, 4) else 0
        bone_w = int(2 * s)
        pygame.draw.rect(self.screen, SKELETON_BONE,
                         (sx - int(3 * s), int(leg_y + walk * 0.4), bone_w, int(7 * s)))
        pygame.draw.rect(self.screen, SKELETON_BONE,
                         (sx + int(1 * s), int(leg_y - walk * 0.4), bone_w, int(7 * s)))
        # Knee joints
        pygame.draw.circle(self.screen, SKELETON_BONE,
                           (sx - int(2 * s), int(leg_y + 3 * s)), int(1.5 * s))
        pygame.draw.circle(self.screen, SKELETON_BONE,
                           (sx + int(2 * s), int(leg_y + 3 * s)), int(1.5 * s))

        # Ribcage
        body_y = int(sy - 3 * s + rattle * 0.3)
        for i in range(3):
            rib_y = body_y + int(i * 2.5 * s)
            rib_w = int((6 - i) * s)
            pygame.draw.arc(self.screen, SKELETON_BONE,
                            (sx - rib_w, rib_y, rib_w * 2, int(3 * s)),
                            0.3, math.pi - 0.3, 1)

        # Spine
        pygame.draw.line(self.screen, SKELETON_BONE,
                         (sx, body_y - int(1 * s)), (sx, int(sy + 5 * s)), 2)

        # Arms
        arm_y = int(body_y + 1 * s)
        arm_swing = math.sin(t * 4) * 2.0 * s
        pygame.draw.line(self.screen, SKELETON_BONE,
                         (sx - int(5 * s), arm_y),
                         (sx - int(9 * s), int(arm_y + 6 * s + arm_swing)), 2)
        pygame.draw.line(self.screen, SKELETON_BONE,
                         (sx + int(5 * s), arm_y),
                         (sx + int(9 * s), int(arm_y + 6 * s - arm_swing)), 2)

        # Sword in right hand when chasing/attacking
        if state in (2, 3):
            hand_x = sx + int(9 * s)
            hand_y = int(arm_y + 6 * s - arm_swing)
            swing = math.sin(t * 10) * 0.4 if state == 3 else 0
            bx = hand_x + int(math.cos(-0.8 + swing) * 7 * s)
            by = hand_y + int(math.sin(-0.8 + swing) * 7 * s)
            pygame.draw.line(self.screen, (200, 200, 210),
                             (hand_x, hand_y), (bx, by), 2)

        # Skull
        head_r = int(5 * s)
        head_y = int(body_y - head_r + 1 * s + rattle * 0.5)
        head_x = int(sx + rattle)
        pygame.draw.circle(self.screen, SKELETON_BONE, (head_x, head_y), head_r)
        # Jaw
        pygame.draw.arc(self.screen, SKELETON_DARK,
                        (head_x - int(3 * s), head_y + int(1 * s),
                         int(6 * s), int(4 * s)),
                        math.pi + 0.3, 2 * math.pi - 0.3, 1)

        # Eye sockets
        eye_y = head_y - int(1 * s)
        eye_sep = int(2.5 * s)
        pygame.draw.circle(self.screen, SKELETON_DARK, (head_x - eye_sep, eye_y), int(2 * s))
        pygame.draw.circle(self.screen, SKELETON_DARK, (head_x + eye_sep, eye_y), int(2 * s))
        # Glowing eyes
        glow = _pulse(3.0, 0.5, 1.0)
        eye_color = _lerp_color((80, 20, 10), SKELETON_EYE, glow)
        pygame.draw.circle(self.screen, eye_color, (head_x - eye_sep, eye_y), int(1.2 * s))
        pygame.draw.circle(self.screen, eye_color, (head_x + eye_sep, eye_y), int(1.2 * s))

        # Nose hole
        pygame.draw.polygon(self.screen, SKELETON_DARK, [
            (head_x, head_y + int(0.5 * s)),
            (head_x - int(1 * s), head_y + int(2 * s)),
            (head_x + int(1 * s), head_y + int(2 * s)),
        ])

    def _draw_sniper(self, sx, sy, state, anim_id):
        """Cloaked sniper with a long rifle and scope glint."""
        t = self._t + anim_id * 1.9
        s = CHAR_SCALE

        self._draw_shadow(sx, int(sy + 9 * s), int(8 * s), int(3 * s))

        breathe = math.sin(t * 2.0) * 0.8

        # Cloak (behind body)
        cloak_w = int(14 * s)
        cloak_h = int(12 * s)
        cloak_x = sx - cloak_w // 2
        cloak_y = int(sy - 2 * s + breathe)
        pygame.draw.ellipse(self.screen, SNIPER_CLOAK,
                            (cloak_x, cloak_y, cloak_w, cloak_h))

        # Body
        body_w = int(11 * s)
        body_h = int(9 * s)
        body_x = sx - body_w // 2
        body_y = int(sy - 1 * s + breathe)
        pygame.draw.rect(self.screen, SNIPER_CLOAK,
                         (body_x, body_y, body_w, body_h),
                         border_radius=int(3 * s))

        # Legs
        leg_y = body_y + body_h - int(2 * s)
        walk = math.sin(t * 5) * 1.0 * s if state in (1, 2, 4) else 0
        pygame.draw.rect(self.screen, SNIPER_CLOAK,
                         (sx - int(4 * s), int(leg_y + walk * 0.3), int(3 * s), int(5 * s)),
                         border_radius=1)
        pygame.draw.rect(self.screen, SNIPER_CLOAK,
                         (sx + int(1 * s), int(leg_y - walk * 0.3), int(3 * s), int(5 * s)),
                         border_radius=1)

        # Hood
        head_r = int(5 * s)
        head_y = int(body_y - head_r + 2 * s + breathe * 0.3)
        hood_r = int(6.5 * s)
        pygame.draw.circle(self.screen, SNIPER_HOOD, (sx, head_y), hood_r)
        # Face
        pygame.draw.circle(self.screen, SNIPER_SKIN, (sx, head_y + int(1 * s)), int(4 * s))
        # Hood top overlay
        pygame.draw.arc(self.screen, SNIPER_HOOD,
                        (sx - hood_r, head_y - hood_r, hood_r * 2, hood_r * 2),
                        0.2, math.pi - 0.2, int(4 * s))

        # Eyes (narrow slits)
        eye_y = head_y + int(0.5 * s)
        eye_sep = int(2.5 * s)
        pygame.draw.line(self.screen, (180, 180, 160),
                         (sx - eye_sep - int(1.5 * s), eye_y),
                         (sx - eye_sep + int(1.5 * s), eye_y), 2)
        pygame.draw.line(self.screen, (180, 180, 160),
                         (sx + eye_sep - int(1.5 * s), eye_y),
                         (sx + eye_sep + int(1.5 * s), eye_y), 2)

        # ── Rifle ──
        gun_x = sx + int(6 * s)
        gun_y = int(body_y + 3 * s)

        aim = -0.3
        recoil = 0.0
        if state == 3:
            recoil = math.sin(t * 12) * 1.5
            aim = -0.3 + math.sin(t * 0.8) * 0.15
            gun_x += int(recoil * 0.5)
        elif state == 2:
            aim = -0.3 + math.sin(t * 0.5) * 0.1

        gun_len = int(14 * s)
        gun_end_x = gun_x + int(math.cos(aim) * gun_len)
        gun_end_y = gun_y + int(math.sin(aim) * gun_len)

        # Barrel
        pygame.draw.line(self.screen, SNIPER_GUN,
                         (gun_x, gun_y), (gun_end_x, gun_end_y), int(2 * s))
        # Stock
        stock_x = gun_x - int(3 * s)
        stock_y = gun_y + int(2 * s)
        pygame.draw.line(self.screen, (90, 70, 50),
                         (gun_x, gun_y), (stock_x, stock_y), int(2.5 * s))

        # Scope
        scope_x = gun_x + int(math.cos(aim) * gun_len * 0.6)
        scope_y = gun_y + int(math.sin(aim) * gun_len * 0.6) - int(2 * s)
        pygame.draw.rect(self.screen, (60, 60, 65),
                         (scope_x - int(2 * s), scope_y, int(4 * s), int(2 * s)),
                         border_radius=1)

        # Scope glint (pulsing red dot)
        glint = _pulse(2.0, 0.3, 1.0)
        sf_x = gun_x + int(math.cos(aim) * gun_len * 0.75)
        sf_y = gun_y + int(math.sin(aim) * gun_len * 0.75) - int(2 * s)
        pygame.draw.circle(self.screen,
                           (int(255 * glint), int(30 * glint), int(30 * glint)),
                           (sf_x, sf_y), int(1.5 * s))

        # Muzzle flash when attacking
        if state == 3 and math.sin(t * 12) > 0.7:
            flash_r = int(3 * s)
            pygame.draw.circle(self.screen, (255, 240, 100),
                               (gun_end_x, gun_end_y), flash_r)
            pygame.draw.circle(self.screen, (255, 200, 50),
                               (gun_end_x, gun_end_y), int(flash_r * 0.6))

    def _draw_homing_archer(self, sx, sy, state, anim_id):
        """Elven forest archer with a magical homing bow."""
        t = self._t + anim_id * 1.6
        s = CHAR_SCALE

        self._draw_shadow(sx, int(sy + 9 * s), int(8 * s), int(3 * s))

        breathe = math.sin(t * 2.2) * 0.8

        # Cloak (behind body)
        cloak_w = int(14 * s)
        cloak_h = int(12 * s)
        cloak_x = sx - cloak_w // 2
        cloak_y = int(sy - 2 * s + breathe)
        pygame.draw.ellipse(self.screen, ARCHER_CLOAK,
                            (cloak_x, cloak_y, cloak_w, cloak_h))

        # Tunic (front)
        body_w = int(10 * s)
        body_h = int(9 * s)
        body_x = sx - body_w // 2
        body_y = int(sy - 1 * s + breathe)
        pygame.draw.rect(self.screen, ARCHER_TUNIC,
                         (body_x, body_y, body_w, body_h),
                         border_radius=int(3 * s))
        # Leather belt
        pygame.draw.rect(self.screen, (70, 50, 30),
                         (body_x, body_y + int(5 * s), body_w, int(2 * s)))
        # Cross-chest strap
        pygame.draw.line(self.screen, (80, 55, 35),
                         (body_x, body_y + int(1 * s)),
                         (body_x + body_w, body_y + int(5 * s)), 2)

        # Legs
        leg_y = body_y + body_h - int(2 * s)
        walk = math.sin(t * 5) * 1.2 * s if state in (1, 2, 4) else 0
        pygame.draw.rect(self.screen, ARCHER_CLOAK,
                         (sx - int(4 * s), int(leg_y + walk * 0.3), int(3 * s), int(5 * s)),
                         border_radius=1)
        pygame.draw.rect(self.screen, ARCHER_CLOAK,
                         (sx + int(1 * s), int(leg_y - walk * 0.3), int(3 * s), int(5 * s)),
                         border_radius=1)

        # Hood
        head_r = int(5 * s)
        head_y = int(body_y - head_r + 2 * s + breathe * 0.3)
        hood_r = int(6 * s)
        pygame.draw.circle(self.screen, ARCHER_CLOAK, (sx, head_y), hood_r)
        # Leafy tip
        pygame.draw.polygon(self.screen, ARCHER_TUNIC, [
            (sx, head_y - hood_r - int(1 * s)),
            (sx - int(2 * s), head_y - hood_r - int(4 * s)),
            (sx + int(2 * s), head_y - hood_r - int(3 * s)),
        ])
        # Face
        pygame.draw.circle(self.screen, ARCHER_SKIN,
                           (sx, head_y + int(1 * s)), int(4 * s))
        # Hood rim
        pygame.draw.arc(self.screen, ARCHER_CLOAK,
                        (sx - hood_r, head_y - hood_r, hood_r * 2, hood_r * 2),
                        0.2, math.pi - 0.2, int(3 * s))

        # Elf ears
        pygame.draw.polygon(self.screen, ARCHER_SKIN, [
            (sx - int(4 * s), head_y),
            (sx - int(6 * s), head_y - int(1 * s)),
            (sx - int(4 * s), head_y + int(1 * s)),
        ])
        pygame.draw.polygon(self.screen, ARCHER_SKIN, [
            (sx + int(4 * s), head_y),
            (sx + int(6 * s), head_y - int(1 * s)),
            (sx + int(4 * s), head_y + int(1 * s)),
        ])

        # Keen green eyes
        eye_y = head_y + int(0.5 * s)
        eye_sep = int(2.2 * s)
        pygame.draw.circle(self.screen, (50, 100, 60),
                           (sx - eye_sep, eye_y), int(1.0 * s))
        pygame.draw.circle(self.screen, (50, 100, 60),
                           (sx + eye_sep, eye_y), int(1.0 * s))

        # Quiver on back
        quiv_x = body_x + body_w - int(2 * s)
        quiv_y = body_y - int(3 * s)
        pygame.draw.rect(self.screen, (90, 65, 40),
                         (quiv_x, quiv_y, int(3 * s), int(5 * s)),
                         border_radius=1)
        for i in range(3):
            pygame.draw.line(self.screen, (210, 210, 190),
                             (quiv_x + int(i * 1 * s), quiv_y),
                             (quiv_x + int(i * 1 * s), quiv_y - int(2 * s)), 1)

        # ── Bow (vertical, held to the right) ──
        bow_x = sx + int(8 * s)
        bow_y = int(sy - 1 * s + breathe)

        draw_amt = 0.0
        if state == 3:
            draw_amt = 0.9 + math.sin(t * 3) * 0.1
        elif state == 2:
            draw_amt = 0.3

        # Recurved bow outline
        upper_top = (bow_x, bow_y - int(9 * s))
        upper_mid = (bow_x + int(3 * s), bow_y - int(4 * s))
        grip = (bow_x, bow_y)
        lower_mid = (bow_x + int(3 * s), bow_y + int(4 * s))
        lower_bot = (bow_x, bow_y + int(9 * s))
        pygame.draw.lines(self.screen, ARCHER_BOW, False,
                          [upper_top, upper_mid, grip, lower_mid, lower_bot],
                          int(2 * s))

        # Bowstring (pulled back when drawing)
        pull_x = bow_x - int(draw_amt * 5 * s)
        pygame.draw.lines(self.screen, (220, 220, 200), False,
                          [upper_top, (pull_x, bow_y), lower_bot], 1)

        # Glowing magic arrow
        if state in (2, 3):
            glow = _pulse(4.0, 0.6, 1.0)
            arrow_color = _lerp_color(ARCHER_ARROW, ARCHER_ARROW_GLOW, glow)
            tip_x = bow_x + int(6 * s)
            tail_x = pull_x - int(1 * s)
            ay = bow_y
            pygame.draw.line(self.screen, arrow_color,
                             (tail_x, ay), (tip_x, ay), 2)
            pygame.draw.polygon(self.screen, ARCHER_ARROW_GLOW, [
                (tip_x + int(2 * s), ay),
                (tip_x, ay - int(1.5 * s)),
                (tip_x, ay + int(1.5 * s)),
            ])
            pygame.draw.polygon(self.screen, ARCHER_TUNIC, [
                (tail_x, ay),
                (tail_x - int(2 * s), ay - int(1.5 * s)),
                (tail_x - int(1 * s), ay),
                (tail_x - int(2 * s), ay + int(1.5 * s)),
            ])
            # Magic halo around arrowhead
            halo_r = int(4 * s)
            halo_surf = pygame.Surface((halo_r * 2, halo_r * 2), pygame.SRCALPHA)
            alpha = int(120 * glow)
            pygame.draw.circle(halo_surf, (255, 240, 150, alpha),
                               (halo_r, halo_r), halo_r)
            self.screen.blit(halo_surf, (tip_x - halo_r, ay - halo_r))

    def _draw_bolt_caster(self, sx, sy, state, anim_id):
        """Hooded spellcaster wielding a crystal staff that arcs with lightning."""
        t = self._t + anim_id * 2.1
        s = CHAR_SCALE

        self._draw_shadow(sx, int(sy + 10 * s), int(9 * s), int(4 * s))

        breathe = math.sin(t * 1.8) * 1.0
        sway = math.sin(t * 1.2) * 1.0 if state in (2, 3) else 0

        # Robe (trapezoidal flowy silhouette)
        robe_top_y = int(sy - 4 * s + breathe)
        robe_bot_y = int(sy + 9 * s)
        pygame.draw.polygon(self.screen, CASTER_ROBE, [
            (sx - int(6 * s), robe_top_y),
            (sx + int(6 * s), robe_top_y),
            (sx + int(9 * s), robe_bot_y),
            (sx - int(9 * s), robe_bot_y),
        ])
        # Inner vertical shading
        pygame.draw.polygon(self.screen, CASTER_ROBE_DARK, [
            (sx - int(3 * s), robe_top_y + int(1 * s)),
            (sx + int(3 * s), robe_top_y + int(1 * s)),
            (sx + int(4 * s), robe_bot_y),
            (sx - int(4 * s), robe_bot_y),
        ])
        # Hem rune line
        pygame.draw.line(self.screen, CASTER_BOLT,
                         (sx - int(9 * s), robe_bot_y - int(1 * s)),
                         (sx + int(9 * s), robe_bot_y - int(1 * s)), 1)

        # Sleeves
        sleeve_y = robe_top_y + int(6 * s)
        pygame.draw.ellipse(self.screen, CASTER_ROBE,
                            (sx - int(9 * s), sleeve_y, int(5 * s), int(4 * s)))
        pygame.draw.ellipse(self.screen, CASTER_ROBE,
                            (sx + int(4 * s), sleeve_y, int(5 * s), int(4 * s)))

        # Hood
        head_r = int(5 * s)
        head_y = int(robe_top_y - head_r + 3 * s + breathe * 0.3)
        hood_r = int(7 * s)
        pygame.draw.circle(self.screen, CASTER_HOOD, (sx, head_y), hood_r)
        # Shadowed face
        pygame.draw.circle(self.screen, (50, 35, 55),
                           (sx, head_y + int(1 * s)), int(4 * s))
        # Hood rim overlay
        pygame.draw.arc(self.screen, CASTER_HOOD,
                        (sx - hood_r, head_y - hood_r, hood_r * 2, hood_r * 2),
                        0.15, math.pi - 0.15, int(4 * s))

        # Glowing cold-blue eyes
        eye_glow = _pulse(3.0, 0.5, 1.0)
        eye_color = _lerp_color((60, 90, 140), CASTER_EYE, eye_glow)
        eye_y = head_y + int(0.5 * s)
        eye_sep = int(2.2 * s)
        pygame.draw.circle(self.screen, eye_color,
                           (sx - eye_sep, eye_y), int(1.3 * s))
        pygame.draw.circle(self.screen, eye_color,
                           (sx + eye_sep, eye_y), int(1.3 * s))

        # ── Staff ──
        staff_bot_x = sx + int(7 * s)
        staff_bot_y = int(sy + 7 * s)
        tilt = (sway * 0.02) + (math.sin(t * 0.7) * 0.05 if state == 2 else 0)
        if state == 3:
            tilt += math.sin(t * 3) * 0.08
        staff_angle = -math.pi / 2 + tilt
        staff_len = int(18 * s)
        staff_top_x = staff_bot_x + int(math.cos(staff_angle) * staff_len)
        staff_top_y = staff_bot_y + int(math.sin(staff_angle) * staff_len)

        pygame.draw.line(self.screen, CASTER_STAFF,
                         (staff_bot_x, staff_bot_y),
                         (staff_top_x, staff_top_y), int(2 * s))
        # Binding
        bind_x = staff_top_x + int(math.cos(staff_angle + math.pi) * 4 * s)
        bind_y = staff_top_y + int(math.sin(staff_angle + math.pi) * 4 * s)
        pygame.draw.circle(self.screen, (140, 100, 60),
                           (bind_x, bind_y), int(1.5 * s))

        # Crystal orb
        orb_r = int(4 * s)
        active = state in (2, 3)
        orb_glow = _pulse(3.0, 0.7, 1.0) if active else 0.3
        orb_color = _lerp_color(CASTER_CRYSTAL, (220, 240, 255), orb_glow)
        # Outer aura
        glow_r = int(7 * s) if state == 3 else int(6 * s)
        aura = pygame.Surface((glow_r * 2, glow_r * 2), pygame.SRCALPHA)
        alpha = int(100 + 100 * orb_glow) if active else 60
        pygame.draw.circle(aura, (*CASTER_BOLT, alpha),
                           (glow_r, glow_r), glow_r)
        self.screen.blit(aura, (staff_top_x - glow_r, staff_top_y - glow_r))
        # Orb core
        pygame.draw.circle(self.screen, orb_color,
                           (staff_top_x, staff_top_y), orb_r)
        pygame.draw.circle(self.screen, (235, 250, 255),
                           (staff_top_x - int(1 * s), staff_top_y - int(1 * s)),
                           int(1.3 * s))

        # Crackling lightning arcs around orb
        if active:
            num_arcs = 4 if state == 3 else 2
            for i in range(num_arcs):
                angle = (i / num_arcs) * 2 * math.pi + t * 4
                reach = int((5 + math.sin(t * 9 + i) * 2) * s)
                last_x, last_y = staff_top_x, staff_top_y
                for j in range(1, 4):
                    frac = j / 3
                    bx = staff_top_x + int(math.cos(angle) * reach * frac)
                    by = staff_top_y + int(math.sin(angle) * reach * frac)
                    jitter = int(math.sin(t * 20 + i * 3 + j) * 2 * s)
                    bx += int(math.cos(angle + math.pi / 2) * jitter)
                    by += int(math.sin(angle + math.pi / 2) * jitter)
                    pygame.draw.line(self.screen, CASTER_BOLT,
                                     (last_x, last_y), (bx, by), 1)
                    last_x, last_y = bx, by

        # Attack flash at orb
        if state == 3 and math.sin(t * 8) > 0.3:
            flash_r = int(3 * s)
            pygame.draw.circle(self.screen, (230, 245, 255),
                               (staff_top_x, staff_top_y), flash_r)

    def _draw_mushroom(self, sx, sy, state, anim_id):
        """Cute but deadly mushroom creature with spore attack."""
        t = self._t + anim_id * 2.7
        s = CHAR_SCALE

        wobble = math.sin(t * 3) * 1.5
        if state == 2:
            wobble = math.sin(t * 6) * 2.0

        self._draw_shadow(sx, int(sy + 8 * s), int(7 * s), int(3 * s))

        # Stem (body)
        stem_w = int(8 * s)
        stem_h = int(8 * s)
        stem_x = sx - stem_w // 2
        stem_y = int(sy - 1 * s + abs(math.sin(t * 3)) * 1.0)
        pygame.draw.rect(self.screen, MUSHROOM_STEM,
                         (stem_x, stem_y, stem_w, stem_h),
                         border_radius=int(2 * s))

        # Feet
        walk = math.sin(t * 7) * 1.5 * s if state in (1, 2, 4) else 0
        foot_w = int(3 * s)
        foot_h = int(3 * s)
        pygame.draw.ellipse(self.screen, MUSHROOM_STEM,
                            (sx - int(4 * s), int(stem_y + stem_h - 1 + walk * 0.3),
                             foot_w, foot_h))
        pygame.draw.ellipse(self.screen, MUSHROOM_STEM,
                            (sx + int(1 * s), int(stem_y + stem_h - 1 - walk * 0.3),
                             foot_w, foot_h))

        # Cap (dome)
        cap_w = int(18 * s)
        cap_h = int(10 * s)
        cap_x = sx - cap_w // 2
        cap_y = int(stem_y - cap_h + int(3 * s) + wobble * 0.3)
        pygame.draw.ellipse(self.screen, MUSHROOM_CAP,
                            (cap_x, cap_y, cap_w, cap_h))

        # White spots on cap
        spots = [(0.3, 0.35), (-0.25, 0.4), (0.0, 0.2), (0.35, 0.55), (-0.3, 0.6)]
        for dx_frac, dy_frac in spots:
            spot_x = int(cap_x + cap_w * (0.5 + dx_frac * 0.8))
            spot_y = int(cap_y + cap_h * dy_frac)
            pygame.draw.circle(self.screen, MUSHROOM_SPOT, (spot_x, spot_y), int(1.5 * s))

        # Eyes (under the cap)
        eye_y = int(stem_y + 2 * s)
        eye_sep = int(2 * s)
        eye_r = int(1.8 * s)
        pygame.draw.circle(self.screen, (240, 240, 240), (sx - eye_sep, eye_y), eye_r)
        pygame.draw.circle(self.screen, (240, 240, 240), (sx + eye_sep, eye_y), eye_r)
        pupil_off = int(0.5 * s) if state in (2, 3) else 0
        pygame.draw.circle(self.screen, MUSHROOM_EYE,
                           (sx - eye_sep + pupil_off, eye_y), int(1.0 * s))
        pygame.draw.circle(self.screen, MUSHROOM_EYE,
                           (sx + eye_sep + pupil_off, eye_y), int(1.0 * s))

        # Blush marks
        blush_y = int(eye_y + 2 * s)
        blush_surf = pygame.Surface((int(3 * s), int(2 * s)), pygame.SRCALPHA)
        pygame.draw.ellipse(blush_surf, (220, 100, 100, 80),
                            (0, 0, int(3 * s), int(2 * s)))
        self.screen.blit(blush_surf, (sx - eye_sep - int(2.5 * s), blush_y))
        self.screen.blit(blush_surf, (sx + eye_sep - int(0.5 * s), blush_y))

        # Spore particles when attacking
        if state == 3:
            for i in range(6):
                angle = (i / 6) * 2 * math.pi + t * 2
                dist = int((5 + math.sin(t * 3 + i) * 3) * s)
                px = sx + int(math.cos(angle) * dist)
                py = int(cap_y + cap_h * 0.3) + int(math.sin(angle) * dist * 0.6)
                spore_r = int(1 * s)
                spore_surf = pygame.Surface((spore_r * 2, spore_r * 2), pygame.SRCALPHA)
                pygame.draw.circle(spore_surf, (180, 220, 80, 150),
                                   (spore_r, spore_r), spore_r)
                self.screen.blit(spore_surf, (px - spore_r, py - spore_r))

    def _draw_wolf(self, sx, sy, state, anim_id):
        """Four-legged wolf with snarling attack."""
        t = self._t + anim_id * 1.5
        s = CHAR_SCALE

        self._draw_shadow(sx, int(sy + 6 * s), int(10 * s), int(3 * s))

        run_cycle = math.sin(t * 7) if state in (1, 2, 4) else math.sin(t * 2) * 0.3
        breathe = math.sin(t * 3) * 0.8

        # Body (horizontal oval)
        body_w = int(18 * s)
        body_h = int(8 * s)
        body_x = sx - body_w // 2
        body_y = int(sy - 4 * s + breathe)
        pygame.draw.ellipse(self.screen, WOLF_FUR,
                            (body_x, body_y, body_w, body_h))
        # Belly
        pygame.draw.ellipse(self.screen, WOLF_BELLY,
                            (body_x + int(3 * s), body_y + int(3 * s),
                             body_w - int(6 * s), body_h - int(3 * s)))

        # 4 Legs
        leg_w = int(2.5 * s)
        leg_h = int(6 * s)
        fl_x = body_x + int(3 * s)
        fl_y = body_y + body_h - int(2 * s)
        pygame.draw.rect(self.screen, WOLF_FUR,
                         (fl_x, int(fl_y + run_cycle * 2 * s), leg_w, leg_h),
                         border_radius=1)
        pygame.draw.rect(self.screen, WOLF_FUR,
                         (fl_x + int(3 * s), int(fl_y - run_cycle * 2 * s), leg_w, leg_h),
                         border_radius=1)
        bl_x = body_x + body_w - int(8 * s)
        pygame.draw.rect(self.screen, WOLF_FUR,
                         (bl_x, int(fl_y - run_cycle * 2 * s), leg_w, leg_h),
                         border_radius=1)
        pygame.draw.rect(self.screen, WOLF_FUR,
                         (bl_x + int(3 * s), int(fl_y + run_cycle * 2 * s), leg_w, leg_h),
                         border_radius=1)

        # Tail
        tail_x = body_x + body_w - int(1 * s)
        tail_y = int(body_y + 1 * s)
        tail_wag = math.sin(t * 4) * 0.4
        tail_end_x = tail_x + int(math.cos(0.8 + tail_wag) * 7 * s)
        tail_end_y = tail_y + int(math.sin(-0.8 + tail_wag) * 5 * s)
        pygame.draw.line(self.screen, WOLF_FUR,
                         (tail_x, tail_y), (tail_end_x, tail_end_y), int(2 * s))

        # Head
        head_r = int(5 * s)
        head_x = body_x - int(1 * s)
        head_y = int(body_y + 2 * s + breathe * 0.3)
        pygame.draw.circle(self.screen, WOLF_FUR, (head_x, head_y), head_r)

        # Snout
        pygame.draw.ellipse(self.screen, WOLF_BELLY,
                            (head_x - int(6 * s), head_y - int(1 * s),
                             int(5 * s), int(3 * s)))
        # Nose
        pygame.draw.circle(self.screen, (30, 30, 30),
                           (head_x - int(5 * s), head_y), int(1.2 * s))

        # Pointed ears
        ear_h = int(4 * s)
        pygame.draw.polygon(self.screen, WOLF_FUR, [
            (head_x - int(2 * s), head_y - head_r + int(1 * s)),
            (head_x - int(4 * s), head_y - head_r - ear_h),
            (head_x, head_y - head_r + int(1 * s)),
        ])
        pygame.draw.polygon(self.screen, WOLF_FUR, [
            (head_x + int(1 * s), head_y - head_r + int(1 * s)),
            (head_x + int(3 * s), head_y - head_r - ear_h),
            (head_x + int(5 * s), head_y - head_r + int(1 * s)),
        ])

        # Eyes
        ey = head_y - int(1 * s)
        pygame.draw.circle(self.screen, WOLF_EYE,
                           (head_x - int(2 * s), ey), int(1.5 * s))
        pygame.draw.circle(self.screen, WOLF_EYE,
                           (head_x + int(2 * s), ey), int(1.5 * s))
        pygame.draw.circle(self.screen, (20, 20, 10),
                           (head_x - int(2 * s), ey), int(0.7 * s))
        pygame.draw.circle(self.screen, (20, 20, 10),
                           (head_x + int(2 * s), ey), int(0.7 * s))

        # Snarl when attacking
        if state in (2, 3):
            mouth_x = head_x - int(5 * s)
            mouth_y = head_y + int(1 * s)
            mouth_open = int(2 * s) if state == 3 else int(1 * s)
            pygame.draw.ellipse(self.screen, (80, 30, 30),
                                (mouth_x - int(2 * s), mouth_y,
                                 int(4 * s), mouth_open + int(1 * s)))
            for i in range(3):
                tx = mouth_x - int(1.5 * s) + int(i * 1.5 * s)
                pygame.draw.line(self.screen, (240, 240, 240),
                                 (tx, mouth_y), (tx, mouth_y + int(1.5 * s)), 1)

    def _draw_drake(self, sx, sy, state, anim_id):
        """Small dragon/drake with wings and fire breath."""
        t = self._t + anim_id * 2.5
        s = CHAR_SCALE * 1.1

        self._draw_shadow(sx, int(sy + 9 * s), int(9 * s), int(4 * s))

        breathe = math.sin(t * 2.5) * 1.0

        # Wings (behind body)
        wing_y = int(sy - 5 * s + breathe)
        wing_flap = math.sin(t * 4) * 0.4
        wing_span = int(12 * s)
        wing_h = int(8 * s)
        pygame.draw.polygon(self.screen, DRAKE_WING, [
            (sx - int(2 * s), wing_y),
            (sx - wing_span, int(wing_y - wing_h * math.cos(wing_flap))),
            (sx - int(wing_span * 0.6), int(wing_y + 2 * s)),
            (sx - int(2 * s), int(wing_y + 3 * s)),
        ])
        pygame.draw.polygon(self.screen, DRAKE_WING, [
            (sx + int(2 * s), wing_y),
            (sx + wing_span, int(wing_y - wing_h * math.cos(wing_flap))),
            (sx + int(wing_span * 0.6), int(wing_y + 2 * s)),
            (sx + int(2 * s), int(wing_y + 3 * s)),
        ])
        # Wing membrane lines
        for i in range(3):
            frac = 0.3 + i * 0.25
            lx = int(sx - int(2 * s) + (-wing_span + int(2 * s)) * frac)
            ly = int(wing_y + (-wing_h * math.cos(wing_flap)) * frac * 0.8)
            pygame.draw.line(self.screen, (160, 40, 30),
                             (sx - int(2 * s), int(wing_y + 1 * s)), (lx, ly), 1)
            rx = int(sx + int(2 * s) + (wing_span - int(2 * s)) * frac)
            pygame.draw.line(self.screen, (160, 40, 30),
                             (sx + int(2 * s), int(wing_y + 1 * s)), (rx, ly), 1)

        # Body
        body_w = int(12 * s)
        body_h = int(10 * s)
        body_x = sx - body_w // 2
        body_y = int(sy - 3 * s + breathe)
        pygame.draw.ellipse(self.screen, DRAKE_BODY,
                            (body_x, body_y, body_w, body_h))
        pygame.draw.ellipse(self.screen, DRAKE_BELLY,
                            (body_x + int(2 * s), body_y + int(3 * s),
                             body_w - int(4 * s), body_h - int(4 * s)))

        # Tail
        tail_x = sx + body_w // 2
        tail_y = int(body_y + body_h * 0.6)
        points = []
        for i in range(5):
            px = tail_x + int(i * 3 * s)
            py = int(tail_y + math.sin(t * 3 + i * 0.8) * 2 * s)
            points.append((px, py))
        if len(points) >= 2:
            pygame.draw.lines(self.screen, DRAKE_BODY, False, points, int(2 * s))
            last = points[-1]
            pygame.draw.polygon(self.screen, DRAKE_BODY, [
                last,
                (last[0] + int(2 * s), last[1] - int(2 * s)),
                (last[0] + int(3 * s), last[1]),
                (last[0] + int(2 * s), last[1] + int(2 * s)),
            ])

        # Legs
        leg_base = body_y + body_h - int(2 * s)
        walk = math.sin(t * 5) * 1.5 * s if state in (1, 2, 4) else 0
        pygame.draw.rect(self.screen, DRAKE_BODY,
                         (sx - int(3 * s), int(leg_base + walk * 0.4),
                          int(3 * s), int(5 * s)), border_radius=1)
        pygame.draw.rect(self.screen, DRAKE_BODY,
                         (sx, int(leg_base - walk * 0.4),
                          int(3 * s), int(5 * s)), border_radius=1)

        # Head
        head_r = int(5 * s)
        head_y = int(body_y - head_r + 3 * s + breathe * 0.4)
        pygame.draw.circle(self.screen, DRAKE_BODY, (sx, head_y), head_r)

        # Horns
        horn_h = int(4 * s)
        pygame.draw.line(self.screen, (200, 180, 100),
                         (sx - int(3 * s), head_y - head_r + int(1 * s)),
                         (sx - int(5 * s), head_y - head_r - horn_h), 2)
        pygame.draw.line(self.screen, (200, 180, 100),
                         (sx + int(3 * s), head_y - head_r + int(1 * s)),
                         (sx + int(5 * s), head_y - head_r - horn_h), 2)

        # Eyes (slit pupils)
        ey = head_y - int(1 * s)
        eye_sep = int(2.5 * s)
        pygame.draw.circle(self.screen, DRAKE_EYE, (sx - eye_sep, ey), int(2 * s))
        pygame.draw.circle(self.screen, DRAKE_EYE, (sx + eye_sep, ey), int(2 * s))
        pygame.draw.line(self.screen, (40, 20, 10),
                         (sx - eye_sep, ey - int(1.5 * s)),
                         (sx - eye_sep, ey + int(1.5 * s)), 1)
        pygame.draw.line(self.screen, (40, 20, 10),
                         (sx + eye_sep, ey - int(1.5 * s)),
                         (sx + eye_sep, ey + int(1.5 * s)), 1)

        # Nostrils
        nostril_y = head_y + int(2 * s)
        pygame.draw.circle(self.screen, (120, 30, 20),
                           (sx - int(1.5 * s), nostril_y), int(0.8 * s))
        pygame.draw.circle(self.screen, (120, 30, 20),
                           (sx + int(1.5 * s), nostril_y), int(0.8 * s))

        # Fire breath when attacking
        if state == 3:
            for i in range(5):
                f_dist = int((3 + i * 2) * s)
                f_x = sx + int(math.sin(t * 8 + i) * 2 * s)
                f_y = nostril_y + f_dist
                f_r = int((3 - i * 0.4) * s)
                alpha = max(50, 200 - i * 40)
                fire_surf = pygame.Surface((f_r * 2, f_r * 2), pygame.SRCALPHA)
                pygame.draw.circle(fire_surf,
                                   (255, max(50, 200 - i * 30), 30, alpha),
                                   (f_r, f_r), f_r)
                self.screen.blit(fire_surf, (f_x - f_r, f_y - f_r))
        elif state == 2:
            for i in range(2):
                smoke_x = sx + int(math.sin(t * 3 + i * 2) * 2 * s)
                smoke_y = int(nostril_y + 2 * s + i * 3 * s)
                smoke_r = int(1.5 * s)
                smoke_surf = pygame.Surface((smoke_r * 2, smoke_r * 2), pygame.SRCALPHA)
                pygame.draw.circle(smoke_surf, (100, 100, 100, 80),
                                   (smoke_r, smoke_r), smoke_r)
                self.screen.blit(smoke_surf, (smoke_x - smoke_r, smoke_y - smoke_r))

    def _draw_generic_monster(self, sx, sy, state, anim_id):
        """Fallback for unknown monster types - a spiky creature."""
        t = self._t + anim_id * 1.3
        s = CHAR_SCALE

        breathe = math.sin(t * 2.5) * 1.5

        self._draw_shadow(sx, int(sy + 8 * s), int(8 * s), int(3 * s))

        # Body
        body_r = int(8 * s)
        body_y = int(sy + breathe)
        pygame.draw.circle(self.screen, GENERIC_MONSTER_BODY, (sx, body_y), body_r)

        # Spikes around body
        num_spikes = 6
        for i in range(num_spikes):
            angle = (i / num_spikes) * 2 * math.pi + t * 0.5
            spike_len = int(4 * s)
            bx = sx + int(math.cos(angle) * body_r)
            by = body_y + int(math.sin(angle) * body_r)
            ex = sx + int(math.cos(angle) * (body_r + spike_len))
            ey = body_y + int(math.sin(angle) * (body_r + spike_len))
            pygame.draw.line(self.screen, (180, 80, 220), (bx, by), (ex, ey), 2)

        # Eyes
        eye_y = body_y - int(1 * s)
        eye_sep = int(3 * s)
        pygame.draw.circle(self.screen, GENERIC_MONSTER_EYE, (sx - eye_sep, eye_y), int(2 * s))
        pygame.draw.circle(self.screen, GENERIC_MONSTER_EYE, (sx + eye_sep, eye_y), int(2 * s))
        pygame.draw.circle(self.screen, (40, 20, 50), (sx - eye_sep, eye_y), int(1 * s))
        pygame.draw.circle(self.screen, (40, 20, 50), (sx + eye_sep, eye_y), int(1 * s))

    def _draw_monster(self, sx, sy, monster):
        """Route to the correct species drawer."""
        state = getattr(monster, 'state', 0)
        name = getattr(monster, 'name', '').lower()
        anim_id = getattr(monster, 'guid', 0) % 100

        if 'goblin' in name:
            self._draw_goblin(sx, sy, state, anim_id)
        elif 'orc' in name:
            self._draw_orc(sx, sy, state, anim_id)
        elif 'slime' in name:
            self._draw_slime(sx, sy, state, anim_id)
        elif 'skeleton' in name:
            self._draw_skeleton(sx, sy, state, anim_id)
        elif 'sniper' in name:
            self._draw_sniper(sx, sy, state, anim_id)
        elif 'archer' in name:
            self._draw_homing_archer(sx, sy, state, anim_id)
        elif 'caster' in name:
            self._draw_bolt_caster(sx, sy, state, anim_id)
        elif 'mushroom' in name:
            self._draw_mushroom(sx, sy, state, anim_id)
        elif 'wolf' in name:
            self._draw_wolf(sx, sy, state, anim_id)
        elif 'drake' in name or 'dragon' in name:
            self._draw_drake(sx, sy, state, anim_id)
        else:
            self._draw_generic_monster(sx, sy, state, anim_id)

    # ── State indicator icon ─────────────────────────────────────────
    def _draw_state_indicator(self, sx, sy, state):
        """Small icon above the monster showing its AI state."""
        color = _STATE_COLORS.get(state, STATE_IDLE_COLOR)
        ind_y = sy - int(22 * CHAR_SCALE)
        r = 3

        if state == 0:  # Idle - "Zzz"
            self._label("z", sx + 8, ind_y, self.font_small, (150, 150, 180))
            self._label("z", sx + 12, ind_y - 5, self.font_small, (130, 130, 160))
        elif state == 1:  # Patrol - footsteps
            pygame.draw.circle(self.screen, color, (sx - 4, ind_y), 2)
            pygame.draw.circle(self.screen, color, (sx + 2, ind_y - 2), 2)
            pygame.draw.circle(self.screen, color, (sx + 8, ind_y), 2)
        elif state == 2:  # Chase - exclamation
            self._label("!", sx, ind_y, self.font_name, STATE_CHASE_COLOR)
        elif state == 3:  # Attack - crossed swords
            self._label("X", sx, ind_y, self.font_name, STATE_ATTACK_COLOR)
        elif state == 4:  # Return - arrow
            pygame.draw.polygon(self.screen, STATE_RETURN_COLOR, [
                (sx - 5, ind_y), (sx + 3, ind_y - 4), (sx + 3, ind_y + 4)
            ])

    # ═══════════════════════════════════════════════════════════════════
    #  MAIN DRAW
    # ═══════════════════════════════════════════════════════════════════

    def draw_frame(self, my_player, other_players: dict, monsters: dict, status_lines: list,
                   hitscan_lines: list = None, projectiles: dict = None,
                   click_markers: list = None):
        self.screen.fill(BG_COLOR)

        if my_player is None:
            self._draw_status(status_lines)
            pygame.display.flip()
            self.clock.tick(60)
            return

        cx, cz = my_player.x, my_player.z
        self.draw_grid(cx, cz)

        # ── Click-to-move markers (LoL의 초록 원 피드백) ──
        if click_markers:
            now_t = time.time()
            for (wx, wz, expire) in click_markers:
                remaining = max(0.0, expire - now_t)
                # 수명 비율에 따라 반경 축소 + 알파 감소 (LoL 느낌)
                lifetime = 0.4  # 기본값. config 와 느슨하게 동기화.
                t = remaining / lifetime if lifetime > 0 else 0.0
                msx, msy = self.world_to_screen(wx, wz, cx, cz)
                r_outer = int(14 * (0.6 + 0.4 * t))
                r_inner = max(2, r_outer - 3)
                alpha = int(200 * t)
                surf = pygame.Surface((r_outer * 2 + 2, r_outer * 2 + 2), pygame.SRCALPHA)
                pygame.draw.circle(surf, (80, 220, 120, alpha),
                                   (r_outer + 1, r_outer + 1), r_outer, width=2)
                pygame.draw.circle(surf, (120, 255, 160, alpha),
                                   (r_outer + 1, r_outer + 1), r_inner, width=1)
                self.screen.blit(surf, (msx - r_outer - 1, msy - r_outer - 1))

        # ── Detect range circles (draw first, behind everything) ──
        for gid, m in monsters.items():
            state = getattr(m, 'state', 0)
            if state in (0, 1):
                msx, msy = self.world_to_screen(m.x, m.z, cx, cz)
                detect_range = getattr(m, 'detect_range', 10.0)
                radius_px = int(detect_range * PIXELS_PER_UNIT)
                if radius_px > 0:
                    circle_surface = pygame.Surface(
                        (radius_px * 2, radius_px * 2), pygame.SRCALPHA)
                    pygame.draw.circle(circle_surface, DETECT_RANGE_COLOR,
                                       (radius_px, radius_px), radius_px)
                    self.screen.blit(circle_surface,
                                     (msx - radius_px, msy - radius_px))

        # ── Other players ──
        for pid, p in other_players.items():
            sx, sy = self.world_to_screen(p.x, p.z, cx, cz)
            self._draw_player(sx, sy, OTHER_BODY, OTHER_ARMOR, OTHER_SKIN,
                              is_local=False, anim_offset=pid * 0.7)
            if p.name:
                self._label(p.name, sx, sy - int(26 * CHAR_SCALE),
                            self.font_name, OTHER_BODY)
            self._draw_hp_bar(sx, sy, getattr(p, 'hp', 100),
                              getattr(p, 'max_hp', 100), HP_BAR_FG_OTHER,
                              y_offset=-int(30 * CHAR_SCALE))

        # ── Aggro lines (나를 타깃팅한 몬스터 → me, 빨간 선) ──
        my_guid = getattr(my_player, 'guid', 0)
        if my_guid:
            my_sx, my_sy = self.world_to_screen(cx, cz, cx, cz)
            for _gid, m in monsters.items():
                if getattr(m, 'target_guid', 0) == my_guid:
                    msx, msy = self.world_to_screen(m.x, m.z, cx, cz)
                    pygame.draw.line(self.screen, (220, 40, 40), (msx, msy), (my_sx, my_sy), 1)

        # ── Monsters ──
        for gid, m in monsters.items():
            sx, sy = self.world_to_screen(m.x, m.z, cx, cz)
            self._draw_monster(sx, sy, m)

            state = getattr(m, 'state', 0)
            self._draw_state_indicator(sx, sy, state)

            # HP bar (이름 라벨 위쪽에 배치)
            max_hp = getattr(m, 'max_hp', 0) or 0
            if max_hp > 0:
                hp = getattr(m, 'hp', max_hp)
                self._draw_hp_bar(sx, sy, hp, max_hp, MONSTER_HP_BAR_FG,
                                  y_offset=-int(30 * CHAR_SCALE), width=28)

            # Name + state tag
            name = getattr(m, 'name', '') or ''
            state_tag = _STATE_NAMES.get(state, '')
            label = f"{name}" if name else ""
            state_color = _STATE_COLORS.get(state, STATE_IDLE_COLOR)
            # HP 바 위로 이름 라벨 이동
            name_y = sy - int(40 * CHAR_SCALE)
            if label:
                self._label(label, sx, name_y, self.font_name, state_color)

        # ── Me (always on top) ──
        sx, sy = self.world_to_screen(cx, cz, cx, cz)
        self._draw_player(sx, sy, ME_BODY, ME_ARMOR, ME_SKIN, is_local=True)

        # Name above head
        name = getattr(my_player, 'name', '')
        if name:
            self._label(name, sx, sy - int(26 * CHAR_SCALE),
                        self.font_name, ME_BODY)

        # HP bar
        max_hp = getattr(my_player, 'max_hp', 100) or 100
        hp = getattr(my_player, 'hp', max_hp)
        self._draw_hp_bar(sx, sy, hp, max_hp, HP_BAR_FG,
                          y_offset=-int(30 * CHAR_SCALE))

        # ── Projectiles ──
        if projectiles:
            for pid, p in projectiles.items():
                psx, psy = self.world_to_screen(p.x, p.z, cx, cz)
                if p.kind == 0:   # Homing
                    color = (240, 220, 80)
                    glow = _pulse(8.0, 0.6, 1.0)
                    pygame.draw.circle(self.screen,
                                       (int(color[0] * glow), int(color[1] * glow), int(color[2] * glow)),
                                       (psx, psy), 6)
                    pygame.draw.circle(self.screen, (255, 255, 200), (psx, psy), 2)
                else:             # Skillshot
                    color = (200, 100, 240)
                    pygame.draw.circle(self.screen, color, (psx, psy), 5)
                    ex = psx + int(p.dir_x * 14)
                    ey = psy - int(p.dir_z * 14)
                    pygame.draw.line(self.screen, color, (psx, psy), (ex, ey), 2)

        # ── Hitscan lines ──
        if hitscan_lines:
            now_t = time.time()
            for (sx, sz, ex, ez, expire) in hitscan_lines:
                remaining = max(0.0, expire - now_t)
                alpha = int(255 * (remaining / 0.4))
                s_screen = self.world_to_screen(sx, sz, cx, cz)
                e_screen = self.world_to_screen(ex, ez, cx, cz)
                line_surf = pygame.Surface((self.width, self.height), pygame.SRCALPHA)
                pygame.draw.line(line_surf, (255, 60, 40, alpha),
                                 s_screen, e_screen, 2)
                # Impact flash at hit point
                if remaining > 0.2:
                    pygame.draw.circle(line_surf, (255, 200, 60, alpha),
                                       e_screen, 5)
                self.screen.blit(line_surf, (0, 0))

        # ── Status HUD ──
        self._draw_status(status_lines)
        pygame.display.flip()
        self.clock.tick(60)

    def _draw_status(self, lines):
        # Semi-transparent background for readability
        panel_h = len(lines) * 16 + 10
        panel = pygame.Surface((self.width, panel_h), pygame.SRCALPHA)
        panel.fill((0, 0, 0, 120))
        self.screen.blit(panel, (0, 0))

        y = 6
        for line in lines:
            surf = self.font.render(line, True, TEXT_COLOR)
            self.screen.blit(surf, (8, y))
            y += 16

    def close(self):
        pygame.quit()

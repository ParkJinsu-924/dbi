"""
몬스터/플레이어에 걸린 CC(행동 제약) 버프의 시각화.

확장 방법:
  1. effects.csv 에 cc_flag 컬럼에 새 문자열 추가 (예: "Fear").
  2. 아래에 _draw_fear(screen, sx, sy, t) 함수 추가.
  3. CC_VISUALIZERS 딕셔너리에 "Fear": _draw_fear 등록.

함수 시그니처는 **고정**이다 — renderer 쪽 디스패치 코드를 건드리지 않고
새 CC 를 추가할 수 있다.

  def _draw_xxx(screen, sx: int, sy: int, t: float) -> None

인자:
  screen: pygame.Surface (self.screen)
  sx, sy: 몬스터 중심 화면 좌표 (이미 camera shake 가 반영된 값)
  t:      이 CC 가 걸린 이후 경과 시간 (초). 애니메이션 위상용.
"""

import math

import pygame


# ── 색상 팔레트 (CC 별로 시그니처 컬러) ──────────────────────────────
STUN_STAR_COLOR  = (255, 220, 80)      # 노란 별
ROOT_CHAIN_COLOR = (180, 170, 150)     # 회색 쇠사슬
ROOT_CHAIN_DARK  = (100, 95, 80)
SLOW_WAVE_COLOR  = (120, 190, 255)     # 푸른 웨이브
SILENCE_COLOR    = (220, 80, 220)      # 보라 입막음


# ── 헬퍼 ─────────────────────────────────────────────────────────────
def _aa_circle(screen, color, pos, radius, width=0):
    """pygame.draw.circle 래퍼 — 알파 지원."""
    if len(color) == 4:
        surf = pygame.Surface((radius * 2 + 2, radius * 2 + 2), pygame.SRCALPHA)
        pygame.draw.circle(surf, color, (radius + 1, radius + 1), radius, width)
        screen.blit(surf, (pos[0] - radius - 1, pos[1] - radius - 1))
    else:
        pygame.draw.circle(screen, color, pos, radius, width)


def _draw_star(screen, cx: int, cy: int, size: int, color, points: int = 5):
    """작은 5각 별. Stun 시각화용."""
    outer = size
    inner = size * 0.5
    verts = []
    for i in range(points * 2):
        angle = -math.pi / 2 + i * math.pi / points
        r = outer if i % 2 == 0 else inner
        verts.append((cx + math.cos(angle) * r, cy + math.sin(angle) * r))
    pygame.draw.polygon(screen, color, verts)


# ── Stun: 머리 위 별들이 궤도를 돈다 ────────────────────────────────
def _draw_stun(screen, sx: int, sy: int, t: float) -> None:
    # 몬스터 머리 위 공중에서 3개의 별이 시계 방향으로 회전.
    head_y = sy - 26      # 머리 위 오프셋
    orbit_radius = 12
    orbit_speed = 3.0     # rad/s
    star_size = 4
    for i in range(3):
        phase = t * orbit_speed + i * (2 * math.pi / 3)
        x = sx + math.cos(phase) * orbit_radius
        # 위에서 봤을 때 약간 타원형으로 눌러 원근감 표현
        y = head_y + math.sin(phase) * (orbit_radius * 0.35)
        # 별의 크기를 위상에 따라 살짝 펄스
        s = int(star_size + math.sin(phase * 2) * 0.8)
        _draw_star(screen, int(x), int(y), max(2, s), STUN_STAR_COLOR)


# ── Root: 발 밑에 쇠사슬 고리가 원형으로 감긴다 ─────────────────────
def _draw_root(screen, sx: int, sy: int, t: float) -> None:
    # 지면 기준 원형으로 감긴 쇠사슬. 고리가 약간 펄스한다.
    ground_y = sy + 4
    base_radius = 18
    pulse = 1.0 + math.sin(t * 4.0) * 0.05
    radius = int(base_radius * pulse)
    # 납작한 타원을 그리기 위해 Surface 에 따로 그림
    ellipse_w = radius * 2
    ellipse_h = int(radius * 0.7)
    surf = pygame.Surface((ellipse_w + 4, ellipse_h + 4), pygame.SRCALPHA)
    # 바깥 어두운 고리
    pygame.draw.ellipse(surf, (*ROOT_CHAIN_DARK, 220),
                        (0, 0, ellipse_w + 4, ellipse_h + 4), width=3)
    # 밝은 고리
    pygame.draw.ellipse(surf, (*ROOT_CHAIN_COLOR, 255),
                        (2, 2, ellipse_w, ellipse_h), width=2)
    # 쇠사슬 "링크" 점들을 원 위에 찍어 체인 느낌
    link_count = 10
    for i in range(link_count):
        angle = (t * 1.2) + i * (2 * math.pi / link_count)
        lx = ellipse_w / 2 + 2 + math.cos(angle) * (ellipse_w / 2)
        ly = ellipse_h / 2 + 2 + math.sin(angle) * (ellipse_h / 2)
        pygame.draw.circle(surf, (*ROOT_CHAIN_COLOR, 255),
                           (int(lx), int(ly)), 3)
    screen.blit(surf, (sx - ellipse_w // 2 - 2, ground_y - ellipse_h // 2 - 2))


# ── Slow: 몬스터 위로 느리게 올라가는 푸른 웨이브 ───────────────────
def _draw_slow(screen, sx: int, sy: int, t: float) -> None:
    # 3개의 웨이브가 시간차로 위로 올라가며 페이드.
    for i in range(3):
        phase = (t * 0.9 + i / 3.0) % 1.0     # 0..1 로 정규화
        rise = int(phase * 30)                # 위로 0~30px 이동
        alpha = int(180 * (1.0 - phase))
        if alpha <= 0:
            continue
        radius = int(8 + phase * 6)
        y = sy - rise
        surf = pygame.Surface((radius * 2 + 2, radius + 2), pygame.SRCALPHA)
        # 반원형 웨이브 (아래쪽 반만)
        pygame.draw.arc(surf, (*SLOW_WAVE_COLOR, alpha),
                        (0, 0, radius * 2 + 2, (radius + 2) * 2),
                        math.pi, 2 * math.pi, 2)
        screen.blit(surf, (sx - radius - 1, y - 1))


# ── Silence: 머리 위에 보라색 "말풍선 취소" 아이콘 ──────────────────
def _draw_silence(screen, sx: int, sy: int, t: float) -> None:
    head_y = sy - 32
    # 작게 맥동하는 원 + 가운데 사선으로 "X"
    pulse = 1.0 + math.sin(t * 5.0) * 0.08
    r = int(7 * pulse)
    _aa_circle(screen, (*SILENCE_COLOR, 230), (sx, head_y), r, width=2)
    # 사선 (말하기 금지)
    off = int(r * 0.7)
    pygame.draw.line(screen, SILENCE_COLOR,
                     (sx - off, head_y - off), (sx + off, head_y + off), 2)


# ── Invulnerable: 몬스터 전체를 감싸는 노란 오라 ────────────────────
def _draw_invulnerable(screen, sx: int, sy: int, t: float) -> None:
    pulse = 1.0 + math.sin(t * 3.0) * 0.1
    r = int(22 * pulse)
    _aa_circle(screen, (255, 240, 120, 90), (sx, sy), r)
    _aa_circle(screen, (255, 240, 120, 200), (sx, sy), r, width=2)


# ── 레지스트리 ───────────────────────────────────────────────────────
# 새 CC 타입 추가 시 여기에만 추가하면 된다.
CC_VISUALIZERS: dict = {
    "Stun":          _draw_stun,
    "Root":          _draw_root,
    "Slow":          _draw_slow,
    "Silence":       _draw_silence,
    "Invulnerable":  _draw_invulnerable,
}


# ── 디스패처 ────────────────────────────────────────────────────────
def draw(screen, sx: int, sy: int, cc_flag: str, t: float) -> None:
    """해당 CC 시각화가 있으면 그린다. 없거나 "None" 이면 noop."""
    if not cc_flag or cc_flag == "None":
        return
    fn = CC_VISUALIZERS.get(cc_flag)
    if fn is not None:
        fn(screen, sx, sy, t)

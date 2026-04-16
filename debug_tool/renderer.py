"""
2D top-down renderer for the debug tool.

Draws:
  - Red circle: the local player (you)
  - Blue circles: other players
  - (Future) Squares: monsters/NPCs

Coordinate transform: world (x, z) on GameServer maps to screen (x, y) with
the local player centered. A fixed pixels-per-unit scale keeps distances readable.
"""

import pygame

BG_COLOR = (30, 30, 35)
GRID_COLOR = (55, 55, 65)
ME_COLOR = (220, 60, 60)
OTHER_COLOR = (60, 120, 220)
MONSTER_COLOR = (60, 200, 90)
MONSTER_CHASE_COLOR = (220, 180, 40)
MONSTER_ATTACK_COLOR = (220, 50, 50)
MONSTER_RETURN_COLOR = (120, 120, 120)
DETECT_RANGE_COLOR = (60, 200, 90, 80)   # semi-transparent green for detect circle
TEXT_COLOR = (220, 220, 220)
HP_BAR_BG = (80, 20, 20)
HP_BAR_FG = (220, 40, 40)

ME_RADIUS = 10
OTHER_RADIUS = 8
MONSTER_SIZE = 14
PIXELS_PER_UNIT = 20  # world 1 unit = 20 pixels on screen
GRID_STEP_UNITS = 5    # draw a grid line every 5 world units


class Renderer:
    def __init__(self, width=800, height=600, title="MMO Debug Tool"):
        pygame.init()
        self.screen = pygame.display.set_mode((width, height))
        pygame.display.set_caption(title)
        self.font = pygame.font.SysFont("consolas", 14)
        self.width = width
        self.height = height
        self.clock = pygame.time.Clock()

    def world_to_screen(self, wx: float, wz: float, cx: float, cz: float):
        """Convert world (x, z) to screen (px, py), centered on local player (cx, cz)."""
        # In this project, world ground plane uses X and Z (Y is up).
        # Screen: +x right, +y down. We map world +x -> screen +x, world +z -> screen -y.
        sx = self.width / 2 + (wx - cx) * PIXELS_PER_UNIT
        sy = self.height / 2 - (wz - cz) * PIXELS_PER_UNIT
        return int(sx), int(sy)

    def draw_grid(self, cx: float, cz: float):
        # Grid every GRID_STEP_UNITS world units
        step = GRID_STEP_UNITS * PIXELS_PER_UNIT
        # Align grid to world origin
        offset_x = int(self.width / 2 - cx * PIXELS_PER_UNIT) % step
        offset_y = int(self.height / 2 + cz * PIXELS_PER_UNIT) % step
        for x in range(offset_x, self.width, step):
            pygame.draw.line(self.screen, GRID_COLOR, (x, 0), (x, self.height))
        for y in range(offset_y, self.height, step):
            pygame.draw.line(self.screen, GRID_COLOR, (0, y), (self.width, y))

    def draw_frame(self, my_player, other_players: dict, monsters: dict, status_lines: list):
        self.screen.fill(BG_COLOR)

        if my_player is None:
            # Not in game yet: just status text
            self._draw_status(status_lines)
            pygame.display.flip()
            self.clock.tick(60)
            return

        cx, cz = my_player.x, my_player.z
        self.draw_grid(cx, cz)

        # Other players
        for pid, p in other_players.items():
            sx, sy = self.world_to_screen(p.x, p.z, cx, cz)
            pygame.draw.circle(self.screen, OTHER_COLOR, (sx, sy), OTHER_RADIUS)
            if p.name:
                self._label(p.name, sx, sy - OTHER_RADIUS - 4)

        # Monsters (squares, color reflects AI state)
        # MonsterStateId: 0=Idle, 1=Patrol, 2=Chase, 3=Attack, 4=Return
        _MONSTER_STATE_COLORS = {
            0: MONSTER_COLOR,         # Idle
            1: MONSTER_COLOR,         # Patrol (same color as Idle)
            2: MONSTER_CHASE_COLOR,   # Chase
            3: MONSTER_ATTACK_COLOR,  # Attack
            4: MONSTER_RETURN_COLOR,  # Return
        }
        _STATE_NAMES = {0: "Idle", 1: "Patrol", 2: "Chase", 3: "ATK", 4: "Return"}
        for gid, m in monsters.items():
            sx, sy = self.world_to_screen(m.x, m.z, cx, cz)
            half = MONSTER_SIZE // 2
            state = getattr(m, 'state', 0)
            color = _MONSTER_STATE_COLORS.get(state, MONSTER_COLOR)

            # Detect range circle (Idle/Patrol only)
            if state in (0, 1):
                detect_range = getattr(m, 'detect_range', 10.0)
                radius_px = int(detect_range * PIXELS_PER_UNIT)
                if radius_px > 0:
                    circle_surface = pygame.Surface((radius_px * 2, radius_px * 2), pygame.SRCALPHA)
                    pygame.draw.circle(circle_surface, DETECT_RANGE_COLOR,
                                       (radius_px, radius_px), radius_px)
                    self.screen.blit(circle_surface, (sx - radius_px, sy - radius_px))

            pygame.draw.rect(self.screen, color,
                             (sx - half, sy - half, MONSTER_SIZE, MONSTER_SIZE))
            state_tag = _STATE_NAMES.get(state, "")
            label = m.name if hasattr(m, 'name') and m.name else ""
            if state_tag:
                label = f"{label} [{state_tag}]" if label else state_tag
            if label:
                self._label(label, sx, sy - half - 4)

        # Me (always on top, center)
        sx, sy = self.world_to_screen(cx, cz, cx, cz)
        pygame.draw.circle(self.screen, ME_COLOR, (sx, sy), ME_RADIUS)

        # HP bar above local player
        max_hp = getattr(my_player, 'max_hp', 100) or 100
        hp = getattr(my_player, 'hp', max_hp)
        bar_w, bar_h = 40, 5
        bar_x = sx - bar_w // 2
        bar_y = sy - ME_RADIUS - 8
        pygame.draw.rect(self.screen, HP_BAR_BG, (bar_x, bar_y, bar_w, bar_h))
        fill_w = int(bar_w * max(0, hp) / max_hp)
        if fill_w > 0:
            pygame.draw.rect(self.screen, HP_BAR_FG, (bar_x, bar_y, fill_w, bar_h))

        self._draw_status(status_lines)
        pygame.display.flip()
        self.clock.tick(60)

    def _draw_status(self, lines):
        y = 6
        for line in lines:
            surf = self.font.render(line, True, TEXT_COLOR)
            self.screen.blit(surf, (8, y))
            y += 16

    def _label(self, text: str, x: int, y: int):
        surf = self.font.render(text, True, TEXT_COLOR)
        rect = surf.get_rect(center=(x, y))
        self.screen.blit(surf, rect)

    def close(self):
        pygame.quit()

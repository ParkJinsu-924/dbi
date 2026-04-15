"""
2D top-down renderer for the debug tool.

Draws:
  - Red circle: the local player (you)
  - Black circles: other players
  - (Future) Squares: monsters/NPCs

Coordinate transform: world (x, z) on GameServer maps to screen (x, y) with
the local player centered. A fixed pixels-per-unit scale keeps distances readable.
"""

import pygame

BG_COLOR = (30, 30, 35)
GRID_COLOR = (55, 55, 65)
ME_COLOR = (220, 60, 60)
OTHER_COLOR = (20, 20, 20)
MONSTER_COLOR = (60, 200, 90)
TEXT_COLOR = (220, 220, 220)

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

        # Monsters (squares)
        for gid, m in monsters.items():
            sx, sy = self.world_to_screen(m.x, m.z, cx, cz)
            half = MONSTER_SIZE // 2
            pygame.draw.rect(self.screen, MONSTER_COLOR,
                             (sx - half, sy - half, MONSTER_SIZE, MONSTER_SIZE))

        # Me (always on top, center)
        sx, sy = self.world_to_screen(cx, cz, cx, cz)
        pygame.draw.circle(self.screen, ME_COLOR, (sx, sy), ME_RADIUS)

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

"""
MMO Debug Tool - 2D top-down visualizer for server testing.

Flow:
  1. Connect to LoginServer (127.0.0.1:9999), send C_Login, receive S_Login with token
  2. Disconnect from LoginServer, connect to GameServer (ip/port from S_Login)
  3. Send C_EnterGame with token, receive S_EnterGame (player id, spawn pos) + S_PlayerList
  4. Main loop:
       - poll WASD input -> update local position -> send C_PlayerMove
       - process incoming S_* packets via PACKET_HANDLERS dispatch table
       - render frame

Usage:
    python client.py [username] [password]
    (defaults: "test" "test")

Adding a new incoming packet:
    1. Register the protobuf class in network.py (_MSG_CLASS_MAP)
    2. Write a _h_xxx(state, msg) handler below
    3. Add it to PACKET_HANDLERS
"""

import logging
import sys
import time
from dataclasses import dataclass, field

import common_pb2, login_pb2, game_pb2
import packet_ids
import config
import log_setup
from network import PacketClient
from renderer import Renderer

log_login = logging.getLogger("login")
log_game = logging.getLogger("game")
log_hit = logging.getLogger("hit")


@dataclass
class PlayerState:
    player_id: int = 0
    guid: int = 0        # 서버 GameObject GUID (Projectile target_guid 매칭용)
    name: str = ""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    tx: float = 0.0
    ty: float = 0.0
    tz: float = 0.0
    hp: int = 100
    max_hp: int = 100

    def set_target(self, x, y, z):
        self.tx, self.ty, self.tz = x, y, z

    def lerp(self, dt, speed=config.PLAYER_LERP_SPEED):
        t = min(1.0, speed * dt)
        self.x += (self.tx - self.x) * t
        self.y += (self.ty - self.y) * t
        self.z += (self.tz - self.z) * t


@dataclass
class MonsterState:
    guid: int = 0
    name: str = ""
    x: float = 0.0       # display position (interpolated)
    y: float = 0.0
    z: float = 0.0
    tx: float = 0.0      # target position (from server)
    ty: float = 0.0
    tz: float = 0.0
    state: int = 0        # 0=Idle, 1=Patrol, 2=Chase, 3=Attack, 4=Return
    target_guid: int = 0  # Chase/Attack target player GUID
    detect_range: float = 10.0

    def set_target(self, x, y, z):
        self.tx, self.ty, self.tz = x, y, z

    def lerp(self, dt, speed=config.MONSTER_LERP_SPEED):
        """Interpolate display position toward server target."""
        t = min(1.0, speed * dt)
        self.x += (self.tx - self.x) * t
        self.y += (self.ty - self.y) * t
        self.z += (self.tz - self.z) * t


@dataclass
class ProjectileState:
    guid: int = 0
    owner_guid: int = 0
    kind: int = 0          # 0=Homing, 1=Skillshot
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    speed: float = 10.0
    target_guid: int = 0
    dir_x: float = 0.0
    dir_z: float = 0.0
    radius: float = 0.3
    max_range: float = 0.0
    max_lifetime: float = 5.0
    spawned_at: float = 0.0
    traveled: float = 0.0


HitscanLine = tuple[float, float, float, float, float]  # (sx, sz, ex, ez, expire_time)


@dataclass
class GameState:
    """모든 핸들러/메인 루프가 공유하는 클라이언트 상태."""
    username: str = ""
    me: PlayerState = field(default_factory=PlayerState)
    others: dict[int, PlayerState] = field(default_factory=dict)
    monsters: dict[int, MonsterState] = field(default_factory=dict)
    projectiles: dict[int, ProjectileState] = field(default_factory=dict)
    hitscan_lines: list[HitscanLine] = field(default_factory=list)
    in_game: bool = False


def _projectile_target_pos(target_guid: int, state: GameState) -> tuple[float, float] | None:
    """Lookup the homing target's current position by server GUID."""
    if target_guid in state.monsters:
        m = state.monsters[target_guid]
        return (m.x, m.z)
    if state.me.guid != 0 and target_guid == state.me.guid:
        return (state.me.x, state.me.z)
    for p in state.others.values():
        if p.guid == target_guid:
            return (p.x, p.z)
    return None


def make_vec3(x: float, y: float, z: float) -> common_pb2.Vector3:
    v = common_pb2.Vector3()
    v.x, v.y, v.z = x, y, z
    return v


# ═══════════════════════════════════════════════════════════════════
#  Packet handlers — 각 핸들러는 (state, msg) 시그니처로 통일
# ═══════════════════════════════════════════════════════════════════

def _h_enter_game(state: GameState, msg):
    state.me.player_id = msg.player_id
    state.me.guid = msg.guid
    state.me.x = msg.spawn_position.x
    state.me.y = msg.spawn_position.y
    state.me.z = msg.spawn_position.z
    state.in_game = True
    log_game.info("entered as playerId=%d guid=%d at (%.1f, %.1f)",
                  state.me.player_id, state.me.guid, state.me.x, state.me.z)


def _h_player_list(state: GameState, msg):
    for p in msg.players:
        if p.player_id == state.me.player_id:
            continue
        px, py, pz = p.position.x, p.position.y, p.position.z
        ps = state.others.setdefault(p.player_id, PlayerState(player_id=p.player_id))
        ps.name = p.name
        ps.guid = p.guid
        ps.x, ps.y, ps.z = px, py, pz
        ps.tx, ps.ty, ps.tz = px, py, pz


def _h_player_move(state: GameState, msg):
    if msg.player_id == state.me.player_id:
        return
    ps = state.others.setdefault(msg.player_id, PlayerState(player_id=msg.player_id))
    ps.set_target(msg.position.x, msg.position.y, msg.position.z)


def _h_player_spawn(state: GameState, msg):
    if msg.player_id == state.me.player_id:
        return
    px, py, pz = msg.position.x, msg.position.y, msg.position.z
    ps = state.others.setdefault(msg.player_id, PlayerState(player_id=msg.player_id))
    ps.name = msg.name
    ps.guid = msg.guid
    ps.x, ps.y, ps.z = px, py, pz
    ps.tx, ps.ty, ps.tz = px, py, pz
    log_game.info("player joined: id=%d name=%s", msg.player_id, msg.name)


def _h_player_leave(state: GameState, msg):
    state.others.pop(msg.player_id, None)


def _h_move_correction(state: GameState, msg):
    state.me.x = msg.position.x
    state.me.y = msg.position.y
    state.me.z = msg.position.z


def _h_error(state: GameState, msg):
    log_game.error("server error: code=%d src_pkt=%d", msg.code, msg.source_packet_id)


def _h_monster_list(state: GameState, msg):
    for m in msg.monsters:
        px, py, pz = m.position.x, m.position.y, m.position.z
        state.monsters[m.guid] = MonsterState(
            guid=m.guid, name=m.name,
            x=px, y=py, z=pz, tx=px, ty=py, tz=pz,
            detect_range=m.detect_range if m.detect_range > 0 else 10.0)


def _h_monster_spawn(state: GameState, msg):
    px, py, pz = msg.position.x, msg.position.y, msg.position.z
    state.monsters[msg.guid] = MonsterState(
        guid=msg.guid, name=msg.name,
        x=px, y=py, z=pz, tx=px, ty=py, tz=pz,
        detect_range=msg.detect_range if msg.detect_range > 0 else 10.0)


def _h_monster_move(state: GameState, msg):
    ms = state.monsters.get(msg.guid)
    if ms:
        ms.set_target(msg.position.x, msg.position.y, msg.position.z)


def _h_monster_despawn(state: GameState, msg):
    state.monsters.pop(msg.guid, None)


def _h_monster_state(state: GameState, msg):
    ms = state.monsters.get(msg.guid)
    if ms:
        ms.state = msg.state
        ms.target_guid = msg.target_guid


def _h_monster_attack(state: GameState, msg):
    ms = state.monsters.get(msg.monster_guid)
    if ms:
        log_game.info("%s attacks player (dmg=%d)", ms.name, msg.damage)


def _h_hitscan_attack(state: GameState, msg):
    state.hitscan_lines.append((
        msg.start_position.x, msg.start_position.z,
        msg.hit_position.x, msg.hit_position.z,
        time.time() + config.HITSCAN_LINE_LIFETIME))
    ms = state.monsters.get(msg.attacker_guid)
    name = ms.name if ms else "?"
    log_game.info("%s hitscan -> player (dmg=%d)", name, msg.damage)


def _h_player_hp(state: GameState, msg):
    if msg.guid == state.me.guid or msg.guid == 0:
        state.me.hp = msg.hp
        state.me.max_hp = msg.max_hp
        return
    for p in state.others.values():
        if p.guid == msg.guid:
            p.hp = msg.hp
            p.max_hp = msg.max_hp
            break


def _h_projectile_spawn(state: GameState, msg):
    state.projectiles[msg.guid] = ProjectileState(
        guid=msg.guid,
        owner_guid=msg.owner_guid,
        kind=msg.kind,
        x=msg.start_pos.x, y=msg.start_pos.y, z=msg.start_pos.z,
        speed=msg.speed,
        target_guid=msg.target_guid,
        dir_x=msg.dir.x, dir_z=msg.dir.z,
        radius=msg.radius if msg.radius > 0 else 0.3,
        max_range=msg.max_range,
        max_lifetime=msg.max_lifetime if msg.max_lifetime > 0 else 5.0,
        spawned_at=time.time())


def _h_projectile_hit(state: GameState, msg):
    state.projectiles.pop(msg.projectile_guid, None)
    if msg.target_guid in state.monsters:
        log_hit.info("%s took %d dmg", state.monsters[msg.target_guid].name, msg.damage)
        return
    if msg.target_guid == state.me.guid:
        log_hit.info("you took %d dmg", msg.damage)
        return
    other = next((p for p in state.others.values() if p.guid == msg.target_guid), None)
    tag = other.name if other else f"guid={msg.target_guid}"
    log_hit.info("%s took %d dmg", tag, msg.damage)


def _h_projectile_destroy(state: GameState, msg):
    state.projectiles.pop(msg.projectile_guid, None)


PACKET_HANDLERS = {
    packet_ids.S_ENTER_GAME:         _h_enter_game,
    packet_ids.S_PLAYER_LIST:        _h_player_list,
    packet_ids.S_PLAYER_MOVE:        _h_player_move,
    packet_ids.S_PLAYER_SPAWN:       _h_player_spawn,
    packet_ids.S_PLAYER_LEAVE:       _h_player_leave,
    packet_ids.S_MOVE_CORRECTION:    _h_move_correction,
    packet_ids.S_ERROR:              _h_error,
    packet_ids.S_MONSTER_LIST:       _h_monster_list,
    packet_ids.S_MONSTER_SPAWN:      _h_monster_spawn,
    packet_ids.S_MONSTER_MOVE:       _h_monster_move,
    packet_ids.S_MONSTER_DESPAWN:    _h_monster_despawn,
    packet_ids.S_MONSTER_STATE:      _h_monster_state,
    packet_ids.S_MONSTER_ATTACK:     _h_monster_attack,
    packet_ids.S_HITSCAN_ATTACK:     _h_hitscan_attack,
    packet_ids.S_PLAYER_HP:          _h_player_hp,
    packet_ids.S_PROJECTILE_SPAWN:   _h_projectile_spawn,
    packet_ids.S_PROJECTILE_HIT:     _h_projectile_hit,
    packet_ids.S_PROJECTILE_DESTROY: _h_projectile_destroy,
}


# ═══════════════════════════════════════════════════════════════════
#  Login phase
# ═══════════════════════════════════════════════════════════════════

def do_login(username: str, password: str) -> login_pb2.S_Login | None:
    """LoginServer에 접속해 S_Login(토큰 + GameServer 주소) 응답을 받아 반환."""
    client = PacketClient()
    if not client.connect(config.LOGIN_HOST, config.LOGIN_PORT):
        return None

    login = login_pb2.C_Login()
    login.username = username
    login.password = password
    client.send(login)

    deadline = time.time() + config.LOGIN_RESPONSE_TIMEOUT
    result = None
    while time.time() < deadline:
        for pkt_id, msg in client.poll():
            if pkt_id is None:
                log_login.error("disconnected before receiving token")
                client.close()
                return None
            if pkt_id == packet_ids.S_LOGIN:
                result = msg
                break
            if pkt_id == packet_ids.S_ERROR:
                log_login.error("server error: code=%d src_pkt=%d",
                                msg.code, msg.source_packet_id)
                client.close()
                return None
        if result:
            break
        time.sleep(0.02)

    client.close()
    return result


# ═══════════════════════════════════════════════════════════════════
#  Main game loop
# ═══════════════════════════════════════════════════════════════════

def _process_input(pygame, keys, state: GameState, client, io):
    """WASD 이동 + Q/E 스킬. io는 dict로 프레임 간 타이밍/방향을 유지."""
    if not state.in_game:
        return

    dx = dz = 0.0
    if keys[pygame.K_w] or keys[pygame.K_UP]:    dz += 1
    if keys[pygame.K_s] or keys[pygame.K_DOWN]:  dz -= 1
    if keys[pygame.K_d] or keys[pygame.K_RIGHT]: dx += 1
    if keys[pygame.K_a] or keys[pygame.K_LEFT]:  dx -= 1
    if dx != 0 or dz != 0:
        norm = (dx * dx + dz * dz) ** 0.5
        dx /= norm; dz /= norm
        io["last_dir_x"], io["last_dir_z"] = dx, dz   # remember facing for skillshot
        dt_frame = 1 / config.TARGET_FPS
        state.me.x += dx * config.MOVE_SPEED * dt_frame
        state.me.z += dz * config.MOVE_SPEED * dt_frame
        io["pending_move"] = True

    now = time.time()
    if io["pending_move"] and now - io["last_send"] >= config.SEND_INTERVAL:
        move = game_pb2.C_PlayerMove()
        move.position.CopyFrom(make_vec3(state.me.x, state.me.y, state.me.z))
        move.yaw = 0.0
        client.send(move)
        io["last_send"] = now
        io["pending_move"] = False

    # Q = Skillshot (last move dir), E = Homing (server picks nearest target)
    if keys[pygame.K_q] and now - io["last_skill_send"] >= config.SKILL_THROTTLE:
        req = game_pb2.C_RequestUseSkill()
        req.skill_name = "bolt"
        req.dir.x = io["last_dir_x"]
        req.dir.y = 0.0
        req.dir.z = io["last_dir_z"]
        req.target_guid = 0
        client.send(req)
        io["last_skill_send"] = now
    elif keys[pygame.K_e] and now - io["last_skill_send"] >= config.SKILL_THROTTLE:
        req = game_pb2.C_RequestUseSkill()
        req.skill_name = "auto_attack"
        req.dir.x = 0.0
        req.dir.y = 0.0
        req.dir.z = 0.0
        req.target_guid = 0   # 서버가 가장 가까운 적 자동 선택
        client.send(req)
        io["last_skill_send"] = now


def _simulate_projectiles(state: GameState, dt_frame: float, now_t: float) -> None:
    """서버는 히트 판정을 담당하고, 클라는 시각적 위치만 예측 이동시킨다."""
    expired = []
    for pid, p in state.projectiles.items():
        step = p.speed * dt_frame
        if p.kind == 1:   # Skillshot
            p.x += p.dir_x * step
            p.z += p.dir_z * step
            p.traveled += step
            if p.max_range > 0 and p.traveled >= p.max_range:
                expired.append(pid)
        else:             # Homing
            target = _projectile_target_pos(p.target_guid, state)
            if target is None:
                expired.append(pid)
                continue
            tx, tz = target
            ddx = tx - p.x
            ddz = tz - p.z
            d = (ddx * ddx + ddz * ddz) ** 0.5
            if d > 1e-3:
                p.x += ddx / d * step
                p.z += ddz / d * step
            if now_t - p.spawned_at > p.max_lifetime + config.PROJECTILE_GRACE_LIFETIME:
                expired.append(pid)
    for pid in expired:
        state.projectiles.pop(pid, None)


def run_game(token: str, game_host: str, game_port: int, username: str) -> None:
    """GameServer에 접속해 pygame 루프를 돌며 렌더링한다."""
    client = PacketClient()
    if not client.connect(game_host, game_port):
        log_game.error("cannot connect to %s:%d", game_host, game_port)
        return

    renderer = Renderer(title=f"MMO Debug - {username}")

    enter = game_pb2.C_EnterGame()
    enter.token = token
    client.send(enter)

    state = GameState(username=username, me=PlayerState(name=username))
    io = {
        "pending_move": False,
        "last_send": 0.0,
        "last_dir_x": 0.0,
        "last_dir_z": 1.0,         # default forward = +Z
        "last_skill_send": 0.0,
    }

    import pygame

    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False

        keys = pygame.key.get_pressed()
        _process_input(pygame, keys, state, client, io)

        for pkt_id, msg in client.poll():
            if pkt_id is None:
                log_game.warning("disconnected")
                running = False
                break
            handler = PACKET_HANDLERS.get(pkt_id)
            if handler is None:
                log_game.warning("unhandled packet id=%d", pkt_id)
                continue
            handler(state, msg)

        dt_frame = 1 / config.TARGET_FPS
        for ps in state.others.values():
            ps.lerp(dt_frame)
        for ms in state.monsters.values():
            ms.lerp(dt_frame)

        now_t = time.time()
        _simulate_projectiles(state, dt_frame, now_t)
        state.hitscan_lines = [h for h in state.hitscan_lines if h[4] > now_t]

        status = [
            f"user={username}  id={state.me.player_id}  "
            f"pos=({state.me.x:.1f}, {state.me.z:.1f})  HP={state.me.hp}/{state.me.max_hp}",
            f"others={len(state.others)}  monsters={len(state.monsters)}  "
            f"proj={len(state.projectiles)}  {'in-game' if state.in_game else 'entering...'}",
            "WASD=move  Q=skillshot(bolt)  E=homing(auto)  ESC=quit",
        ]
        if keys[pygame.K_ESCAPE]:
            running = False
        renderer.draw_frame(
            state.me if state.in_game else None,
            state.others, state.monsters, status,
            hitscan_lines=state.hitscan_lines, projectiles=state.projectiles)

    renderer.close()
    client.close()


# ═══════════════════════════════════════════════════════════════════
#  Entry point
# ═══════════════════════════════════════════════════════════════════

def main() -> None:
    log_setup.setup_logging()

    username = sys.argv[1] if len(sys.argv) > 1 else "test"
    password = sys.argv[2] if len(sys.argv) > 2 else "test"

    log_login.info("connecting to %s:%d as %s...",
                   config.LOGIN_HOST, config.LOGIN_PORT, username)
    s_login = do_login(username, password)
    if s_login is None:
        log_login.error("failed")
        return

    log_login.info("got token=%s -> %s:%d",
                   s_login.token, s_login.game_server_ip, s_login.game_server_port)
    run_game(s_login.token, s_login.game_server_ip, s_login.game_server_port, username)


if __name__ == "__main__":
    main()

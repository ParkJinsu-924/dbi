"""
MMO Debug Tool - 2D top-down visualizer for server testing.

Flow:
  1. Connect to LoginServer (127.0.0.1:9999), send C_Login, receive S_Login with token
  2. Disconnect from LoginServer, connect to GameServer (ip/port from S_Login)
  3. Send C_EnterGame with token, receive S_EnterGame (player id, spawn pos) + S_PlayerList
  4. Main loop:
       - poll WASD input -> update local position -> send C_PlayerMove
       - process incoming S_PlayerMove / S_PlayerLeave / S_MoveCorrection / ...
       - render frame

Usage:
    python client.py [username] [password]
    (defaults: "test" "test")
"""

import sys
import time
from dataclasses import dataclass

import common_pb2, login_pb2, game_pb2
import packet_ids
from network import PacketClient
from renderer import Renderer


MOVE_SPEED = 5.0    # world units per second
SEND_INTERVAL = 0.05  # 20 Hz move packets

LOGIN_HOST = "127.0.0.1"
LOGIN_PORT = 9999


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

    def lerp(self, dt, speed=15.0):
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

    def lerp(self, dt, speed=12.0):
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


def _projectile_target_pos(target_guid: int, me, others: dict, monsters: dict):
    """Lookup the homing target's current position by server GUID."""
    if target_guid in monsters:
        m = monsters[target_guid]
        return (m.x, m.z)
    if me.guid != 0 and target_guid == me.guid:
        return (me.x, me.z)
    for p in others.values():
        if p.guid == target_guid:
            return (p.x, p.z)
    return None


def make_vec3(x, y, z):
    v = common_pb2.Vector3()
    v.x, v.y, v.z = x, y, z
    return v


# ------------- Login phase -------------
def do_login(username: str, password: str):
    client = PacketClient()
    if not client.connect(LOGIN_HOST, LOGIN_PORT):
        return None

    login = login_pb2.C_Login()
    login.username = username
    login.password = password
    client.send(login)

    # Wait up to 5 seconds for S_Login
    deadline = time.time() + 5.0
    result = None
    while time.time() < deadline:
        for pkt_id, msg in client.poll():
            if pkt_id is None:
                print("[login] disconnected before receiving token")
                client.close()
                return None
            if pkt_id == packet_ids.S_LOGIN:
                result = msg
                break
            if pkt_id == packet_ids.S_ERROR:
                print(f"[login] server error: code={msg.code} src_pkt={msg.source_packet_id}")
                client.close()
                return None
        if result:
            break
        time.sleep(0.02)

    client.close()
    return result


# ------------- Main game loop -------------
def run_game(token: str, game_host: str, game_port: int, username: str):
    client = PacketClient()
    if not client.connect(game_host, game_port):
        print(f"[game] cannot connect to {game_host}:{game_port}")
        return

    renderer = Renderer(title=f"MMO Debug - {username}")

    # Send C_EnterGame
    enter = game_pb2.C_EnterGame()
    enter.token = token
    client.send(enter)

    me = PlayerState(name=username)
    others: dict[int, PlayerState] = {}
    monsters: dict[int, MonsterState] = {}
    projectiles: dict[int, ProjectileState] = {}
    hitscan_lines: list = []  # [(sx, sz, ex, ez, expire_time), ...]
    in_game = False
    pending_move = False
    last_send = 0.0
    last_dir_x = 0.0
    last_dir_z = 1.0           # default forward = +Z
    last_skill_send = 0.0      # throttle skill key
    SKILL_THROTTLE = 0.25      # client-side rate limit (server still has cooldown)

    import pygame

    running = True
    while running:
        # pygame events
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False

        # input -> move
        if in_game:
            keys = pygame.key.get_pressed()
            dx = dz = 0.0
            if keys[pygame.K_w] or keys[pygame.K_UP]:    dz += 1
            if keys[pygame.K_s] or keys[pygame.K_DOWN]:  dz -= 1
            if keys[pygame.K_d] or keys[pygame.K_RIGHT]: dx += 1
            if keys[pygame.K_a] or keys[pygame.K_LEFT]:  dx -= 1
            if dx != 0 or dz != 0:
                norm = (dx * dx + dz * dz) ** 0.5
                dx /= norm; dz /= norm
                last_dir_x, last_dir_z = dx, dz   # remember facing for skillshot
                dt_frame = 1 / 60.0
                me.x += dx * MOVE_SPEED * dt_frame
                me.z += dz * MOVE_SPEED * dt_frame
                pending_move = True

            # Throttle C_PlayerMove to SEND_INTERVAL
            now = time.time()
            if pending_move and now - last_send >= SEND_INTERVAL:
                move = game_pb2.C_PlayerMove()
                move.position.CopyFrom(make_vec3(me.x, me.y, me.z))
                move.yaw = 0.0
                client.send(move)
                last_send = now
                pending_move = False

            # Skill keys: Q = Skillshot (last move dir), E = Homing (auto-target nearest monster)
            if keys[pygame.K_q] and now - last_skill_send >= SKILL_THROTTLE:
                req = game_pb2.C_RequestUseSkill()
                req.skill_name = "bolt"
                req.dir.x = last_dir_x
                req.dir.y = 0.0
                req.dir.z = last_dir_z
                req.target_guid = 0
                client.send(req)
                last_skill_send = now
            elif keys[pygame.K_e] and now - last_skill_send >= SKILL_THROTTLE:
                req = game_pb2.C_RequestUseSkill()
                req.skill_name = "auto_attack"
                req.dir.x = 0.0
                req.dir.y = 0.0
                req.dir.z = 0.0
                req.target_guid = 0   # 서버가 가장 가까운 적 자동 선택
                client.send(req)
                last_skill_send = now

        # incoming packets
        for pkt_id, msg in client.poll():
            if pkt_id is None:
                print("[game] disconnected")
                running = False
                break

            if pkt_id == packet_ids.S_ENTER_GAME:
                me.player_id = msg.player_id
                me.guid = msg.guid
                me.x = msg.spawn_position.x
                me.y = msg.spawn_position.y
                me.z = msg.spawn_position.z
                in_game = True
                print(f"[game] entered as playerId={me.player_id} guid={me.guid} at ({me.x:.1f}, {me.z:.1f})")

            elif pkt_id == packet_ids.S_PLAYER_LIST:
                for p in msg.players:
                    if p.player_id == me.player_id:
                        continue
                    px, py, pz = p.position.x, p.position.y, p.position.z
                    ps = others.setdefault(p.player_id, PlayerState(player_id=p.player_id))
                    ps.name = p.name
                    ps.guid = p.guid
                    ps.x, ps.y, ps.z = px, py, pz
                    ps.tx, ps.ty, ps.tz = px, py, pz

            elif pkt_id == packet_ids.S_PLAYER_MOVE:
                if msg.player_id == me.player_id:
                    continue
                ps = others.setdefault(msg.player_id, PlayerState(player_id=msg.player_id))
                ps.set_target(msg.position.x, msg.position.y, msg.position.z)

            elif pkt_id == packet_ids.S_PLAYER_SPAWN:
                if msg.player_id != me.player_id:
                    px, py, pz = msg.position.x, msg.position.y, msg.position.z
                    ps = others.setdefault(msg.player_id,
                                           PlayerState(player_id=msg.player_id))
                    ps.name = msg.name
                    ps.guid = msg.guid
                    ps.x, ps.y, ps.z = px, py, pz
                    ps.tx, ps.ty, ps.tz = px, py, pz
                    print(f"[game] player joined: id={msg.player_id} name={msg.name}")

            elif pkt_id == packet_ids.S_PLAYER_LEAVE:
                others.pop(msg.player_id, None)

            elif pkt_id == packet_ids.S_MOVE_CORRECTION:
                me.x = msg.position.x
                me.y = msg.position.y
                me.z = msg.position.z

            elif pkt_id == packet_ids.S_ERROR:
                print(f"[game] server error: code={msg.code} src_pkt={msg.source_packet_id}")

            # --- Monster packets ---
            elif pkt_id == packet_ids.S_MONSTER_LIST:
                for m in msg.monsters:
                    px, py, pz = m.position.x, m.position.y, m.position.z
                    monsters[m.guid] = MonsterState(
                        guid=m.guid, name=m.name,
                        x=px, y=py, z=pz, tx=px, ty=py, tz=pz,
                        detect_range=m.detect_range if m.detect_range > 0 else 10.0)

            elif pkt_id == packet_ids.S_MONSTER_SPAWN:
                px, py, pz = msg.position.x, msg.position.y, msg.position.z
                monsters[msg.guid] = MonsterState(
                    guid=msg.guid, name=msg.name,
                    x=px, y=py, z=pz, tx=px, ty=py, tz=pz,
                    detect_range=msg.detect_range if msg.detect_range > 0 else 10.0)

            elif pkt_id == packet_ids.S_MONSTER_MOVE:
                ms = monsters.get(msg.guid)
                if ms:
                    ms.set_target(msg.position.x, msg.position.y, msg.position.z)

            elif pkt_id == packet_ids.S_MONSTER_DESPAWN:
                monsters.pop(msg.guid, None)

            elif pkt_id == packet_ids.S_MONSTER_STATE:
                ms = monsters.get(msg.guid)
                if ms:
                    ms.state = msg.state
                    ms.target_guid = msg.target_guid

            elif pkt_id == packet_ids.S_MONSTER_ATTACK:
                ms = monsters.get(msg.monster_guid)
                if ms:
                    print(f"[game] {ms.name} attacks player (dmg={msg.damage})")

            elif pkt_id == packet_ids.S_HITSCAN_ATTACK:
                hitscan_lines.append((
                    msg.start_position.x, msg.start_position.z,
                    msg.hit_position.x, msg.hit_position.z,
                    time.time() + 0.4))
                ms = monsters.get(msg.attacker_guid)
                name = ms.name if ms else "?"
                print(f"[game] {name} hitscan -> player (dmg={msg.damage})")

            elif pkt_id == packet_ids.S_PLAYER_HP:
                me.hp = msg.hp
                me.max_hp = msg.max_hp

            # --- Projectile packets ---
            elif pkt_id == packet_ids.S_PROJECTILE_SPAWN:
                projectiles[msg.guid] = ProjectileState(
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

            elif pkt_id == packet_ids.S_PROJECTILE_HIT:
                projectiles.pop(msg.projectile_guid, None)
                if msg.target_guid in monsters:
                    print(f"[hit] {monsters[msg.target_guid].name} took {msg.damage} dmg")
                elif msg.target_guid == me.guid:
                    print(f"[hit] you took {msg.damage} dmg")
                else:
                    other = next((p for p in others.values() if p.guid == msg.target_guid), None)
                    tag = other.name if other else f"guid={msg.target_guid}"
                    print(f"[hit] {tag} took {msg.damage} dmg")

            elif pkt_id == packet_ids.S_PROJECTILE_DESTROY:
                projectiles.pop(msg.projectile_guid, None)

        # interpolate positions
        dt_frame = 1 / 60.0
        for ps in others.values():
            ps.lerp(dt_frame)
        for ms in monsters.values():
            ms.lerp(dt_frame)

        # simulate projectiles deterministically (server is authoritative on hit)
        now_t = time.time()
        expired_proj = []
        for pid, p in projectiles.items():
            step = p.speed * dt_frame
            if p.kind == 1:   # Skillshot
                p.x += p.dir_x * step
                p.z += p.dir_z * step
                p.traveled += step
                if p.max_range > 0 and p.traveled >= p.max_range:
                    expired_proj.append(pid)
            else:             # Homing
                target = _projectile_target_pos(p.target_guid, me, others, monsters)
                if target is None:
                    expired_proj.append(pid)
                else:
                    tx, tz = target
                    ddx = tx - p.x
                    ddz = tz - p.z
                    d = (ddx * ddx + ddz * ddz) ** 0.5
                    if d > 1e-3:
                        p.x += ddx / d * step
                        p.z += ddz / d * step
                    if now_t - p.spawned_at > p.max_lifetime + 0.5:
                        expired_proj.append(pid)
        for pid in expired_proj:
            projectiles.pop(pid, None)

        # expire old hitscan lines
        hitscan_lines = [h for h in hitscan_lines if h[4] > now_t]

        # render
        status = [
            f"user={username}  id={me.player_id}  pos=({me.x:.1f}, {me.z:.1f})  HP={me.hp}/{me.max_hp}",
            f"others={len(others)}  monsters={len(monsters)}  proj={len(projectiles)}  {'in-game' if in_game else 'entering...'}",
            "WASD=move  Q=skillshot(bolt)  E=homing(auto)  ESC=quit",
        ]
        keys = pygame.key.get_pressed()
        if keys[pygame.K_ESCAPE]:
            running = False
        renderer.draw_frame(me if in_game else None, others, monsters, status,
                            hitscan_lines=hitscan_lines, projectiles=projectiles)

    renderer.close()
    client.close()


# ------------- Entry point -------------
def main():
    username = sys.argv[1] if len(sys.argv) > 1 else "test"
    password = sys.argv[2] if len(sys.argv) > 2 else "test"

    print(f"[login] connecting to {LOGIN_HOST}:{LOGIN_PORT} as {username}...")
    s_login = do_login(username, password)
    if s_login is None:
        print("[login] failed")
        return

    print(f"[login] got token={s_login.token} -> {s_login.game_server_ip}:{s_login.game_server_port}")
    run_game(s_login.token, s_login.game_server_ip, s_login.game_server_port, username)


if __name__ == "__main__":
    main()

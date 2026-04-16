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
    name: str = ""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    hp: int = 100
    max_hp: int = 100


@dataclass
class MonsterState:
    guid: int = 0
    name: str = ""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    state: int = 0        # 0=Idle, 1=Patrol, 2=Chase, 3=Attack, 4=Return
    target_guid: int = 0  # Chase/Attack target player GUID
    detect_range: float = 10.0


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
    in_game = False
    pending_move = False
    last_send = 0.0

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

        # incoming packets
        for pkt_id, msg in client.poll():
            if pkt_id is None:
                print("[game] disconnected")
                running = False
                break

            if pkt_id == packet_ids.S_ENTER_GAME:
                me.player_id = msg.player_id
                me.x = msg.spawn_position.x
                me.y = msg.spawn_position.y
                me.z = msg.spawn_position.z
                in_game = True
                print(f"[game] entered as playerId={me.player_id} at ({me.x:.1f}, {me.z:.1f})")

            elif pkt_id == packet_ids.S_PLAYER_LIST:
                for p in msg.players:
                    if p.player_id == me.player_id:
                        continue
                    ps = others.setdefault(p.player_id, PlayerState(player_id=p.player_id))
                    ps.name = p.name
                    ps.x, ps.y, ps.z = p.position.x, p.position.y, p.position.z

            elif pkt_id == packet_ids.S_PLAYER_MOVE:
                if msg.player_id == me.player_id:
                    continue
                ps = others.setdefault(msg.player_id, PlayerState(player_id=msg.player_id))
                ps.x, ps.y, ps.z = msg.position.x, msg.position.y, msg.position.z

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
                    monsters[m.guid] = MonsterState(
                        guid=m.guid, name=m.name,
                        x=m.position.x, y=m.position.y, z=m.position.z)

            elif pkt_id == packet_ids.S_MONSTER_SPAWN:
                monsters[msg.guid] = MonsterState(
                    guid=msg.guid, name=msg.name,
                    x=msg.position.x, y=msg.position.y, z=msg.position.z)

            elif pkt_id == packet_ids.S_MONSTER_MOVE:
                ms = monsters.get(msg.guid)
                if ms:
                    ms.x, ms.y, ms.z = msg.position.x, msg.position.y, msg.position.z

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

            elif pkt_id == packet_ids.S_PLAYER_HP:
                me.hp = msg.hp
                me.max_hp = msg.max_hp

        # render
        status = [
            f"user={username}  id={me.player_id}  pos=({me.x:.1f}, {me.z:.1f})  HP={me.hp}/{me.max_hp}",
            f"others={len(others)}  monsters={len(monsters)}  {'in-game' if in_game else 'entering...'}",
            "WASD to move,  ESC to quit",
        ]
        keys = pygame.key.get_pressed()
        if keys[pygame.K_ESCAPE]:
            running = False
        renderer.draw_frame(me if in_game else None, others, monsters, status)

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

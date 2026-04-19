"""
MMO Debug Tool - LoL-style 2D top-down visualizer.

Flow:
  1. Connect to LoginServer (127.0.0.1:9999), send C_Login, receive S_Login with token
  2. Disconnect from LoginServer, connect to GameServer (ip/port from S_Login)
  3. Send C_EnterGame with token, receive S_EnterGame (player id, spawn pos) + S_PlayerList
  4. Main loop:
       - 우클릭 → C_MoveCommand 송신 + 로컬 예측 이동
       - S 키 → C_StopMove
       - QWER → _cast_skill (타게팅 방식별로 dir/target_guid/target_pos 결정)
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
import math
import sys
import time
from dataclasses import dataclass, field

import common_pb2, login_pb2, game_pb2
import packet_ids
import config
import log_setup
import skill_data
from network import PacketClient
from renderer import Renderer

log_login = logging.getLogger("login")
log_game = logging.getLogger("game")
log_hit = logging.getLogger("hit")

# 시작 시 1회 CSV 로드. 이후 조회만 한다 (테스트에서는 임시 table로 교체 가능).
SKILL_TABLE: skill_data.SkillTable = skill_data.load_from_csv()


@dataclass
class PlayerState:
    player_id: int = 0
    guid: int = 0        # 서버 GameObject GUID (Projectile target_guid 매칭용)
    name: str = ""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    tx: float = 0.0      # 다른 플레이어 보간 타깃
    ty: float = 0.0
    tz: float = 0.0
    hp: int = 100
    max_hp: int = 100
    # Click-to-move 전용 (me 에만 의미 있음)
    destination_x: float = 0.0
    destination_z: float = 0.0
    is_moving: bool = False

    def set_target(self, x, y, z):
        """다른 플레이어 위치 교정용 보간 타깃 설정."""
        self.tx, self.ty, self.tz = x, y, z

    def lerp(self, dt, speed=config.PLAYER_LERP_SPEED):
        """다른 플레이어(보간 기반) 전용 업데이트."""
        t = min(1.0, speed * dt)
        self.x += (self.tx - self.x) * t
        self.y += (self.ty - self.y) * t
        self.z += (self.tz - self.z) * t

    def set_destination(self, wx: float, wz: float):
        """자기 캐릭터 클릭 이동 목적지 설정."""
        self.destination_x = wx
        self.destination_z = wz
        self.is_moving = True

    def move_toward_destination(self, dt: float):
        """자기 캐릭터 이동 시뮬 (서버와 동일 규칙)."""
        if not self.is_moving:
            return
        dx = self.destination_x - self.x
        dz = self.destination_z - self.z
        dist = math.sqrt(dx * dx + dz * dz)
        if dist < 0.001:
            self.is_moving = False
            return
        step = config.MOVE_SPEED * dt
        if step >= dist:
            self.x = self.destination_x
            self.z = self.destination_z
            self.is_moving = False
            return
        self.x += dx / dist * step
        self.z += dz / dist * step


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
    hp: int = 100
    max_hp: int = 100

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
ClickMarker = tuple[float, float, float]                 # (world_x, world_z, expire_time)


@dataclass
class GameState:
    """모든 핸들러/메인 루프가 공유하는 클라이언트 상태."""
    username: str = ""
    me: PlayerState = field(default_factory=PlayerState)
    others: dict[int, PlayerState] = field(default_factory=dict)
    monsters: dict[int, MonsterState] = field(default_factory=dict)
    projectiles: dict[int, ProjectileState] = field(default_factory=dict)
    hitscan_lines: list[HitscanLine] = field(default_factory=list)
    click_markers: list[ClickMarker] = field(default_factory=list)  # 우클릭 피드백
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


def _screen_to_world(sx: int, sy: int, me: PlayerState,
                     screen_w: int, screen_h: int) -> tuple[float, float]:
    """renderer.world_to_screen 의 역함수. 카메라 중심은 항상 me."""
    wx = me.x + (sx - screen_w / 2) / config.PIXELS_PER_UNIT
    wz = me.z - (sy - screen_h / 2) / config.PIXELS_PER_UNIT
    return wx, wz


def _nearest_monster_to_point(state: GameState, wx: float, wz: float,
                              max_range: float = 3.0) -> MonsterState | None:
    """커서 월드좌표 근처의 적 찾기 (W Point-click 용)."""
    best = None
    best_dsq = max_range * max_range
    for m in state.monsters.values():
        dx = m.x - wx
        dz = m.z - wz
        dsq = dx * dx + dz * dz
        if dsq < best_dsq:
            best = m
            best_dsq = dsq
    return best


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
    """서버로부터 플레이어 위치 업데이트. 자기 자신일 경우 예측 위치와 비교해 교정."""
    if msg.player_id == state.me.player_id:
        # 서버 권위 위치와 로컬 예측을 비교. 작은 오차는 무시, 큰 차이는 스냅.
        dx = msg.position.x - state.me.x
        dz = msg.position.z - state.me.z
        if dx * dx + dz * dz > config.POSITION_CORRECTION_EPSILON ** 2:
            state.me.x = msg.position.x
            state.me.z = msg.position.z
        return
    ps = state.others.setdefault(msg.player_id, PlayerState(player_id=msg.player_id))
    ps.set_target(msg.position.x, msg.position.y, msg.position.z)


def _h_player_spawn(state: GameState, msg):
    p = msg.player
    if p.player_id == state.me.player_id:
        return
    px, py, pz = p.position.x, p.position.y, p.position.z
    ps = state.others.setdefault(p.player_id, PlayerState(player_id=p.player_id))
    ps.name = p.name
    ps.guid = p.guid
    ps.x, ps.y, ps.z = px, py, pz
    ps.tx, ps.ty, ps.tz = px, py, pz
    log_game.info("player joined: id=%d name=%s", p.player_id, p.name)


def _h_player_leave(state: GameState, msg):
    state.others.pop(msg.player_id, None)


def _h_move_correction(state: GameState, msg):
    state.me.x = msg.position.x
    state.me.y = msg.position.y
    state.me.z = msg.position.z
    # 서버가 보정을 냈다는 건 로컬 예측이 틀렸다는 뜻 → destination 도 해제
    state.me.is_moving = False


def _h_error(state: GameState, msg):
    log_game.error("server error: code=%d src_pkt=%d", msg.code, msg.source_packet_id)


def _h_monster_list(state: GameState, msg):
    for m in msg.monsters:
        px, py, pz = m.position.x, m.position.y, m.position.z
        state.monsters[m.guid] = MonsterState(
            guid=m.guid, name=m.name,
            x=px, y=py, z=pz, tx=px, ty=py, tz=pz,
            detect_range=m.detect_range if m.detect_range > 0 else 10.0,
            hp=m.hp if m.max_hp > 0 else 100,
            max_hp=m.max_hp if m.max_hp > 0 else 100)


def _h_monster_spawn(state: GameState, msg):
    m = msg.monster
    px, py, pz = m.position.x, m.position.y, m.position.z
    state.monsters[m.guid] = MonsterState(
        guid=m.guid, name=m.name,
        x=px, y=py, z=pz, tx=px, ty=py, tz=pz,
        detect_range=m.detect_range if m.detect_range > 0 else 10.0,
        hp=m.hp if m.max_hp > 0 else 100,
        max_hp=m.max_hp if m.max_hp > 0 else 100)


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


def _h_unit_hp(state: GameState, msg):
    """S_UnitHp 는 guid 기반 범용 HP 갱신 패킷 (Player/Monster/NPC 공통)."""
    if msg.guid == state.me.guid or msg.guid == 0:
        state.me.hp = msg.hp
        state.me.max_hp = msg.max_hp
        return
    # Monster guid 매칭 (몬스터 HP 바 갱신)
    ms = state.monsters.get(msg.guid)
    if ms is not None:
        ms.hp = msg.hp
        ms.max_hp = msg.max_hp
        return
    # Other players
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
    packet_ids.S_UNIT_HP:            _h_unit_hp,
    packet_ids.S_PROJECTILE_SPAWN:   _h_projectile_spawn,
    packet_ids.S_PROJECTILE_HIT:     _h_projectile_hit,
    packet_ids.S_PROJECTILE_DESTROY: _h_projectile_destroy,
}


# ═══════════════════════════════════════════════════════════════════
#  Skill casting — 타게팅 타입별 분기
# ═══════════════════════════════════════════════════════════════════

def _resolve_input_mode(tpl: skill_data.SkillTemplate) -> str:
    """CSV targeting + config override(sid 기준) 로 클라 입력 수집 방식 결정."""
    override = config.SKILL_INPUT_MODE_OVERRIDES.get(tpl.sid)
    if override:
        return override
    # Homing 기본은 서버 자동 타깃, Skillshot 기본은 커서 방향
    return "auto_homing" if tpl.targeting == "Homing" else "skillshot_dir"


def _cast_skill(state: GameState, client: PacketClient, skill_id: int,
                cursor_world: tuple[float, float]) -> bool:
    """QWER 키 입력 시 호출. 성공 송신 시 True, 방향 계산 불가/미지원 시 False."""
    tpl = SKILL_TABLE.get(skill_id)
    if tpl is None:
        log_game.warning("unknown skill sid in cast: %d", skill_id)
        return False

    mode = _resolve_input_mode(tpl)
    req = game_pb2.C_RequestUseSkill()
    req.skill_id = tpl.sid
    cursor_wx, cursor_wz = cursor_world

    if mode == "skillshot_dir":
        dx = cursor_wx - state.me.x
        dz = cursor_wz - state.me.z
        d = math.sqrt(dx * dx + dz * dz)
        if d < 1e-4:
            return False
        req.dir.x, req.dir.y, req.dir.z = dx / d, 0.0, dz / d
        req.target_guid = 0

    elif mode == "point_click":
        target = _nearest_monster_to_point(state, cursor_wx, cursor_wz, max_range=3.0)
        req.target_guid = target.guid if target else 0
        req.dir.x = req.dir.y = req.dir.z = 0.0

    elif mode == "auto_homing":
        req.target_guid = 0
        req.dir.x = req.dir.y = req.dir.z = 0.0

    elif mode == "ground_target":
        dx = cursor_wx - state.me.x
        dz = cursor_wz - state.me.z
        d = math.sqrt(dx * dx + dz * dz)
        if d < 1e-4:
            return False
        req.dir.x, req.dir.y, req.dir.z = dx / d, 0.0, dz / d
        req.target_pos.x, req.target_pos.y, req.target_pos.z = cursor_wx, 0.0, cursor_wz
        req.target_guid = 0

    else:
        log_game.warning("unknown input mode '%s' for skill sid=%d (%s)",
                         mode, tpl.sid, tpl.name)
        return False

    client.send(req)
    # 스킬이 실제로 발동될 때만 로컬 이동 중단 (쿨다운 검사는 호출부가 담당)
    state.me.is_moving = False
    return True


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
    if result is None:
        log_login.error("timed out waiting for S_Login (%.1fs)",
                        config.LOGIN_RESPONSE_TIMEOUT)
    return result


# ═══════════════════════════════════════════════════════════════════
#  Main game loop
# ═══════════════════════════════════════════════════════════════════

_KEY_TO_SKILL_CHAR = [
    ("q", "q"),
    ("w", "w"),
    ("e", "e"),
    ("r", "r"),
]


def _process_input(pygame, events: list, keys, state: GameState,
                   client: PacketClient, io: dict, renderer: Renderer) -> None:
    """이벤트 기반(우클릭/스킬키) + 상태 기반(S키 정지) 입력 처리."""
    if not state.in_game:
        return

    now = time.time()
    mouse_x, mouse_y = pygame.mouse.get_pos()
    cursor_wx, cursor_wz = _screen_to_world(mouse_x, mouse_y, state.me,
                                            renderer.width, renderer.height)

    # 이벤트 루프: 우클릭(이동), QWER 키다운(스킬)
    for event in events:
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 3:
            # 우클릭: 월드 좌표로 이동 명령
            cmd = game_pb2.C_MoveCommand()
            cmd.target_pos.x, cmd.target_pos.y, cmd.target_pos.z = cursor_wx, 0.0, cursor_wz
            client.send(cmd)
            state.me.set_destination(cursor_wx, cursor_wz)   # 로컬 즉시 예측
            state.click_markers.append((cursor_wx, cursor_wz, now + config.CLICK_MARKER_LIFETIME))
        elif event.type == pygame.KEYDOWN:
            if event.key == pygame.K_s:
                # S: 정지 명령 + 로컬 즉시 정지
                client.send(game_pb2.C_StopMove())
                state.me.is_moving = False
            else:
                # QWER: 스킬
                for key_name, _char in _KEY_TO_SKILL_CHAR:
                    if event.key == getattr(pygame, f"K_{key_name}"):
                        # 1) 글로벌 키 throttle (키 연타 방지)
                        if now - io["last_skill_send"] < config.SKILL_THROTTLE:
                            break
                        skill_id = config.SKILL_BINDINGS.get(key_name)
                        if skill_id is None:
                            break
                        tpl = SKILL_TABLE.get(skill_id)
                        if tpl is None:
                            log_game.warning("binding '%s' -> sid=%d not in SKILL_TABLE",
                                             key_name, skill_id)
                            break
                        # 2) 로컬 쿨다운 체크 — 쿨다운 중이면 아예 무시 (이동 유지).
                        #    통과한 경우에만 서버로 요청 + 로컬 is_moving 해제.
                        if now < io["skill_next_usable"].get(skill_id, 0.0):
                            break
                        if _cast_skill(state, client, skill_id, (cursor_wx, cursor_wz)):
                            io["skill_next_usable"][skill_id] = now + tpl.cooldown
                            io["last_skill_send"] = now
                        break


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
        "last_skill_send": 0.0,
        # skill_id -> next_usable_time (time.time() 기준). 서버 쿨다운과 동일 규칙으로
        # 로컬에서 선제 차단하여, 쿨다운 중 키 입력이 이동 예측을 끊지 않게 한다.
        "skill_next_usable": {},
    }

    import pygame

    running = True
    while running:
        events = pygame.event.get()
        for event in events:
            if event.type == pygame.QUIT:
                running = False

        keys = pygame.key.get_pressed()
        _process_input(pygame, events, keys, state, client, io, renderer)

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
        # 자기 캐릭터: 목적지 방향 직선 예측 이동
        state.me.move_toward_destination(dt_frame)
        # 다른 플레이어/몬스터: 서버가 보내준 타깃으로 보간
        for ps in state.others.values():
            ps.lerp(dt_frame)
        for ms in state.monsters.values():
            ms.lerp(dt_frame)

        now_t = time.time()
        _simulate_projectiles(state, dt_frame, now_t)
        state.hitscan_lines = [h for h in state.hitscan_lines if h[4] > now_t]
        state.click_markers = [c for c in state.click_markers if c[2] > now_t]

        moving_tag = "moving" if state.me.is_moving else "idle"
        status = [
            f"user={username}  id={state.me.player_id}  "
            f"pos=({state.me.x:.1f}, {state.me.z:.1f})  HP={state.me.hp}/{state.me.max_hp}  [{moving_tag}]",
            f"others={len(state.others)}  monsters={len(state.monsters)}  "
            f"proj={len(state.projectiles)}  {'in-game' if state.in_game else 'entering...'}",
            "RClick=move  S=stop  Q/W/E/R=skills  ESC=quit",
        ]
        if keys[pygame.K_ESCAPE]:
            running = False
        renderer.draw_frame(
            state.me if state.in_game else None,
            state.others, state.monsters, status,
            hitscan_lines=state.hitscan_lines, projectiles=state.projectiles,
            click_markers=state.click_markers)

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
    try:
        main()
    except KeyboardInterrupt:
        # Ctrl+C는 에러가 아니라 사용자 요청이므로 스택트레이스 없이 종료
        logging.getLogger("game").info("interrupted by user")
    except Exception as e:
        # 크래시가 났다면 콘솔만 닫혀 원인을 놓치지 않도록 반드시 로그로 남긴다
        logging.getLogger("game").exception("unhandled exception: %s", e)
        raise

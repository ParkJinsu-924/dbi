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
import effect_data
import monster_data
from feedback import FeedbackSystem
from network import PacketClient
from renderer import Renderer

log_login = logging.getLogger("login")
log_game = logging.getLogger("game")
log_hit = logging.getLogger("hit")

# 시작 시 1회 CSV 로드. 이후 조회만 한다 (테스트에서는 임시 table로 교체 가능).
SKILL_TABLE: skill_data.SkillTable = skill_data.load_from_csv()
EFFECT_TABLE: effect_data.EffectTable = effect_data.load_from_csv()
# 서버는 MonsterInfo.tid 만 내려주고 name 은 소유하지 않는다 — 여기서 tid→name 조회.
MONSTER_TABLE: monster_data.MonsterTable = monster_data.load_from_csv()


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

    # 활성 Buff/Debuff — eid → remaining_duration(sec).
    # 서버의 S_BuffApplied 로 추가, Tick 과 S_BuffRemoved 로 제거.
    # self(me) 의 경우 move_toward_destination 이 MoveSpeed 버프를 반영한다.
    buffs: dict[int, float] = field(default_factory=dict)

    # 활성 CC 시각화용: cc_flag(str) → (expire_wall_time, applied_wall_time).
    # MonsterState 와 동일한 스키마 — renderer/cc_visuals 가 공통으로 처리.
    active_ccs: dict[str, tuple[float, float]] = field(default_factory=dict)

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

    def get_effective_move_speed(self) -> float:
        """활성 MoveSpeed StatMod buff 를 반영한 최종 이동속도.
        서버의 Unit::GetEffectiveMoveSpeed 와 같은 공식: base * (1 + pct) + flat."""
        flat = 0.0
        pct = 0.0
        for eid in self.buffs:
            e = EFFECT_TABLE.get(eid)
            if not e or e.type != "StatMod" or e.stat != "MoveSpeed":
                continue
            if e.is_percent:
                pct += e.magnitude
            else:
                flat += e.magnitude
        return max(0.0, config.MOVE_SPEED * (1.0 + pct) + flat)

    def tick_buffs(self, dt: float):
        """남은 지속시간 감소. 0 이하는 제거. 서버 S_BuffRemoved 와 경미한 오차 가능
        (서버 권위이지만 이 dict 는 prediction 용이라 자체 만료해도 안전)."""
        if not self.buffs:
            return
        expired = [eid for eid, remaining in self.buffs.items() if remaining - dt <= 0.0]
        for eid in self.buffs:
            self.buffs[eid] -= dt
        for eid in expired:
            self.buffs.pop(eid, None)

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
        step = self.get_effective_move_speed() * dt
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
    state: int = 0        # 0=Idle, 1=Patrol, 2=Engage, 3=Return (MonsterStateId enum)
    target_guid: int = 0  # Engage 중일 때 현재 target player GUID
    detect_range: float = 10.0
    hp: int = 100
    max_hp: int = 100
    # 활성 CC(행동 제약) 상태: cc_flag(str) → (expire_wall_time, applied_wall_time).
    # applied_wall_time 은 cc_visuals 애니메이션 위상 계산용.
    # 서버 S_BuffApplied/S_BuffRemoved 로 갱신. 만료는 서버 권위이지만 wall-clock 기반
    # 자체 만료도 허용 (네트워크 누락 대비).
    active_ccs: dict[str, tuple[float, float]] = field(default_factory=dict)

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
    skill_id: int = 0      # 시전 스킬 sid — 렌더러가 시각 차별화에 사용


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
    # 시각/청각 피드백 (damage popup, 파티클, 카메라 흔들림, 사운드).
    # run_game 에서 주입한다 (dataclass default 로 두면 전역 pygame init 이 import 시점에 일어남).
    feedback: "FeedbackSystem | None" = None


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


def make_vec2(x: float, y: float) -> common_pb2.Vector2:
    v = common_pb2.Vector2()
    v.x, v.y = x, y
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
    # 서버 Vector2: .y = horizontal-second axis (클라 내부 z 로 매핑).
    state.me.z = msg.spawn_position.y
    state.in_game = True
    log_game.info("entered as playerId=%d guid=%d at (%.1f, %.1f)",
                  state.me.player_id, state.me.guid, state.me.x, state.me.z)


def _h_player_list(state: GameState, msg):
    # 서버 Vector2: .y = horizontal-second axis → 클라 내부 z 로 매핑.
    for p in msg.players:
        if p.player_id == state.me.player_id:
            continue
        px, pz = p.position.x, p.position.y
        ps = state.others.setdefault(p.player_id, PlayerState(player_id=p.player_id))
        ps.name = p.name
        ps.guid = p.guid
        ps.x, ps.z = px, pz
        ps.tx, ps.tz = px, pz


def _h_unit_positions(state: GameState, msg):
    """S_UnitPositions — Player/Monster 이동 상태를 guid 로 식별해 일괄 갱신.
    서버는 이동 중인 Player 와 전체 Monster 를 한 패킷에 묶어 10Hz 로 뿌린다.
    guid lookup 은 me → monsters(dict) → others(guid index) 순서."""
    # others 는 player_id 키라 guid→player lookup 위해 패킷당 1회 역인덱스 구성.
    others_by_guid = {ps.guid: ps for ps in state.others.values() if ps.guid}

    for u in msg.units:
        gid = u.guid
        if gid == state.me.guid:
            # 내 위치: 로컬 예측과 서버 위치 차이가 threshold 초과 시 교정.
            dx = u.position.x - state.me.x
            dz = u.position.y - state.me.z
            if dx * dx + dz * dz > config.POSITION_CORRECTION_EPSILON ** 2:
                state.me.x = u.position.x
                state.me.z = u.position.y
            continue
        ms = state.monsters.get(gid)
        if ms is not None:
            ms.set_target(u.position.x, 0.0, u.position.y)
            continue
        ps = others_by_guid.get(gid)
        if ps is not None:
            ps.set_target(u.position.x, 0.0, u.position.y)


def _h_player_move(state: GameState, msg):
    """S_PlayerMove — client-authoritative 이동 모드에서 각 클라가 자발적으로 송신한
    C_PlayerMove 를 서버가 검증 후 그대로 브로드캐스트. 자기 자신 패킷은 무시."""
    if msg.player_id == state.me.player_id:
        return
    ps = state.others.get(msg.player_id)
    if ps is None:
        return
    ps.set_target(msg.position.x, 0.0, msg.position.y)


def _h_player_spawn(state: GameState, msg):
    p = msg.player
    if p.player_id == state.me.player_id:
        return
    px, pz = p.position.x, p.position.y
    ps = state.others.setdefault(p.player_id, PlayerState(player_id=p.player_id))
    ps.name = p.name
    ps.guid = p.guid
    ps.x, ps.z = px, pz
    ps.tx, ps.tz = px, pz
    log_game.info("player joined: id=%d name=%s", p.player_id, p.name)


def _h_player_leave(state: GameState, msg):
    state.others.pop(msg.player_id, None)


def _h_move_correction(state: GameState, msg):
    state.me.x = msg.position.x
    state.me.z = msg.position.y
    # 서버가 보정을 냈다는 건 로컬 예측이 틀렸다는 뜻 → destination 도 해제
    state.me.is_moving = False


def _h_error(state: GameState, msg):
    log_game.error("server error: code=%d src_pkt=%d", msg.code, msg.source_packet_id)


def _h_monster_list(state: GameState, msg):
    for m in msg.monsters:
        px, pz = m.position.x, m.position.y
        state.monsters[m.guid] = MonsterState(
            guid=m.guid, name=MONSTER_TABLE.name(m.tid),
            x=px, z=pz, tx=px, tz=pz,
            detect_range=m.detect_range if m.detect_range > 0 else 10.0,
            hp=m.hp if m.max_hp > 0 else 100,
            max_hp=m.max_hp if m.max_hp > 0 else 100)


def _h_monster_spawn(state: GameState, msg):
    m = msg.monster
    px, pz = m.position.x, m.position.y
    state.monsters[m.guid] = MonsterState(
        guid=m.guid, name=MONSTER_TABLE.name(m.tid),
        x=px, z=pz, tx=px, tz=pz,
        detect_range=m.detect_range if m.detect_range > 0 else 10.0,
        hp=m.hp if m.max_hp > 0 else 100,
        max_hp=m.max_hp if m.max_hp > 0 else 100)
    if state.feedback is not None:
        state.feedback.on_monster_spawn(px, pz)


def _h_monster_despawn(state: GameState, msg):
    state.monsters.pop(msg.guid, None)


def _h_monster_state(state: GameState, msg):
    ms = state.monsters.get(msg.guid)
    if ms:
        ms.state = msg.state
        ms.target_guid = msg.target_guid


def _h_skill_hit(state: GameState, msg):
    """모든 공격 적중의 단일 핸들러 (Melee/Hitscan/Homing/Skillshot 공통).
    caster→hit 사이 거리가 일정 이상이면 hitscan 라인으로 간주해 그려준다 —
    길이가 짧은 Melee/Projectile 은 자연스럽게 눈에 띄지 않는다."""
    dx = msg.hit_pos.x - msg.caster_pos.x
    dz = msg.hit_pos.y - msg.caster_pos.y
    if dx * dx + dz * dz > 4.0:   # 2m 초과 = 원거리 적중 → 라인 표시
        state.hitscan_lines.append((
            msg.caster_pos.x, msg.caster_pos.y,
            msg.hit_pos.x, msg.hit_pos.y,
            time.time() + config.HITSCAN_LINE_LIFETIME))

    # 시각/청각 피드백: 피격 위치에 데미지 숫자 + 스파크 + 사운드 + 필요 시 카메라 흔들림.
    if state.feedback is not None and msg.damage > 0:
        target_is_me = (state.me.guid != 0 and msg.target_guid == state.me.guid)
        state.feedback.on_hit(
            wx=msg.hit_pos.x, wz=msg.hit_pos.y,
            damage=msg.damage, target_is_me=target_is_me)

    ms = state.monsters.get(msg.caster_guid)
    name = ms.name if ms else "?"
    log_game.info("%s skill=%d -> guid=%d (dmg=%d)",
                  name, msg.skill_id, msg.target_guid, msg.damage)


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
        x=msg.start_pos.x, z=msg.start_pos.y,
        speed=msg.speed,
        target_guid=msg.target_guid,
        dir_x=msg.dir.x, dir_z=msg.dir.y,
        radius=msg.radius if msg.radius > 0 else 0.3,
        max_range=msg.max_range,
        max_lifetime=msg.max_lifetime if msg.max_lifetime > 0 else 5.0,
        spawned_at=time.time(),
        skill_id=msg.skill_id)

    # 시전자 발 밑에서 확산 링 + 캐스트 사운드.
    if state.feedback is not None:
        caster_is_me = (state.me.guid != 0 and msg.owner_guid == state.me.guid)
        state.feedback.on_skill_cast(
            wx=msg.start_pos.x, wz=msg.start_pos.y,
            caster_is_me=caster_is_me,
            skill_id=msg.skill_id)


def _h_projectile_destroy(state: GameState, msg):
    state.projectiles.pop(msg.projectile_guid, None)


def _apply_cc_to_target(state: GameState, target_guid: int,
                        eid: int, duration: float) -> None:
    """eid 가 CC 이면 target 의 active_ccs 에 (expire, applied) 를 기록한다.
    target 은 me / monster / other player 중 하나. 못 찾으면 조용히 무시."""
    effect = EFFECT_TABLE.get(eid)
    if effect is None or effect.cc_flag in (None, "", "None"):
        return
    now = time.time()
    entry = (now + duration, now)

    if state.me.guid != 0 and target_guid == state.me.guid:
        state.me.active_ccs[effect.cc_flag] = entry
        return
    ms = state.monsters.get(target_guid)
    if ms is not None:
        ms.active_ccs[effect.cc_flag] = entry
        return
    for p in state.others.values():
        if p.guid == target_guid:
            p.active_ccs[effect.cc_flag] = entry
            return


def _remove_cc_from_target(state: GameState, target_guid: int, eid: int) -> None:
    effect = EFFECT_TABLE.get(eid)
    if effect is None or effect.cc_flag in (None, "", "None"):
        return

    if state.me.guid != 0 and target_guid == state.me.guid:
        state.me.active_ccs.pop(effect.cc_flag, None)
        return
    ms = state.monsters.get(target_guid)
    if ms is not None:
        ms.active_ccs.pop(effect.cc_flag, None)
        return
    for p in state.others.values():
        if p.guid == target_guid:
            p.active_ccs.pop(effect.cc_flag, None)
            return


def _h_buff_applied(state: GameState, msg):
    """self 버프는 local prediction(이동속도 등)에 반영되도록 me.buffs 에 저장.
    target 에 CC(행동 제약) 속성이 있으면 active_ccs 에도 기록 — renderer 가
    쇠사슬/별 등으로 시각화한다."""
    if msg.target_guid == state.me.guid:
        state.me.buffs[msg.eid] = msg.duration
    _apply_cc_to_target(state, msg.target_guid, msg.eid, msg.duration)
    log_game.info("BUFF+  target=%d eid=%d caster=%d dur=%.1fs",
                  msg.target_guid, msg.eid, msg.caster_guid, msg.duration)


def _h_buff_removed(state: GameState, msg):
    if msg.target_guid == state.me.guid:
        state.me.buffs.pop(msg.eid, None)
    _remove_cc_from_target(state, msg.target_guid, msg.eid)
    log_game.info("BUFF-  target=%d eid=%d", msg.target_guid, msg.eid)


PACKET_HANDLERS = {
    packet_ids.S_ENTER_GAME:         _h_enter_game,
    packet_ids.S_PLAYER_LIST:        _h_player_list,
    packet_ids.S_PLAYER_MOVE:        _h_player_move,
    packet_ids.S_UNIT_POSITIONS:     _h_unit_positions,
    packet_ids.S_PLAYER_SPAWN:       _h_player_spawn,
    packet_ids.S_PLAYER_LEAVE:       _h_player_leave,
    packet_ids.S_MOVE_CORRECTION:    _h_move_correction,
    packet_ids.S_ERROR:              _h_error,
    packet_ids.S_MONSTER_LIST:       _h_monster_list,
    packet_ids.S_MONSTER_SPAWN:      _h_monster_spawn,
    packet_ids.S_MONSTER_DESPAWN:    _h_monster_despawn,
    packet_ids.S_MONSTER_STATE:      _h_monster_state,
    packet_ids.S_SKILL_HIT:          _h_skill_hit,
    packet_ids.S_UNIT_HP:            _h_unit_hp,
    packet_ids.S_PROJECTILE_SPAWN:   _h_projectile_spawn,
    packet_ids.S_PROJECTILE_DESTROY: _h_projectile_destroy,
    packet_ids.S_BUFF_APPLIED:       _h_buff_applied,
    packet_ids.S_BUFF_REMOVED:       _h_buff_removed,
}


# ═══════════════════════════════════════════════════════════════════
#  Skill casting — 타게팅 타입별 분기
# ═══════════════════════════════════════════════════════════════════

def _resolve_input_mode(tpl: skill_data.SkillTemplate) -> str:
    """CSV targeting + config override(sid 기준) 로 클라 입력 수집 방식 결정.
    Homing 은 press-release 타겟팅 플로우(_process_input 에서 처리)로 송신하므로
    _cast_skill 경로에 도달하지 않는다. 여기서는 Skillshot/ground-target 만 다룬다."""
    override = config.SKILL_INPUT_MODE_OVERRIDES.get(tpl.sid)
    if override:
        return override
    return "skillshot_dir"   # Homing 은 호출자가 사전 분기하므로 도달 안 함


def _cast_skill(state: GameState, client: PacketClient, skill_id: int,
                cursor_world: tuple[float, float]) -> bool:
    """Skillshot / ground-target 즉발 경로. Homing 은 타겟팅 플로우가 별도 송신.
    성공 시 True. 방향/좌표 계산 불가면 False."""
    tpl = SKILL_TABLE.get(skill_id)
    if tpl is None:
        log_game.warning("unknown skill sid in cast: %d", skill_id)
        return False

    mode = _resolve_input_mode(tpl)
    req = game_pb2.C_UseSkill()
    req.skill_id = tpl.sid
    cursor_wx, cursor_wz = cursor_world

    if mode == "skillshot_dir":
        dx = cursor_wx - state.me.x
        dz = cursor_wz - state.me.z
        d = math.sqrt(dx * dx + dz * dz)
        if d < 1e-4:
            return False
        # Vector2.y = horizontal-second axis (클라 내부 z 에 대응).
        req.dir.x, req.dir.y = dx / d, dz / d
        req.target_guid = 0

    elif mode == "ground_target":
        dx = cursor_wx - state.me.x
        dz = cursor_wz - state.me.z
        d = math.sqrt(dx * dx + dz * dz)
        if d < 1e-4:
            return False
        req.dir.x, req.dir.y = dx / d, dz / d
        req.target_pos.x, req.target_pos.y = cursor_wx, cursor_wz
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
#  Homing 타겟팅 플로우 (LoL Quick Cast with Indicator 스타일)
#  W 누름 → 사거리 원 표시 + hover 갱신 / W 뗌 → target_guid 로 송신
#  우클릭 → 취소. 쿨다운/throttle 은 송신 시점(release)에 검증.
# ═══════════════════════════════════════════════════════════════════

def _pick_target_near_cursor(state: GameState, cursor_wx: float, cursor_wz: float,
                             hover_tol: float = 1.0) -> MonsterState | None:
    """커서 hover_tol 이내 가장 가까운 살아있는 Monster. 사거리 무관.
    범위 안/밖 판정은 호출자(targeting 렌더, finalize 분기) 가 별도로 수행."""
    best = None
    best_dsq = hover_tol * hover_tol
    for m in state.monsters.values():
        if getattr(m, 'hp', 0) <= 0:
            continue
        dx = m.x - cursor_wx
        dz = m.z - cursor_wz
        dsq = dx * dx + dz * dz
        if dsq < best_dsq:
            best = m
            best_dsq = dsq
    return best


def _dist_sq(ax: float, az: float, bx: float, bz: float) -> float:
    dx = ax - bx
    dz = az - bz
    return dx * dx + dz * dz


def _enter_targeting(io: dict, skill_id: int, tpl: skill_data.SkillTemplate) -> None:
    io["targeting"].update({
        "active": True,
        "skill_id": skill_id,
        "tpl": tpl,
        "hovered_guid": None,
        "hovered_in_range": False,
    })


def _cancel_targeting(io: dict) -> None:
    io["targeting"].update({
        "active": False,
        "skill_id": None,
        "tpl": None,
        "hovered_guid": None,
        "hovered_in_range": False,
    })


def _send_use_skill(client: PacketClient, skill_id: int, target_guid: int) -> None:
    req = game_pb2.C_UseSkill()
    req.skill_id = skill_id
    req.target_guid = target_guid
    req.dir.x = req.dir.y = 0.0
    client.send(req)


def _enter_pending_cast(io: dict, skill_id: int, target_guid: int) -> None:
    """W 뗐는데 타겟이 사거리 밖 → 접근 후 자동 시전하는 상태로 진입."""
    io["pending_cast"] = {
        "skill_id": skill_id,
        "target_guid": target_guid,
        "last_sent_target_pos": None,   # 마지막으로 C_MoveCommand 날린 target 위치
        "last_sent_time": 0.0,
    }


def _cancel_pending_cast(io: dict) -> None:
    io["pending_cast"] = {
        "skill_id": None,
        "target_guid": None,
        "last_sent_target_pos": None,
        "last_sent_time": 0.0,
    }


def _tick_pending_cast(state: GameState, client: PacketClient, io: dict,
                       now: float) -> None:
    """매 프레임 호출. 타겟 추적 → 사거리 진입 시 자동 시전, 사망/소멸 시 취소."""
    pc = io["pending_cast"]
    target_guid = pc["target_guid"]
    if target_guid is None:
        return
    skill_id = pc["skill_id"]
    tpl = SKILL_TABLE.get(skill_id)
    if tpl is None:
        _cancel_pending_cast(io)
        return

    target = state.monsters.get(target_guid)
    if target is None or getattr(target, 'hp', 0) <= 0:
        # 타겟 소멸/사망 → 취소 + 이동 중단.
        _cancel_pending_cast(io)
        state.me.is_moving = False
        return

    me = state.me
    dsq = _dist_sq(me.x, me.z, target.x, target.z)
    cast_range_sq = tpl.cast_range * tpl.cast_range

    if dsq <= cast_range_sq:
        # 도달 — 쿨다운/throttle 검사 후 시전.
        if now - io["last_skill_send"] < config.SKILL_THROTTLE:
            return
        if now < io["skill_next_usable"].get(skill_id, 0.0):
            return  # 쿨다운 중 — 대기 (취소하지 않음. 곧 풀리면 자동 발사)
        _send_use_skill(client, skill_id, target_guid)
        state.me.is_moving = False
        io["skill_next_usable"][skill_id] = now + tpl.cooldown
        io["last_skill_send"] = now
        _cancel_pending_cast(io)
        return

    # 사거리 밖 — 타겟 방향으로 cast_range 안쪽까지만 접근점 계산.
    d = math.sqrt(dsq)
    step = max(0.0, d - (tpl.cast_range - 0.5))  # 사거리 내부 0.5 여유 남김
    dest_x = me.x + (target.x - me.x) / d * step
    dest_z = me.z + (target.z - me.z) / d * step

    # 재송신 조건: 최초 1회 / 타겟이 1유닛 이상 이동 / 0.5초 경과.
    last_pos = pc["last_sent_target_pos"]
    moved = (last_pos is None) or (abs(target.x - last_pos[0]) > 1.0) \
                               or (abs(target.z - last_pos[1]) > 1.0)
    elapsed = (now - pc["last_sent_time"]) > 0.5
    if moved or elapsed:
        # Client-authoritative: destination 만 로컬 세팅. 서버 보고는 주기적 C_PlayerMove.
        state.me.set_destination(dest_x, dest_z)
        pc["last_sent_target_pos"] = (target.x, target.z)
        pc["last_sent_time"] = now


def _finalize_targeting(state: GameState, client: PacketClient, io: dict,
                        now: float) -> None:
    """W release 시점 호출. hover 있으면:
       - 사거리 내 → 즉시 C_UseSkill
       - 사거리 밖 → pending_cast 진입 (접근 후 자동 시전)
       hover 없으면 조용히 취소."""
    t = io["targeting"]
    hovered_guid = t["hovered_guid"]
    tpl = t["tpl"]
    skill_id = t["skill_id"]

    if hovered_guid is None or tpl is None:
        _cancel_targeting(io)
        return

    target = state.monsters.get(hovered_guid)
    if target is None or getattr(target, 'hp', 0) <= 0:
        _cancel_targeting(io)
        return

    dsq = _dist_sq(state.me.x, state.me.z, target.x, target.z)
    if dsq > tpl.cast_range * tpl.cast_range:
        # 사거리 밖 → 접근 + 자동 시전 모드로 진입.
        _cancel_targeting(io)
        _enter_pending_cast(io, skill_id, hovered_guid)
        _tick_pending_cast(state, client, io, now)  # 즉시 첫 approach move 발사
        return

    # 사거리 내 → 기존 즉시 송신 경로.
    if now - io["last_skill_send"] < config.SKILL_THROTTLE:
        _cancel_targeting(io)
        return
    if now < io["skill_next_usable"].get(skill_id, 0.0):
        _cancel_targeting(io)
        return

    _send_use_skill(client, skill_id, hovered_guid)
    state.me.is_moving = False
    io["skill_next_usable"][skill_id] = now + tpl.cooldown
    io["last_skill_send"] = now
    _cancel_targeting(io)


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

    # Homing 타겟팅 활성 시 매 프레임 hover 대상 + 범위 내 플래그 갱신
    if io["targeting"]["active"]:
        tpl = io["targeting"]["tpl"]
        if tpl is not None:
            hovered = _pick_target_near_cursor(state, cursor_wx, cursor_wz)
            io["targeting"]["hovered_guid"] = hovered.guid if hovered else None
            if hovered:
                dsq = _dist_sq(state.me.x, state.me.z, hovered.x, hovered.z)
                io["targeting"]["hovered_in_range"] = dsq <= tpl.cast_range * tpl.cast_range
            else:
                io["targeting"]["hovered_in_range"] = False

    # pending_cast 활성 시 매 프레임 타겟 추적 / 도달 시 자동 시전
    _tick_pending_cast(state, client, io, now)

    # 이벤트 루프
    for event in events:
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 3:
            # 우클릭: targeting 중이면 취소만 (이동 명령 안 나감).
            # pending_cast 중이면 취소 + 클릭한 위치로 일반 이동.
            if io["targeting"]["active"]:
                _cancel_targeting(io)
                continue
            if io["pending_cast"]["target_guid"] is not None:
                _cancel_pending_cast(io)
                # 아래 일반 이동으로 이어짐 (명시적 덮어쓰기).
            # Client-authoritative: destination 은 로컬에만 세팅. 실제 위치는 메인 루프의
            # 주기적 C_PlayerMove 송신 (SEND_INTERVAL 주기) 으로 서버에 보고.
            state.me.set_destination(cursor_wx, cursor_wz)
            state.click_markers.append((cursor_wx, cursor_wz, now + config.CLICK_MARKER_LIFETIME))

        elif event.type == pygame.KEYUP:
            # 타겟팅 중이던 키 release → 시전 확정 or pending_cast 진입 or 무음 취소
            if io["targeting"]["active"]:
                for key_name, _char in _KEY_TO_SKILL_CHAR:
                    if event.key == getattr(pygame, f"K_{key_name}"):
                        if config.SKILL_BINDINGS.get(key_name) == io["targeting"]["skill_id"]:
                            _finalize_targeting(state, client, io, now)
                        break

        elif event.type == pygame.KEYDOWN:
            if event.key == pygame.K_s:
                # S: 정지 — 타겟팅/pending 모두 취소 겸함.
                if io["targeting"]["active"]:
                    _cancel_targeting(io)
                if io["pending_cast"]["target_guid"] is not None:
                    _cancel_pending_cast(io)
                client.send(game_pb2.C_StopMove())
                state.me.is_moving = False
            else:
                # QWER: 스킬
                for key_name, _char in _KEY_TO_SKILL_CHAR:
                    if event.key == getattr(pygame, f"K_{key_name}"):
                        skill_id = config.SKILL_BINDINGS.get(key_name)
                        if skill_id is None:
                            break
                        tpl = SKILL_TABLE.get(skill_id)
                        if tpl is None:
                            log_game.warning("binding '%s' -> sid=%d not in SKILL_TABLE",
                                             key_name, skill_id)
                            break
                        # 쿨다운 중이면 타겟팅 진입/즉발 모두 금지 (이동 예측 유지).
                        if now < io["skill_next_usable"].get(skill_id, 0.0):
                            break

                        # 새 키 입력 시 기존 세션은 모두 무효 처리.
                        if io["targeting"]["active"]:
                            _cancel_targeting(io)
                        if io["pending_cast"]["target_guid"] is not None:
                            _cancel_pending_cast(io)

                        if tpl.targeting == "Homing":
                            # press-release 플로우 진입. 송신은 KEYUP 에서.
                            _enter_targeting(io, skill_id, tpl)
                        else:
                            # Skillshot / ground-target: 기존 즉발 경로.
                            if now - io["last_skill_send"] < config.SKILL_THROTTLE:
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

    # Feedback 은 Renderer 다음에 초기화해야 pygame.init() 이 이미 완료된 상태에서
    # mixer.init() 을 시도할 수 있다.
    feedback = FeedbackSystem(
        enable_audio=config.ENABLE_AUDIO,
        audio_volume=config.AUDIO_VOLUME)
    state = GameState(username=username, me=PlayerState(name=username),
                      feedback=feedback)
    io = {
        "last_skill_send": 0.0,
        # Client-authoritative move 보고: 마지막 C_PlayerMove 송신 시각 / 위치.
        # SEND_INTERVAL 주기로 transform.position 을 서버에 보고, 서버는 그대로 권위로 채택.
        "last_move_send": 0.0,
        "last_sent_move_x": None,
        "last_sent_move_z": None,
        # skill_id -> next_usable_time (time.time() 기준). 서버 쿨다운과 동일 규칙으로
        # 로컬에서 선제 차단하여, 쿨다운 중 키 입력이 이동 예측을 끊지 않게 한다.
        "skill_next_usable": {},
        # Homing 스킬 press-release 타겟팅 상태. active=True 동안 사거리 원 + hover ring 렌더.
        "targeting": {
            "active": False,
            "skill_id": None,
            "tpl": None,
            "hovered_guid": None,
            "hovered_in_range": False,  # 렌더링: 범위 안(적색) vs 밖(황색) ring 구분
        },
        # "타겟이 사거리 밖이라 일단 걸어가서 도달하면 자동 시전" 상태. target_guid!=None 동안 활성.
        "pending_cast": {
            "skill_id": None,
            "target_guid": None,
            "last_sent_target_pos": None,
            "last_sent_time": 0.0,
        },
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
        # Buff 만료 tick (effective move speed 계산 전에 반영)
        state.me.tick_buffs(dt_frame)
        # 자기 캐릭터: 목적지 방향 직선 예측 이동
        state.me.move_toward_destination(dt_frame)

        # Client-authoritative: SEND_INTERVAL 주기로 현재 위치를 C_PlayerMove 로 보고.
        # 위치 변동이 거의 없으면 skip. 서버는 받은 위치를 권위로 채택하고
        # S_PlayerMove 로 다른 클라에 브로드캐스트.
        _now_send = time.time()
        if state.in_game and _now_send - io["last_move_send"] >= config.SEND_INTERVAL:
            _lx = io["last_sent_move_x"]
            _lz = io["last_sent_move_z"]
            if _lx is None or abs(_lx - state.me.x) > 0.01 or abs(_lz - state.me.z) > 0.01:
                mv = game_pb2.C_PlayerMove()
                mv.position.x = state.me.x
                mv.position.y = state.me.z
                mv.yaw = 0.0
                client.send(mv)
                io["last_move_send"] = _now_send
                io["last_sent_move_x"] = state.me.x
                io["last_sent_move_z"] = state.me.z
        # 다른 플레이어/몬스터: 서버가 보내준 타깃으로 보간
        for ps in state.others.values():
            ps.lerp(dt_frame)
        for ms in state.monsters.values():
            ms.lerp(dt_frame)

        now_t = time.time()
        _simulate_projectiles(state, dt_frame, now_t)
        state.hitscan_lines = [h for h in state.hitscan_lines if h[4] > now_t]
        state.click_markers = [c for c in state.click_markers if c[2] > now_t]
        state.feedback.tick(dt_frame)

        # CC 만료 정리 — 서버 S_BuffRemoved 가 손실됐을 때를 대비해 wall-clock 자체 만료.
        def _expire_ccs(d: dict):
            for flag in [f for f, (expire, _) in d.items() if expire <= now_t]:
                d.pop(flag, None)
        _expire_ccs(state.me.active_ccs)
        for ms in state.monsters.values():
            if ms.active_ccs:
                _expire_ccs(ms.active_ccs)
        for ps in state.others.values():
            if ps.active_ccs:
                _expire_ccs(ps.active_ccs)

        moving_tag = "moving" if state.me.is_moving else "idle"
        status = [
            f"user={username}  id={state.me.player_id}  "
            f"pos=({state.me.x:.1f}, {state.me.z:.1f})  HP={state.me.hp}/{state.me.max_hp}  [{moving_tag}]",
            f"others={len(state.others)}  monsters={len(state.monsters)}  "
            f"proj={len(state.projectiles)}  {'in-game' if state.in_game else 'entering...'}",
            "RClick=move  S=stop  Q/W/E/R=skills (W=hold→aim→release)  ESC=quit",
        ]
        if io["targeting"]["active"]:
            tname = io["targeting"]["tpl"].name if io["targeting"]["tpl"] else "?"
            if io["targeting"]["hovered_guid"]:
                hover = "in range (release → cast)" if io["targeting"]["hovered_in_range"] \
                    else "out of range (release → approach & cast)"
            else:
                hover = "no target"
            status.append(f"[TARGETING {tname}]  {hover}  — RClick to cancel")
        elif io["pending_cast"]["target_guid"] is not None:
            tpl_pc = SKILL_TABLE.get(io["pending_cast"]["skill_id"])
            pname = tpl_pc.name if tpl_pc else "?"
            status.append(f"[APPROACHING to cast {pname}]  — RClick/S to cancel")
        if keys[pygame.K_ESCAPE]:
            running = False
        renderer.draw_frame(
            state.me if state.in_game else None,
            state.others, state.monsters, status,
            hitscan_lines=state.hitscan_lines, projectiles=state.projectiles,
            click_markers=state.click_markers, feedback=state.feedback,
            targeting=io["targeting"] if state.in_game else None)

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

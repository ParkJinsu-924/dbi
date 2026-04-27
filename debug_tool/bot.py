"""
봇 투입기 — 헤드리스 봇 N마리를 한 프로세스에서 띄운다.

각 봇은 자기 쓰레드에서:
  1. LoginServer 로그인 → 토큰 수령
  2. GameServer 접속 + C_EnterGame
  3. 주기적으로 랜덤 목적지 이동 + 랜덤 스킬 시전
  4. 들어오는 패킷을 drain 하며 자기 위치/GUID 갱신

렌더러/pygame 의존 X — 완전히 헤드리스. 기존 debug_tool 모듈(network, protobuf,
skill_data, config)을 그대로 재사용한다.

사용:
    python bot.py [count] [prefix]

    count   봇 수. 기본 10.
    prefix  사용자명 접두어. 기본 "bot".

예시:
    python bot.py 20          # bot_0001 ~ bot_0020
    python bot.py 50 loader   # loader_0001 ~ loader_0050

Ctrl+C 로 모든 봇을 정지시키고 종료한다.

봇 행동 튜닝은 아래 `# ── 봇 행동 파라미터 ──` 섹션.
"""

import logging
import math
import random
import sys
import threading
import time

import common_pb2
import login_pb2
import game_pb2
import packet_ids
import config
import log_setup
import skill_data
from network import PacketClient


log = logging.getLogger("bot")


# ── 봇 행동 파라미터 ────────────────────────────────────────────────
MAP_MIN_X, MAP_MAX_X = -25.0, 25.0   # 이동 목적지 샘플링 범위 (world units)
MAP_MIN_Z, MAP_MAX_Z = -25.0, 25.0

MOVE_INTERVAL_MIN = 1.5   # 새 이동 목적지 선택 간격 (초)
MOVE_INTERVAL_MAX = 4.0
SKILL_INTERVAL_MIN = 0.8  # 스킬 시전 간격 (초). 쿨다운 0 가정.
SKILL_INTERVAL_MAX = 2.5
CONNECT_STAGGER_MAX = 3.0 # 초기 접속을 분산시키는 랜덤 지연 상한

PLAYER_SKILL_IDS = [2001, 2002, 2003, 2004]


SKILL_TABLE = skill_data.load_from_csv()


# ── 봇 한 마리 ──────────────────────────────────────────────────────
class Bot:
    """단일 봇. run() 을 쓰레드로 돌린다. stop_event 로 중단."""

    def __init__(self, username: str, password: str = "bot"):
        self.username = username
        self.password = password
        self.prefix = f"[{username}]"
        self.client: PacketClient | None = None

        # 서버 권위 상태
        self.player_id = 0
        self.guid = 0
        self.x = 0.0
        self.z = 0.0
        self.in_game = False

        self.stop_event = threading.Event()

        # 이동 상태 (client.py 와 동일 규칙: destination 잡고 MOVE_SPEED 로 보간)
        self.dest_x = 0.0
        self.dest_z = 0.0
        self.is_moving = False
        # 마지막 이동 방향에서 파생한 facing yaw (Unity 컨벤션, degrees).
        # 정지 후에도 마지막 방향을 유지해 Unity 클라이언트의 RemotePlayer 회전과 동기화.
        self.yaw = 0.0
        self._last_move_send = 0.0
        self._last_sent_x: float | None = None
        self._last_sent_z: float | None = None

        # 다음 행동 타임스탬프 (wall-clock)
        self._next_move_at = 0.0
        self._next_skill_at = 0.0

    # ── 로그인 / 입장 ────────────────────────────────────────────────
    def _login(self) -> login_pb2.S_Login | None:
        client = PacketClient()
        if not client.connect(config.LOGIN_HOST, config.LOGIN_PORT):
            return None
        try:
            req = login_pb2.C_Login()
            req.username = self.username
            req.password = self.password
            client.send(req)

            deadline = time.time() + config.LOGIN_RESPONSE_TIMEOUT
            while time.time() < deadline and not self.stop_event.is_set():
                for pkt_id, msg in client.poll():
                    if pkt_id is None:
                        return None
                    if pkt_id == packet_ids.S_LOGIN:
                        return msg
                    if pkt_id == packet_ids.S_ERROR:
                        log.warning("%s login error code=%d", self.prefix, msg.code)
                        return None
                time.sleep(0.02)
            return None
        finally:
            client.close()

    def _enter_game(self, s_login: login_pb2.S_Login) -> bool:
        self.client = PacketClient()
        if not self.client.connect(s_login.game_server_ip, s_login.game_server_port):
            return False
        enter = game_pb2.C_EnterGame()
        enter.token = s_login.token
        self.client.send(enter)
        return True

    # ── 패킷 처리 (최소한: 자기 위치/GUID 만 추적) ───────────────────
    def _handle_packet(self, pkt_id: int, msg) -> None:
        # Vector2.y == world z (2D top-down 규칙). client.py/renderer.py 도 동일.
        if pkt_id == packet_ids.S_ENTER_GAME:
            self.player_id = msg.player_id
            self.guid = msg.guid
            self.x = msg.spawn_position.x
            self.z = msg.spawn_position.y
            self.in_game = True
            log.info("%s entered id=%d guid=%d at (%.1f, %.1f)",
                     self.prefix, self.player_id, self.guid, self.x, self.z)
        elif pkt_id == packet_ids.S_UNIT_POSITIONS:
            # 내 guid 만 필터. 서버가 전체 Zone 의 Unit 위치를 묶어 보낸다.
            for u in msg.units:
                if u.guid == self.guid:
                    self.x = u.position.x
                    self.z = u.position.y
                    break
        elif pkt_id == packet_ids.S_MOVE_CORRECTION:
            # 서버 보정 = 로컬 예측이 틀렸다는 뜻. client.py 와 동일하게 이동 중단.
            self.x = msg.position.x
            self.z = msg.position.y
            self.is_moving = False

    # ── AI 행동 ─────────────────────────────────────────────────────
    def _pick_destination(self) -> tuple[float, float]:
        return (random.uniform(MAP_MIN_X, MAP_MAX_X),
                random.uniform(MAP_MIN_Z, MAP_MAX_Z))

    def _pick_new_destination(self) -> None:
        """랜덤 목적지를 잡고 이동 시작 (client.py 의 set_destination 과 동일 규칙)."""
        self.dest_x, self.dest_z = self._pick_destination()
        self.is_moving = True

    def _tick_move(self, dt: float, now: float) -> None:
        """매 프레임 호출. client.py 와 동일하게:
          1. 목적지 방향으로 MOVE_SPEED * dt 만큼 보간 이동
          2. SEND_INTERVAL 주기로 현재 위치를 C_PlayerMove 로 보고
        Vector2.y 는 월드 z 축 (common.proto 규칙)."""
        if self.is_moving:
            dx = self.dest_x - self.x
            dz = self.dest_z - self.z
            dist = math.sqrt(dx * dx + dz * dz)
            # Unity transform.eulerAngles.y 컨벤션: +Z 가 yaw 0, atan2(dx, dz) deg.
            if dist > 1e-3:
                self.yaw = math.degrees(math.atan2(dx, dz))
            step = config.MOVE_SPEED * dt
            if dist <= 1e-3 or step >= dist:
                self.x = self.dest_x
                self.z = self.dest_z
                self.is_moving = False
            else:
                self.x += dx / dist * step
                self.z += dz / dist * step

        if now - self._last_move_send < config.SEND_INTERVAL:
            return
        if self._last_sent_x is not None \
                and abs(self._last_sent_x - self.x) <= 0.01 \
                and abs(self._last_sent_z - self.z) <= 0.01:
            self._last_move_send = now
            return
        try:
            mv = game_pb2.C_PlayerMove()
            mv.position.x = self.x
            mv.position.y = self.z
            mv.yaw = self.yaw
            self.client.send(mv)
            self._last_move_send = now
            self._last_sent_x = self.x
            self._last_sent_z = self.z
        except Exception as e:
            log.debug("%s send move failed: %s", self.prefix, e)

    def _build_skill_request(self, skill_id: int) -> game_pb2.C_UseSkill:
        """스킬 시전 요청 빌드. 서버가 스킬 타입에 맞게 필요한 필드만 사용한다 —
        bot 은 모든 스킬 타입을 다 채워서 보낸다 (skillshot_dir/ground_target/homing 공통).
        Vector2.y 는 월드 z 축 규칙."""
        req = game_pb2.C_UseSkill()
        req.skill_id = skill_id
        ang = random.uniform(0, 2 * math.pi)
        req.dir.x = math.cos(ang)
        req.dir.y = math.sin(ang)   # y = 월드 z
        # ground target 은 자기 앞쪽 약 8m 지점
        req.target_pos.x = self.x + math.cos(ang) * 8.0
        req.target_pos.y = self.z + math.sin(ang) * 8.0
        # Homing auto 는 target_guid=0 → 서버가 자동 선택
        req.target_guid = 0
        return req

    def _send_skill(self) -> None:
        sid = random.choice(PLAYER_SKILL_IDS)
        if sid not in SKILL_TABLE:
            return
        try:
            self.client.send(self._build_skill_request(sid))
        except Exception as e:
            log.debug("%s send skill failed: %s", self.prefix, e)

    # ── 메인 루프 ────────────────────────────────────────────────────
    def run(self) -> None:
        # 초기 접속 스태거 (다수 봇이 동시에 LoginServer 를 치지 않도록)
        time.sleep(random.uniform(0.0, CONNECT_STAGGER_MAX))

        s_login = self._login()
        if s_login is None or self.stop_event.is_set():
            log.warning("%s login failed", self.prefix)
            return
        if not self._enter_game(s_login):
            log.warning("%s enter_game failed", self.prefix)
            return

        now = time.time()
        self._next_move_at = now + random.uniform(MOVE_INTERVAL_MIN, MOVE_INTERVAL_MAX)
        self._next_skill_at = now + random.uniform(SKILL_INTERVAL_MIN, SKILL_INTERVAL_MAX)
        last_tick = now

        try:
            while not self.stop_event.is_set():
                # 수신 drain — 버퍼가 차지 않도록 + 자기 위치 추적
                for pkt_id, msg in self.client.poll():
                    if pkt_id is None:
                        log.info("%s disconnected", self.prefix)
                        return
                    self._handle_packet(pkt_id, msg)

                if self.in_game:
                    now = time.time()
                    dt = now - last_tick
                    last_tick = now
                    if now >= self._next_move_at:
                        self._pick_new_destination()
                        self._next_move_at = now + random.uniform(
                            MOVE_INTERVAL_MIN, MOVE_INTERVAL_MAX)
                    self._tick_move(dt, now)
                    if now >= self._next_skill_at:
                        self._send_skill()
                        self._next_skill_at = now + random.uniform(
                            SKILL_INTERVAL_MIN, SKILL_INTERVAL_MAX)
                else:
                    last_tick = time.time()

                time.sleep(0.05)
        finally:
            if self.client is not None:
                try:
                    self.client.close()
                except Exception:
                    pass


# ── 런처 ────────────────────────────────────────────────────────────
def _parse_args() -> tuple[int, str]:
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    prefix = sys.argv[2] if len(sys.argv) > 2 else "bot"
    if count <= 0:
        raise SystemExit("count must be positive")
    return count, prefix


def main() -> None:
    log_setup.setup_logging()
    count, prefix = _parse_args()

    log.info("spawning %d bots (prefix=%s, target=%s:%d)",
             count, prefix, config.LOGIN_HOST, config.LOGIN_PORT)

    bots: list[Bot] = []
    threads: list[threading.Thread] = []
    for i in range(count):
        b = Bot(username=f"{prefix}_{i + 1:04d}")
        t = threading.Thread(target=b.run, name=b.username, daemon=True)
        t.start()
        bots.append(b)
        threads.append(t)
        # 프로세스 포트 열림 순차성을 위해 살짝 스태거. 쓰레드 폭주 방지용.
        time.sleep(0.05)

    log.info("all %d bots spawned. Ctrl+C to stop.", count)

    try:
        while any(t.is_alive() for t in threads):
            time.sleep(1.0)
    except KeyboardInterrupt:
        log.info("stopping bots...")
    finally:
        for b in bots:
            b.stop_event.set()
        for t in threads:
            t.join(timeout=2.0)
        log.info("done")


if __name__ == "__main__":
    main()

"""
TCP client with MMO server packet framing.

Packet format: [uint16 size][uint32 id][protobuf payload]  (little-endian, 6-byte header)
  - size: total packet size including header
  - id:   PacketId (auto-generated)

Send/recv is non-blocking: the client runs a background reader thread that
parses incoming packets and puts them onto a queue for the main loop.
"""

import logging
import socket
import struct
import threading
import queue
from typing import Any, Iterator, Optional, Tuple

import common_pb2, login_pb2, game_pb2
import packet_ids
import config

log = logging.getLogger("net")

HEADER_FMT = "<HI"  # little-endian: uint16 size, uint32 id
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 6


# Map PacketId -> protobuf class (for parsing incoming packets)
_MSG_CLASS_MAP = {
    packet_ids.S_LOGIN:               login_pb2.S_Login,
    packet_ids.S_ENTER_GAME:          game_pb2.S_EnterGame,
    packet_ids.S_PLAYER_LIST:         game_pb2.S_PlayerList,
    packet_ids.S_UNIT_POSITIONS:      game_pb2.S_UnitPositions,
    packet_ids.S_CHAT:                game_pb2.S_Chat,
    packet_ids.S_PLAYER_LEAVE:        game_pb2.S_PlayerLeave,
    packet_ids.S_PLAYER_SPAWN:        game_pb2.S_PlayerSpawn,
    packet_ids.S_MOVE_CORRECTION:     game_pb2.S_MoveCorrection,
    packet_ids.S_ERROR:               game_pb2.S_Error,
    packet_ids.S_MONSTER_SPAWN:       game_pb2.S_MonsterSpawn,
    packet_ids.S_MONSTER_DESPAWN:     game_pb2.S_MonsterDespawn,
    packet_ids.S_MONSTER_LIST:        game_pb2.S_MonsterList,
    packet_ids.S_MONSTER_STATE:       game_pb2.S_MonsterState,
    packet_ids.S_SKILL_HIT:           game_pb2.S_SkillHit,
    packet_ids.S_UNIT_HP:             game_pb2.S_UnitHp,
    packet_ids.S_PROJECTILE_SPAWN:    game_pb2.S_ProjectileSpawn,
    packet_ids.S_PROJECTILE_DESTROY:  game_pb2.S_ProjectileDestroy,
    packet_ids.S_BUFF_APPLIED:        game_pb2.S_BuffApplied,
    packet_ids.S_BUFF_REMOVED:        game_pb2.S_BuffRemoved,
}

# Map message class -> PacketId (for sending)
_ID_MAP = {
    login_pb2.C_Login:           packet_ids.C_LOGIN,
    game_pb2.C_EnterGame:        packet_ids.C_ENTER_GAME,
    game_pb2.C_PlayerMove:       packet_ids.C_PLAYER_MOVE,
    game_pb2.C_MoveCommand:      packet_ids.C_MOVE_COMMAND,
    game_pb2.C_StopMove:         packet_ids.C_STOP_MOVE,
    game_pb2.C_Chat:             packet_ids.C_CHAT,
    game_pb2.C_UseSkill:         packet_ids.C_USE_SKILL,
}


class PacketClient:
    """Blocking connect/send, background recv thread posting to a queue."""

    def __init__(self):
        self.sock: Optional[socket.socket] = None
        self.recv_queue: queue.Queue = queue.Queue()
        self.reader_thread: Optional[threading.Thread] = None
        self.stop_event = threading.Event()

    # ----- connection lifecycle -----
    def connect(self, host: str, port: int, timeout: float = config.CONNECT_TIMEOUT) -> bool:
        try:
            self.sock = socket.create_connection((host, port), timeout=timeout)
            self.sock.settimeout(None)  # blocking for reader thread
            self.stop_event.clear()
            self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
            self.reader_thread.start()
            return True
        except OSError as e:
            log.error("connect failed: %s", e)
            self.sock = None
            return False

    def close(self) -> None:
        self.stop_event.set()
        if self.sock:
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.sock.close()
            self.sock = None

    def is_connected(self) -> bool:
        return self.sock is not None

    # ----- send -----
    def send(self, msg: Any) -> bool:
        if not self.sock:
            return False
        pkt_id = _ID_MAP.get(type(msg))
        if pkt_id is None:
            raise ValueError(f"Unknown message type: {type(msg).__name__}")

        payload = msg.SerializeToString()
        size = HEADER_SIZE + len(payload)
        header = struct.pack(HEADER_FMT, size, pkt_id)
        try:
            self.sock.sendall(header + payload)
            return True
        except OSError as e:
            log.error("send failed: %s", e)
            return False

    # ----- recv (reader thread) -----
    def _reader_loop(self) -> None:
        buf = bytearray()
        try:
            while not self.stop_event.is_set():
                chunk = self.sock.recv(4096)
                if not chunk:
                    break
                buf.extend(chunk)

                # Parse all complete packets in buffer
                while len(buf) >= HEADER_SIZE:
                    size, pkt_id = struct.unpack_from(HEADER_FMT, buf, 0)
                    if len(buf) < size:
                        break  # wait for more data
                    payload = bytes(buf[HEADER_SIZE:size])
                    del buf[:size]

                    msg_cls = _MSG_CLASS_MAP.get(pkt_id)
                    if msg_cls is None:
                        log.warning("unknown packet id=%d", pkt_id)
                        continue

                    msg = msg_cls()
                    try:
                        msg.ParseFromString(payload)
                    except Exception as e:
                        log.error("parse failed id=%d: %s", pkt_id, e)
                        continue

                    self.recv_queue.put((pkt_id, msg))
        except OSError as e:
            # 정상적인 원격 종료(connection reset 등)는 info 레벨로 기록
            log.info("reader loop ended: %s", e)
        except Exception as e:
            # 프레이밍 깨짐 등 예기치 못한 예외도 스레드를 조용히 죽이지 말고 기록
            log.exception("reader loop crashed: %s", e)
        finally:
            self.recv_queue.put((None, None))  # sentinel: disconnected

    def poll(self) -> Iterator[Tuple[Optional[int], Any]]:
        """Yield (pkt_id, msg) tuples. Non-blocking.
        (None, None) is a sentinel meaning the reader thread detected a disconnect.
        """
        while True:
            try:
                yield self.recv_queue.get_nowait()
            except queue.Empty:
                return

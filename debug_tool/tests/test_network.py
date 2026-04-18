"""network.py 프레이밍/디스패치 라운드트립 테스트."""

import socket
import struct
import threading
import time

import login_pb2
import game_pb2
import packet_ids
from network import (
    HEADER_FMT,
    HEADER_SIZE,
    PacketClient,
    _ID_MAP,
    _MSG_CLASS_MAP,
)


def test_header_format():
    # 헤더는 little-endian uint16 size + uint32 id = 6바이트
    assert HEADER_SIZE == 6
    raw = struct.pack(HEADER_FMT, 1234, 0xABCDEF)
    assert len(raw) == 6
    size, pkt_id = struct.unpack(HEADER_FMT, raw)
    assert size == 1234
    assert pkt_id == 0xABCDEF


def test_id_map_covers_client_packets():
    # 클라가 보낼 주요 메시지들은 _ID_MAP 에 등록되어 있어야 한다
    assert login_pb2.C_Login in _ID_MAP
    assert game_pb2.C_EnterGame in _ID_MAP
    assert game_pb2.C_PlayerMove in _ID_MAP
    # 대응되는 packet_ids 값과 일치해야 한다
    assert _ID_MAP[login_pb2.C_Login] == packet_ids.C_LOGIN
    assert _ID_MAP[game_pb2.C_PlayerMove] == packet_ids.C_PLAYER_MOVE


def test_msg_class_map_covers_server_packets():
    # 서버에서 올 주요 메시지의 역매핑
    assert _MSG_CLASS_MAP[packet_ids.S_LOGIN] is login_pb2.S_Login
    assert _MSG_CLASS_MAP[packet_ids.S_ENTER_GAME] is game_pb2.S_EnterGame
    assert _MSG_CLASS_MAP[packet_ids.S_PLAYER_MOVE] is game_pb2.S_PlayerMove


def _build_packet(pkt_id: int, payload: bytes) -> bytes:
    size = HEADER_SIZE + len(payload)
    return struct.pack(HEADER_FMT, size, pkt_id) + payload


def _start_one_shot_server(payload_to_send: bytes) -> int:
    """임시 listener를 띄워 1회 접속을 수락한 뒤 payload를 보내고 닫는다. 포트 반환."""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]

    def handler():
        try:
            conn, _ = srv.accept()
            try:
                conn.sendall(payload_to_send)
                time.sleep(0.2)   # 클라이언트가 읽을 시간을 준다
            finally:
                conn.close()
        finally:
            srv.close()

    threading.Thread(target=handler, daemon=True).start()
    return port


def _poll_until(client: PacketClient, predicate, timeout: float = 2.0):
    deadline = time.time() + timeout
    collected = []
    while time.time() < deadline:
        for pkt_id, msg in client.poll():
            collected.append((pkt_id, msg))
            if predicate(pkt_id, msg):
                return collected
        time.sleep(0.02)
    return collected


def test_parses_incoming_s_login_from_socket():
    """실제 로컬 소켓으로 S_Login 패킷을 흘려 보내 PacketClient가 올바르게 파싱하는지 검증."""
    msg = login_pb2.S_Login()
    msg.token = "round-trip-token"
    msg.game_server_ip = "127.0.0.1"
    msg.game_server_port = 7777

    raw = _build_packet(packet_ids.S_LOGIN, msg.SerializeToString())
    port = _start_one_shot_server(raw)

    client = PacketClient()
    assert client.connect("127.0.0.1", port, timeout=2.0) is True

    got = _poll_until(client, lambda pid, _m: pid == packet_ids.S_LOGIN)
    client.close()

    # S_LOGIN 이 수신되었는지 확인
    s_login = [(pid, m) for pid, m in got if pid == packet_ids.S_LOGIN]
    assert s_login, f"S_LOGIN not received; got={[p for p, _ in got]}"
    _, parsed = s_login[0]
    assert parsed.token == "round-trip-token"
    assert parsed.game_server_ip == "127.0.0.1"
    assert parsed.game_server_port == 7777


def test_fragmented_packets_are_reassembled():
    """헤더/바디가 분할 전송되어도 PacketClient가 하나의 패킷으로 조립해야 한다."""
    msg = login_pb2.S_Login()
    msg.token = "fragmented"
    msg.game_server_ip = "127.0.0.1"
    msg.game_server_port = 9000
    raw = _build_packet(packet_ids.S_LOGIN, msg.SerializeToString())

    # 반으로 쪼개서 텀을 두고 송신
    part1, part2 = raw[:3], raw[3:]

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]

    def handler():
        try:
            conn, _ = srv.accept()
            try:
                conn.sendall(part1)
                time.sleep(0.05)
                conn.sendall(part2)
                time.sleep(0.2)
            finally:
                conn.close()
        finally:
            srv.close()

    threading.Thread(target=handler, daemon=True).start()

    client = PacketClient()
    assert client.connect("127.0.0.1", port, timeout=2.0) is True
    got = _poll_until(client, lambda pid, _m: pid == packet_ids.S_LOGIN)
    client.close()

    s_login = [(pid, m) for pid, m in got if pid == packet_ids.S_LOGIN]
    assert s_login, "fragmented S_LOGIN not reassembled"
    assert s_login[0][1].token == "fragmented"


def test_connect_failure_returns_false():
    # 열리지 않은 포트에 연결 시도 → False 반환 (예외 발생하지 않아야 한다)
    client = PacketClient()
    # 0번 포트는 대부분 OS에서 즉시 거부된다
    ok = client.connect("127.0.0.1", 1, timeout=0.5)
    assert ok is False
    # close 가 안전해야 한다
    client.close()

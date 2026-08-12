#!/usr/bin/env python3
"""Create persistent FlorrBt protocol clients for local server load testing.

The clients connect directly to the game's TCP listener instead of opening browser
windows.  They still use the normal authentication, chat/RCON, and input packets,
and continuously drain server snapshots, so their server-side behavior matches a
connected web client without making the browser renderer the bottleneck.
"""

from __future__ import annotations

import argparse
import json
import math
import select
import socket
import struct
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


AUTH_RESULT_TYPE = 0x02
AUTH_PACKET_TYPE = 0xF0
CHAT_PACKET_TYPE = 0xF1
SNAPSHOT_TYPE = 0x01
SNAPSHOT_ACK_PACKET_TYPE = 0xF7
INPUT_FRAME_PACKET_TYPE = 0xF8
INPUT_PACKET = bytes((0x00, 0x00, 0x00))
FULL_SNAPSHOT_BASE_ID = 0xFFFFFFFF
SERVER_SNAPSHOT_HEADER_SIZE = 27
DEFAULT_TICK_RATE = 20
INPUT_LEAD_TICKS = 2
MAX_AUTH_NAME_BYTES = 32
MAX_AUTH_PASSWORD_BYTES = 64
MAX_AUTH_EMAIL_BYTES = 254
MAX_CHAT_MESSAGE_BYTES = 180
MAX_RECEIVE_BUFFER_BYTES = 8 * 1024 * 1024


@dataclass
class LoadClient:
    index: int
    name: str
    password: str
    socket: socket.socket
    receive_buffer: bytearray = field(default_factory=bytearray)
    authenticated: bool = False
    auth_error: str | None = None
    retried_login: bool = False
    received_bytes: int = 0
    received_packets: int = 0
    received_snapshots: int = 0
    received_delta_snapshots: int = 0
    sent_snapshot_acks: int = 0
    snapshot_id: int | None = None
    base_snapshot_id: int | None = None
    server_tick: int | None = None
    server_tick_received_at: float = 0.0
    tick_rate: int = DEFAULT_TICK_RATE
    input_sequence: int = 0
    supports_input_frames: bool = False

    def send(self, payload: bytes) -> None:
        self.socket.sendall(payload)

    def send_auth(self, register: bool, email: str = "", code: str = "") -> None:
        name = self.name.encode("utf-8")
        password = self.password.encode("utf-8")
        encoded_email = email.encode("utf-8")
        encoded_code = code.encode("utf-8")
        if not name or len(name) > MAX_AUTH_NAME_BYTES:
            raise ValueError(f"account name must be 1-{MAX_AUTH_NAME_BYTES} UTF-8 bytes: {self.name!r}")
        if len(password) > MAX_AUTH_PASSWORD_BYTES:
            raise ValueError(f"password must be at most {MAX_AUTH_PASSWORD_BYTES} UTF-8 bytes")
        if len(encoded_email) > MAX_AUTH_EMAIL_BYTES or len(encoded_code) > 32:
            raise ValueError("email or verification code is too long")
        self.send(bytes((AUTH_PACKET_TYPE, int(register), len(name), len(password), len(encoded_email),
                         len(encoded_code))) + name + password + encoded_email + encoded_code)

    def send_chat(self, text: str) -> None:
        message = text.encode("utf-8")
        if not message or len(message) > MAX_CHAT_MESSAGE_BYTES:
            raise ValueError(f"chat message must be 1-{MAX_CHAT_MESSAGE_BYTES} UTF-8 bytes: {text!r}")
        self.send(bytes((CHAT_PACKET_TYPE, 0, len(message))) + message)

    def send_snapshot_ack(self, snapshot_id: int) -> None:
        self.send(struct.pack("<BI", SNAPSHOT_ACK_PACKET_TYPE, snapshot_id))
        self.sent_snapshot_acks += 1

    def estimate_target_tick(self) -> int:
        if self.server_tick is None:
            return 0
        elapsed = max(0.0, time.monotonic() - self.server_tick_received_at)
        estimated_tick = self.server_tick + int(elapsed * max(1, self.tick_rate)) + INPUT_LEAD_TICKS
        return min(estimated_tick, 0xFFFFFFFFFFFFFFFF)

    def send_input(self, elapsed: float, attack: bool, move: bool) -> None:
        if not self.supports_input_frames:
            self.send(INPUT_PACKET)
            return

        move_x = 0
        move_y = 0
        if move:
            # Alternate direction every two seconds so clients exercise movement without
            # permanently leaving their assigned farming area.
            direction = (self.index * 2.399963229728653 + math.pi * int(elapsed / 2.0))
            move_x = round(math.cos(direction) * 127)
            move_y = round(math.sin(direction) * 127)

        self.send(struct.pack(
            "<BIQbbB",
            INPUT_FRAME_PACKET_TYPE,
            self.input_sequence,
            self.estimate_target_tick(),
            move_x,
            move_y,
            1 if attack else 0,
        ))
        self.input_sequence = (self.input_sequence + 1) & 0xFFFFFFFF

    def close(self) -> None:
        try:
            self.socket.close()
        except OSError:
            pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create persistent FlorrBt load-test clients.")
    parser.add_argument("--host", default="127.0.0.1", help="game TCP host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=10012, help="game TCP port (default: 10012)")
    parser.add_argument("--count", type=int, default=100, help="number of clients (default: 100)")
    parser.add_argument("--prefix", default="loadbot", help="account name prefix (default: loadbot)")
    parser.add_argument("--account-password", default="FlorrBtLoadTest", help="test account password")
    parser.add_argument("--rcon-password", default="1DnjLE", help="RCON password")
    parser.add_argument("--world-layout", default="garden,ant_hel,garden,ant_hel",
                        help="comma-separated map name for each world ID")
    parser.add_argument("--command-interval", type=float, default=0.04,
                        help="minimum spacing between RCON commands in seconds (default: 0.04)")
    parser.add_argument("--equip-petal", default="",
                        help="petal to equip in every requested test slot (disabled by default)")
    parser.add_argument("--equip-rarity", default="primordial", help="rarity used by --equip-petal")
    parser.add_argument("--equip-slots", type=int, default=5, help="slots per client used by --equip-petal")
    parser.add_argument("--input-interval", type=float, default=0.05,
                        help="seconds between input packets; 0 disables them (default: 0.05)")
    parser.add_argument("--no-attack", action="store_true", help="do not hold attack during the load test")
    parser.add_argument("--no-move", action="store_true", help="do not move clients during the load test")
    parser.add_argument("--duration", type=float, default=0.0,
                        help="run duration in seconds; 0 runs until Ctrl+C (default: 0)")
    return parser.parse_args()


def iter_map_objects(map_json: dict) -> Iterable[dict]:
    for layer in map_json.get("layers", []):
        yield from layer.get("objects", [])


def property_value(obj: dict, name: str, default: object = None) -> object:
    for prop in obj.get("properties", []):
        if prop.get("name") == name:
            return prop.get("value", default)
    return default


def polygon_center(obj: dict) -> tuple[float, float]:
    origin_x = float(obj.get("x", 0.0))
    origin_y = float(obj.get("y", 0.0))
    polygon = obj.get("polygon") or []
    if polygon:
        return (
            origin_x + sum(float(point.get("x", 0.0)) for point in polygon) / len(polygon),
            origin_y + sum(float(point.get("y", 0.0)) for point in polygon) / len(polygon),
        )
    return (
        origin_x + float(obj.get("width", 0.0)) * 0.5,
        origin_y + float(obj.get("height", 0.0)) * 0.5,
    )


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise RuntimeError(f"could not read map {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise RuntimeError(f"could not parse map {path}: {error}") from error


def spawn_zone_positions(map_path: Path) -> list[tuple[float, float]]:
    map_json = load_json(map_path)
    positions: list[tuple[float, float]] = []
    for obj in iter_map_objects(map_json):
        if obj.get("class") != "spawn_mobs":
            continue
        if float(property_value(obj, "density", 0.0) or 0.0) <= 0.0:
            continue
        positions.append(polygon_center(obj))
    if not positions:
        raise RuntimeError(f"no non-zero-density spawn zones found in {map_path}")
    return positions


def distribute_positions(positions: list[tuple[float, float]], count: int) -> list[tuple[float, float]]:
    if count <= 0:
        return []
    if count == 1:
        return [positions[len(positions) // 2]]

    selected: list[tuple[float, float]] = []
    for index in range(count):
        source_index = round(index * (len(positions) - 1) / (count - 1))
        x, y = positions[source_index]
        reuse_index = index // len(positions)
        selected.append((x + reuse_index * 160.0, y + reuse_index * 160.0))
    return selected


def make_client(host: str, port: int, index: int, name: str, password: str) -> LoadClient:
    client_socket = socket.create_connection((host, port), timeout=10.0)
    client_socket.setblocking(False)
    return LoadClient(index=index, name=name, password=password, socket=client_socket)


def parse_snapshot_header(payload: bytes) -> tuple[int, int, int] | None:
    if len(payload) < SERVER_SNAPSHOT_HEADER_SIZE or payload[0] != SNAPSHOT_TYPE:
        return None

    snapshot_id, base_snapshot_id, server_tick = struct.unpack_from("<IIQ", payload, 1)
    return snapshot_id, base_snapshot_id, server_tick


def process_frames(client: LoadClient) -> None:
    while len(client.receive_buffer) >= 2:
        packet_size = client.receive_buffer[0] | (client.receive_buffer[1] << 8)
        if len(client.receive_buffer) < packet_size + 2:
            return

        payload = bytes(client.receive_buffer[2:packet_size + 2])
        del client.receive_buffer[:packet_size + 2]
        client.received_packets += 1
        if not payload:
            continue

        if payload[0] == SNAPSHOT_TYPE:
            header = parse_snapshot_header(payload)
            if header is None:
                continue
            snapshot_id, base_snapshot_id, server_tick = header
            client.snapshot_id = snapshot_id
            client.base_snapshot_id = base_snapshot_id
            client.server_tick = server_tick
            client.server_tick_received_at = time.monotonic()
            client.supports_input_frames = True
            client.received_snapshots += 1
            if base_snapshot_id != FULL_SNAPSHOT_BASE_ID:
                client.received_delta_snapshots += 1
            client.send_snapshot_ack(snapshot_id)
            continue

        if payload[0] == 0x00 and len(payload) >= 6:
            client.tick_rate = payload[5] or DEFAULT_TICK_RATE
            continue

        if len(payload) < 3 or payload[0] != AUTH_RESULT_TYPE:
            continue

        result_code = payload[1]
        message_length = payload[2]
        message = payload[3:3 + message_length].decode("utf-8", errors="replace")
        if result_code == 1:
            client.authenticated = True
            client.auth_error = None
        else:
            client.auth_error = message or "authentication failed"


def pump(clients: Iterable[LoadClient], timeout: float) -> None:
    active = [client for client in clients if client.socket.fileno() >= 0]
    if not active:
        return

    readable, _, failed = select.select([client.socket for client in active], [], [client.socket for client in active], timeout)
    failed_set = set(failed)
    for client in active:
        if client.socket in failed_set:
            raise ConnectionError(f"socket error for {client.name}")
        if client.socket not in readable:
            continue
        try:
            data = client.socket.recv(65536)
        except BlockingIOError:
            continue
        if not data:
            raise ConnectionError(f"server disconnected {client.name}")
        client.received_bytes += len(data)
        client.receive_buffer.extend(data)
        if len(client.receive_buffer) > MAX_RECEIVE_BUFFER_BYTES:
            raise BufferError(f"receive buffer overflow for {client.name}")
        process_frames(client)


def wait_for_auth(clients: list[LoadClient], timeout: float) -> None:
    deadline = time.monotonic() + timeout
    pending = list(clients)
    while pending and time.monotonic() < deadline:
        pump(clients, min(0.1, max(0.0, deadline - time.monotonic())))
        for client in pending[:]:
            if client.authenticated:
                pending.remove(client)
                continue
            if client.auth_error == "Account already exists" and not client.retried_login:
                client.retried_login = True
                client.auth_error = None
                client.send_auth(register=False)
                continue
            if client.auth_error:
                raise RuntimeError(f"{client.name}: {client.auth_error}")
    if pending:
        names = ", ".join(client.name for client in pending)
        raise TimeoutError(f"timed out waiting for authentication: {names}")


def wait_while_pumping(clients: list[LoadClient], seconds: float) -> None:
    deadline = time.monotonic() + max(0.0, seconds)
    while time.monotonic() < deadline:
        pump(clients, min(0.1, deadline - time.monotonic()))


def send_rcon(leader: LoadClient, command: str, command_interval: float, clients: list[LoadClient]) -> None:
    leader.send_chat(f"/rcon {command}")
    wait_while_pumping(clients, command_interval)


def set_positions(leader: LoadClient, targets: list[LoadClient], world_id: int,
                  positions: list[tuple[float, float]], command_interval: float,
                  clients: list[LoadClient]) -> None:
    for target, (x, y) in zip(targets, positions):
        send_rcon(leader, f"tele {target.name} {world_id} {x:.0f},{y:.0f}", command_interval, clients)


def setup_world_distribution(clients: list[LoadClient], root: Path, args: argparse.Namespace) -> None:
    world_maps = [name.strip() for name in args.world_layout.split(",") if name.strip()]
    if not world_maps:
        raise ValueError("--world-layout must contain at least one map")
    world_zones = [spawn_zone_positions(root / "data" / "maps" / f"{name}.tmj") for name in world_maps]
    world_clients = [[] for _ in world_maps]
    for index, client in enumerate(clients):
        world_clients[index % len(world_maps)].append(client)
    leader = clients[0]

    print(f"RCON: authorizing {leader.name}", flush=True)
    send_rcon(leader, args.rcon_password, args.command_interval, clients)
    send_rcon(leader, f"add null primordial {leader.name}", args.command_interval, clients)
    if args.equip_petal:
        slot_count = max(0, args.equip_slots)
        print(f"Equipping {slot_count} {args.equip_rarity} {args.equip_petal} slots per client", flush=True)
        for target in clients:
            for slot in range(slot_count):
                send_rcon(leader, f"equip {target.name} {slot} {args.equip_petal} {args.equip_rarity}",
                          args.command_interval, clients)

    for world_id, (map_name, targets, zones) in enumerate(zip(world_maps, world_clients, world_zones)):
        print(f"World {world_id} ({map_name}): distributing {len(targets)} clients across "
              f"{len(zones)} spawn zones", flush=True)
        set_positions(leader, targets, world_id, distribute_positions(zones, len(targets)),
                      args.command_interval, clients)


def run_clients(clients: list[LoadClient], duration: float, input_interval: float,
                attack: bool, move: bool) -> None:
    started = time.monotonic()
    deadline = time.monotonic() + duration if duration > 0.0 else None
    next_input = time.monotonic()
    next_status = time.monotonic() + 10.0
    print("Load clients are connected. Press Ctrl+C to stop." if deadline is None else
          f"Load clients are connected for {duration:g}s.", flush=True)

    while deadline is None or time.monotonic() < deadline:
        now = time.monotonic()
        if input_interval > 0.0 and now >= next_input:
            for client in clients:
                client.send_input(now - started, attack, move)
            next_input = now + input_interval

        wait_timeout = 0.1
        if deadline is not None:
            wait_timeout = min(wait_timeout, max(0.0, deadline - now))
        pump(clients, wait_timeout)

        if now >= next_status:
            received_bytes = sum(client.received_bytes for client in clients)
            received_packets = sum(client.received_packets for client in clients)
            received_snapshots = sum(client.received_snapshots for client in clients)
            received_deltas = sum(client.received_delta_snapshots for client in clients)
            sent_acks = sum(client.sent_snapshot_acks for client in clients)
            print(
                f"status: clients={len(clients)} packets={received_packets} "
                f"snapshots={received_snapshots} deltas={received_deltas} acks={sent_acks} "
                f"received={received_bytes / 1024 / 1024:.1f} MiB",
                flush=True,
            )
            clients[0].send_chat("/rcon perf")
            next_status = now + 10.0


def main() -> int:
    args = parse_args()
    if args.count <= 0:
        raise ValueError("--count must be positive")
    if len(args.prefix.encode("utf-8")) > MAX_AUTH_NAME_BYTES - 3:
        raise ValueError("--prefix is too long to reserve a two-digit client suffix")

    root = Path(__file__).resolve().parents[1]
    clients: list[LoadClient] = []
    try:
        print(f"Connecting {args.count} clients to {args.host}:{args.port}", flush=True)
        for index in range(args.count):
            name = f"{args.prefix}{index:02d}"
            client = make_client(args.host, args.port, index, name, args.account_password)
            clients.append(client)
            client.send_auth(register=False)

        wait_for_auth(clients, timeout=max(30.0, args.count * 2.0))
        print("All clients authenticated.", flush=True)
        setup_world_distribution(clients, root, args)
        run_clients(clients, args.duration, args.input_interval, not args.no_attack, not args.no_move)
        return 0
    finally:
        for client in clients:
            client.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nStopped.")
    except Exception as error:
        print(f"load test failed: {error}", file=sys.stderr)
        raise SystemExit(1)

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
import select
import socket
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


AUTH_RESULT_TYPE = 0x02
AUTH_PACKET_TYPE = 0xF0
CHAT_PACKET_TYPE = 0xF1
INPUT_PACKET = bytes((0x00, 0x00, 0x00))
MAX_AUTH_NAME_BYTES = 32
MAX_AUTH_PASSWORD_BYTES = 64
MAX_CHAT_MESSAGE_BYTES = 180
MAX_RECEIVE_BUFFER_BYTES = 8 * 1024 * 1024


@dataclass
class LoadClient:
    name: str
    password: str
    socket: socket.socket
    receive_buffer: bytearray = field(default_factory=bytearray)
    authenticated: bool = False
    auth_error: str | None = None
    retried_login: bool = False
    received_bytes: int = 0
    received_packets: int = 0

    def send(self, payload: bytes) -> None:
        self.socket.sendall(payload)

    def send_auth(self, register: bool) -> None:
        name = self.name.encode("utf-8")
        password = self.password.encode("utf-8")
        if not name or len(name) > MAX_AUTH_NAME_BYTES:
            raise ValueError(f"account name must be 1-{MAX_AUTH_NAME_BYTES} UTF-8 bytes: {self.name!r}")
        if len(password) > MAX_AUTH_PASSWORD_BYTES:
            raise ValueError(f"password must be at most {MAX_AUTH_PASSWORD_BYTES} UTF-8 bytes")
        self.send(bytes((AUTH_PACKET_TYPE, int(register), len(name), len(password))) + name + password)

    def send_chat(self, text: str) -> None:
        message = text.encode("utf-8")
        if not message or len(message) > MAX_CHAT_MESSAGE_BYTES:
            raise ValueError(f"chat message must be 1-{MAX_CHAT_MESSAGE_BYTES} UTF-8 bytes: {text!r}")
        self.send(bytes((CHAT_PACKET_TYPE, 0, len(message))) + message)

    def close(self) -> None:
        try:
            self.socket.close()
        except OSError:
            pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create persistent FlorrBt load-test clients.")
    parser.add_argument("--host", default="127.0.0.1", help="game TCP host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=10012, help="game TCP port (default: 10012)")
    parser.add_argument("--count", type=int, default=32, help="number of clients (default: 32)")
    parser.add_argument("--prefix", default="loadbot", help="account name prefix (default: loadbot)")
    parser.add_argument("--account-password", default="FlorrBtLoadTest", help="test account password")
    parser.add_argument("--rcon-password", default="1DnjLE", help="RCON password")
    parser.add_argument("--portal-wait", type=float, default=4.0,
                        help="seconds to wait for spawn protection before portal transfer (default: 4)")
    parser.add_argument("--transfer-wait", type=float, default=1.0,
                        help="seconds to wait for the Anthell portal transfer (default: 1)")
    parser.add_argument("--command-interval", type=float, default=0.04,
                        help="minimum spacing between RCON commands in seconds (default: 0.04)")
    parser.add_argument("--input-interval", type=float, default=1.0,
                        help="seconds between no-op input packets; 0 disables them (default: 1)")
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


def portal_position(map_path: Path, target_map: str) -> tuple[float, float]:
    map_json = load_json(map_path)
    target_map = target_map.lower()
    for obj in iter_map_objects(map_json):
        if obj.get("class") != "warp":
            continue
        goal = str(property_value(obj, "map", "")).lower()
        if goal == target_map:
            return float(obj.get("x", 0.0)), float(obj.get("y", 0.0))
    raise RuntimeError(f"no warp from {map_path.name} to {target_map}")


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


def make_client(host: str, port: int, name: str, password: str) -> LoadClient:
    client_socket = socket.create_connection((host, port), timeout=10.0)
    client_socket.setblocking(False)
    return LoadClient(name=name, password=password, socket=client_socket)


def process_frames(client: LoadClient) -> None:
    while len(client.receive_buffer) >= 2:
        packet_size = client.receive_buffer[0] | (client.receive_buffer[1] << 8)
        if len(client.receive_buffer) < packet_size + 2:
            return

        payload = bytes(client.receive_buffer[2:packet_size + 2])
        del client.receive_buffer[:packet_size + 2]
        client.received_packets += 1
        if len(payload) < 3 or payload[0] != AUTH_RESULT_TYPE:
            continue

        success = payload[1] != 0
        message_length = payload[2]
        message = payload[3:3 + message_length].decode("utf-8", errors="replace")
        if success:
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


def set_positions(leader: LoadClient, targets: list[LoadClient], positions: list[tuple[float, float]],
                  command_interval: float, clients: list[LoadClient]) -> None:
    for target, (x, y) in zip(targets, positions):
        send_rcon(leader, f"tele {target.name} {x:.0f},{y:.0f}", command_interval, clients)


def setup_world_distribution(clients: list[LoadClient], root: Path, args: argparse.Namespace) -> None:
    garden_map = root / "data" / "maps" / "garden.tmj"
    anthell_map = root / "data" / "maps" / "ant_hel.tmj"
    garden_zones = spawn_zone_positions(garden_map)
    anthell_zones = spawn_zone_positions(anthell_map)
    garden_portal = portal_position(garden_map, "ant_hel")

    garden_count = (len(clients) + 1) // 2
    garden_clients = clients[:garden_count]
    anthell_clients = clients[garden_count:]
    leader = garden_clients[0]

    print(f"RCON: authorizing {leader.name}", flush=True)
    send_rcon(leader, args.rcon_password, args.command_interval, clients)
    send_rcon(leader, f"add null primordial {leader.name}", args.command_interval, clients)

    print(f"Garden: distributing {len(garden_clients)} clients across {len(garden_zones)} spawn zones", flush=True)
    set_positions(leader, garden_clients, distribute_positions(garden_zones, len(garden_clients)),
                  args.command_interval, clients)
    if not anthell_clients:
        return

    print(f"Waiting {args.portal_wait:g}s for spawn protection before Anthell transfer", flush=True)
    wait_while_pumping(clients, args.portal_wait)
    print(f"Anthell: moving {len(anthell_clients)} clients through Garden portal at {garden_portal[0]:.0f},{garden_portal[1]:.0f}", flush=True)
    set_positions(leader, anthell_clients, [garden_portal] * len(anthell_clients), args.command_interval, clients)
    wait_while_pumping(clients, args.transfer_wait)
    print(f"Anthell: distributing clients across {len(anthell_zones)} spawn zones", flush=True)
    set_positions(leader, anthell_clients, distribute_positions(anthell_zones, len(anthell_clients)),
                  args.command_interval, clients)


def run_clients(clients: list[LoadClient], duration: float, input_interval: float) -> None:
    deadline = time.monotonic() + duration if duration > 0.0 else None
    next_input = time.monotonic()
    next_status = time.monotonic() + 10.0
    print("Load clients are connected. Press Ctrl+C to stop." if deadline is None else
          f"Load clients are connected for {duration:g}s.", flush=True)

    while deadline is None or time.monotonic() < deadline:
        now = time.monotonic()
        if input_interval > 0.0 and now >= next_input:
            for client in clients:
                client.send(INPUT_PACKET)
            next_input = now + input_interval

        wait_timeout = 0.1
        if deadline is not None:
            wait_timeout = min(wait_timeout, max(0.0, deadline - now))
        pump(clients, wait_timeout)

        if now >= next_status:
            received_bytes = sum(client.received_bytes for client in clients)
            received_packets = sum(client.received_packets for client in clients)
            print(f"status: clients={len(clients)} packets={received_packets} received={received_bytes / 1024 / 1024:.1f} MiB", flush=True)
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
            client = make_client(args.host, args.port, name, args.account_password)
            clients.append(client)
            client.send_auth(register=True)

        wait_for_auth(clients, timeout=max(30.0, args.count * 2.0))
        print("All clients authenticated.", flush=True)
        setup_world_distribution(clients, root, args)
        run_clients(clients, args.duration, args.input_interval)
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

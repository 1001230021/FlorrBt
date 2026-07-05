import argparse
import socket
import struct
import time


COORD_SCALE = 64.0
RADIUS_SCALE = 16.0
ANGLE_SCALE = 1000.0

MSG_WELCOME = 0x00
MSG_SNAPSHOT = 0x01
MSG_OWNER_STATE = 0x10

MOB_NAMES = {
    0: "None",
    1: "Beetle",
    2: "Gambler",
    3: "NormalLadybug",
    4: "MechaFlower",
    5: "NormalFlower",
    6: "PlayerFlower",
}

CHORE_AGREE_BIT = 4
CHORE_ATTACK_BIT = 2
CHORE_DEFEND_BIT = 3
CLIENT_CHORES_TYPE = 0x03


def unpack_coord(value):
    return value / COORD_SCALE


def unpack_radius(value):
    return value / RADIUS_SCALE


def unpack_percent(value):
    return value / 255.0


def unpack_angle(value):
    return value / ANGLE_SCALE


def recv_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("server closed the connection")
        data.extend(chunk)
    return bytes(data)


def read_packet(sock):
    header = recv_exact(sock, 2)
    (length,) = struct.unpack_from("<H", header)
    if length == 0:
        raise ValueError("received empty packet")
    return recv_exact(sock, length)


def parse_entity(data, offset):
    entity_id, entity_type, team, x, y, radius, hp, flags, angle = struct.unpack_from("<HBBiiHBBh", data, offset)
    return {
        "id": entity_id,
        "type": entity_type,
        "team": team,
        "x": unpack_coord(x),
        "y": unpack_coord(y),
        "radius": unpack_radius(radius),
        "hp": unpack_percent(hp),
        "flags": flags,
        "angle": unpack_angle(angle),
    }, offset + 18


def parse_server_message(data):
    msg_type = data[0]
    offset = 1

    if msg_type == MSG_WELCOME:
        player_id, owner_id, tick_rate = struct.unpack_from("<HHB", data, offset)
        return {
            "type": "Welcome",
            "player_id": player_id,
            "owner_entity_id": owner_id,
            "tick_rate": tick_rate,
        }

    if msg_type == MSG_SNAPSHOT:
        snapshot_id, owner_id, view_radius_raw, count = struct.unpack_from("<IHiH", data, offset)
        offset += 12
        entities = []
        for _ in range(count):
            entity, offset = parse_entity(data, offset)
            entities.append(entity)
        return {
            "type": "Snapshot",
            "snapshot_id": snapshot_id,
            "owner_entity_id": owner_id,
            "view_radius": unpack_coord(view_radius_raw),
            "entities": entities,
        }

    if msg_type == MSG_OWNER_STATE:
        level, flags, petal_slots = struct.unpack_from("<BBB", data, offset)
        return {
            "type": "OwnerState",
            "level": level,
            "flags": flags,
            "petal_slots": petal_slots,
        }

    return {
        "type": "Unknown",
        "raw_type": msg_type,
        "bytes": data.hex(" "),
    }


def send_input(sock, move_x, move_y):
    sock.sendall(bytes([0x00, move_x & 0xFF, move_y & 0xFF]))


def send_attack(sock, attack=True, defend=False, agree=False):
    value = CLIENT_CHORES_TYPE
    if attack:
        value |= 1 << CHORE_ATTACK_BIT
    if defend:
        value |= 1 << CHORE_DEFEND_BIT
    if agree:
        value |= 1 << CHORE_AGREE_BIT
    sock.sendall(bytes([value]))


def format_entity(entity):
    if entity["type"] >= 100:
        type_name = f"Petal({entity['type'] - 100})"
    else:
        type_name = MOB_NAMES.get(entity["type"], f"Mob({entity['type']})")
    return (
        f"id={entity['id']} type={type_name} team={entity['team']} "
        f"pos=({entity['x']:.2f}, {entity['y']:.2f}) "
        f"r={entity['radius']:.2f} hp={entity['hp']:.2f} flags=0x{entity['flags']:02x}"
    )


def main():
    parser = argparse.ArgumentParser(description="Tiny FlorrBt server packet tester")
    parser.add_argument("host", nargs="?", default="127.0.0.1")
    parser.add_argument("port", nargs="?", type=int, default=10012)
    parser.add_argument("--seconds", type=float, default=0.0, help="0 means run until disconnected")
    parser.add_argument("--move-x", type=int, default=127)
    parser.add_argument("--move-y", type=int, default=0)
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5.0) as sock:
        print(f"Connected to {args.host}:{args.port}")
        send_input(sock, args.move_x, args.move_y)
        send_attack(sock, attack=True)
        print("Sent input and attack packets")

        sock.settimeout(2.0)
        end_time = None if args.seconds <= 0.0 else time.time() + args.seconds
        while end_time is None or time.time() < end_time:
            try:
                packet = read_packet(sock)
            except socket.timeout:
                print("No packet for 2 seconds; still connected, waiting...")
                continue

            msg = parse_server_message(packet)

            if msg["type"] == "Snapshot":
                print(
                    f"Snapshot #{msg['snapshot_id']} owner={msg['owner_entity_id']} "
                    f"view={msg['view_radius']:.2f} entities={len(msg['entities'])}"
                )
                for entity in msg["entities"][:5]:
                    print("  " + format_entity(entity))
                if len(msg["entities"]) > 5:
                    print(f"  ... {len(msg['entities']) - 5} more")
            else:
                print(msg)


if __name__ == "__main__":
    main()

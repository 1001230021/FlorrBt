"""Offline protocol tests: never open sockets or authenticate real accounts."""
import struct
import unittest
from unittest.mock import patch

from tools import load_test_clients as load


class FakeSocket:
    def __init__(self, writes=None):
        self.writes = list(writes or [])
        self.output = bytearray()

    def send(self, payload):
        count = self.writes.pop(0) if self.writes else len(payload)
        if count is None:
            raise BlockingIOError
        count = min(count, len(payload))
        self.output.extend(payload[:count])
        return count

    def fileno(self):
        return 1


class LoadClientProtocolTests(unittest.TestCase):
    def client(self, writes=None):
        return load.LoadClient(index=0, name="offline", password="fixture", socket=FakeSocket(writes))

    def test_version_precedes_auth_once_per_connection(self):
        client = self.client()
        client.send_auth(register=False)
        first = bytes(client.socket.output)
        self.assertEqual(first[:4], load.version_packet())
        self.assertEqual(first[4], load.AUTH_PACKET_TYPE)
        client.send_auth(register=False)
        self.assertEqual(client.socket.output[len(first)], load.AUTH_PACKET_TYPE)

    def test_invalid_auth_has_no_transport_side_effects(self):
        client = self.client()
        client.name = ""
        with self.assertRaises(ValueError):
            client.send_auth(register=False)
        self.assertFalse(client.version_sent)
        self.assertEqual(client.socket.output, b"")

    def test_partial_send_and_backpressure_preserve_packet_order(self):
        client = self.client([1, None, 2, None])
        client.send_auth(register=False)
        with patch.object(load.select, "select", return_value=([], [client.socket], [])):
            load.pump([client], 0)
        expected = self.client()
        expected.send_auth(register=False)
        self.assertEqual(client.socket.output, expected.socket.output)
        self.assertEqual(client.send_buffer, b"")

    def test_output_queue_is_bounded(self):
        client = self.client([None])
        client.send(b"a" * load.MAX_SEND_BUFFER_BYTES)
        with self.assertRaises(BufferError):
            client.send(b"b")
        self.assertEqual(len(client.send_buffer), load.MAX_SEND_BUFFER_BYTES)

    def test_welcome_uses_wide_owner_id_layout(self):
        client = self.client()
        payload = struct.pack("<BHIBBB", 0, 12, 0x12345678, 30, 0, 1)
        frame = struct.pack("<H", len(payload)) + payload
        client.receive_buffer.extend(frame[:-1])
        load.process_frames(client)
        self.assertEqual(client.received_packets, 0)
        client.receive_buffer.extend(frame[-1:])
        load.process_frames(client)
        self.assertEqual(client.tick_rate, 30)
        self.assertEqual(client.received_packets, 1)

    def test_snapshot_header_and_ack_keep_full_32_bit_ids(self):
        client = self.client()
        payload = struct.pack("<BIIQIihh", 1, 0xFFFFFFFE, load.FULL_SNAPSHOT_BASE_ID,
                              12345, 65537, 64000, 0, 0) + bytes(6)
        client.receive_buffer.extend(struct.pack("<H", len(payload)) + payload)
        load.process_frames(client)
        self.assertEqual(client.snapshot_id, 0xFFFFFFFE)
        self.assertEqual(client.server_tick, 12345)
        self.assertEqual(client.socket.output, struct.pack("<BI", load.SNAPSHOT_ACK_PACKET_TYPE, 0xFFFFFFFE))


if __name__ == "__main__":
    unittest.main()

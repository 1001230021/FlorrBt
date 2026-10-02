// Pure bridge/codec tests: no listeners, sockets, child processes or accounts.
import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { DebugFrameDecoder, packDebugMessage } from "../../web/src/dev_debug_protocol.js";
const require = createRequire(import.meta.url);
const lib = require("../../tools/web_bridge_lib.js");

function fakeSocket() {
  return { destroyed: false, writableLength: 0, writes: [],
    write(data) { this.writes.push(Buffer.from(data)); return true; },
    destroy() { this.destroyed = true; },
  };
}
function serverDebugFrame(message) {
  const data = Buffer.from(JSON.stringify(message)), header = Buffer.alloc(6);
  header[0] = 0xd0; header[1] = 1; header.writeUInt32LE(data.length, 2);
  return Buffer.concat([header, data]);
}
function websocketPayloads(writes) {
  const messages = [];
  const parser = lib.createFrameParser(payload => messages.push(Buffer.from(payload)), () => assert.fail("invalid test websocket frame"), () => {});
  for (const frame of writes) parser(frame);
  return messages;
}

test("debug bridge preserves fragmented frames larger than the game 64KiB limit", () => {
  const socket = fakeSocket(), forwarder = lib.createServerPacketForwarder(socket, { snapshotBacklogDropBytes: 1 });
  assert.equal(forwarder.selectProtocol(packDebugMessage({ op: "hello", ticket: "a".repeat(64) })), true);
  const snapshot = { kind: "snapshot", world: 1, worldGeneration: "2", note: "x".repeat(90000), entities: [] };
  const result = { kind: "result", request: 4, ok: true, message: "世界" };
  const frames = Buffer.concat([serverDebugFrame(snapshot), serverDebugFrame(result)]);
  forwarder.push(frames.subarray(0, 3)); forwarder.push(frames.subarray(3, 19)); forwarder.push(frames.subarray(19));
  const decoder = new DebugFrameDecoder(), decoded = [];
  for (const payload of websocketPayloads(socket.writes)) decoded.push(...decoder.push(payload));
  assert.deepEqual(decoded, [snapshot, result], "debug snapshots and action results must never enter lossy coalescing");
  forwarder.close();
});

test("game bridge keeps its framing/coalescing and cannot switch protocol mid-connection", async () => {
  const socket = fakeSocket(), forwarder = lib.createServerPacketForwarder(socket, { snapshotBacklogDropBytes: 128 * 1024 });
  forwarder.selectProtocol(Buffer.from([0xfb, 1, 0, 3]));
  forwarder.selectProtocol(packDebugMessage({ op: "hello", ticket: "b".repeat(64) }));
  const first = lib.frameServerPayload(Buffer.from([1, 10]));
  const last = lib.frameServerPayload(Buffer.from([1, 11]));
  const control = lib.frameServerPayload(Buffer.from([2, 12]));
  forwarder.push(Buffer.concat([first, last, control]));
  await Promise.resolve();
  assert.deepEqual(websocketPayloads(socket.writes), [control, last]);
  forwarder.close();
});

test("debug bridge bounds slow websocket and TCP peers without dropping successful replies", () => {
  for (const direction of ["websocket", "tcp", "request-size"]) {
    const socket = fakeSocket(); let closed = 0;
    const forwarder = lib.createServerPacketForwarder(socket, { onOverflow: () => { closed++; socket.destroy(); } });
    const hello = packDebugMessage({ op: "hello", ticket: "c".repeat(64) });
    forwarder.selectProtocol(hello);
    if (direction === "websocket") {
      socket.writableLength = lib.maxDebugBridgeBacklog;
      forwarder.push(serverDebugFrame({ kind: "logs", lines: [] }));
    } else if (direction === "tcp") {
      assert.equal(forwarder.selectProtocol(hello, lib.maxDebugBridgeBacklog), false);
    } else {
      assert.equal(forwarder.selectProtocol(new Uint8Array(lib.maxDebugClientFrameSize + 1)), false);
    }
    assert.equal(closed, 1); assert.equal(socket.writes.length, 0);
    forwarder.push(Buffer.from([1, 2, 3])); assert.equal(socket.writes.length, 0);
    forwarder.close();
  }
});

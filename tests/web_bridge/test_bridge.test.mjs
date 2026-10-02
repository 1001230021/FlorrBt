import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { spawn } from "node:child_process";
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const lib = require("../../tools/web_bridge_lib.js");
const { createFrameParser, createFileSender } = lib;

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");

function frame(opcode, payload, { masked = false, extended = null } = {}) {
  const data = Buffer.from(payload || []);
  const maskFlag = masked ? 0x80 : 0;
  const mask = masked ? Buffer.from([0x12, 0x34, 0x56, 0x78]) : null;
  let header;
  let lengthCode;
  let lengthBytes;
  if (extended !== null) {
    lengthCode = 127;
    lengthBytes = Buffer.alloc(8);
    lengthBytes.writeBigUInt64BE(BigInt(extended));
  } else if (data.length < 126) {
    lengthCode = data.length;
    lengthBytes = Buffer.alloc(0);
  } else if (data.length <= 0xffff) {
    lengthCode = 126;
    lengthBytes = Buffer.alloc(2);
    lengthBytes.writeUInt16BE(data.length);
  } else {
    lengthCode = 127;
    lengthBytes = Buffer.alloc(8);
    lengthBytes.writeBigUInt64BE(BigInt(data.length));
  }
  header = Buffer.concat([
    Buffer.from([0x80 | opcode, maskFlag | lengthCode]),
    lengthBytes,
    mask || Buffer.alloc(0),
  ]);
  const payloadBytes = masked ? maskPayload(data, mask) : data;
  return Buffer.concat([header, payloadBytes]);
}

function maskPayload(data, mask) {
  const out = Buffer.from(data);
  for (let i = 0; i < out.length; i += 1) out[i] ^= mask[i & 3];
  return out;
}

function collectParser(limits) {
  const events = [];
  const parser = createFrameParser(
    (payload) => events.push({ type: "data", payload: Buffer.from(payload) }),
    () => events.push({ type: "close" }),
    (payload) => events.push({ type: "ping", payload: Buffer.from(payload) }),
  );
  return { parser, events };
}

function freePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.listen(0, "127.0.0.1", () => {
      const port = server.address().port;
      server.close(() => resolve(port));
    });
    server.on("error", reject);
  });
}

function waitFor(child, predicate, timeoutMs = 10000) {
  return new Promise((resolve, reject) => {
    let buffer = "";
    const timer = setTimeout(
      () => reject(new Error(`timeout waiting for bridge output; got: ${buffer}`)),
      timeoutMs,
    );
    child.stdout.on("data", (chunk) => {
      buffer += chunk.toString();
      if (predicate(buffer)) {
        clearTimeout(timer);
        resolve(buffer);
      }
    });
    child.stderr.on("data", (chunk) => {
      buffer += chunk.toString();
      if (predicate(buffer)) {
        clearTimeout(timer);
        resolve(buffer);
      }
    });
  });
}

test("frame parser decodes masked text frames", () => {
  const { parser, events } = collectParser();
  const payload = Buffer.from("hello bridge");
  parser(frame(0x1, payload, { masked: true }));
  assert.equal(events.length, 1);
  assert.equal(events[0].type, "data");
  assert.equal(events[0].payload.toString(), "hello bridge");
});

test("frame parser handles ping and pong data", () => {
  const { parser, events } = collectParser();
  parser(frame(0x9, Buffer.from("probe"), { masked: false }));
  assert.equal(events.length, 1);
  assert.equal(events[0].type, "ping");
  assert.equal(events[0].payload.toString(), "probe");
});

test("frame parser splits packets arriving in fragments", () => {
  const { parser, events } = collectParser();
  const packet = frame(0x1, Buffer.from("fragmented"), { masked: true });
  const cut = Math.floor(packet.length / 2);
  parser(packet.subarray(0, cut));
  assert.equal(events.length, 0);
  parser(packet.subarray(cut));
  assert.equal(events.length, 1);
  assert.equal(events[0].payload.toString(), "fragmented");
});

test("frame parser closes on 64-bit oversized length", () => {
  const { parser, events } = collectParser();
  const packet = frame(0x2, Buffer.alloc(0), { masked: false, extended: 2 ** 40 });
  assert.doesNotThrow(() => parser(packet));
  assert.equal(events[events.length - 1].type, "close");
});

test("frame parser decodes 16-bit extended length frames", () => {
  const { parser, events } = collectParser();
  const payload = Buffer.alloc(300, 0x61);
  parser(frame(0x1, payload, { masked: true }));
  assert.equal(events.length, 1);
  assert.equal(events[0].type, "data");
  assert.equal(events[0].payload.length, 300);
  assert.equal(events[0].payload[0], 0x61);
});

test("frame parser closes when accumulation exceeds buffer cap", () => {
  const { parser, events } = collectParser();
  // Announce a large (but under the frame cap) length, then drip payload bytes.
  const header = frame(0x2, Buffer.alloc(0), { masked: false, extended: 6 * 1024 * 1024 });
  parser(header);
  assert.equal(events.length, 0);
  parser(Buffer.alloc(1024));
  assert.equal(events.length, 0); // still waiting, not yet over the cap
  // Push past the accumulated-buffer cap in one burst: must close, not buffer forever.
  parser(Buffer.alloc(9 * 1024 * 1024));
  assert.equal(events[events.length - 1].type, "close");
});

test("file sender rejects malformed percent encoding", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "florrbt-web-"));
  const sender = createFileSender(dir, path.join(dir, "data"));
  fs.writeFileSync(path.join(dir, "index.html"), "ok");
  const res = { status: 0, body: "", headers: {} };
  res.writeHead = (code, headers) => { res.status = code; res.headers = headers || {}; };
  res.end = (body) => { res.body = body; };
  sender(res, "/%E0%A4%A");
  assert.equal(res.status, 400);
});

test("file sender rejects path escapes", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "florrbt-web-"));
  const dataDir = path.join(dir, "data");
  fs.mkdirSync(dataDir);
  fs.writeFileSync(path.join(dataDir, "secret.txt"), "secret");
  fs.writeFileSync(path.join(dir, "index.html"), "ok");
  const sender = createFileSender(dir, dataDir);
  const request = (url) => {
    const res = { status: 0, body: "", headers: {} };
    res.writeHead = (code, headers) => { res.status = code; res.headers = headers || {}; };
    res.end = (body) => { res.body = body; };
    sender(res, url);
    return new Promise((resolve) => setImmediate(() => resolve(res)));
  };
  assert.equal((await request("/../data/secret.txt")).status, 403);
  assert.equal((await request("/..%2fdata%2fsecret.txt")).status, 403);
  assert.equal((await request("/data/../secret.txt")).status, 403);
  assert.equal((await request("/data/..%2f..%2findex.html")).status, 403);
});

test("file sender serves normal files", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "florrbt-web-"));
  fs.writeFileSync(path.join(dir, "index.html"), "hello client");
  const sender = createFileSender(dir, path.join(dir, "data"));
  const res = { status: 0, body: "", headers: {} };
  res.writeHead = (code, headers) => { res.status = code; res.headers = headers || {}; };
  res.end = (body) => { res.body = body; };
  sender(res, "/");
  const deadline = Date.now() + 2000;
  while (res.status === 0 && Date.now() < deadline)
    await new Promise((resolve) => setTimeout(resolve, 5));
  assert.equal(res.status, 200);
  assert.equal(res.body.toString("utf8"), "hello client");
});

test("fitUtf8 does not split multi-byte code points", async () => {
  const { fitUtf8 } = await import("../../web/src/protocol.js");
  const decoder = new TextDecoder();
  const text = "aé中😀尾";
  const bytes = new TextEncoder().encode(text);
  for (let limit = 0; limit <= bytes.length; limit += 1) {
    const cut = fitUtf8(text, limit);
    assert.ok(cut.length <= limit, `limit ${limit}`);
    const decoded = decoder.decode(cut);
    assert.ok(!decoded.includes("\uFFFD"), `limit ${limit} produced replacement char`);
  }
  // Cuts at a valid boundary stay put.
  const ascii = fitUtf8("hello", 3);
  assert.equal(new TextDecoder().decode(ascii), "hel");
});

test("packVersion matches the server's wire version", async () => {
  const { packVersion } = await import("../../web/src/protocol.js");
  const { appVersionMajor, appVersionMinor, appVersionPatch } =
    await import("../../web/src/version.js");
  // Server expects [0xfb, major, minor, patch] as the first packet (see
  // ClientVersionPacket in src/Shared/network_msg.h).
  const packet = packVersion();
  assert.deepEqual(
    [...packet],
    [0xfb, appVersionMajor, appVersionMinor, appVersionPatch],
  );
  const serverVersion = fs.readFileSync(path.join(repoRoot, "src/Shared/version.h"), "utf8");
  const triple = ["major", "minor", "patch"].map(part => Number(serverVersion.match(new RegExp(`version_${part}\\s*=\\s*(\\d+)`))?.[1]));
  assert.deepEqual([appVersionMajor, appVersionMinor, appVersionPatch], triple);
});

test("live bridge survives malformed URL and oversized frames", async () => {
  const port = await freePort();
  const child = spawn(process.execPath, [path.join(repoRoot, "tools", "web_client_server.js")], {
    env: {
      ...process.env,
      WEB_HOST: "127.0.0.1",
      WEB_PORT: String(port),
      GAME_HOST: "127.0.0.1",
      GAME_PORT: String(await freePort()), // nothing listening -> TCP connect error path
      FLORRBT_ROOT: repoRoot,
    },
    stdio: ["ignore", "pipe", "pipe"],
  });

  const exited = new Promise((resolve) => child.on("exit", (code, signal) => resolve({ code, signal })));

  try {
    await waitFor(child, (out) => out.includes(`http://127.0.0.1:${port}`));

    // Malformed percent-encoding used to throw inside the request handler and
    // crash the whole process. Now it must answer 400 and keep running.
    const malformed = await fetch(`http://127.0.0.1:${port}/%E0%A4%A`);
    assert.equal(malformed.status, 400);
    assert.equal(child.exitCode, null, "bridge must survive a malformed URL");

    // Path traversal attempts must be rejected.
    const traversal = await fetch(`http://127.0.0.1:${port}/..%2f..%2fREADME.md`);
    assert.ok([403, 404].includes(traversal.status), `unexpected status ${traversal.status}`);
    assert.equal(child.exitCode, null);

    // A WebSocket frame advertising a huge length must close that peer without
    // crashing the process.
    await new Promise((resolve, reject) => {
      const socket = net.connect(port, "127.0.0.1");
      const timeout = setTimeout(() => { socket.destroy(); reject(new Error("ws handshake timeout")); }, 8000);
      let handshake = false;
      socket.on("connect", () => {
        socket.write(
          "GET /ws HTTP/1.1\r\n" +
            "Host: 127.0.0.1\r\n" +
            "Upgrade: websocket\r\n" +
            "Connection: Upgrade\r\n" +
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n" +
            "Sec-WebSocket-Version: 13\r\n\r\n",
        );
      });
      socket.on("data", (chunk) => {
        const text = chunk.toString("latin1");
        if (text.startsWith("HTTP/1.1 101")) {
          handshake = true;
          // FIN | binary, 64-bit extended length claiming 2^40 bytes.
          const header = Buffer.from([0x82, 127]);
          const lengthBytes = Buffer.alloc(8);
          lengthBytes.writeBigUInt64BE(BigInt(2 ** 40));
          socket.write(Buffer.concat([header, lengthBytes]));
        }
      });
      socket.on("close", () => {
        clearTimeout(timeout);
        resolve();
      });
      socket.on("error", () => {
        clearTimeout(timeout);
        resolve();
      });
    });
    assert.equal(child.exitCode, null, "bridge must survive oversized frame");
  } finally {
    child.kill("SIGTERM");
    await exited;
  }
});

test("asset ETags revalidate content without caching HTML or account data", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "florrbt-assets-"));
  const dataDir = path.join(dir, "data");
  fs.mkdirSync(path.join(dir, "assets")); fs.mkdirSync(dataDir);
  fs.writeFileSync(path.join(dir, "assets", "unit.svg"), "<svg/>");
  fs.writeFileSync(path.join(dir, "index.html"), "page");
  fs.writeFileSync(path.join(dataDir, "accounts.json"), "{}");
  const sender = createFileSender(dir, dataDir);
  const request = (url, headers = {}) => new Promise(resolve => {
    const result = {};
    sender({ writeHead(code, responseHeaders) { result.code = code; result.headers = responseHeaders; },
      end(body) { result.body = body; resolve(result); } }, url, headers);
  });
  try {
    const first = await request("/assets/unit.svg");
    assert.equal(first.code, 200); assert.ok(first.headers.ETag);
    const cached = await request("/assets/unit.svg", { "if-none-match": first.headers.ETag });
    assert.equal(cached.code, 304); assert.equal(cached.body, undefined);
    fs.writeFileSync(path.join(dir, "assets", "unit.svg"), "<svg><path/></svg>");
    const changed = await request("/assets/unit.svg", { "if-none-match": first.headers.ETag });
    assert.equal(changed.code, 200); assert.notEqual(changed.headers.ETag, first.headers.ETag);
    for (const url of ["/", "/data/accounts.json"]) {
      const reply = await request(url);
      assert.equal(reply.headers["Cache-Control"], "no-store, max-age=0");
      assert.equal(reply.headers.ETag, undefined);
    }
  } finally {
    const target = fs.realpathSync(dir), tempRoot = fs.realpathSync(os.tmpdir());
    assert.equal(path.dirname(target), tempRoot);
    assert.ok(path.basename(target).startsWith("florrbt-assets-"));
    fs.rmSync(target, { recursive: true, force: true });
  }
});

"use strict";

// Shared bridge logic for tools/web_client_server.js. Extracted so the frame
// parser, file sender and PROXY handling can be unit-tested without spawning a
// live server (see tests/web_bridge/).

const crypto = require("node:crypto");
const fs = require("node:fs");
const net = require("node:net");
const path = require("node:path");

// Hard cap on a single WebSocket frame from the client. Game frames are a few
// KB; anything past this is a malicious or broken client, not a legit payload.
const DEFAULT_MAX_WS_FRAME_SIZE = 8 * 1024 * 1024;
const MAX_WS_FRAME_SIZE = Number(
  process.env.WEB_MAX_FRAME_SIZE || DEFAULT_MAX_WS_FRAME_SIZE,
);

// Cap on the accumulated (partially received) frame buffer so a peer that
// announces a huge length cannot balloon memory before the frame cap trips.
const MAX_WS_BUFFER_SIZE = MAX_WS_FRAME_SIZE + 64 * 1024;

// Gameplay framing uses a 2-byte length prefix, so gameplay payloads
// cannot exceed 64 KiB. This matches the server's UINT16_MAX packet limit.
const MAX_SERVER_PAYLOAD_SIZE = 0xffff;

const serverSnapshotType = 0x01;
const debugProtocolMagic = 0xd0;
const maxDebugClientFrameSize = 32768 + 6;
const maxDebugBridgeBacklog = 4 * 1024 * 1024;

function normalizeIpAddress(value) {
  let address = String(value || "").trim();
  const zoneOffset = address.indexOf("%");
  if (zoneOffset >= 0) address = address.slice(0, zoneOffset);
  if (address.toLowerCase().startsWith("::ffff:")) {
    const mapped = address.slice(7);
    if (net.isIP(mapped) === 4) return mapped;
  }
  return net.isIP(address) ? address : "";
}

function makeProxyV1Header(socket, gameHost, gamePort) {
  const source = normalizeIpAddress(socket?.remoteAddress);
  const family = net.isIP(source);
  const sourcePort = Number(socket?.remotePort || 0);
  if (
    !family ||
    !Number.isInteger(sourcePort) ||
    sourcePort < 0 ||
    sourcePort > 65535 ||
    !Number.isInteger(gamePort) ||
    gamePort < 1 ||
    gamePort > 65535
  )
    return "";

  const configuredDestination = normalizeIpAddress(gameHost);
  const destination =
    net.isIP(configuredDestination) === family
      ? configuredDestination
      : family === 4
        ? "0.0.0.0"
        : "::";
  return `PROXY TCP${family} ${source} ${destination} ${sourcePort} ${gamePort}\r\n`;
}

const mimeTypes = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".tmj": "application/json; charset=utf-8",
  ".tsj": "application/json; charset=utf-8",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".gif": "image/gif",
  ".webp": "image/webp",
  ".svg": "image/svg+xml",
  ".mp3": "audio/mpeg",
};

const MAX_REQUEST_URL_LENGTH = 8192;

// Decodes and normalizes a request path, then serves a file from webRoot (or
// dataRoot for /data paths). Path escapes are rejected: the resolved path must
// be inside the intended root (path.relative check, which also kills the
// "prefix sibling directory" class of traversal).
function createFileSender(webRoot, dataRoot) {
  return function sendFile(res, requestPath, requestHeaders = {}) {
    if (!requestPath || requestPath.length > MAX_REQUEST_URL_LENGTH) {
      res.writeHead(414);
      res.end("Request line too long");
      return;
    }

    let cleanPath;
    try {
      cleanPath = decodeURIComponent(requestPath.split("?")[0]);
    } catch {
      res.writeHead(400);
      res.end("Bad Request");
      return;
    }

    const servingData = cleanPath === "/data" || cleanPath.startsWith("/data/");
    const baseRoot = servingData ? dataRoot : webRoot;
    const relative = servingData
      ? cleanPath.replace(/^\/data\/?/, "")
      : cleanPath === "/"
        ? "index.html"
        : cleanPath.replace(/^\/+/, "");
    const filePath = path.resolve(baseRoot, relative);

    const relativeCheck = path.relative(baseRoot, filePath);
    if (relativeCheck.startsWith("..") || path.isAbsolute(relativeCheck)) {
      res.writeHead(403);
      res.end("Forbidden");
      return;
    }

    fs.readFile(filePath, (error, data) => {
      if (error) {
        res.writeHead(404);
        res.end("Not found");
        return;
      }
      const extension = path.extname(filePath).toLowerCase();
      const reusableAsset = /\.(svg|png|jpe?g|webp|ttf|woff2?|ogg|mp3)$/.test(extension) &&
        (cleanPath.startsWith("/assets/") || cleanPath.startsWith("/data/tiles/"));
      const headers = {
        "Content-Type":
          mimeTypes[extension] ||
          "application/octet-stream",
        "Cache-Control": "no-store, max-age=0",
      };
      if (reusableAsset) {
        headers["Cache-Control"] = "public, max-age=0, must-revalidate";
        headers.ETag = `"${crypto.createHash("sha256").update(data).digest("base64")}"`;
        const supplied = String(requestHeaders["if-none-match"] || "").split(",").map(tag => tag.trim().replace(/^W\//, ""));
        if (supplied.includes(headers.ETag) || supplied.includes("*")) {
          res.writeHead(304, headers); res.end(); return;
        }
      }
      res.writeHead(200, headers);
      res.end(data);
    });
  };
}

function makeAcceptValue(key) {
  return crypto
    .createHash("sha1")
    .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
    .digest("base64");
}

function writeWsFrame(socket, payload, opcode = 0x2) {
  if (!socket || socket.destroyed) return;
  const data = Buffer.isBuffer(payload) ? payload : Buffer.from(payload || "");
  let header;
  if (data.length < 126) {
    header = Buffer.from([0x80 | opcode, data.length]);
  } else if (data.length <= 0xffff) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | opcode;
    header[1] = 126;
    header.writeUInt16BE(data.length, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | opcode;
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(data.length), 2);
  }
  return socket.write(Buffer.concat([header, data]));
}

function closePair(wsSocket, tcpSocket) {
  if (tcpSocket && !tcpSocket.destroyed) tcpSocket.destroy();
  if (wsSocket && !wsSocket.destroyed) wsSocket.destroy();
}

// Frames a server->client payload with the 2-byte length prefix the client
// expects. Payloads larger than 64 KiB cannot be represented; drop them.
function frameServerPayload(payload) {
  if (!payload || payload.length === 0 || payload.length > MAX_SERVER_PAYLOAD_SIZE)
    return null;
  const frame = Buffer.allocUnsafe(2 + payload.length);
  frame[0] = payload.length & 0xff;
  frame[1] = (payload.length >> 8) & 0xff;
  payload.copy(frame, 2);
  return frame;
}

function createServerPacketForwarder(wsSocket, options) {
  const snapshotFlushMs = options?.snapshotFlushMs ?? 0;
  const snapshotBacklogDropBytes = options?.snapshotBacklogDropBytes ?? 0;
  let buffer = Buffer.alloc(0);
  let pendingSnapshot = null;
  let snapshotTimer = null;
  let protocolSelected = false;
  let debugProtocol = false;
  let closed = false;

  const rejectDebugTraffic = () => {
    closed = true;
    if (options?.onOverflow) options.onOverflow();
    else wsSocket.destroy?.();
    return false;
  };

  const flushSnapshot = () => {
    snapshotTimer = null;
    if (!pendingSnapshot || wsSocket.destroyed) {
      pendingSnapshot = null;
      return;
    }
    if (wsSocket.writableLength > snapshotBacklogDropBytes) {
      pendingSnapshot = null;
      return;
    }
    const frame = pendingSnapshot;
    pendingSnapshot = null;
    writeWsFrame(wsSocket, frame, 0x2);
  };

  const scheduleSnapshot = () => {
    if (snapshotTimer) return;
    if (snapshotFlushMs <= 0) {
      snapshotTimer = { immediate: true };
      queueMicrotask(flushSnapshot);
      return;
    }
    snapshotTimer = setTimeout(flushSnapshot, Math.max(0, snapshotFlushMs));
    if (snapshotTimer.unref) snapshotTimer.unref();
  };

  const forwardPayload = (payload) => {
    if (!payload || payload.length <= 0 || wsSocket.destroyed) return;
    const frame = frameServerPayload(payload);
    if (!frame) return;
    if (payload[0] === serverSnapshotType) {
      pendingSnapshot = frame;
      scheduleSnapshot();
      return;
    }
    writeWsFrame(wsSocket, frame, 0x2);
  };

  return {
    // Client hello chooses once, before it is forwarded to TCP. Debug frames
    // have their own six-byte header and must NOT enter the gameplay length
    // decoder or lossy snapshot coalescer below.
    selectProtocol(payload, tcpBacklog = 0) {
      if (closed) return false;
      if (!protocolSelected && payload?.length) {
        protocolSelected = true;
        debugProtocol = payload[0] === debugProtocolMagic;
      }
      if (debugProtocol && (payload.length > maxDebugClientFrameSize || tcpBacklog + payload.length > maxDebugBridgeBacklog))
        return rejectDebugTraffic();
      return true;
    },
    push(chunk) {
      if (closed || wsSocket.destroyed) return;
      if (debugProtocol) {
        if ((wsSocket.writableLength || 0) + chunk.length + 10 > maxDebugBridgeBacklog) {
          rejectDebugTraffic(); return;
        }
        writeWsFrame(wsSocket, chunk, 0x2);
        return;
      }
      buffer = Buffer.concat([buffer, chunk]);
      for (;;) {
        if (buffer.length < 2) return;
        const len = buffer[0] | (buffer[1] << 8);
        if (len <= 0) {
          buffer = buffer.subarray(2);
          continue;
        }
        if (buffer.length < 2 + len) return;
        const payload = buffer.subarray(2, 2 + len);
        forwardPayload(payload);
        buffer = buffer.subarray(2 + len);
      }
    },
    close() {
      closed = true;
      pendingSnapshot = null;
      buffer = Buffer.alloc(0);
      if (snapshotTimer) clearTimeout(snapshotTimer);
      snapshotTimer = null;
    },
  };
}

// Incremental WebSocket frame parser with masking support, a per-frame size cap
// and a bounded accumulation buffer. On any protocol violation the connection is
// closed via onClose instead of buffering unbounded data.
function createFrameParser(onData, onClose, onPing) {
  const maxFrameSize = MAX_WS_FRAME_SIZE;
  const maxBufferSize = MAX_WS_BUFFER_SIZE;
  let buffer = Buffer.alloc(0);
  let start = 0;

  return function parse(chunk) {
    if (chunk.length > maxBufferSize) {
      onClose();
      return;
    }
    const remaining = buffer.length - start;
    if (remaining + chunk.length > maxBufferSize) {
      onClose();
      return;
    }
    if (start > 0) {
      buffer = buffer.subarray(start);
      start = 0;
    }
    buffer = Buffer.concat([buffer, chunk]);

    for (;;) {
      const available = buffer.length - start;
      if (available < 2) return;

      const first = buffer[start];
      const second = buffer[start + 1];
      const opcode = first & 0x0f;
      const masked = (second & 0x80) !== 0;
      let length = second & 0x7f;
      let offset = 2;

      if (length === 126) {
        if (available < offset + 2) return;
        length = buffer.readUInt16BE(start + offset);
        offset += 2;
      } else if (length === 127) {
        if (available < offset + 8) return;
        const bigLength = buffer.readBigUInt64BE(start + offset);
        offset += 8;
        if (bigLength > BigInt(maxFrameSize)) {
          onClose();
          return;
        }
        length = Number(bigLength);
      }

      if (length > maxFrameSize) {
        onClose();
        return;
      }

      let mask;
      if (masked) {
        if (available < offset + 4) return;
        mask = buffer.subarray(start + offset, start + offset + 4);
        offset += 4;
      }

      if (available < offset + length) return;
      let payload = buffer.subarray(start + offset, start + offset + length);
      start += offset + length;

      if (masked) {
        payload = Buffer.from(payload);
        for (let i = 0; i < payload.length; i += 1) payload[i] ^= mask[i & 3];
      }

      if (opcode === 0x8) {
        onClose();
        return;
      }
      if (opcode === 0x9) {
        onPing(payload);
        continue;
      }
      if (opcode === 0x1 || opcode === 0x2) onData(payload);
    }
  };
}

module.exports = {
  debugProtocolMagic,
  maxDebugClientFrameSize,
  maxDebugBridgeBacklog,
  MAX_WS_FRAME_SIZE,
  MAX_WS_BUFFER_SIZE,
  MAX_SERVER_PAYLOAD_SIZE,
  serverSnapshotType,
  normalizeIpAddress,
  makeProxyV1Header,
  createFileSender,
  makeAcceptValue,
  writeWsFrame,
  closePair,
  frameServerPayload,
  createServerPacketForwarder,
  createFrameParser,
};

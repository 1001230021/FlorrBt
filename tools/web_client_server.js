#!/usr/bin/env node
"use strict";

const crypto = require("node:crypto");
const fs = require("node:fs");
const http = require("node:http");
const net = require("node:net");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const webRoot = path.join(root, "web");
const dataRoot = path.join(root, "data");
const listenHost = process.env.WEB_HOST || "127.0.0.1";
const listenPort = Number(process.env.WEB_PORT || 8080);
const gameHost = process.env.GAME_HOST || "127.0.0.1";
const gamePort = Number(process.env.GAME_PORT || 10012);

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
  ".webp": "image/webp",
  ".svg": "image/svg+xml",
};

function sendFile(res, requestPath) {
  const cleanPath = decodeURIComponent(requestPath.split("?")[0]);
  const servingData = cleanPath === "/data" || cleanPath.startsWith("/data/");
  const baseRoot = servingData ? dataRoot : webRoot;
  const relative = servingData
    ? cleanPath.replace(/^\/data\/?/, "")
    : (cleanPath === "/" ? "index.html" : cleanPath.replace(/^\/+/, ""));
  const filePath = path.resolve(baseRoot, relative);

  if (!filePath.startsWith(baseRoot)) {
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
    res.writeHead(200, {
      "Content-Type": mimeTypes[path.extname(filePath).toLowerCase()] || "application/octet-stream",
      "Cache-Control": "no-cache",
    });
    res.end(data);
  });
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
  socket.write(Buffer.concat([header, data]));
}

function closePair(wsSocket, tcpSocket) {
  if (tcpSocket && !tcpSocket.destroyed) tcpSocket.destroy();
  if (wsSocket && !wsSocket.destroyed) wsSocket.destroy();
}

function createFrameParser(onData, onClose, onPing) {
  let buffer = Buffer.alloc(0);

  return function parse(chunk) {
    buffer = Buffer.concat([buffer, chunk]);
    for (;;) {
      if (buffer.length < 2) return;

      const first = buffer[0];
      const second = buffer[1];
      const opcode = first & 0x0f;
      const masked = (second & 0x80) !== 0;
      let length = second & 0x7f;
      let offset = 2;

      if (length === 126) {
        if (buffer.length < offset + 2) return;
        length = buffer.readUInt16BE(offset);
        offset += 2;
      } else if (length === 127) {
        if (buffer.length < offset + 8) return;
        const bigLength = buffer.readBigUInt64BE(offset);
        if (bigLength > BigInt(Number.MAX_SAFE_INTEGER)) {
          onClose();
          return;
        }
        length = Number(bigLength);
        offset += 8;
      }

      let mask;
      if (masked) {
        if (buffer.length < offset + 4) return;
        mask = buffer.subarray(offset, offset + 4);
        offset += 4;
      }

      if (buffer.length < offset + length) return;
      let payload = buffer.subarray(offset, offset + length);
      buffer = buffer.subarray(offset + length);

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

const server = http.createServer((req, res) => sendFile(res, req.url || "/"));

server.on("upgrade", (req, socket) => {
  if (req.url && !req.url.startsWith("/ws")) {
    socket.destroy();
    return;
  }

  const key = req.headers["sec-websocket-key"];
  if (!key) {
    socket.destroy();
    return;
  }

  socket.write([
    "HTTP/1.1 101 Switching Protocols",
    "Upgrade: websocket",
    "Connection: Upgrade",
    `Sec-WebSocket-Accept: ${makeAcceptValue(key)}`,
    "",
    "",
  ].join("\r\n"));

  const tcp = net.createConnection({ host: gameHost, port: gamePort });
  tcp.on("data", (data) => writeWsFrame(socket, data, 0x2));
  tcp.on("close", () => closePair(socket, tcp));
  tcp.on("error", (error) => {
    writeWsFrame(socket, `TCP error: ${error.message}`, 0x1);
    closePair(socket, tcp);
  });

  const parse = createFrameParser(
    (payload) => {
      if (!tcp.destroyed) tcp.write(payload);
    },
    () => closePair(socket, tcp),
    (payload) => writeWsFrame(socket, payload, 0xA),
  );

  socket.on("data", parse);
  socket.on("close", () => closePair(socket, tcp));
  socket.on("error", () => closePair(socket, tcp));
});

server.listen(listenPort, listenHost, () => {
  console.log(`FlorrBt web client: http://${listenHost}:${listenPort}`);
  console.log(`WebSocket proxy: ws://${listenHost}:${listenPort}/ws -> ${gameHost}:${gamePort}`);
});

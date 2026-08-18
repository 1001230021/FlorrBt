#!/usr/bin/env node
"use strict";

// Static web server + WebSocket-to-TCP bridge for the FlorrBt browser client.
// Core logic (frame parsing, file serving, PROXY header) lives in
// tools/web_bridge_lib.js so it can be tested in isolation.

const fs = require("node:fs");
const http = require("node:http");
const net = require("node:net");
const path = require("node:path");

const {
  createFrameParser,
  createFileSender,
  createServerPacketForwarder,
  makeAcceptValue,
  makeProxyV1Header,
  writeWsFrame,
  closePair,
} = require("./web_bridge_lib.js");

const root = path.resolve(
  process.env.FLORRBT_ROOT || path.resolve(__dirname, ".."),
);
const webRoot = path.join(root, "web");
const dataRoot = path.join(root, "data");
const listenHost = process.env.WEB_HOST || "127.0.0.1";
const listenPort = Number(process.env.WEB_PORT || 8080);
const gameHost = process.env.GAME_HOST || "127.0.0.1";
const gamePort = Number(process.env.GAME_PORT || 10012);
const snapshotFlushMs = Number(process.env.WEB_SNAPSHOT_FLUSH_MS || 0);
const snapshotBacklogDropBytes = Number(
  process.env.WEB_SNAPSHOT_BACKLOG_DROP_BYTES || 128 * 1024,
);

// A single malformed request or peer must never take the whole bridge down.
// Log and keep serving; the alternative (process exit) turns a trivial URL into
// a denial of service.
process.on("uncaughtException", (error) => {
  console.error(
    `[web-bridge] uncaught exception (keeping server alive): ${
      error && error.stack ? error.stack : error
    }`,
  );
});
process.on("unhandledRejection", (reason) => {
  console.error(`[web-bridge] unhandled rejection: ${reason}`);
});

function main() {
  if (!fs.existsSync(path.join(webRoot, "index.html"))) {
    console.error(`Cannot find web/index.html under root: ${root}`);
    process.exit(1);
  }

  const sendFile = createFileSender(webRoot, dataRoot);
  const server = http.createServer((req, res) => sendFile(res, req.url || "/"));

  server.on("upgrade", (req, socket) => {
    if (req.url && !req.url.startsWith("/ws")) {
      socket.destroy();
      return;
    }

    const key = req.headers["sec-websocket-key"];
    const proxyHeader = makeProxyV1Header(socket, gameHost, gamePort);
    if (!key || !proxyHeader) {
      socket.destroy();
      return;
    }

    socket.write(
      [
        "HTTP/1.1 101 Switching Protocols",
        "Upgrade: websocket",
        "Connection: Upgrade",
        `Sec-WebSocket-Accept: ${makeAcceptValue(key)}`,
        "",
        "",
      ].join("\r\n"),
    );

    const tcp = net.createConnection({ host: gameHost, port: gamePort });
    tcp.write(proxyHeader);
    const forwardServerPackets = createServerPacketForwarder(socket, {
      snapshotFlushMs,
      snapshotBacklogDropBytes,
    });
    tcp.on("data", (data) => forwardServerPackets.push(data));
    tcp.on("close", () => {
      forwardServerPackets.close();
      closePair(socket, tcp);
    });
    tcp.on("error", (error) => {
      forwardServerPackets.close();
      writeWsFrame(socket, `TCP error: ${error.message}`, 0x1);
      closePair(socket, tcp);
    });

    const parse = createFrameParser(
      (payload) => {
        if (!tcp.destroyed) tcp.write(payload);
      },
      () => closePair(socket, tcp),
      (payload) => writeWsFrame(socket, payload, 0xa),
    );

    socket.on("data", parse);
    socket.on("close", () => {
      forwardServerPackets.close();
      closePair(socket, tcp);
    });
    socket.on("error", () => {
      forwardServerPackets.close();
      closePair(socket, tcp);
    });
  });

  server.on("error", (error) => {
    console.error(`Web server error: ${error.message}`);
    if (error.code === "EADDRINUSE") {
      console.error(
        `Port ${listenPort} is already in use. Change WEB_PORT or stop the old web server.`,
      );
    }
    process.exit(1);
  });

  server.listen(listenPort, listenHost, () => {
    const actualPort = server.address() && server.address().port;
    const displayPort = actualPort || listenPort;
    console.log(`FlorrBt web client: http://${listenHost}:${displayPort}`);
    console.log(
      `WebSocket proxy: ws://${listenHost}:${displayPort}/ws -> ${gameHost}:${gamePort}`,
    );
  });
}

if (require.main === module) {
  main();
}

module.exports = { main, root, webRoot, dataRoot };
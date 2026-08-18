// End-to-end weather system test: connects to the game server, authenticates,
// and exercises the weather command surface over RCON while asserting on the
// wire protocol (Environment anchor messages, per-snapshot weather bytes).
//
// Usage:
//   node tools/weather_test.cjs --rcon <rcon-password> [--account name] [--password pw]
//
// Prints PASS/FAIL per check and exits non-zero on failure.

const net = require("net");

async function loadProto() {
  return import(
    new URL("../web/src/protocol.js", "file:///" + __dirname.replace(/\\/g, "/") + "/").href
  );
}

function parseArgs(argv) {
  const args = { account: "1001230021", password: "", rcon: "" };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === "--rcon") args.rcon = argv[++i] || "";
    else if (argv[i] === "--account") args.account = argv[++i] || args.account;
    else if (argv[i] === "--password") args.password = argv[++i] || "";
  }
  return args;
}

const DAY_SECONDS = 21600;

function decodeEnvironment(payload) {
  let offset = 1; // skip type byte
  const anchor = Number(payload.readBigUInt64LE(offset));
  offset += 8;
  const timeU16 = payload.readUInt16LE(offset);
  offset += 2;
  const dayDuration = payload.readFloatLE(offset);
  offset += 4;
  const fixedDt = payload.readFloatLE(offset);
  offset += 4;
  const weather = [];
  for (let i = 0; i < 5; i += 1) weather.push(payload[offset++]);
  return {
    type: payload[0],
    anchor,
    timeOfDay: timeU16 / 0xffff,
    virtualHour: (timeU16 / 0xffff) * 24,
    dayDuration,
    fixedDt,
    weather,
  };
}

function decodeSnapshotWeather(payload) {
  let offset = 1; // type
  offset += 4 + 4 + 8; // snapshot_id, base_snapshot_id, server_tick
  offset += 2 + 4; // owner_entity_id, view_radius
  const count = payload.readUInt16LE(offset);
  offset += 2;
  const removedCount = payload.readUInt16LE(offset);
  offset += 2;
  // Skip entity records: their sizes vary, so walk from the END instead.
  // weather is the final 5 bytes of the payload.
  const weather = [];
  for (let i = 0; i < 5; i += 1) weather.push(payload[payload.length - 5 + i]);
  void count;
  void removedCount;
  void offset;
  return weather;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const proto = await loadProto();

  const results = [];
  const check = (name, ok, detail = "") => {
    results.push({ name, ok });
    console.log(`${ok ? "PASS" : "FAIL"} ${name}${detail ? ` (${detail})` : ""}`);
  };

  const authBytes = proto.packAuth({
    mode: proto.AuthMode.Login,
    name: args.account,
    password: args.password,
  });
  if (!authBytes) {
    console.log("FAIL auth packet (empty account?)");
    process.exit(1);
  }

  const socket = net.connect(10012, "127.0.0.1");
  let buffer = Buffer.alloc(0);
  let lastEnvironment = null;
  let lastSnapshotWeather = null;
  let chatBuffer = "";
  const waiters = [];
  let authenticated = false;

  const send = (bytes) => socket.write(Buffer.from(bytes));

  function onFrame(payload) {
    const type = payload[0];
    if (type === 0x14) {
      lastEnvironment = decodeEnvironment(payload);
      const w = waiters.shift();
      if (w && w.kind === "environment") w.resolve(lastEnvironment);
    } else if (type === 0x01) {
      lastSnapshotWeather = decodeSnapshotWeather(payload);
      const w = waiters.shift();
      if (w && w.kind === "weather-change") {
        // rain index is 2
        if (lastSnapshotWeather[2] !== w.previousRain) w.resolve(lastSnapshotWeather);
        else {
          waiters.unshift(w);
        }
      }
    } else if (type === 0x12) {
      // Chat (RCON replies arrive here; player flag may follow).
      let offset = 1;
      const flag = payload[offset++];
      const nameLen = payload[offset++];
      offset += nameLen;
      const msgLen = payload[offset++];
      const text = payload.subarray(offset, offset + msgLen).toString("utf8");
      chatBuffer += text + "\n";
    }
  }

  function pump() {
    for (;;) {
      if (buffer.length < 2) return;
      const len = buffer.readUInt16LE(0);
      if (buffer.length < 2 + len) return;
      const payload = buffer.subarray(2, 2 + len);
      buffer = buffer.subarray(2 + len);
      onFrame(payload);
    }
  }

  socket.on("data", (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    pump();
  });

  socket.on("error", (e) => {
    console.log("FAIL socket:", e.message);
    process.exit(1);
  });

  const waitEnvironment = (timeoutMs = 8000) =>
    new Promise((resolve) => {
      const w = { kind: "environment", resolve };
      waiters.push(w);
      setTimeout(() => {
        const i = waiters.indexOf(w);
        if (i >= 0) {
          waiters.splice(i, 1);
          resolve(null);
        }
      }, timeoutMs);
    });

  const waitWeatherChange = (previousRain, timeoutMs = 25000) =>
    new Promise((resolve) => {
      const w = { kind: "weather-change", previousRain, resolve };
      waiters.push(w);
      setTimeout(() => {
        const i = waiters.indexOf(w);
        if (i >= 0) {
          waiters.splice(i, 1);
          resolve(null);
        }
      }, timeoutMs);
    });

  const rcon = (command) => {
    const text = args.rcon ? `/rcon ${args.rcon} ${command}` : `/rcon ${command}`;
    send(proto.packChat(proto.ChatFlag.Global, text));
  };

  // 1. Connect + authenticate.
  send(authBytes);
  const firstEnv = await waitEnvironment(10000);
  check("connect + Environment anchor received", !!firstEnv, firstEnv ? `virtual ${firstEnv.virtualHour.toFixed(2)}h` : "timeout");
  if (!firstEnv) {
    console.log("aborting: server did not send Environment (old binary?)");
    process.exit(1);
  }

  const dayDurationOk = Math.abs(firstEnv.dayDuration - DAY_SECONDS) < 0.5;
  check("day duration matches config (21600s)", dayDurationOk, `${firstEnv.dayDuration}s`);
  check(
    "weather intensities present (5 bytes)",
    firstEnv.weather.length === 5,
    firstEnv.weather.join(","),
  );

  // 2. Weather set: force rain at full intensity, expect snapshot weather to rise.
  const beforeRain = (lastSnapshotWeather && lastSnapshotWeather[2]) || firstEnv.weather[2];
  rcon("weather set rain 1");
  const rainSnap = await waitWeatherChange(beforeRain, 30000);
  check("snapshots carry weather (rain intensity rose after `weather set rain 1`)",
        !!rainSnap && rainSnap[2] > beforeRain,
        rainSnap ? `rain ${beforeRain} -> ${rainSnap[2]}` : "timeout");

  // 3. Time jump: `weather time 23:00` must broadcast a new Environment anchor.
  rcon("weather time 23:00");
  const nightEnv = await waitEnvironment(8000);
  check("time jump broadcasts new Environment (23:00)", !!nightEnv && Math.abs(nightEnv.virtualHour - 23) < 0.5,
        nightEnv ? `virtual ${nightEnv.virtualHour.toFixed(2)}h` : "timeout");

  // 4. Back to noon, same assertion.
  rcon("weather time 12:00");
  const noonEnv = await waitEnvironment(8000);
  check("time jump broadcasts new Environment (12:00)", !!noonEnv && Math.abs(noonEnv.virtualHour - 12) < 0.5,
        noonEnv ? `virtual ${noonEnv.virtualHour.toFixed(2)}h` : "timeout");

  // 5. Status echo via RCON chat reply.
  chatBuffer = "";
  rcon("weather status");
  await new Promise((r) => setTimeout(r, 2000));
  check("RCON reply received for `weather status`", chatBuffer.includes("weather="), chatBuffer.trim().split("\n").pop());

  // 6. Restore automatic weather.
  rcon("weather auto");
  await new Promise((r) => setTimeout(r, 1000));

  const failed = results.filter((r) => !r.ok).length;
  console.log(`\n${results.length - failed}/${results.length} checks passed`);
  socket.destroy();
  process.exit(failed === 0 ? 0 : 1);
}

main().catch((e) => {
  console.log("fatal:", e.message);
  process.exit(1);
});

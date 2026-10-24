// End-to-end weather system test: connects to the game server, authenticates,
// and exercises the weather command surface over RCON while asserting on the
// wire protocol (Environment anchor messages, per-snapshot weather bytes).
//
// Usage:
//   node tools/weather_test.cjs --account <test-account> --rcon <rcon-password> [--password pw]
// Run only against a disposable test server: this changes weather and time.
//
// Prints PASS/FAIL per check and exits non-zero on failure.

const net = require("net");
const { pathToFileURL } = require("url");
const path = require("path");

async function loadProto() {
  return import(pathToFileURL(path.join(__dirname, "../web/src/protocol.js")).href);
}

function parseArgs(argv) {
  const args = { account: "", password: "", rcon: "" };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === "--rcon") args.rcon = argv[++i] || "";
    else if (argv[i] === "--account") args.account = argv[++i] || args.account;
    else if (argv[i] === "--password") args.password = argv[++i] || "";
  }
  return args;
}

const DAY_SECONDS = 21600;

function decodeEnvironment(payload, proto) {
  const message = proto.parseServerMessage(payload);
  if (message.type !== proto.ServerType.Environment) return null;
  return {
    type: message.type,
    anchor: message.anchorServerTick,
    timeOfDay: message.timeOfDay,
    virtualHour: message.timeOfDay * 24,
    dayDuration: message.dayDurationSeconds,
    fixedDt: message.fixedDtSeconds,
    weather: message.weatherIntensity,
    windDirection: message.windDirection,
    windSpeed: message.windSpeed,
  };
}

function decodeSnapshotWeather(payload, proto) {
  const message = proto.parseServerMessage(payload);
  return message.type === proto.ServerType.Snapshot ? message.weatherIntensity : null;
}

function sendHandshake(send, proto, authBytes) {
  send(proto.packVersion());
  send(authBytes);
}

function rconCommand(command) { return `/rcon ${command}`; }

function createWaiters() {
  const pending = new Set();
  let closed = false;
  return {
    wait(kind, timeoutMs, accepts = () => true) {
      if (closed) return Promise.resolve(null);
      return new Promise(resolve => {
        const waiter = { kind, accepts, finish(value) {
          if (!pending.delete(waiter)) return;
          clearTimeout(waiter.timer);
          resolve(value);
        } };
        pending.add(waiter);
        waiter.timer = setTimeout(() => waiter.finish(null), timeoutMs);
      });
    },
    publish(kind, value) {
      for (const waiter of [...pending])
        if (waiter.kind === kind && waiter.accepts(value)) waiter.finish(value);
    },
    clear() { closed = true; for (const waiter of [...pending]) waiter.finish(null); },
  };
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (!args.account || !args.rcon) throw new Error("--account (test account) and --rcon are required");
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
    throw new Error("Invalid authentication arguments");
  }

  const socket = net.connect(10012, "127.0.0.1");
  let buffer = Buffer.alloc(0);
  let lastEnvironment = null;
  let lastSnapshotWeather = null;
  let chatBuffer = "";
  const waiters = createWaiters();

  const send = (bytes) => {
    if (socket.destroyed) throw new Error("Socket closed");
    return socket.write(Buffer.from(bytes));
  };

  try {

  function onFrame(payload) {
    const type = payload[0];
    if (type === 0x14) {
      lastEnvironment = decodeEnvironment(payload, proto);
      if (lastEnvironment) waiters.publish("environment", lastEnvironment);
    } else if (type === 0x01) {
      lastSnapshotWeather = decodeSnapshotWeather(payload, proto);
      if (lastSnapshotWeather) waiters.publish("weather-change", lastSnapshotWeather);
    } else if (type === 0x12) {
      const message = proto.parseServerMessage(payload);
      if (message.type !== proto.ServerType.Chat) return;
      const text = message.chat.message;
      chatBuffer += text + "\n";
      waiters.publish("chat", text);
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
    waiters.clear();
    socket.destroy();
    process.exitCode = 1;
  });

  socket.on("close", () => waiters.clear());
  const waitEnvironment = (timeoutMs = 8000) => waiters.wait("environment", timeoutMs);
  const waitWeatherChange = (previousRain, timeoutMs = 25000) =>
    waiters.wait("weather-change", timeoutMs, weather => weather[2] !== previousRain);

  const rcon = (command) => {
    send(proto.packChat(proto.ChatFlag.Global, rconCommand(command)));
  };

  // 1. Connect + authenticate.
  const initialEnvironment = waitEnvironment(10000);
  sendHandshake(send, proto, authBytes);
  const firstEnv = await initialEnvironment;
  check("connect + Environment anchor received", !!firstEnv, firstEnv ? `virtual ${firstEnv.virtualHour.toFixed(2)}h` : "timeout");
  if (!firstEnv) {
    throw new Error("Server did not send Environment; check authentication and protocol version");
  }

  const dayDurationOk = Math.abs(firstEnv.dayDuration - DAY_SECONDS) < 0.5;
  check("day duration matches config (21600s)", dayDurationOk, `${firstEnv.dayDuration}s`);
  check(
    "weather intensities present (4 bytes, excluding wind)",
    firstEnv.weather.length === 4,
    firstEnv.weather.join(","),
  );

  // RCON authorizes once per connection; subsequent packets contain commands,
  // not another password prefix that the console would treat as a command.
  const authorization = waiters.wait("chat", 8000, text => text.startsWith("RCON ") || text.startsWith("Too many RCON"));
  rcon(args.rcon);
  const authorized = await authorization;
  check("RCON authorized", authorized === "RCON authorized.");
  if (authorized !== "RCON authorized.") {
    socket.destroy();
    waiters.clear();
    process.exitCode = 1;
    return;
  }

  // 2. Weather set: force rain at full intensity, expect snapshot weather to rise.
  const beforeRain = lastSnapshotWeather?.[2] ?? firstEnv.weather[2];
  const rainChange = waitWeatherChange(beforeRain, 30000);
  rcon("weather set rain 1");
  const rainSnap = await rainChange;
  check("snapshots carry weather (rain intensity rose after `weather set rain 1`)",
        !!rainSnap && rainSnap[2] > beforeRain,
        rainSnap ? `rain ${beforeRain} -> ${rainSnap[2]}` : "timeout");

  // 3. Time jump: `weather time 23:00` must broadcast a new Environment anchor.
  const nightChange = waitEnvironment(8000);
  rcon("weather time 23:00");
  const nightEnv = await nightChange;
  check("time jump broadcasts new Environment (23:00)", !!nightEnv && Math.abs(nightEnv.virtualHour - 23) < 0.5,
        nightEnv ? `virtual ${nightEnv.virtualHour.toFixed(2)}h` : "timeout");

  // 4. Back to noon, same assertion.
  const noonChange = waitEnvironment(8000);
  rcon("weather time 12:00");
  const noonEnv = await noonChange;
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
  process.exitCode = failed === 0 ? 0 : 1;
  } finally {
    waiters.clear();
    socket.destroy();
  }
}

module.exports = { decodeEnvironment, decodeSnapshotWeather, sendHandshake, createWaiters, rconCommand, loadProto };

if (require.main === module) main().catch((e) => {
    console.log("fatal:", e.message);
    process.exitCode = 1;
  });

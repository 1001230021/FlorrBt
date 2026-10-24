// Offline checks only; importing the live tool must never open a connection.
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import * as proto from "../web/src/protocol.js";

const require = createRequire(import.meta.url);
const net = require("node:net");
const connect = net.connect;
net.connect = () => { throw new Error("offline test attempted a network connection"); };
let weather;
try { weather = require("./weather_test.cjs"); } finally { net.connect = connect; }

const auth = proto.packAuth({ mode: proto.AuthMode.Login, name: "offline", password: "fixture" });
const sent = [];
weather.sendHandshake(bytes => sent.push(bytes), proto, auth);
assert.deepEqual(sent[0], proto.packVersion());
assert.equal(sent[1], auth);
assert.equal(weather.rconCommand("fixture-password"), "/rcon fixture-password");
assert.equal(weather.rconCommand("weather status"), "/rcon weather status");

const environment = Buffer.alloc(25);
environment[0] = proto.ServerType.Environment;
environment.writeBigUInt64LE(123n, 1);
environment.writeUInt16LE(32768, 9);
environment.writeFloatLE(21600, 11);
environment.writeFloatLE(0.05, 15);
environment.set([10, 20, 30, 40, 50, 60], 19);
const decoded = weather.decodeEnvironment(environment, proto);
assert.equal(decoded.anchor, 123);
assert.deepEqual(decoded.weather, [10, 20, 30, 40]);
assert.equal(decoded.windDirection, 50);
assert.equal(decoded.windSpeed, 60);
assert.equal(weather.decodeEnvironment(environment.subarray(0, 24), proto), null);

const snapshot = Buffer.alloc(35);
snapshot[0] = proto.ServerType.Snapshot;
snapshot.writeUInt32LE(9, 1);
snapshot.writeUInt32LE(proto.FULL_SNAPSHOT_BASE_ID, 5);
snapshot.writeBigUInt64LE(456n, 9);
snapshot.writeUInt32LE(65537, 17);
snapshot.set([1, 2, 200, 4, 5, 6], 29);
assert.deepEqual(weather.decodeSnapshotWeather(snapshot, proto), [1, 2, 200, 4]);
assert.equal(weather.decodeSnapshotWeather(snapshot.subarray(0, 34), proto), null);

const waiters = weather.createWaiters();
const env = waiters.wait("environment", 500);
let rainDone = false;
const rain = waiters.wait("weather-change", 500, values => values[2] > 0).then(value => { rainDone = true; return value; });
waiters.publish("weather-change", [0, 0, 0, 0]);
waiters.publish("chat", "unrelated reply");
waiters.publish("environment", decoded);
assert.equal(await env, decoded);
assert.equal(rainDone, false);
waiters.publish("weather-change", [0, 0, 200, 0]);
assert.deepEqual(await rain, [0, 0, 200, 0]);
assert.equal(await waiters.wait("missing", 5), null);
const cancelled = waiters.wait("environment", 500);
waiters.clear();
assert.equal(await cancelled, null);
assert.equal(await waiters.wait("after-close", 500), null);
console.log("PASS live-tool import isolation, version handshake, weather/wind layout and typed waiter dispatch");

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { appendBytes, popFrame, ServerType } from '../web/src/protocol.js';
import { createSceneTerrainStream } from '../web/src/scene_terrain_stream.js';

const applied = [];
let overflow = 0;
const stream = createSceneTerrainStream(delta => applied.push(delta), { limit: 3, onOverflow: () => overflow++ });
const old = stream.begin();
stream.accept({ gid: 1 });
assert.equal(applied.length, 0, 'existing old map must not receive new scene terrain');
const current = stream.begin();
stream.accept({ gid: 2 });
assert.equal(stream.finish(old), false, 'stale asynchronous map load cannot publish terrain');
assert.equal(stream.finish(current), true);
assert.deepEqual(applied, [{ gid: 2 }]);
stream.accept({ wallRemove: true });
assert.equal(applied.length, 2);
const overfull = stream.begin();
for (let i = 0; i < 4; i++) stream.accept({ gid: i });
assert.equal(overflow, 1);
assert.equal(stream.finish(overfull), false, 'overflow must trigger resync, not a truncated baseline');
const retry = stream.begin();
stream.accept({ gid: 9 });
assert.equal(stream.finish(retry), true);
assert.deepEqual(applied.at(-1), { gid: 9 });
const renderer = readFileSync(new URL('../web/src/map_renderer.js', import.meta.url), 'utf8');
assert.match(renderer, /!freshInstance && state\.map\?\.path === path/);
const client = readFileSync(new URL('../web/src/game.js', import.meta.url), 'utf8');
assert.match(client, /loadMap\(msg\.mapName, \{ freshInstance: true \}\)/);
const reset = client.match(/function resetNetworkScene\([^]*?\n\}/)?.[0];
assert.ok(reset);
assert.doesNotMatch(reset, /receiveBuffer\s*=/, 'a scene reset cannot discard unread transport frames');
const receive = client.match(/function receiveBytes\(bytes\) \{[^]*?\n\}/)?.[0];
assert.ok(receive);
const transport = { receiveBuffer: new Uint8Array() };
const delivered = [];
const receiveBytes = vm.runInNewContext(`(${receive})`, {
  state: transport, appendBytes, popFrame, ServerType,
  parseServerMessage: payload => ({ type: payload[0] }),
  dispatchServerMessage: message => delivered.push(message.type),
});
receiveBytes(new Uint8Array([1, 0, ServerType.Welcome, 1, 0, ServerType.Environment,
  1, 0, ServerType.TerrainDelta, 2]));
assert.deepEqual(delivered, [ServerType.Welcome, ServerType.Environment, ServerType.TerrainDelta]);
assert.equal(transport.receiveBuffer.length, 1, 'incomplete next frame must survive Welcome');
receiveBytes(new Uint8Array([0, ServerType.Chat, 0]));
assert.equal(delivered.at(-1), ServerType.Chat);
assert.equal(transport.receiveBuffer.length, 0);
console.log('PASS scene epochs, asynchronous terrain buffering, overflow recovery and same-asset room isolation');
console.log('PASS coalesced Welcome/environment/terrain frames and fragmented subsequent frame');

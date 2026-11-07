import assert from "node:assert/strict";
import { parseServerMessage, ServerType } from "../web/src/protocol.js";
import { partyRoster, partyMemberForEntity } from "../web/src/party_state.js";

const generation = 0x20000000000001n;
const key = value => value.toString(16).padStart(16, "0");
function packet(type, write) {
  const bytes = [type];
  const u8 = x => bytes.push(x & 255);
  const u32 = x => { for (let i = 0; i < 4; ++i) u8(x >>> (i * 8)); };
  const u64 = x => { u32(Number(x & 0xffffffffn)); u32(Number(x >> 32n)); };
  const string = x => { const value = new TextEncoder().encode(x); u8(value.length); bytes.push(...value); };
  write({ u8, u32, u64, string, bytes });
  return Uint8Array.from(bytes);
}
const welcome = packet(ServerType.Welcome, ({ u8, u32, u64, string }) => {
  u32(70000); u64(generation); u32(80000); u8(20); u32(9); u64(generation + 1n); string("garden.tmj"); u8(1);
});
const hello = parseServerMessage(welcome);
assert.equal(hello.playerId, 70000);
assert.equal(hello.playerSessionGeneration, key(generation));
assert.equal(hello.worldGeneration, key(generation + 1n));
const chat = packet(ServerType.Chat, ({ u8, u32, u64, bytes }) => {
  u8(4); u32(70000); u64(generation); u32(123); u8(1); u8(2); bytes.push(65, 66, 67);
});
assert.equal(parseServerMessage(chat).chat.sessionGeneration, key(generation));
assert.equal(parseServerMessage(chat).chat.playerId, 70000);
function party(overrides = {}) {
  return packet(ServerType.PartyState, ({ u8, u32, u64, string }) => {
    u32(70000); u64(generation); u8(1);
    u32(70000); u64(generation); u8(overrides.flags ?? 3);
    u32(9); u64(overrides.worldGeneration ?? generation + 1n);
    u32(80000); u64(generation + 2n); string("同名玩家");
  });
}
const frame = party();
const parsed = parseServerMessage(frame);
assert.equal(parsed.type, ServerType.PartyState);
assert.equal(parsed.members[0].entityGeneration, key(generation + 2n));
for (const valid of [welcome, chat, frame])
  for (let end = 0; end < valid.length; ++end)
    assert.equal(parseServerMessage(valid.slice(0, end)).type, 255, `truncated type ${valid[0]} at ${end}`);
assert.equal(parseServerMessage(party({ flags: 7 })).type, 255);
assert.equal(parseServerMessage(party({ worldGeneration: 0n })).type, 255);
assert.equal(parseServerMessage(party({ flags: 1 })).type, 255);
assert.equal(parseServerMessage(Uint8Array.from([...frame, 0])).type, 255);

const snapshot = { entityId: 80000, entityGeneration: key(generation + 2n), entityType: 17, name: "NPC" };
const state = { playerId: 1, playerSessionGeneration: key(1n), worldId: 9, worldGeneration: key(generation + 1n),
  entities: new Map([[80000, { snapshot }]]), partyMembers: parsed.members };
assert.equal(partyRoster(state)[0].status, "visible");
assert.equal(partyMemberForEntity(state, snapshot).name, "同名玩家");
assert.equal(partyMemberForEntity(state, { ...snapshot, entityGeneration: key(99n) }), null);
state.worldGeneration = key(99n);
assert.equal(partyRoster(state)[0].status, "otherWorld");
assert.equal(partyRoster(state)[0].entity, null);
state.worldGeneration = key(generation + 1n);
state.partyMembers[0].entityGeneration = key(99n);
assert.equal(partyRoster(state)[0].status, "unavailable");
state.partyMembers[0].connected = false;
assert.equal(partyRoster(state)[0].status, "offline");
state.playerId = 70000; state.playerSessionGeneration = key(generation);
assert.equal(partyRoster(state).length, 0);
console.log("PASS party/session identity, malformed frames, NPC association and world isolation");

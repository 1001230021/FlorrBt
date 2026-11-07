import assert from "node:assert/strict";
import { createEconomyRequests, packEconomyRequest, EconomyOperation as Op, EconomyOutcome as Outcome } from "../web/src/economy_requests.js";
import { parseServerMessage, ServerType } from "../web/src/protocol.js";

const hex = n => BigInt(n).toString(16).padStart(16, "0");
const baseline = (revision = 0n, last = 0n, inventoryScope = "formal") => ({
  inventoryRevision: hex(revision), inventoryLastRequest: hex(last), inventoryScope, actorScope: "world:body",
});
const intent = { operation: Op.Craft, petalType: 1, rarity: 1, count: 5 };
let now = 0, available = true;
const sent = [], completed = [], scopes = [];
const client = createEconomyRequests({ send: bytes => {
  if (!available) return false;
  sent.push(Uint8Array.from(bytes)); return true;
}, onResult: (result, request) => completed.push([result, request]), onScopeChanged: () => scopes.push(1), now: () => now });
client.connect("server/account"); client.owner(); client.inventory(baseline(0x20000000000001n, 0x20000000000001n));
assert.ok(client.submit(intent));
assert.equal(new DataView(sent[0].buffer).getBigUint64(4, true), 0x20000000000002n, "no Number precision loss");
const original = sent[0];
now += 2001; client.tick(); assert.deepEqual(sent[1], original, "timeout repeats exact packet");
client.disconnect(); client.connect("server/account"); client.owner();
client.inventory({ ...baseline(0x20000000000002n, 0x20000000000002n), actorScope: "different-body" });
assert.deepEqual(sent.at(-1), original, "reconnect retains original actor fence");
assert.ok(client.submit({ ...intent, petalType: 2 }));
const result = { inventoryScope: "formal", requestId: hex(0x20000000000002n),
  inventoryRevision: hex(0x20000000000002n), outcome: Outcome.Completed, recorded: true };
assert.equal(client.result({ ...result, requestId: hex(4) }), false);
client.result(result);
const count = sent.length;
client.inventory(baseline(0x20000000000002n, 0x20000000000002n));
assert.equal(sent.length, count, "next request waits for authoritative slots too");
client.owner(); assert.equal(completed.length, 1); assert.equal(sent.length, count + 1);
assert.equal(new DataView(sent.at(-1).buffer).getBigUint64(4, true), 0x20000000000003n);

client.inventory(baseline(0, 0, "npc:nonce")); assert.equal(client.pending, false); assert.equal(scopes.length, 1);
available = false; assert.ok(client.submit(intent)); assert.ok(client.pending);
available = true; now += 2001; client.tick(); const npcPacket = sent.at(-1);
client.result({ inventoryScope: "npc:nonce", requestId: hex(1), inventoryRevision: hex(0),
  outcome: Outcome.StorageBusy, recorded: false });
now += 2001; client.tick(); assert.deepEqual(sent.at(-1), npcPacket, "storage retry does not allocate a new request");
client.connect("different-server/account"); client.owner(); client.inventory(baseline()); assert.equal(client.pending, false);

const queuedPackets = [];
const queued = createEconomyRequests({ send: bytes => { queuedPackets.push(Uint8Array.from(bytes)); return true; }, now: () => 0 });
queued.connect("one"); queued.owner(); queued.inventory(baseline());
queued.submit(intent);
queued.submit({ operation: Op.Loadout, primary: [null], secondary: [null] });
queued.result({ inventoryScope: "formal", requestId: hex(1), inventoryRevision: hex(1), recorded: true, outcome: Outcome.Completed });
queued.inventory({ ...baseline(1, 1), actorScope: "new-body" }); queued.owner();
const waitingPacket = queuedPackets.at(-1);
const actorOffset = 20 + waitingPacket[2];
assert.equal(new TextDecoder().decode(waitingPacket.slice(actorOffset, actorOffset + waitingPacket[3])), "world:body",
  "an unsent queued loadout keeps the actor it was requested for, even after respawn");

const layout = packEconomyRequest({ ...intent, operation: Op.Loadout, inventoryScope: "临时", actorScope: "a",
  requestId: hex(2), expectedRevision: hex(3), primary: [{ petalType: 1, rarity: 8 }, null], secondary: [null, { petalType: 2, rarity: 3 }] });
assert.equal(layout[2], 6); assert.deepEqual([...layout.slice(-10)], [2, 2, 1, 8, 0, 0, 0, 0, 2, 3]);

const inventory = Buffer.alloc(21 + 3);
inventory[0] = ServerType.Inventory;
inventory.writeBigUInt64LE(0x20000000000001n, 3); inventory.writeBigUInt64LE(0x20000000000002n, 11);
inventory[19] = 2; inventory[20] = 1; inventory.write("xxa", 21);
const decoded = parseServerMessage(inventory);
assert.equal(decoded.inventoryRevision, hex(0x20000000000001n)); assert.equal(decoded.inventoryScope, "xx"); assert.equal(decoded.actorScope, "a");
for (let n = 1; n < inventory.length; ++n) assert.equal(parseServerMessage(inventory.subarray(0, n)).type, 0xff);
const response = Buffer.alloc(32);
response[0] = ServerType.EconomyResult; response[1] = Op.Craft; response[3] = 1;
response.writeBigUInt64LE(0x20000000000002n, 4); response.writeBigUInt64LE(0x20000000000003n, 12);
response[20] = 2; response.write("xx", 21);
const receipt = parseServerMessage(response);
assert.equal(receipt.requestId, hex(0x20000000000002n)); assert.equal(receipt.inventoryScope, "xx"); assert.equal(receipt.recorded, true);
for (let n = 1; n < response.length; ++n) assert.equal(parseServerMessage(response.subarray(0, n)).type, 0xff);
console.log("PASS exact counters, frozen retries, receipt/baseline ordering, reconnect and NPC scope isolation, wire truncation");

import assert from "node:assert/strict";
import { parseServerMessage, ServerType, sameEntityIncarnation, packVersion, FULL_SNAPSHOT_BASE_ID } from "../web/src/protocol.js";
import { appVersionPatch } from "../web/src/version.js";
import { createCombatFeedback } from "../web/src/combat_feedback.js";
import { parseChainSnapshot } from "../web/src/chain_renderer.js";

const bytes = [];
const u8 = x => bytes.push(x & 255);
const u16 = x => { u8(x); u8(x >>> 8); };
const u32 = x => { u16(x); u16(x >>> 16); };
const u64 = x => { u32(Number(x & 0xffffffffn)); u32(Number(x >> 32n)); };
u8(ServerType.Snapshot); u32(7); u32(FULL_SNAPSHOT_BASE_ID); u64(99n);
u32(65537); u32(64000); u16(2); u16(1);
function entity(format, id, generation) {
  u8(format); u32(id); u64(generation); u8(7); u8(2);
  if (format === 3) { u32(6400); u32(12800); }
  else { u16(10); u16(20); }
  u16(10); u8(255); u8(0); u16(0); u16(0); u8(1);
  if (format === 3) { u8(0); u8(0); u8(0); u8(0); }
}
entity(3, 65537, 0xfedcba9876543210n);
entity(1, 1, 0x20000000000001n);
u32(70000);
for (let i = 0; i < 6; ++i) u8(i);
const parsed = parseServerMessage(Uint8Array.from(bytes));
assert.equal(parsed.type, ServerType.Snapshot);
assert.equal(parsed.ownerEntityId, 65537);
assert.deepEqual(parsed.entities.map(x => x.entityId), [65537, 1]);
assert.equal(parsed.entities[0].entityGeneration, "fedcba9876543210");
assert.equal(parsed.entities[1].entityGeneration, "0020000000000001");
assert.deepEqual(parsed.entities[1].pos, { x: 110, y: 220 });
assert.deepEqual(parsed.removedEntityIds, [70000]);
assert.equal(parsed.windSpeed, 5);
assert.ok(sameEntityIncarnation(parsed.entities[0], { ...parsed.entities[0] }));
assert.ok(!sameEntityIncarnation(parsed.entities[0], { ...parsed.entities[0], entityGeneration: "fedcba9876543211" }));
assert.ok(!sameEntityIncarnation(null, null));
assert.equal(parseServerMessage(Uint8Array.from(bytes.slice(0, -1))).type, 255);
assert.equal(packVersion()[3], appVersionPatch);

const feedback = createCombatFeedback();
for (const generation of ["1", "2"])
  feedback.add([{ targetId: 1, targetGeneration: generation, kind: 0, amount: 5, radius: 1, pos: { x: 0, y: 0 } }], 0);
assert.equal(feedback.pendingCount(), 2, "reused ids must not combine damage from different lifetimes");
const chain = parseChainSnapshot("chain:65537:18364758544493064720:1:9007199254740993:100:90:0.1:-1");
assert.equal(chain.aGenerationKey, parsed.entities[0].entityGeneration);
assert.equal(chain.bGenerationKey, parsed.entities[1].entityGeneration);
assert.equal(parseChainSnapshot("chain:1:18446744073709551616:2:1:100:90:0:-1"), null);
console.log("PASS wide entity ids, exact 64-bit generations, compact/full decode, reincarnation and feedback identity");

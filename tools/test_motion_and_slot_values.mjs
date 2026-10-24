// Pure, offline checks only: no browser, server, sockets or persisted data.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { getPointerMoveIntent, clampMoveIntent, quantizeMoveIntent } from "../web/src/movement_input.js";
import { resolveStaticCircleMotion } from "../web/src/circle_contact.js";
import { getSlotCounter, getSlotCounterColor } from "../web/src/slot_counter.js";
import { PETAL_IDS } from "../web/src/generated_ids.js";
import { PetalSlotVisualType, packInputFrame, parseServerMessage, ServerType } from "../web/src/protocol.js";

const near = (a, b) => assert.ok(Math.abs(a - b) < 1e-6, `${a} != ${b}`);
assert.deepEqual(getPointerMoveIntent(0, 0, 100, 4), { x: 0, y: 0 });
assert.deepEqual(getPointerMoveIntent(4, 0, 100, 4), { x: 0, y: 0 });
near(getPointerMoveIntent(52, 0, 100, 4).x, 0.5);
near(getPointerMoveIntent(100, 0, 100, 4).x, 1);
near(getPointerMoveIntent(200, 0, 100, 4).x, 1);
near(Math.hypot(...Object.values(clampMoveIntent(1, 1))), 1);
assert.deepEqual(clampMoveIntent(NaN, 1), { x: 0, y: 0 });
for (const strength of [0, 0.05, 0.25, 0.5, 1]) {
  const intent = quantizeMoveIntent(strength, 0);
  const packet = packInputFrame(1, 2, intent.y, intent.x, false, false, false);
  near(new DataView(packet.buffer).getInt8(14) / 127, intent.x);
}

let contact = resolveStaticCircleMotion({ x: 12, y: 0 }, { x: 8, y: 2 }, { x: 0, y: 0 }, 10);
assert.ok(contact && Math.hypot(contact.pos.x, contact.pos.y) >= 10 - 1e-6);
assert.ok(contact.pos.y > 0, "tangential movement must survive");
assert.equal(resolveStaticCircleMotion({ x: 10, y: 0 }, { x: 11, y: 0 }, { x: 0, y: 0 }, 10), null);
contact = resolveStaticCircleMotion({ x: 12, y: 0 }, { x: -12, y: 0 }, { x: 0, y: 0 }, 10);
near(contact.pos.x, 10); // A fast frame cannot pass through the dummy.
contact = resolveStaticCircleMotion({ x: 0, y: 0 }, { x: 0, y: 0 }, { x: 0, y: 0 }, 10);
assert.ok(Number.isFinite(contact.pos.x));

const slot = { petalType: PETAL_IDS.battery, rarity: 6 };
const runtime = { ...slot, visualType: PetalSlotVisualType.Counter, copies: [
  { visual: 3, loading: false }, { visual: 2, loading: false }, { visual: 20, loading: true },
] };
assert.equal(getSlotCounter(slot, runtime, "primary"), 5);
assert.equal(getSlotCounter(slot, { ...runtime, banned: true }, "primary"), 0);
assert.equal(getSlotCounter(slot, runtime, "secondary"), 0);
assert.equal(getSlotCounter(slot, { ...runtime, rarity: 7 }, "primary"), undefined);
assert.equal(getSlotCounter({ petalType: PETAL_IDS.basic, rarity: 6 }, null, "primary"), null);
assert.equal(getSlotCounter(null, runtime, "primary"), null);

const lightning = { petalType: PETAL_IDS.lightning, rarity: 6 };
for (const kind of ["primary", "secondary"]) {
  assert.equal(getSlotCounter(lightning, null, kind), null, "Lightning has no pending/side count");
  assert.equal(getSlotCounter(lightning, { ...runtime, ...lightning }, kind), null,
    "legacy Lightning hit accumulation must not appear as a charge count");
  assert.equal(getSlotCounter(lightning, { ...runtime, ...lightning, visualType: PetalSlotVisualType.None }, kind), null);
}
const mimic = { petalType: PETAL_IDS.mimic, rarity: 6 };
assert.equal(getSlotCounter(mimic, { ...runtime, ...mimic }, "primary"), 5,
  "server-advertised counters such as Mimic copying Battery must still work");
for (let rarity = 0; rarity <= 18; rarity++) {
  const index = Math.max(0, Math.min(10, (rarity || 1) - 1));
  const svg = readFileSync(new URL(`../web/assets/petals/0_${index}.svg`, import.meta.url), "utf8");
  const fill = svg.match(/<path\b[^>]*\bfill="(#[a-f0-9]+)"/i)?.[1];
  assert.equal(getSlotCounterColor(rarity), fill, `rarity ${rarity} badge must match its card fill exactly`);
}
const css = readFileSync(new URL("../web/styles.css", import.meta.url), "utf8");
const badgeStyle = css.match(/\.slot-value\s*\{([^}]+)\}/)?.[1] || "";
assert.match(badgeStyle, /background:\s*var\(--slot-rarity-color,/);
assert.match(badgeStyle, /border:\s*0\s*;/);
assert.match(badgeStyle, /box-shadow:\s*none\s*;/);
const game = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
const slotButton = game.slice(game.indexOf("function makeSlotButton("), game.indexOf("function slotVisualPetalType("));
assert.match(slotButton, /setProperty\("--slot-rarity-color", getSlotCounterColor\(slot\.rarity\)\)/);

// A full snapshot with Counter followed by Angle/Size/None. Verifies every
// payload consumes exactly its bytes and leaves following fields aligned.
const bytes = [];
const u8 = value => bytes.push(value & 255);
const u16 = value => { u8(value); u8(value >>> 8); };
const u32 = value => { u16(value); u16(value >>> 16); };
u8(ServerType.Snapshot); u32(1); u32(0xffffffff); u32(1); u32(0);
u32(42); u32(64000); u16(1); u16(0);
u8(0); u32(42); u32(1); u32(0); u8(1); u8(1); u32(640); u32(1280);
u16(36); u8(255); u8(0); u16(8); u16(0); u8(1); u32(0); u32(0); u8(0);
u8(4);
for (const [type, raw] of [[PetalSlotVisualType.Counter, 65535], [PetalSlotVisualType.Angle, 1000],
  [PetalSlotVisualType.Size, 65535], [PetalSlotVisualType.None, 0]]) {
  u8(PETAL_IDS.battery); u8(6); u8(type); u8(1); u8(0); u8(255);
  if (type !== PetalSlotVisualType.None) u16(raw);
}
u8(0); // states, format 0 has no hallucination trailer
for (let i = 0; i < 6; i++) u8(i); // weather and wind
const parsed = parseServerMessage(Uint8Array.from(bytes));
assert.ok(parsed?.entities?.length === 1, "counter snapshot must decode");
assert.deepEqual(parsed.entities[0].primarySlots.map(s => s.copies[0].visual), [65535, 1, 1, null]);
assert.equal(parsed.windSpeed, 5);
console.log("PASS analog input, static circle contact, slot counter semantics and wire decoding");

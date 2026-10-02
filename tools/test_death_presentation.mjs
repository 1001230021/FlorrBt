// Pure visual death tests. No game/browser is launched and no account data
// is touched; every authoritative position object stays frozen below.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { beginDeathMotion, stepDeathMotion, clearDeathMotion, getDeathTilt,
  stepDeathViewFactor, captureDeathLoadout } from "../web/src/death_motion.js";
import { buildDeathPortraitPetals, drawDeathPortrait } from "../web/src/death_portrait.js";
import { stepFlowerFace, flowerAttachmentLayout } from "../web/src/flower_renderer.js";
import { PETAL_IDS } from "../web/src/generated_ids.js";

const near = (a, b, epsilon = 1e-6) => assert.ok(Math.abs(a - b) < epsilon, `${a} != ${b}`);
const snap = Object.freeze({ pos: Object.freeze({ x: 100, y: 200 }), radius: 36, flags: 4 });
const makeEntity = () => ({ snapshot: snap, renderPos: { ...snap.pos }, snapshotVelocity: { x: 180, y: -60 } });
let reference;
for (const fps of [30, 60, 120]) {
  const entity = makeEntity();
  const motion = beginDeathMotion(entity, 1);
  let zoom = 1;
  for (let frame = 0; frame < fps; frame++) {
    stepDeathMotion(entity, 1 / fps);
    zoom = stepDeathViewFactor(zoom, true, 1 / fps);
    entity.snapshotVelocity = { x: 0, y: 0 }; // subsequent dead snapshots
    assert.equal(beginDeathMotion(entity, 2), motion, "must not restart on another dead snapshot");
  }
  assert.ok(entity.renderPos.x > 120 && entity.renderPos.y < 200);
  assert.equal(entity.snapshot, snap);
  assert.deepEqual(snap.pos, { x: 100, y: 200 });
  assert.ok(Math.abs(getDeathTilt(entity)) < 0.5);
  if (!reference) reference = { ...entity.renderPos, zoom };
  else { near(entity.renderPos.x, reference.x); near(entity.renderPos.y, reference.y); near(zoom, reference.zoom); }
  const atRest = { ...entity.renderPos };
  stepDeathMotion(entity, 10);
  assert.deepEqual(entity.renderPos, atRest);
  for (let frame = 0; frame < fps; frame++) zoom = stepDeathViewFactor(zoom, false, 1 / fps);
  near(zoom, 1);
  clearDeathMotion(entity);
  assert.equal(stepDeathMotion(entity, 1 / fps), false);
}
const stationary = makeEntity();
beginDeathMotion(stationary, 1, { velocity: { x: 0, y: 0 } }); stepDeathMotion(stationary, 1);
assert.deepEqual(stationary.renderPos, snap.pos);
const loadedDead = makeEntity();
beginDeathMotion(loadedDead, 1, { resting: true }); stepDeathMotion(loadedDead, 1);
assert.deepEqual(loadedDead.renderPos, snap.pos);
const thrown = makeEntity();
beginDeathMotion(thrown, 1, { velocity: { x: 1e8, y: 1e8 } }); stepDeathMotion(thrown, 1);
assert.ok(Math.hypot(thrown.renderPos.x - 100, thrown.renderPos.y - 200) <= 36 * 3);
const teleport = makeEntity(); teleport.renderPos = { x: 10000, y: 20000 };
beginDeathMotion(teleport, 1); stepDeathMotion(teleport, 1);
assert.deepEqual(teleport.renderPos, snap.pos);
let face = stepFlowerFace(null, {}, 0);
for (let i = 0; i < 120; i++) face = stepFlowerFace(face, { dead: true, deadTilt: -0.3 }, 1 / 120);
assert.ok(face.dead && Math.abs(face.tilt + 0.3) < 0.001);

const living = { primarySlots: [
  { petalType: PETAL_IDS.bubble, rarity: 6, copies: [{ state: 0 }, { state: 1 }] },
  { petalType: PETAL_IDS.antennae, rarity: 6, copies: [] },
  { petalType: PETAL_IDS.bandage, rarity: 6, copies: [] },
] };
const loadout = captureDeathLoadout(living);
living.primarySlots[0].copies[0].state = 1;
living.primarySlots[0].petalType = PETAL_IDS.basic;
assert.equal(loadout[0].petalType, PETAL_IDS.bubble);
assert.equal(loadout[0].copies[0].state, 0);
const petals = buildDeathPortraitPetals(loadout);
assert.equal(petals.length, 2); assert.equal(petals[1].alpha, 0.45);
assert.ok(petals.every(petal => petal.petalType !== PETAL_IDS.antennae));
assert.equal(buildDeathPortraitPetals(Array.from({ length: 16 }, () => loadout[0]), () => 100).length, 32);
assert.equal(buildDeathPortraitPetals(Array.from({ length: 16 }, () => ({ ...loadout[0], copies: [] })), () => 100).length, 64);

let saves = 0, restores = 0, images = 0;
const imageCalls = [];
const ctx = new Proxy({}, { get: (_, key) => (...args) => {
  if (key === "save") saves++; if (key === "restore") restores++; if (key === "drawImage") images++;
  if (key === "drawImage") imageCalls.push(args);
  for (const arg of args) if (typeof arg === "number") assert.ok(Number.isFinite(arg));
} });
const canvas = { width: 0, height: 0, getContext: () => ctx };
const attachmentImages = {
  antennae: { complete: true, naturalWidth: 110, id: "antennae-svg" },
  bandage: { complete: true, naturalWidth: 110, id: "bandage-svg" },
};
drawDeathPortrait(canvas, { slots: loadout, snapshot: { states: [] }, startedAt: 0 }, 300, {
  getImage: () => ({ complete: true, naturalWidth: 100 }),
  getAttachmentImage: kind => attachmentImages[kind],
});
assert.equal(saves, restores); assert.equal(images, 4);
assert.equal(imageCalls[0][0], attachmentImages.bandage);
assert.equal(imageCalls[1][0], attachmentImages.antennae);
for (const kind of ["antennae", "bandage"]) {
  const layout = flowerAttachmentLayout[kind];
  const size = 36 * layout.size;
  assert.deepEqual(imageCalls.find(call => call[0] === attachmentImages[kind]),
    [attachmentImages[kind], -size / 2, 36 * layout.y - size / 2, size, size]);
}
assert.ok(canvas.width >= 176 && canvas.width <= 528);

const game = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
const ownerHandler = game.slice(game.indexOf("function handleOwnerSnapshot("), game.indexOf("function shouldFadeMissingEntity("));
assert.ok(ownerHandler.indexOf("if (isDead)") < ownerHandler.indexOf("initOwnerPrediction("), "dead snapshots must bypass position resets");
console.log("PASS death coast/zoom at 30/60/120fps, repeated snapshots, zero speed, teleport/respawn, frozen loadout and portrait");

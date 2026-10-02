// Pure transition/particle checks. No game, browser, socket or account I/O.
import assert from "node:assert/strict";
import { getBurrowTransition, createBurrowParticles } from "../web/src/burrow_particles.js";
import { drawSquareParticle } from "../web/src/square_particles.js";
import { flagDead, stateDigging } from "../web/src/game_ids.js";

const near = (a, b) => assert.ok(Math.abs(a - b) < 1e-6, `${a} != ${b}`);
const surface = Object.freeze({ entityId: 1, entityType: 2, radius: 36, flags: 0,
  pos: Object.freeze({ x: 100, y: 200 }), states: Object.freeze([]) });
const underground = Object.freeze({ ...surface, states: Object.freeze([{ type: stateDigging, rarity: 6 }]) });
assert.equal(getBurrowTransition(surface, underground), "enter");
assert.equal(getBurrowTransition(underground, surface), "exit");
for (const [previous, next, continuous] of [
  [null, underground, true], [underground, underground, true], [surface, surface, true],
  [surface, underground, false], [underground, { ...surface, flags: flagDead }, true],
  [{ ...surface, flags: flagDead }, underground, true], [surface, { ...underground, entityId: 2 }, true],
  [surface, { ...underground, entityType: 3 }, true],
  [{ ...surface, generation: "7" }, { ...underground, generation: "8" }, true],
  [underground, { ...surface, pos: { x: 9999, y: 200 } }, true],
  [surface, { ...underground, pos: { x: NaN, y: 200 } }, true],
]) assert.equal(getBurrowTransition(previous, next, continuous), null);

function canvas() {
  const calls = [], alphas = [], saved = [];
  const ctx = new Proxy({ globalAlpha: 1 }, {
    get(target, key) {
      if (key in target) return target[key];
      return (...args) => {
        calls.push([key, ...args]);
        if (key === "save") saved.push(target.globalAlpha);
        if (key === "restore") target.globalAlpha = saved.pop();
        if (key === "fillRect") alphas.push(target.globalAlpha);
        for (const arg of args) if (typeof arg === "number") assert.ok(Number.isFinite(arg));
      };
    },
  });
  return { ctx, calls, alphas, saved };
}

const hidden = createBurrowParticles({ isVisible: () => false });
assert.equal(hidden.observe(surface, underground), 0);
const initial = createBurrowParticles();
assert.equal(initial.observe(null, underground), 0);
assert.equal(initial.observe(underground, underground), 0);

let reference;
for (const fps of [30, 60, 120]) {
  const layer = createBurrowParticles({ random: () => 0.5, reducedMotion: () => false });
  assert.equal(layer.observe(surface, underground), 18);
  for (let i = 0; i < fps / 5; i++) layer.step(1 / fps);
  const draw = canvas(); layer.draw(draw.ctx, 1, { x: 0, y: 0 });
  assert.equal(draw.calls.filter(call => call[0] === "fillRect").length, 18);
  assert.equal(draw.saved.length, 0);
  const poses = draw.calls.filter(call => call[0] === "translate" || call[0] === "rotate" || call[0] === "fillRect");
  if (!reference) reference = poses;
  else for (let i = 0; i < poses.length; i++) for (let j = 1; j < poses[i].length; j++) near(poses[i][j], reference[i][j]);
  const shifted = canvas(); layer.draw(shifted.ctx, 2, { x: -300, y: -400 });
  assert.deepEqual(shifted.calls.find(call => call[0] === "translate"), ["translate", -300, -400]);
  assert.deepEqual(shifted.calls.find(call => call[0] === "scale"), ["scale", 2, 2]);
  assert.deepEqual(shifted.calls.filter(call => call[0] === "translate").slice(1),
    draw.calls.filter(call => call[0] === "translate").slice(1), "camera motion cannot move the world-space particle origins");
  layer.step(1); assert.equal(layer.count(), 0);
}

for (const [previous, next, direction] of [[surface, underground, -1], [underground, surface, 1]]) {
  const layer = createBurrowParticles({ random: () => 0.5 });
  layer.observe(previous, next);
  const before = canvas(); layer.draw(before.ctx, 1, { x: 0, y: 0 }); layer.step(0.1);
  const after = canvas(); layer.draw(after.ctx, 1, { x: 0, y: 0 });
  const a = before.calls.filter(call => call[0] === "translate")[1];
  const b = after.calls.filter(call => call[0] === "translate")[1];
  assert.ok(direction * (Math.hypot(b[1] - 100, b[2] - 200) - Math.hypot(a[1] - 100, a[2] - 200)) > 0);
}
const crowded = createBurrowParticles({ random: () => 0.5 });
let emitted = 0;
for (let id = 1; id <= 100; id++) emitted += crowded.observe({ ...surface, entityId: id }, { ...underground, entityId: id });
assert.ok(emitted <= 96, "simultaneous transitions must have a bounded allocation budget");
for (let i = 0; i < 100; i++) { crowded.step(0.01); crowded.observe(underground, surface); assert.ok(crowded.count() <= 192); }
crowded.clear(); assert.equal(crowded.count(), 0);
assert.equal(crowded.observe(underground, surface), 28, "changing worlds restores the burst budget");
const reduced = createBurrowParticles({ random: () => 0.5, reducedMotion: () => true });
assert.equal(reduced.observe(surface, underground), 6);
reduced.step(1); assert.equal(reduced.count(), 0);
const draw = canvas(); draw.ctx.globalAlpha = 0.4;
drawSquareParticle(draw.ctx, { x: 10, y: 20, size: 8, life: 1, age: 0.5, rot: 1, color: "#c7955c" }, 2);
near(draw.alphas[0], 0.1); near(draw.ctx.globalAlpha, 0.4);
assert.deepEqual(draw.calls.find(call => call[0] === "fillRect"), ["fillRect", -6.2, -6.2, 12.4, 12.4]);
assert.equal(draw.saved.length, 0);
console.log("PASS burrow enter/exit, suppressed spawn/death/gaps/teleport, shared square rendering, world anchors, particle budgets and 30/60/120fps motion");

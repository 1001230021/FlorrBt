import assert from 'node:assert/strict';
import { createMenuPetalField } from '../web/src/menu_petal_motion.js';
import { PETAL_IDS as p, RARITY_IDS as r } from '../web/src/generated_ids.js';

const inventory = [{ petalType: p.leaf, rarity: r.common, count: 90 }, { petalType: p.rose, rarity: r.ultra, count: 10 }];
const view = { width: 1910, height: 884, scale: 1 };
const rng = () => { let seed = 32; return () => ((seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) / 4294967296); };
const make = () => { const field = createMenuPetalField({ random: rng() }); field.setInventory(inventory); return field; };
const run = fps => { const field = make(); for (let i = 0; i < fps * 30; i++) field.step(1 / fps, view); return field; };
const reference = run(60);
for (const fps of [30, 120, 144]) {
  const field = run(fps); assert.equal(field.born, reference.born);
  assert.deepEqual(field.particles.map(item => item.id), reference.particles.map(item => item.id));
  field.particles.forEach((item, i) => {
    for (const key of ['x', 'y', 'angle', 'age']) assert(Math.abs(item[key] - reference.particles[i][key]) < 1e-6, key);
  });
}
assert(reference.born > reference.particles.length && reference.particles.length <= 64);
assert(new Set(reference.particles.map(item => item.speed)).size > 5);
assert(new Set(reference.particles.map(item => item.angularSpeed)).size > 5);
const field = make();
assert.equal(field.particles.length, 0);
field.step(0, view); assert.equal(field.particles.length, 1);
const first = { ...field.particles[0] }; assert(first.x < 0);
for (let i = 0; i < 60; i++) field.step(1 / 60, view);
assert(Math.abs(field.particles[0].x - first.x - first.speed) < 1e-6);
assert(Math.abs(field.particles[0].angle - first.angle - first.angularSpeed) < 1e-6);
assert(field.born >= 2 && field.born <= 3);
const unchanged = JSON.stringify(field.particles);
field.setInventory([...inventory].reverse()); assert.equal(JSON.stringify(field.particles), unchanged);
for (let i = 0; i < 30; i++) field.step(.1, { ...view, enabled: false });
assert.equal(field.particles.length, 0); assert.equal(field.born, 0);
field.step(0, view); assert.equal(field.particles.length, 1);
assert(field.particles[0].x < 0);
const resumedX = field.particles[0].x;
field.step(.1, view); assert(field.particles[0].x > resumedX);
field.setInventory([]); field.step(1, view); assert.equal(field.particles.length, 0);
field.setInventory(inventory); field.step(0, view);
assert.equal(field.particles.length, 1);
field.step(1 / 60, { width: 390, height: 844, scale: .65 });
assert(field.particles.every(item => [item.x, item.y, item.angle].every(Number.isFinite)));
console.log('PASS continuous births, independent motion, frame-rate consistency, off clears petals and on restarts emission');

import assert from 'node:assert/strict';
import { normalizeMenuInventory, sampleMenuPetal } from '../web/src/menu_inventory.js';
import { PETAL_IDS as p, RARITY_IDS as r } from '../web/src/generated_ids.js';

const inventory = normalizeMenuInventory([
  { petalType: p.leaf, rarity: r.common, count: 80 },
  { petalType: p.leaf, rarity: r.common, count: 10 },
  { petalType: p.rose, rarity: r.ultra, count: 10 },
]);
assert.equal(inventory.find(item => item.petalType === p.leaf).count, 90);
assert.deepEqual(inventory, normalizeMenuInventory([...inventory].reverse()));
assert.deepEqual(normalizeMenuInventory([null, { petalType: p.leaf, rarity: r.common, count: NaN },
  { petalType: 255, rarity: r.common, count: 10 }, { petalType: p.leaf, rarity: 255, count: 10 },
  { petalType: p.leaf, rarity: r.common, count: -1 }]), []);
const unbalanced = normalizeMenuInventory([{ petalType: p.leaf, rarity: r.common, count: 0xffffffff },
  { petalType: p.rose, rarity: r.common, count: 1 }]);
const before = JSON.stringify(inventory);
sampleMenuPetal(inventory, .13);
assert.equal(JSON.stringify(inventory), before);
let leaves = 0, roses = 0;
for (let index = 0; index < 10000; index++) {
  const sampled = sampleMenuPetal(inventory, (index + .5) / 10000);
  if (sampled.petalType === p.leaf) leaves++; else if (sampled.petalType === p.rose) roses++;
}
assert.equal(leaves, 9000); assert.equal(roses, 1000);
let cumulative = 0;
const total = unbalanced.reduce((sum, item) => sum + item.count, 0);
for (const item of unbalanced) {
  assert.equal(sampleMenuPetal(unbalanced, (cumulative + item.count / 2) / total).petalType, item.petalType);
  cumulative += item.count;
}
assert.equal(sampleMenuPetal([], .5), null);
console.log('PASS menu inventory quantities, rarity identity, weighted sampling, empty and malformed inventory');

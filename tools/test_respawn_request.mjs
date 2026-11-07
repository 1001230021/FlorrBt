import assert from 'node:assert/strict';
import { createRespawnRequest } from '../web/src/respawn_request.js';
import { createEconomyRequests, EconomyOperation, EconomyOutcome } from '../web/src/economy_requests.js';

const hex = value => BigInt(value).toString(16).padStart(16, '0');
const events = [];
let now = 0, canSend = true;
const economy = createEconomyRequests({ send: () => { events.push('loadout'); return true; } });
const respawn = createRespawnRequest({ now: () => now, hasPendingChanges: () => economy.pending,
  send: () => { if (!canSend) return false; events.push('respawn'); return true; } });
const inventory = id => ({ inventoryScope: 'account', actorScope: 'corpse',
  inventoryRevision: hex(id), inventoryLastRequest: hex(id) });
economy.connect('test'); economy.inventory(inventory(0)); economy.owner();
const layout = type => ({ operation: EconomyOperation.Loadout, primary: [{ petalType: type, rarity: 1 }], secondary: [{}] });
economy.submit(layout(4)); economy.submit(layout(21));
assert(respawn.request()); assert(!respawn.request());
now = 10000; respawn.tick();
assert(respawn.pending, 'waiting for receipts does not expire or silently discard the build');
assert.deepEqual(events, ['loadout']);
for (const id of [1, 2]) {
  economy.result({ inventoryScope: 'account', requestId: hex(id), inventoryRevision: hex(id),
    outcome: EconomyOutcome.Completed, recorded: true });
  respawn.tick(); assert(!events.includes('respawn'));
  economy.inventory(inventory(id)); respawn.tick(); assert(!events.includes('respawn'));
  economy.owner();
}
assert.deepEqual(events, ['loadout', 'loadout']);
canSend = false; respawn.tick(); assert(respawn.pending);
canSend = true; respawn.tick(); respawn.tick();
assert.deepEqual(events, ['loadout', 'loadout', 'respawn'], 'respawn follows both committed layouts exactly once');
assert(respawn.pending); now += 3000; assert(!respawn.pending);
assert(respawn.request()); respawn.reset(); respawn.tick();
assert(!respawn.pending); assert.equal(events.length, 3, 'scene reset or rejected build cancels a queued respawn');
console.log('PASS respawn waits for all build receipts and baselines, retries failed sends, resets and deduplicates');

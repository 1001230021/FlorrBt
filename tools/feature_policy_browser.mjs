import assert from 'node:assert/strict';
import { setTimeout as delay } from 'node:timers/promises';

export async function checkFeaturePolicies({ page, command, until }) {
  await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js');
    const p = await import('/src/protocol.js'); let bytes = new Uint8Array();
    window.__featureQA = { map: state.map, socket: state.ws, token: state.mapLoadToken, inventory: null, results: [], welcomes: 0, ownerEpoch: 0 };
    state.ws.addEventListener('message', event => {
      if (!(event.data instanceof ArrayBuffer)) return;
      bytes = p.appendBytes(bytes, new Uint8Array(event.data));
      for (;;) { const frame = p.popFrame(bytes); if (!frame) break; bytes = frame.rest;
        if (![p.ServerType.Inventory, p.ServerType.EconomyResult, p.ServerType.Welcome, p.ServerType.OwnerState].includes(frame.payload[0])) continue;
        const message = p.parseServerMessage(frame.payload);
        if (message.type === p.ServerType.Inventory) window.__featureQA.inventory = message;
        if (message.type === p.ServerType.EconomyResult) window.__featureQA.results.push(message);
        if (message.type === p.ServerType.Welcome) window.__featureQA.welcomes++;
        if (message.type === p.ServerType.OwnerState) window.__featureQA.ownerEpoch++;
      }
    });
    (await import('/src/net_outbound.js')).sendBytes(p.packStateRequest());
  });
  await until(() => !!window.__featureQA.inventory, null, 'authoritative inventory is observed');
  for (const id of ['quickBackpackBtn', 'quickTalentBtn', 'quickCraftBtn']) assert(await page.locator('#' + id).isVisible());
  await command('/rcon world_feature 0 craft hide');
  await until(() => document.querySelector('#quickCraftBtn').classList.contains('hidden'), null, 'world hides the craft shortcut');
  await page.locator('#game').focus(); await page.keyboard.press('c');
  assert(await page.locator('#craftPanel').isVisible(), 'hiding a shortcut does not revoke permitted access');
  await command('/rcon world_feature 0 craft disable');
  await until(async () => !(await import('/src/app_context.js')).state.craftOpen, null, 'revoking view closes an open panel');
  await page.keyboard.press('c'); await page.locator('#craftPanel').waitFor({ state: 'hidden' });
  await command('/rcon world_feature 0 craft show');
  await until(() => document.querySelector('#quickCraftBtn').disabled && !document.querySelector('#quickCraftBtn').classList.contains('hidden'), null, 'disabled feature explains its presence');
  const rejectedCraft = await page.evaluate(async () => {
    const { packEconomyRequest, EconomyOperation } = await import('/src/economy_requests.js');
    const baseline = window.__featureQA.inventory, id = (BigInt('0x' + baseline.inventoryLastRequest) + 1n).toString(16).padStart(16, '0');
    const bytes = packEconomyRequest({ inventoryScope: baseline.inventoryScope, actorScope: baseline.actorScope, requestId: id,
      expectedRevision: baseline.inventoryRevision, operation: EconomyOperation.Craft, petalType: 4, rarity: 1, count: 5 });
    (await import('/src/net_outbound.js')).sendBytes(bytes); return id;
  });
  await until(id => window.__featureQA.results.some(result => result.requestId === id && result.outcome === 3), rejectedCraft, 'server rejects raw craft requests while the feature is disabled');
  await command('/rcon world_feature 0 inventory readonly');
  await until(async () => !(await import('/src/client_features.js')).featureAllowed('inventory', 2), null, 'inventory editing revoked');
  await page.locator('#quickBackpackBtn').click(); assert(await page.locator('#backpackPanel').isVisible());
  await page.locator('#backpackCloseBtn').click(); await delay(250); await page.locator('#game').focus();
  const sequence = await page.evaluate(async () => (await import('/src/app_context.js')).state.slotTxSeq);
  await page.keyboard.press('r'); assert.equal(await page.evaluate(async () => (await import('/src/app_context.js')).state.slotTxSeq), sequence);
  for (const key of ['inventory', 'craft']) await command(`/rcon world_feature 0 ${key} default`);
  await until(async () => (await import('/src/client_features.js')).featureAllowed('craft', 2), null, 'default policy restores access');
  const scene = await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js'), qa = window.__featureQA;
    return { sameMap: state.map === qa.map, sameSocket: state.ws === qa.socket, sameToken: state.mapLoadToken === qa.token, welcomes: qa.welcomes };
  });
  assert.deepEqual(scene, { sameMap: true, sameSocket: true, sameToken: true, welcomes: 0 });
  return { ...scene, hiddenShortcut: true, readOnlyInventory: true, rawCraftRejected: true, disabledPanelClosed: true };
}

import assert from 'node:assert/strict';
import { setTimeout as delay } from 'node:timers/promises';

export async function checkControl({ page, command, until, getServerText, probe = false, larva = false }) {
  await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js');
    window.__controlQA = { map: state.map, token: state.mapLoadToken, socket: state.ws, loading: 0 };
    const screen = document.querySelector('#loadingScreen');
    window.__controlQA.observer = new MutationObserver(records => {
      for (const record of records) if (record.oldValue === 'false') window.__controlQA.loading++;
    });
    window.__controlQA.observer.observe(screen, { attributes: true, attributeFilter: ['aria-busy'], attributeOldValue: true });
    state.movementMode = 'keyboard';
  });
  const results = [];
  for (const name of larva ? ['firefly_larva'] : ['bee', 'mecha_flower']) {
    const before = await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      return { ids: [...state.entities.keys()], pos: state.entities.get(state.ownerEntityId).snapshot.pos, world: state.worldId };
    });
    await command(`/rcon spawn ${before.world} ${name} super ${Math.round(before.pos.x + 250)},${Math.round(before.pos.y)}`);
    await until(async ({ name, ids }) => {
      const { state } = await import('/src/app_context.js'), { MOB_IDS } = await import('/src/generated_ids.js');
      return [...state.entities.values()].some(e => e.snapshot.entityType === MOB_IDS[name] && !ids.includes(e.snapshot.entityId));
    }, { name, ids: before.ids }, `spawned ${name} is visible`);
    const target = await page.evaluate(async ({ name, ids }) => {
      const { state } = await import('/src/app_context.js'), { MOB_IDS } = await import('/src/generated_ids.js');
      return [...state.entities.values()].find(e => e.snapshot.entityType === MOB_IDS[name] && !ids.includes(e.snapshot.entityId) &&
        (name !== 'firefly_larva' || (e.snapshot.flags & 0x70) === 0)).snapshot.entityId;
    }, { name, ids: before.ids });
    const requested = larva ? await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      return [...state.entities.values()].find(e => e.snapshot.entityType === 44 && (e.snapshot.flags & 0x70) !== 0).snapshot.entityId;
    }) : target;
    await command(`/rcon control ReferenceQA ${before.world}:${requested}`);
    await delay(1200);
    const first = await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      return { id: state.ownerEntityId, type: state.entities.get(state.ownerEntityId)?.snapshot.entityType,
        pos: state.entities.get(state.ownerEntityId)?.snapshot.pos, connected: state.connected };
    });
    await page.locator('#game').focus(); await page.keyboard.down('d'); await delay(650); await page.keyboard.up('d'); await delay(600);
    const after = await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      const p = window.__controlQA;
      return { id: state.ownerEntityId, type: state.entities.get(state.ownerEntityId)?.snapshot.entityType,
        pos: state.entities.get(state.ownerEntityId)?.snapshot.pos, sameMap: state.map === p.map,
        sameToken: state.mapLoadToken === p.token, sameSocket: state.ws === p.socket, loading: p.loading,
        npcInventory: state.npcInventory };
    });
    results.push({ name, requested, target, first, after });
    if (!probe) {
      assert.equal(first.id, target, `${name} remains controlled after the first second`);
      assert.equal(after.id, target, `${name} remains controlled after movement`);
      assert(after.sameMap && after.sameToken && after.sameSocket && after.loading === 0, 'same-world control must preserve the scene and connection');
      assert(after.npcInventory, 'control keeps the NPC inventory isolated');
      assert(Math.hypot(after.pos.x - first.pos.x, after.pos.y - first.pos.y) > 5, 'movement reaches the controlled NPC');
    }
  }
  if (larva) {
    const root = results[0].target;
    const bodies = await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      return [...state.entities.values()].filter(e => e.snapshot.entityType === 44 && (e.snapshot.flags & 0x70) !== 0)
        .map(e => ({ id: e.snapshot.entityId, variant: (e.snapshot.flags & 0x70) >> 4 })).sort((a, b) => a.variant - b.variant);
    });
    assert(bodies.length > 0);
    const world = await page.evaluate(async () => (await import('/src/app_context.js')).state.worldId);
    for (const part of [bodies[0], bodies.at(-1), { id: root, variant: 0 }]) {
      await command(`/rcon control ReferenceQA ${world}:${part.id}`); await delay(1100);
      const after = await page.evaluate(async () => {
        const { state } = await import('/src/app_context.js');
        return { id: state.ownerEntityId, type: state.entities.get(state.ownerEntityId)?.snapshot.entityType,
          parts: [...state.entities.values()].filter(e => e.snapshot.entityType === 44 && !e.dying).map(e => e.snapshot.entityId),
          sameSocket: state.ws === window.__controlQA.socket, loading: window.__controlQA.loading };
      });
      results.push({ name: 'larva_part', requested: part.id, variant: part.variant, root, after });
      if (!probe) {
        assert.equal(after.id, root); assert(after.parts.includes(root)); assert.equal(after.parts.length, bodies.length + 1);
        assert.equal(after.loading, 0); assert(after.sameSocket);
      }
    }
    if (!probe) {
      await page.evaluate(async () => { const { state } = await import('/src/app_context.js'); window.__larvaOldSocket = state.ws; state.ws.close(); });
      await until(async root => {
        const { state } = await import('/src/app_context.js');
        return state.ws !== window.__larvaOldSocket && state.authenticated && state.ownerEntityId === root &&
          !!state.entities.get(root) && !state.mapLoadPromise && state.ownerStateLoaded && state.inventoryLoaded;
      }, root, 'reconnect keeps the controlled larva rather than spawning a player flower', 150);
      results.push({ name: 'larva_reconnect', root, keptControl: true });
    }
  }
  if (!probe && getServerText && !larva) {
    const waitLog = async pattern => {
      for (let i = 0; i < 100; ++i) {
        const match = [...getServerText().matchAll(pattern)].at(-1);
        if (match) return match;
        await delay(100);
      }
      throw new Error(`Missing test command result: ${pattern}`);
    };
    await command('/rcon world_create data/maps/test.tmj deathmatch');
    const created = await waitLog(/Created world id=(\d+) map=data\/maps\/test\.tmj/g);
    const world = Number(created[1]);
    await command(`/rcon spawn ${world} bee super 35072,26368`);
    const spawned = await waitLog(new RegExp(`Spawned .* Bee entity ${world}:(\\d+) at`, 'g'));
    const target = Number(spawned[1]);
    await command(`/rcon control ReferenceQA ${world}:${target}`);
    await until(async ({ world, target }) => {
      const { state } = await import('/src/app_context.js');
      return state.worldId === world && state.ownerEntityId === target && !!state.entities.get(target) && !state.mapLoadPromise;
    }, { world, target }, 'cross-world control finishes loading the destination with the selected NPC', 150);
    await delay(1000);
    const after = await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      return { id: state.ownerEntityId, world: state.worldId, type: state.entities.get(state.ownerEntityId)?.snapshot.entityType,
        sameSocket: state.ws === window.__controlQA.socket, npcInventory: state.npcInventory };
    });
    assert.equal(after.id, target); assert.equal(after.world, world); assert.equal(after.type, 13);
    assert(after.sameSocket && after.npcInventory);
    results.push({ name: 'cross_world_bee', target, after });
  }
  await page.evaluate(() => window.__controlQA.observer.disconnect());
  return results;
}

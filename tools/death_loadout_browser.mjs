import assert from 'node:assert/strict';
import { setTimeout as delay } from 'node:timers/promises';

export async function beginDeathProbe(page) {
  await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js');
    const { packChores } = await import('/src/protocol.js');
    const reviveBytes = packChores(false, false, true, false, false);
    window.__deathQA = { map: state.map, token: state.mapLoadToken, loading: 0, respawns: 0 };
    const screen = document.querySelector('#loadingScreen');
    window.__deathQA.observer = new MutationObserver(records => {
      // oldValue=false 说明至少发生过一次由非加载到加载的切换，即使同一批中已经结束。
      for (const record of records) if (record.oldValue === 'false') window.__deathQA.loading++;
    });
    window.__deathQA.observer.observe(screen, { attributes: true, attributeFilter: ['aria-busy'], attributeOldValue: true });
    const send = state.ws.send.bind(state.ws);
    state.ws.send = bytes => {
      if (bytes.length === reviveBytes.length && bytes.every((byte, i) => byte === reviveBytes[i])) window.__deathQA.respawns++;
      send(bytes);
    };
  });
}

export async function checkDeathLoadout({ page, until }) {
  await page.locator('#deathCloseBtn').click();
  await page.locator('#quickBackpackBtn').click(); await delay(350);
  const moveCard = async (name, rarity, target) => {
    const card = page.locator('.inventory-group').filter({ has: page.locator('.inventory-group-title', { hasText: new RegExp(`^${rarity}$`) }) })
      .locator('.inventory-item').filter({ has: page.locator('.petal-card-name', { hasText: new RegExp(`^${name}$`) }) }).first();
    await card.scrollIntoViewIfNeeded();
    const from = await card.boundingBox(), to = await page.locator(target).first().boundingBox();
    await page.mouse.move(from.x + from.width / 2, from.y + from.height / 2); await page.mouse.down();
    await page.mouse.move(to.x + to.width / 2, to.y + to.height / 2, { steps: 10 }); await page.mouse.up();
  };
  await moveCard('Leaf', 'Ultra', '#primarySlots .slot');
  await until(async () => {
    const { state } = await import('/src/app_context.js');
    const { PETAL_IDS } = await import('/src/generated_ids.js');
    return state.ownerSlots[0]?.petalType === PETAL_IDS.leaf && state.ownerSlots[0]?.rarity === 7;
  }, null, 'dead player can drag an inventory card into the primary build');
  await moveCard('Rose', 'Common', '#secondarySlots .slot');
  await until(async () => {
    const { state } = await import('/src/app_context.js');
    const { PETAL_IDS } = await import('/src/generated_ids.js');
    return state.secondarySlots[0]?.petalType === PETAL_IDS.rose && state.secondarySlots[0]?.rarity === 1;
  }, null, 'dead player can configure the reserve build');
  await page.locator('#backpackCloseBtn').click(); await delay(200); await page.locator('#game').focus();
  const expected = await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js');
    const primary = structuredClone(state.ownerSlots), secondary = structuredClone(state.secondarySlots);
    [primary[0], secondary[0]] = [secondary[0], primary[0]];
    window.__qaDelivery.socket = state.ws;
    return { primary, secondary };
  });
  try {
    for (let i = 0; i < 3; i++) await page.keyboard.press('1');
    await page.locator('#reviveBtn').click(); await delay(400);
    assert.equal(await page.evaluate(() => window.__deathQA.respawns), 0, 'respawn must wait while build receipts are withheld');
    assert(await page.locator('#reviveBtn').isDisabled());
  } finally { await page.evaluate(() => window.__qaDelivery.stop()); }
  await until(async expected => {
    const { state } = await import('/src/app_context.js');
    const same = (a, b) => a?.length === b.length && a.every((s, i) => s.petalType === b[i].petalType && s.rarity === b[i].rarity);
    const owner = state.entities.get(state.ownerEntityId);
    return owner && !(owner.snapshot.flags & 4) && !state.wasOwnerDead &&
      same(state.ownerSlots, expected.primary) && same(state.secondarySlots, expected.secondary) && same(owner.snapshot.primarySlots, expected.primary);
  }, expected, 'respawn uses the last primary and reserve build, including queued rapid swaps', 150);
  const scene = await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js');
    const probe = window.__deathQA; probe.observer.disconnect();
    return { sameMap: probe.map === state.map, sameToken: probe.token === state.mapLoadToken,
      loading: probe.loading, respawns: probe.respawns };
  });
  assert.deepEqual(scene, { sameMap: true, sameToken: true, loading: 0, respawns: 1 });
  return { ...scene, deadPrimaryEdit: true, deadReserveEdit: true, expected };
}

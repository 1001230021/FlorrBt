import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";

const game = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
function method(name) {
  const start = game.indexOf(`function ${name}(`);
  assert.ok(start >= 0, name);
  const end = /^}/m.exec(game.slice(start));
  assert.ok(end, name);
  return game.slice(start, start + end.index + 1);
}

const transportSource = readFileSync(new URL("../web/src/net_outbound.js", import.meta.url), "utf8")
  .replace(/^import[^\n]+\n/gm, "").replace(/^export /gm, "");
const transportState = { ws: null }, writes = [];
const transport = vm.runInNewContext(`${transportSource}\n;({sendBytes, sendPacketBatch})`, {
  state: transportState, WebSocket: { OPEN: 1 }, console: { warn() {} }, Uint8Array,
});
assert.equal(transport.sendBytes(new Uint8Array([1])), false);
transportState.ws = { readyState: 1, send() { throw new Error("socket closing"); } };
assert.equal(transport.sendBytes(new Uint8Array([1])), false);
transportState.ws.send = bytes => writes.push([...bytes]);
assert.equal(transport.sendPacketBatch(new Uint8Array([1]), null, new Uint8Array([2, 3])), true);
assert.deepEqual(writes, [[1, 2, 3]]);

{
  const state = { connected: true, authenticated: true, ownerStateLoaded: true, inventoryLoaded: true,
    ownerEntityId: 1, entities: new Map(), ownerLoadoutLocked: false };
  const respawnRequest = { pending: false };
  const blocked = vm.runInNewContext(`${method("loadoutEditingBlocked")}; loadoutEditingBlocked`,
    { state, respawnRequest, featureAllowed: () => true, FeatureAccess: { Edit: 2 } });
  assert.equal(blocked(), true, "a missing/respawning owner cannot edit a live loadout");
  state.entities.set(1, { snapshot: { flags: 0 } }); assert.equal(blocked(), false);
  state.ownerLoadoutLocked = true; assert.equal(blocked(), true);
  state.ownerLoadoutLocked = false; state.entities.get(1).snapshot.flags = 4;
  assert.equal(blocked(), false, "death keeps build editing available");
  respawnRequest.pending = true; assert.equal(blocked(), true, "freeze edits after requesting respawn");
  respawnRequest.pending = false; state.inventoryLoaded = false; assert.equal(blocked(), true);
}

const item = { petalType: 1, rarity: 1 }, second = { petalType: 2, rarity: 1 };
const functions = ["equipFromInventoryAt", "unequipSlot", "setSecondaryFromInventory", "clearSecondarySlot",
  "quickSwapSlot", "movePrimaryToPrimary", "movePrimaryToSecondary", "moveSecondaryToPrimary", "moveSecondaryToSecondary"];
for (const sent of [false, true]) for (const name of functions) {
  const transactions = [], animations = [];
  const state = { authenticated: true, selectedSlot: 0 };
  const primary = [{ ...item }, { ...second }], secondary = [{ ...second }, { ...item }];
  const inventory = [{ ...item, count: 10 }, { ...second, count: 10 }];
  const scope = { state, performance: { now: () => 1000 }, loadoutEditingBlocked: () => false,
    slotHasItem: x => !!x?.petalType && !!x?.rarity, copySlot: x => ({ ...x }), emptySlot: () => ({ petalType: 0, rarity: 0 }),
    displayOwnerSlots: () => structuredClone(primary), displaySecondarySlots: () => structuredClone(secondary),
    displayInventory: () => structuredClone(inventory), makeDisplaySlotState: () => ({ ownerSlots: structuredClone(primary),
      secondarySlots: structuredClone(secondary), inventory: structuredClone(inventory) }),
    getDisplayInventoryCount: () => 10, applyInventoryDelta: rows => rows,
    enqueueSlotTransaction: (...args) => sent ? transactions.push(args) : null, renderInventoryPanel() {}, scheduleInventoryUiRender() {},
    animateSlotSwapFlight: index => animations.push(index) };
  const operation = vm.runInNewContext(`let lastSlotSwapAt = -Infinity; ${method(name)}; ${name}`, scope);
  if (name.startsWith("move")) operation(0, 1);
  else if (name === "equipFromInventoryAt" || name === "setSecondaryFromInventory") operation(0, item);
  else operation(0);
  assert.equal(transactions.length, sent ? 1 : 0, `${name}: failed sends cannot commit optimistic inventory`);
  if (name === "quickSwapSlot") assert.equal(animations.length, sent ? 1 : 0);
}

for (const forgeMode of [false, true]) for (const sent of [false, true]) {
  let starts = 0;
  const state = { authenticated: true, forgeMode, craftPhase: "staged", craftSource: { petalType: 1, rarity: 8 } };
  const submit = vm.runInNewContext(`${method("submitCraft")}; submitCraft`, {
    state, raritySuper: 8, getCraftSlotCount: () => 5, getInventoryCount: () => 5, canCraftRarity: () => true,
    featureAllowed: () => true, FeatureAccess: { Edit: 2, Forge: 4 },
    economyRequests: { submit: () => sent }, EconomyOperation: { Craft: 1, Forge: 2 }, renderCraftPanel() {},
    performance: { now: () => 100 }, beginCraftSpinMotion: () => starts++,
  });
  submit(); assert.equal(state.craftPhase, sent ? "spinning" : "staged"); assert.equal(starts, sent ? 1 : 0);
}
{
  let cancelled = 0, resolved = 0;
  const messages = [];
  const state = { craftPhase: "spinning", craftSource: { petalType: 1, rarity: 1 }, craftSlots: Array(5).fill(item) };
  const handler = vm.runInNewContext(`let craftVisualGeneration = 0, craftResultDismissing = false;
    ${method("clearCraftDisplay")} ${method("handleCraftResult")}; handleCraftResult`, {
    state, craftSpin: { cancel: () => cancelled++, isActive: () => true, resolve: outcome => { resolved++; outcome.onFinished(); } },
    renderCraftPanel() {}, addConsoleLine: text => messages.push(text), t: key => key,
    compactCraftItems: items => items, placeCraftReturns: (_slots, items) => items,
    slotHasItem: x => !!x?.petalType && !!x?.rarity,
  });
  handler({ success: false, consumed: 0, petalType: 2, rarity: 1 });
  assert.equal(cancelled, 0, "a different recipe's late reply cannot finish the current request");
  handler({ success: false, consumed: 0, petalType: 1, rarity: 1 });
  assert.equal(cancelled, 1); assert.equal(state.craftPhase, "idle"); assert.equal(resolved, 0);
  assert.deepEqual(messages, ["status.craftRejected"]);
  state.craftPhase = "spinning"; state.craftSource = { ...item }; state.craftSlots = Array(5).fill(item);
  handler({ success: false, consumed: 5, petalType: 1, rarity: 1, items: [{ ...item, count: 3 }] });
  assert.equal(resolved, 1); assert.equal(state.craftPhase, "returned");
}
{
  let renders = 0;
  const getCache = vm.runInNewContext(`let minimapCache = null; ${method("getMinimapCache")}; getMinimapCache`, {
    minimapGridMetrics: () => ({ subdivisions: 1, cellWidth: 1, cellHeight: 1 }), colorfulMinimapEnabled: () => true,
    minimapBackgroundColor: "#000", minimapMonochromeBackgroundColor: "#000",
    createMinimapColorResolver: () => () => [0, 0, 0, 255], fillMinimapPixels() {},
    rasterizeMinimapTerrain: () => renders++, rasterizeMinimapDifficulty() {},
    document: { createElement: () => ({ getContext: () => ({ createImageData: () => ({ data: [] }), putImageData() {} }) }) },
  });
  const map = { path: "test", width: 2, height: 2, layers: [{ tiles: [1, 1, 1, 1] }], terrainRevision: 0 };
  const first = getCache(map); assert.equal(getCache(map), first); assert.equal(renders, 1);
  map.layers[0].tiles[0] = 2; map.terrainRevision++;
  assert.notEqual(getCache(map), first); assert.equal(renders, 2, "same-size terrain edits invalidate the minimap");
}
console.log("PASS transport failure, 9 inventory paths, craft/forge rejection and send gates, late recipe replies, minimap revisions");

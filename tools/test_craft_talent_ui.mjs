// Offline model/animation checks. No browser, game connection or account data.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { initI18n } from "../web/src/i18n.js";
import { talentNodes, talentNodeByKey } from "../web/src/talent_data.js";
import { buildTalentLayout, collectTalentChain, collectTalentRemoval, zoomTalentView } from "../web/src/talent_layout.js";
import { placeCraftReturns } from "../web/src/craft_slots.js";
import { createCraftSpinController, getCraftPulseRadius } from "../web/src/craft_motion.js";
import { createTalentTree } from "../web/src/ui_talents.js";

const layout = buildTalentLayout(talentNodes);
assert.equal(layout.positions.size, talentNodes.length);
assert.equal(layout.edges.length, talentNodes.length);
for (const node of talentNodes) {
  const edge = layout.edges.find(edge => edge.to === node.key);
  assert.equal(edge.from, node.base || null);
  const chain = collectTalentChain(node, new Set(), talentNodeByKey);
  assert.equal(chain.at(-1), node);
  for (let i = 1; i < chain.length; i++) assert.equal(chain[i].base, chain[i - 1].key);
}
for (const [aKey, a] of layout.positions) for (const [bKey, b] of layout.positions) {
  if (aKey >= bKey) continue;
  assert.ok(Math.hypot(a.x - b.x, a.y - b.y) >= 84 - 1e-6, `${aKey} overlaps ${bKey}`);
}
const allOwned = new Set(talentNodes.map(node => node.key));
for (const root of layout.roots) {
  const removing = collectTalentRemoval(root, allOwned, talentNodes);
  const keys = new Set(removing.map(node => node.key));
  assert.ok(keys.has(root.key));
  for (const node of talentNodes) if (keys.has(node.base)) assert.ok(keys.has(node.key));
}
const view = { x: 40, y: 50, scale: 0.8 }, point = { x: 100, y: 120 };
const zoomed = zoomTalentView(view, point, 1.2);
assert.ok(Math.abs((point.x - zoomed.x) / zoomed.scale - (point.x - view.x) / view.scale) < 1e-8);
assert.equal(zoomTalentView(view, point, 100).scale, 1.65);

for (let original = 5; original < 120; original += 7) {
  const previous = Array.from({ length: 5 }, (_, index) => ({ petalType: 4, rarity: 6, count: Math.floor(original / 5) + (index < original % 5 ? 1 : 0) }));
  for (let left = 0; left <= original; left++) {
    const result = placeCraftReturns(previous, [{ petalType: 4, rarity: 6, count: left }], () => 0.4);
    assert.equal(result.reduce((sum, slot) => sum + (slot?.count || 0), 0), left);
    result.forEach((slot, i) => assert.ok((slot?.count || 0) <= previous[i].count));
  }
}
for (const low of [0.3, 0.5, 0.68]) {
  assert.equal(getCraftPulseRadius(0, low), 1);
  assert.equal(getCraftPulseRadius(1, low), 1);
  assert.ok(Math.abs(getCraftPulseRadius(0.38, low) - low) < 1e-8);
  assert.ok(Math.abs(getCraftPulseRadius(0.38 - 1e-5, low) - getCraftPulseRadius(0.38 + 1e-5, low)) < 1e-6);
}

// Minimal animation surface and fake clock: ensure success, failure, timeout,
// cancel and delayed verdict all terminate once and restore CSS ownership.
const originals = new Map(["performance", "requestAnimationFrame", "cancelAnimationFrame", "setTimeout", "clearTimeout", "getComputedStyle", "document", "window", "innerWidth", "innerHeight", "fetch", "ResizeObserver"].map(key => [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
let clock = 0, serial = 0;
const frames = new Map(), timers = new Map();
const fakeGlobals = {
  performance: { now: () => clock },
  requestAnimationFrame: fn => { const id = ++serial; frames.set(id, fn); return id; },
  cancelAnimationFrame: id => frames.delete(id),
  setTimeout: (fn, ms) => { const id = ++serial; timers.set(id, { fn, at: clock + ms }); return id; },
  clearTimeout: id => timers.delete(id),
  getComputedStyle: () => ({ getPropertyValue: () => "" }),
};
for (const [key, value] of Object.entries(fakeGlobals)) Object.defineProperty(globalThis, key, { configurable: true, value });
function advance(ms, fps = 60) {
  const end = clock + ms;
  while (clock < end) {
    clock = Math.min(end, clock + 1000 / fps);
    for (const [id, timer] of [...timers]) if (timer.at <= clock) { timers.delete(id); timer.fn(); }
    const due = [...frames.values()]; frames.clear();
    for (const fn of due) fn(clock);
  }
}
function classes() {
  const values = new Set();
  return { add: value => values.add(value), remove: value => values.delete(value), contains: value => values.has(value),
    toggle(value, on) { if (on ?? !values.has(value)) values.add(value); else values.delete(value); } };
}
function element() {
  const listeners = new Map(), attributes = new Map();
  return { children: [], classList: classes(), dataset: {}, style: { setProperty() {} },
    clientWidth: 600, clientHeight: 560, offsetWidth: 200, offsetHeight: 140,
    append(...nodes) { this.children.push(...nodes); }, appendChild(node) { this.children.push(node); },
    replaceChildren(...nodes) { this.children = nodes; },
    setAttribute(key, value) { attributes.set(key, value); }, getAttribute: key => attributes.get(key),
    addEventListener(key, fn) { if (!listeners.has(key)) listeners.set(key, []); listeners.get(key).push(fn); },
    emit(key, event = {}) { for (const fn of listeners.get(key) || []) fn({ preventDefault() {}, stopPropagation() {}, ...event }); },
    closest() { return this.dataset.talentKey ? this : null; },
    getBoundingClientRect: () => ({ left: 100, top: 200, width: 64, height: 64, bottom: 264 }),
    setPointerCapture() {}, hasPointerCapture: () => false,
  };
}
function makeController(rounds = 1) {
  const cards = Array.from({ length: 5 }, (_, i) => {
    const angle = i * Math.PI * 2 / 5 - Math.PI / 2;
    const stack = { style: {}, classList: classes() };
    return { dataset: { craftSlot: String(i) }, classList: classes(), style: {},
      offsetLeft: 160 + 88 * Math.cos(angle), offsetTop: 135 + 88 * Math.sin(angle), offsetWidth: 56, offsetHeight: 56,
      querySelector: () => stack };
  });
  const bursts = [];
  const controller = createCraftSpinController({ stage: { clientWidth: 320, clientHeight: 270, querySelectorAll: () => cards },
    particles: { burst: (...args) => bursts.push(args), clear() {} }, rarityColor: rarity => `rarity-${rarity}`,
    minRounds: rounds, maxRounds: rounds, watchdogMs: 3000 });
  return { cards, bursts, controller };
}
try {
  for (const fps of [30, 60, 120]) {
    for (const success of [true, false]) {
      const { controller, cards, bursts } = makeController();
      let done = 0;
      controller.begin({ slotRarities: [6, 6, 6, 6, 6] });
      controller.resolve({ success, resultRarity: 7, slotWillBeEmpty: [true, false, true, false, false], onFinished: () => done++ });
      advance(2400, fps);
      assert.equal(done, 1); assert.equal(controller.isActive(), false);
      assert.ok(bursts.length > 0);
      assert.ok(cards.every(card => !card.classList.contains("craft-orbiting")));
      assert.ok(cards.every(card => card.style.transform === ""));
      assert.equal(frames.size, 0); assert.equal(timers.size, 0);
    }
  }
  const { controller, cards } = makeController(0);
  let done = 0, timeout = 0;
  controller.begin({ onWatchdogTimeout: () => timeout++ });
  advance(2000); assert.equal(controller.isActive(), true);
  controller.resolve({ success: true, resultRarity: 7, onFinished: () => done++ });
  advance(700); assert.equal(done, 1);
  controller.begin({ onWatchdogTimeout: () => timeout++ });
  advance(3100); assert.equal(timeout, 1);
  controller.begin(); controller.resolve({ success: false, slotWillBeEmpty: [true, true, true, true, true], onFinished: () => done++ });
  advance(90); controller.cancel(); advance(500);
  assert.equal(done, 1); assert.equal(frames.size, 0); assert.equal(timers.size, 0);
  assert.ok(cards.every(card => !card.classList.contains("craft-orbiting")));

  const half = makeController(7).controller;
  let halfDone = 0;
  half.begin(); half.resolve({ success: true, resultRarity: 7, onFinished: () => halfDone++ });
  advance(2100); assert.equal(halfDone, 0);
  advance(400); assert.equal(halfDone, 1, "7 drawn rounds must finish as 3.5 effective rounds, not 7");

  // Talent DOM is a stable retained graph. Inventory refreshes do not cancel
  // a hold; closing or turning it into a drag DOES cancel, without a packet.
  const doc = { createElement: element, createElementNS: element, body: element(), documentElement: {}, querySelectorAll: () => [], addEventListener() {} };
  Object.defineProperty(globalThis, "document", { configurable: true, value: doc });
  Object.defineProperty(globalThis, "window", { configurable: true, value: { addEventListener() {} } });
  Object.defineProperty(globalThis, "innerWidth", { configurable: true, value: 1920 });
  Object.defineProperty(globalThis, "innerHeight", { configurable: true, value: 1080 });
  Object.defineProperty(globalThis, "fetch", { configurable: true, value: async url => {
    assert.ok(url.href.startsWith(new URL("../web/locales/", import.meta.url).href));
    return { ok: true, json: async () => JSON.parse(readFileSync(url, "utf8")) };
  } });
  await initI18n("en");
  let resizeTree;
  Object.defineProperty(globalThis, "ResizeObserver", { configurable: true, value: class {
    constructor(callback) { resizeTree = callback; }
    observe() {}
  } });
  const panel = element(), viewport = element(), points = element(); panel.classList.add("open");
  const state = { authenticated: true, talentPoints: 100, talents: [] }, requests = [];
  const tree = createTalentTree({ panel, viewport, points, getState: () => state,
    request: (action, nodes) => { requests.push({ action, nodes }); return true; } });
  tree.render(); advance(20);
  const graph = viewport.children[0];
  const target = graph.children.find(node => node.dataset?.talentKey === talentNodes[0].key);
  viewport.emit("pointerdown", { target, button: 0, pointerId: 1, clientX: 200, clientY: 300 });
  advance(250); tree.render();
  assert.equal(viewport.children[0], graph);
  advance(290); assert.equal(requests.length, 1);
  assert.equal(target.getAttribute("aria-pressed"), "false", "wait for server before lighting");
  state.talents = requests[0].nodes; tree.acknowledge();
  assert.equal(target.getAttribute("aria-pressed"), "true");
  viewport.emit("pointerup", { pointerId: 1 });

  viewport.emit("pointerdown", { target, button: 0, pointerId: 2, clientX: 200, clientY: 300 });
  advance(200); tree.close(); advance(500);
  assert.equal(requests.length, 1);
  viewport.emit("pointerdown", { target, button: 0, pointerId: 3, clientX: 200, clientY: 300 });
  viewport.emit("pointermove", { pointerId: 3, clientX: 220, clientY: 300 });
  advance(700); assert.equal(requests.length, 1);
  viewport.emit("pointerup", { pointerId: 3 });

  // A tree panned outside the viewport is intentional. Even a tiny footer /
  // resize notification must not snap its offset back to the viewport bounds.
  viewport.emit("pointerdown", { target: viewport, button: 0, pointerId: 4, clientX: 200, clientY: 300 });
  viewport.emit("pointermove", { pointerId: 4, clientX: -600, clientY: 300 });
  viewport.emit("pointerup", { pointerId: 4 });
  advance(20);
  const pannedView = graph.style.transform;
  resizeTree(); advance(20);
  assert.equal(graph.style.transform, pannedView);
  const xBeforeResize = Number(pannedView.match(/translate\(([-\d.]+)px/)[1]);
  viewport.clientWidth += 20;
  resizeTree(); advance(20);
  const xAfterResize = Number(graph.style.transform.match(/translate\(([-\d.]+)px/)[1]);
  assert.equal(xAfterResize - xBeforeResize, 10);

  const footerButtons = panel.children.at(-1).children;
  const zoomIn = footerButtons.find(button => button.className === "talent-zoom-control" && button.textContent === "+");
  const beforeZoom = Number(graph.style.transform.match(/scale\(([\d.]+)\)/)[1]);
  zoomIn.emit("click"); advance(20);
  assert.ok(Number(graph.style.transform.match(/scale\(([\d.]+)\)/)[1]) > beforeZoom);
  const reset = footerButtons.find(button => button.className === "talent-reset");
  reset.emit("click"); assert.equal(requests.length, 1, "reset requires confirmation");
  reset.emit("click"); assert.equal(requests.length, 2);
  assert.equal(requests[1].action, "remove");
  tree.reset(); advance(20);
  assert.equal(frames.size, 0); assert.equal(timers.size, 0);
} finally {
  for (const [key, descriptor] of originals) {
    if (descriptor) Object.defineProperty(globalThis, key, descriptor); else delete globalThis[key];
  }
}
const craftGameSource = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
const craftMotionSource = readFileSync(new URL("../web/src/craft_motion.js", import.meta.url), "utf8");
const craftStyles = readFileSync(new URL("../web/styles.css", import.meta.url), "utf8");
const petalInfoSource = readFileSync(new URL("../web/src/petal_info.js", import.meta.url), "utf8");
assert.match(craftGameSource, /const showRing = true/);
assert.match(craftGameSource, /craft-card-fading/);
assert.match(craftMotionSource, /craft-card-fading/);
assert.match(craftMotionSource, /card\.stack\.style\.opacity = String\(cardAlpha\)/);
assert.match(craftStyles, /--craft-card-size: 54px/);
assert.match(craftStyles, /\.craft-stage-slot\.empty/);
assert.match(craftStyles, /\.craft-stage\.success \.craft-stage-slot\.empty[\s\S]*?background: transparent/);
assert.doesNotMatch(craftStyles, /\.craft-matrix-cell\.selected\s*\{/);
assert.match(petalInfoSource, /if \(count > 1 \|\| options\.showCount === true\)/);
assert.match(petalInfoSource, /badge\.hidden = count <= 1/);
console.log(`PASS craft lifecycle at 30/60/120fps; conserved returns; ${talentNodes.length} talent nodes, prerequisites, layout and zoom`);

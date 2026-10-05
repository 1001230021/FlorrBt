// Performance contracts, not real-device FPS: no browser/server/account I/O.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { createCraftMatrix } from "../web/src/craft_matrix.js";

let writes = 0, createdCards = 0, open = true, observer, copyRevision = 1;
const frames = [], vanishes = [], selections = [];
function element(tag = "div") {
  const events = new Map();
  return { tagName: tag.toUpperCase(), children: [], parentElement: null, dataset: {}, className: "", title: "", textContent: "",
    style: { setProperty() {} }, classList: { toggle() {} },
    get firstElementChild() { return this.children[0] || null; },
    get nextElementSibling() { const siblings = this.parentElement?.children || []; return siblings[siblings.indexOf(this) + 1] || null; },
    insertBefore(node, before) {
      writes++; node.remove();
      const i = before ? this.children.indexOf(before) : this.children.length;
      assert.ok(i >= 0); this.children.splice(i, 0, node); node.parentElement = this;
    },
    replaceChildren(...nodes) { writes++; for (const child of [...this.children]) child.remove(); for (const node of nodes) this.insertBefore(node, null); },
    appendChild(node) { this.insertBefore(node, null); },
    remove() { if (!this.parentElement) return; writes++; const siblings = this.parentElement.children; siblings.splice(siblings.indexOf(this), 1); this.parentElement = null; },
    addEventListener(name, fn) { events.set(name, fn); },
    emit(name) { events.get(name)?.(); },
    querySelector(selector) { return this.children.find(node => selector === `.${node.className}`) || null; },
  };
}
class Observer {
  constructor(callback, options) { this.callback = callback; this.targets = new Set(); this.options = options; observer = this; }
  observe(node) { this.targets.add(node); }
  unobserve(node) { this.targets.delete(node); }
  disconnect() { this.targets.clear(); }
  show(nodes, visible = true) { this.callback(nodes.map(target => ({ target, isIntersecting: visible }))); }
}
const root = element();
const options = {
  root, document: { createElement: element }, Observer,
  schedule: callback => frames.push(callback), clock: () => 0, isOpen: () => open,
  createStack(item) {
    createdCards++;
    const stack = element("span"), badge = element("span");
    stack.className = "petal-stack"; badge.className = "petal-count";
    stack.replaceChildren(badge); return stack;
  },
  visualKey: item => `${item.name}:${copyRevision}`,
  describe: item => `${item.name} ${item.count}`,
  formatCount: item => item.forge ? `${item.count}/5` : `x${item.count}`,
  attachTooltip(node, type, rarity) { node.dataset.tooltip = `${type}:${rarity}`; },
  clearTooltip(node) { delete node.dataset.tooltip; },
  vanish(stack, options) { vanishes.push({ stack, ...options }); },
  select: (...args) => selections.push(args),
};
const matrixUi = createCraftMatrix(options);
const items = Array.from({ length: 630 }, (_, i) => ({ key: `${Math.floor(i / 9) + 1}:${i % 9 + 1}`,
  petalType: Math.floor(i / 9) + 1, rarity: i % 9 + 1, baseCount: 15, count: 15, lit: true, name: `petal${i}`, forge: false }));
matrixUi.render(items);
const matrix = root.firstElementChild;
assert.equal(matrix.children.length, 630);
assert.equal(createdCards, 0, "opening does not synchronously build 630 multi-copy SVG cards");
assert.equal(observer.options.root, root);
const visible = matrix.children.slice(0, 54);
observer.show(visible);
assert.equal(frames.length, 1, "a whole observer batch shares one preparation task");
frames.shift()();
assert.equal(createdCards, 12, "bounded card creation per frame");
while (frames.length) frames.shift()();
assert.equal(createdCards, 54, "only visible/overscan cards are materialized");
const first = matrix.children[0], firstStack = first.firstElementChild;
writes = 0;
for (let i = 0; i < 20; i++) matrixUi.render(items);
assert.equal(createdCards, 54);
assert.equal(writes, 0, "unchanged reopens neither rebuild cards nor reappend every cell");

items[0] = { ...items[0], count: 10 };
matrixUi.render(items);
assert.equal(first.firstElementChild, firstStack);
assert.equal(firstStack.firstElementChild.textContent, "x10");
assert.equal(createdCards, 54, "count-only updates reuse imagery and labels");
items[0] = { ...items[0], count: 1 };
matrixUi.render(items);
assert.equal(firstStack.firstElementChild.hidden, true, "single cards hide their badge without rebuilding");
items[0] = { ...items[0], count: 10 };
matrixUi.render(items);
assert.equal(firstStack.firstElementChild.hidden, false, "multi-stack badge comes back on count changes");
items[0] = { ...items[0], count: 0 };
matrixUi.render(items); matrixUi.render(items);
assert.equal(vanishes.length, 1, "a staged-to-zero card vanishes once");
assert.match(first.className, /empty lit/, "lighting still uses actual count, not remaining count");
assert.equal(first.dataset.tooltip, undefined);
items[0] = { ...items[0], count: 3 };
matrixUi.render(items);
const replenished = first.firstElementChild;
assert.notEqual(replenished, firstStack);
vanishes[0].onFinish();
assert.equal(first.firstElementChild, replenished, "old vanish cannot remove new inventory");
items[0] = { ...items[0], lit: false };
matrixUi.render(items);
const pointer = { button: 0, shiftKey: true, preventDefault() {}, stopPropagation() {} };
first.onpointerdown(pointer); assert.equal(selections.length, 0);
items[0] = { ...items[0], lit: true };
matrixUi.render(items); first.onpointerdown(pointer);
assert.deepEqual(selections.pop(), [1, 1, { all: true }], "retained handlers read current availability");

observer.show(visible, false);
const nextVisible = matrix.children.slice(54, 108);
open = false; observer.show(nextVisible);
const beforeClosed = createdCards;
while (frames.length) frames.shift()();
assert.equal(createdCards, beforeClosed, "closed panels stop background DOM work");
open = true; matrixUi.render(items);
while (frames.length) frames.shift()();
assert.equal(createdCards - beforeClosed, 54);
const offscreen = matrix.children[400];
offscreen.emit("focus"); assert.ok(offscreen.firstElementChild, "keyboard focus prepares an offscreen card");
const beforeCopyChange = createdCards;
copyRevision++; matrixUi.render(items);
assert.ok(createdCards > beforeCopyChange, "copy/talent/name changes invalidate the visible card only");

matrixUi.render([{ ...items[0], key: "forge:1", forge: true, rarity: 8 }], { forge: true });
assert.equal(root.firstElementChild, matrix);
assert.equal(matrix.children.length, 1);
observer.show(matrix.children); while (frames.length) frames.shift()();
assert.equal(matrix.children[0].firstElementChild.firstElementChild.textContent, "3/5");
matrixUi.dispose(); assert.equal(root.children.length, 0); assert.equal(observer.targets.size, 0);
while (frames.length) frames.shift()();

const fallback = createCraftMatrix({ ...options, root: element(), Observer: null });
const beforeFallback = createdCards;
fallback.render(items.slice(0, 30));
assert.equal(createdCards, beforeFallback);
while (frames.length) frames.shift()();
assert.equal(createdCards - beforeFallback, 30, "without IntersectionObserver work is still sliced");
fallback.dispose();

const singleRoot = element();
const singleGrid = createCraftMatrix({ ...options, root: singleRoot, Observer: null,
  createStack: () => element("span") });
singleGrid.render([{ ...items[0], count: 1 }]); while (frames.length) frames.shift()();
const singleStack = singleRoot.firstElementChild.firstElementChild.firstElementChild;
assert.equal(singleStack.children.length, 0);
singleGrid.render([{ ...items[0], count: 2 }]);
assert.equal(singleStack.firstElementChild.textContent, "x2");
assert.equal(singleStack.firstElementChild.hidden, false);
singleGrid.render([{ ...items[0], count: 1 }]);
assert.equal(singleStack.firstElementChild.hidden, true);
singleGrid.dispose();

// Exercise the actual matrix-model assembly: it must not invoke the old
// O(cells * inventory) copy/find path, including staged display subtraction.
const game = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
const start = game.indexOf("function renderCraftMatrix()");
const end = game.indexOf("function renderForgeMatrix()", start);
let modelItems, reads = 0;
const scope = {
  craftMatrixUi: { render: items => { modelItems = items; } },
  state: { forgeMode: false, craftPhase: "staged", craftSlots: [{ petalType: 1, rarity: 1, count: 5 }] },
  slotHasItem: item => item?.petalType > 0, craftRarityDisplayOrder: [1, 2, 3],
  buildCraftRows: () => { reads++; return [{ petalType: 1, counts: new Map([[1, 5], [2, 4]]) }]; },
  getPetalDisplayName: type => `petal${type}`, canCraftRarity: rarity => rarity >= 1 && rarity <= 9,
};
vm.runInNewContext(`${game.slice(start, end)};renderCraftMatrix();`, scope);
assert.equal(reads, 1);
assert.equal(modelItems[0].count, 0); assert.equal(modelItems[0].lit, true);
assert.equal(modelItems[1].count, 4); assert.equal(modelItems[1].lit, false);
assert.equal(modelItems[2].baseCount, 0);
console.log("PASS 630-cell deferred preparation, 12-card frame bound, zero rebuild/reorder on repeat opens, live counts/copies, vanish races, forge, close/resume and single inventory scan");

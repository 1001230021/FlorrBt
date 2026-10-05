// No browser/server: run the actual swap lifecycle with fake DOM + rAF.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createSlotSwapMotion } from "../web/src/slot_swap_motion.js";
import { getPetalLabelFontSize, getPetalLabelContainerSize, getCardFlightPose,
  petalCardReferenceSize } from "../web/src/petal_card_layout.js";

const near = (a, b) => assert.ok(Math.abs(a - b) < 1e-7, `${a} != ${b}`);
for (const measured of [80, 260, 500, 3000]) {
  const referenceFont = getPetalLabelFontSize(measured);
  assert.ok(referenceFont <= 14);
  for (const size of [26, 44, 55, 56, 64, 77, 123.2]) {
    const font = getPetalLabelContainerSize(referenceFont) * size / 100;
    assert.ok(measured * font / 100 <= 45 * size / petalCardReferenceSize + 1e-6);
    const from = { x: 20, y: 800, size }, to = { x: 200, y: 900, size: size * 55 / 77 };
    const landed = getCardFlightPose(from, to, 1);
    near(font * landed.size / size, getPetalLabelContainerSize(referenceFont) * to.size / 100);
  }
}
near(getPetalLabelFontSize(500), 9);
assert.ok(Number.isFinite(getPetalLabelFontSize(NaN)));

function node(className = "", rect = { left: 0, top: 0, width: 77, height: 77 }) {
  const properties = new Map(), attributes = new Map();
  const element = { className, children: [], parentNode: null, rect, isConnected: true, textContent: "", dataset: {}, rectReads: 0,
    style: { setProperty: (name, value) => properties.set(name, value), getPropertyValue: name => properties.get(name) || "" },
    classList: {
      contains: name => element.className.split(/\s+/).includes(name),
      add(name) { if (!this.contains(name)) element.className += ` ${name}`; },
      remove(name) { element.className = element.className.split(/\s+/).filter(value => value !== name).join(" "); },
    },
    setAttribute: (name, value) => attributes.set(name, value), getAttribute: name => attributes.get(name),
    appendChild(child) { element.children.push(child); child.parentNode = element; return child; },
    getBoundingClientRect: () => { element.rectReads++; return { ...element.rect }; },
    querySelector(selector) {
      const className = selector.replace(":scope > .", "");
      return element.children.find(child => child.classList.contains(className)) || null;
    },
    cloneNode(deep) {
      const copy = node(element.className, { ...element.rect });
      copy.textContent = element.textContent; copy.dataset = { ...element.dataset };
      for (const [name, value] of properties) copy.style.setProperty(name, value);
      if (deep) for (const child of element.children) copy.appendChild(child.cloneNode(true));
      return copy;
    },
    remove() {
      if (element.parentNode) element.parentNode.children = element.parentNode.children.filter(child => child !== element);
      element.parentNode = null; element.isConnected = false;
    },
  };
  return element;
}
function slot(kind, index, filled = true) {
  const size = kind === "primary" ? 77 : 55;
  const rect = { left: 100 + index * 80, top: kind === "primary" ? 800 : 885, width: size, height: size };
  const button = node("slot", rect);
  button.style.setProperty("--slot-rarity-color", "#1fdbde");
  if (filled) {
    const stack = node("petal-stack multi-copy", rect);
    for (let copy = 0; copy < 3; copy++) {
      const icon = node("petal-icon petal-icon-copy petal-live-visual");
      icon.dataset.source = `blob:prepared-${copy}`; icon.style.setProperty("--live-angle", "0.4rad");
      stack.appendChild(icon);
    }
    const name = node("petal-card-name");
    name.textContent = "Observed Mimic Name";
    name.style.setProperty("--petal-label-font-cqw", "18");
    stack.appendChild(name); button.appendChild(stack);
    const counter = node("slot-value"); counter.textContent = "7"; button.appendChild(counter);
    button.appendChild(node("slot-durability-overlays"));
  }
  return button;
}

for (const fps of [30, 60, 120]) {
  let now = 0, nextId = 0, settled = 0;
  const frames = new Map(), body = node("body");
  const controller = createSlotSwapMotion({ document: { body, createElement: () => node() },
    clock: () => now, schedule: fn => { frames.set(++nextId, fn); return nextId; }, cancel: id => frames.delete(id),
    onSettled: () => settled++ });
  const p = slot("primary", 0), s = slot("secondary", 0);
  assert.equal(controller.swap(0, p, s), true);
  assert.equal(body.children.length, 2);
  assert.ok(controller.has(0) && p.classList.contains("slot-swap-hidden") && s.classList.contains("slot-swap-hidden"));
  const ghost = body.children[0], stack = ghost.children[0];
  assert.notEqual(stack, p.children[0]);
  assert.equal(stack.children.filter(child => child.classList.contains("petal-icon-copy")).length, 3);
  assert.equal(stack.children.at(-1).textContent, "Observed Mimic Name");
  assert.equal(stack.children[0].style.getPropertyValue("--live-angle"), "0.4rad");
  assert.equal(ghost.querySelector(":scope > .slot-value").textContent, "7");
  assert.equal(ghost.style.getPropertyValue("--slot-rarity-color"), "#1fdbde");
  assert.equal(ghost.getAttribute("aria-hidden"), "true");
  const step = dt => { now += dt; const pending = [...frames.values()]; frames.clear(); for (const callback of pending) callback(now); };
  const reads = p.rectReads + s.rectReads;
  for (let i = 0; i < Math.floor(fps * 0.13); i++) step(1000 / fps);
  assert.equal(p.rectReads + s.rectReads, reads, "no per-frame layout reads in a flight");
  assert.equal(body.children.length, 2); assert.equal(settled, 0);
  assert.doesNotMatch(ghost.style.transform, /rotate/);
  const oldPose = getCardFlightPose({ x: 100, y: 800, size: 77 }, { x: 100, y: 885, size: 55 }, now / 260);
  assert.ok(controller.swap(0, p, s), "rapid repeat redirects the pair rather than stacking ghosts");
  assert.equal(body.children.length, 2);
  near(parseFloat(ghost.style.left), oldPose.x); near(parseFloat(ghost.style.top), oldPose.y);
  near(parseFloat(ghost.style.width), oldPose.size);
  assert.equal(ghost.style.transform, "none");
  for (let i = 0; i < fps; i++) step(1000 / fps);
  assert.equal(body.children.length, 0); assert.equal(controller.has(0), false);
  assert.equal(p.classList.contains("slot-swap-hidden"), false); assert.equal(settled, 1);
  assert.equal(frames.size, 0);

  // R swaps all pairs in parallel but settles/repaints only once per frame.
  const buttons = [0, 1, 2].map(index => [slot("primary", index, index !== 1), slot("secondary", index, index !== 2)]);
  buttons.forEach(([primary, secondary], index) => controller.swap(index, primary, secondary));
  assert.equal(body.children.length, 4); assert.equal(frames.size, 1);
  step(300); assert.equal(settled, 2); assert.equal(body.children.length, 0);

  controller.swap(0, p, s); step(50); controller.cancelAll({ notify: true });
  assert.equal(body.children.length, 0); assert.equal(frames.size, 0); assert.equal(settled, 3);
  controller.swap(0, p, s); p.isConnected = false; step(20);
  assert.equal(body.children.length, 0); assert.equal(settled, 4);
  assert.equal(controller.swap(4, null, null), false);
  assert.equal(controller.swap(5, slot("primary", 5, false), slot("secondary", 5, false)), false);
}

const petal = readFileSync(new URL("../web/src/petal_info.js", import.meta.url), "utf8");
const css = readFileSync(new URL("../web/styles.css", import.meta.url), "utf8");
const game = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
assert.match(petal, /stripped: true/);
assert.match(petal, /--copy-x", `\$\{part.x\}cqw`/);
assert.doesNotMatch(petal, /span\.style\.fontSize/);
assert.match(css, /font-size: calc\(var\(--petal-label-font-cqw, 25\) \* 1cqw\)/);
assert.match(css, /\.slot\.slot-swap-hidden \.slot-durability-overlays/);
const row = game.slice(game.indexOf("function renderSlotRow("), game.indexOf("function updateOwnerSlotRuntimeOverlays("));
assert.ok(row.indexOf("slotSwapMotion.has(index)") < row.indexOf("const key = slotButtonRenderKey"));
assert.match(game, /function resetNetworkScene[^]*?slotSwapMotion.cancelAll\(\)/);
assert.doesNotMatch(game, /function launchPetalFlight\(/);
console.log("PASS proportional card labels/copy offsets, no landing font jump, cloned live/Mimic visuals, batch swap, rapid reversal, cancellation and network-refresh isolation at 30/60/120fps");

// Offline: real touch controller + builtin commands, fake DOM/pointers/viewport.
// Does not launch a browser, log in or send anything to the running game.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { executeBinding, initBindingContext, releaseInputHolds, defaultBindings, findBuiltinCommand } from "../web/src/bindings.js";

let clock = 1000, nextTimer = 0;
const timers = new Map(), frames = new Map();
const zh = JSON.parse(readFileSync(new URL("../web/locales/zh-Hans.json", import.meta.url), "utf8"));
const translate = key => key.split(".").reduce((value, part) => value?.[part], zh) || key;
function node(className = "") {
  const listeners = new Map(), attributes = new Map(), styles = new Map(), captured = new Set();
  const item = { className, children: [], parentElement: null, dataset: {}, textContent: "", clientWidth: 0,
    style: { setProperty: (key, value) => styles.set(key, value), removeProperty: key => styles.delete(key), getPropertyValue: key => styles.get(key) || "" },
    classList: {
      contains: key => item.className.split(/\s+/).includes(key),
      toggle(key, force) { const set = new Set(item.className.split(/\s+/).filter(Boolean)); const next = force ?? !set.has(key); if (next) set.add(key); else set.delete(key); item.className = [...set].join(" "); return next; },
      add: key => item.classList.toggle(key, true), remove: key => item.classList.toggle(key, false),
    },
    setAttribute: (key, value) => attributes.set(key, String(value)), getAttribute: key => attributes.get(key),
    append(...nodes) { for (const child of nodes) { child.parentElement = item; item.children.push(child); } },
    appendChild(child) { item.append(child); return child; },
    replaceChildren(...nodes) { for (const child of item.children) child.parentElement = null; item.children = []; item.append(...nodes); },
    contains: other => item === other || item.children.some(child => child.contains(other)),
    get isConnected() { return item === document.body || !!item.parentElement?.isConnected; },
    querySelectorAll: selector => walk(item).filter(child => selector === "[data-i18n-label]" && child.dataset.i18nLabel),
    getBoundingClientRect: () => ({ top: 12, left: 12, right: window.innerWidth - 12, bottom: 60, width: 120, height: 48 }),
    addEventListener(type, handler, options = {}) { const list = listeners.get(type) || []; list.push({ handler, capture: options === true || !!options.capture }); listeners.set(type, list); },
    setPointerCapture: id => captured.add(id), hasPointerCapture: id => captured.has(id),
    releasePointerCapture(id) { captured.delete(id); emit(item, "lostpointercapture", { pointerId: id }); },
    listeners,
  };
  return item;
}
const walk = item => [item, ...item.children.flatMap(walk)];
const window = Object.assign(node(), { innerWidth: 390, innerHeight: 844, matchMedia: () => ({ matches: true, addEventListener() {} }) });
const document = Object.assign(node(), { body: node(), createElement: () => node(), hidden: false, activeElement: null });
function emit(target, type, props = {}) {
  const event = { target, type, pointerType: "touch", pointerId: 1, button: 0, clientX: 80, clientY: 450,
    preventDefault() { this.defaultPrevented = true; }, stopPropagation() { this.stopped = true; },
    stopImmediatePropagation() { this.stopped = true; this.immediate = true; }, ...props };
  const path = []; for (let current = target; current; current = current.parentElement) path.push(current);
  if (!path.includes(document) && target !== window) path.push(document);
  if (!path.includes(window)) path.push(window);
  for (const capture of [true, false]) {
    for (const item of capture ? [...path].reverse() : path) {
      for (const listener of item.listeners.get(type) || []) {
        if (listener.capture === capture) listener.handler(event);
        if (event.immediate) break;
      }
      if (event.stopped) break;
    }
    if (event.stopped) break;
  }
  return event;
}
const scope = {
  window, document, performance: { now: () => clock }, t: translate,
  setAnimatedVisibility: (el, show) => el.classList.toggle("hidden", !show),
  setTimeout: fn => { timers.set(++nextTimer, fn); return nextTimer; }, clearTimeout: id => timers.delete(id),
  requestAnimationFrame: fn => { frames.set(++nextTimer, fn); return nextTimer; },
};
function evaluate(file, exports) {
  const source = readFileSync(new URL(`../web/src/${file}`, import.meta.url), "utf8")
    .replace(/^import[\s\S]*?from "[^"]+";\r?\n/gm, "").replace(/^export /gm, "");
  return vm.runInNewContext(`${source}\n;({${exports}})`, scope);
}
const { createMobileControls, mobileActionBindings, getJoystickVector } = evaluate("mobile_controls.js", "createMobileControls,mobileActionBindings,getJoystickVector");
const operations = [], input = { attacking: false, defending: false,
  setMoveVector() {}, setAttacking(value) { this.attacking = value; }, setDefending(value) { this.defending = value; },
  quickSwapAllSlots: () => operations.push("swap"), toggleBackpack: () => operations.push("inventory"),
  toggleCraft: () => operations.push("craft"), toggleTalent: () => operations.push("talent"),
  toggleCodex: () => operations.push("codex"), toggleSettings: () => operations.push("settings"),
  toggleConsole: () => operations.push("console"), toggleFullscreen: () => operations.push("fullscreen"),
  toggleMinimap: () => operations.push("minimap"), openChat: () => operations.push("chat"),
  zoomIn: () => operations.push("zoom_in"), zoomOut: () => operations.push("zoom_out"),
};
initBindingContext(input);
const canvas = node(); document.body.append(canvas);
const ui = createMobileControls({ movementSurface: canvas,
  isUiTarget: target => { for (let current = target; current; current = current.parentElement) if (current.classList.contains("ui")) return true; return false; },
  onAction: (action, phase) => executeBinding(mobileActionBindings, "touch", action, phase, { now: clock }),
});
ui.setEnabled(true);
const find = name => walk(document.body).find(el => el.classList.contains(name));
const button = action => walk(document.body).find(el => el.dataset.mobileAction === action);
assert.equal(button("attack").textContent, "A");
assert.equal(button("defend").textContent, "B");
assert.equal(button("swap").textContent, "X");
assert.match(button("attack").getAttribute("aria-label"), /A.*攻击/);
assert.match(button("defend").getAttribute("aria-label"), /B.*防御/);
assert.match(button("swap").getAttribute("aria-label"), /X.*主槽.*副槽/);
assert.equal(find("mobile-nav"), undefined, "keep the original corner icons, do not replace them with a nav bar");
assert.equal(find("mobile-menu-toggle"), undefined);
assert.equal(find("mobile-move-hint"), undefined);
assert.equal(find("mobile-joystick").classList.contains("hidden"), true, "idle joystick must not cover the world");
assert.equal(Number(document.body.style.getPropertyValue("--mob-scale")), 390 / 678);
assert.deepEqual(Object.keys(mobileActionBindings.touch), ["attack", "defend", "swap"]);
for (const commands of Object.values(mobileActionBindings.touch)) for (const command of commands) assert.ok(findBuiltinCommand(command));
emit(button("swap"), "click"); assert.equal(operations.at(-1), "swap");

// Each gesture owns its pointer; releasing an unrelated finger is a no-op.
emit(canvas, "pointerdown", { pointerId: 1 });
assert.equal(find("mobile-joystick").classList.contains("hidden"), false);
emit(canvas, "pointermove", { pointerId: 1, clientX: 118 });
assert.equal(ui.getMove().x, 1);
emit(button("attack"), "pointerdown", { pointerId: 2, clientX: 340 });
emit(button("attack"), "pointerdown", { pointerId: 3, clientX: 340 });
assert.equal(input.attacking, true);
emit(button("defend"), "pointerup", { pointerId: 77 });
assert.equal(input.attacking, true);
emit(button("attack"), "pointerup", { pointerId: 2 });
assert.equal(input.attacking, true, "another finger still holds attack");
emit(canvas, "pointerup", { pointerId: 1 });
assert.equal(ui.hasMovement(), false);
assert.equal(find("mobile-joystick").classList.contains("hidden"), true);
assert.equal(input.attacking, true, "joystick release cannot release attack");
emit(button("attack"), "pointercancel", { pointerId: 3 });
assert.equal(input.attacking, false);

executeBinding(defaultBindings, "keyboard", "space", "down");
emit(button("attack"), "pointerdown", { pointerId: 4 });
emit(button("attack"), "pointerup", { pointerId: 4 });
assert.equal(input.attacking, true, "touch release must not release a keyboard hold");
releaseInputHolds();
emit(button("defend"), "pointerdown", { pointerId: 5 });
assert.equal(input.defending, true);
ui.setBlocked(true); assert.equal(input.defending, false);
emit(button("attack"), "pointerdown", { pointerId: 6 }); assert.equal(input.attacking, false);
ui.setBlocked(false);
emit(canvas, "pointerdown", { pointerId: 7 }); emit(canvas, "pointermove", { pointerId: 7, clientX: 100 });
emit(window, "resize"); assert.equal(ui.hasMovement(), false);
emit(canvas, "pointerdown", { pointerId: 8, clientX: 360 }); assert.equal(canvas.hasPointerCapture(8), false);
find("mobile-joystick").clientWidth = 104; find("mobile-joystick-knob").clientWidth = 44;
emit(canvas, "pointerdown", { pointerId: 9 }); emit(canvas, "pointermove", { pointerId: 9, clientX: 110 });
assert.equal(ui.getMove().x, 1, "landscape joystick reaches full speed at its actual visual travel");
emit(canvas, "lostpointercapture", { pointerId: 9 }); assert.equal(ui.hasMovement(), false);
assert.equal(getJoystickVector(5, 0, 38).x, 0);
assert.equal(getJoystickVector(22, 0, 38).x, 0.5);
const diagonal = getJoystickVector(100, 100, 38);
assert.ok(Math.abs(Math.hypot(diagonal.x, diagonal.y) - 1) < 1e-9);

ui.showSlotActions({ title: "Basic", onSwap: () => operations.push("slot-swap"), onRemove: () => operations.push("slot-remove") });
assert.ok(!find("mobile-slot-menu").classList.contains("hidden"));
emit(find("mobile-slot-menu").children[2], "click"); assert.equal(operations.at(-1), "slot-remove");

// Touch long press is deliberate; moving or pointer cancellation cancels it.
const { bindTouchLongPress } = evaluate("touch_gestures.js", "bindTouchLongPress");
const slot = node("ui"); document.body.append(slot); let holds = 0;
bindTouchLongPress(slot, () => holds++);
const runTimers = () => { for (const [id, fn] of [...timers]) { timers.delete(id); fn(); } };
emit(slot, "pointerdown", { pointerId: 11 }); emit(slot, "pointermove", { pointerId: 11, clientX: 95 }); runTimers(); assert.equal(holds, 0);
emit(slot, "pointerdown", { pointerId: 12 }); emit(slot, "pointercancel", { pointerId: 12 }); runTimers(); assert.equal(holds, 0);
emit(slot, "pointerdown", { pointerId: 13 }); runTimers(); assert.equal(holds, 1);
emit(slot, "pointerup", { pointerId: 13 }); assert.equal(emit(slot, "click").defaultPrevented, true);
clock += 1100; assert.equal(emit(slot, "click").defaultPrevented, undefined);

// Software keyboards resize chrome, never transform the canvas's ancestor.
window.visualViewport = Object.assign(node(), { height: 844, offsetTop: 0 });
const { bindMobileViewport } = evaluate("mobile_viewport.js", "bindMobileViewport");
let typingResets = 0, scrolled = 0;
const syncViewport = bindMobileViewport({ onTyping: () => typingResets++ });
document.activeElement = { matches: () => true, getBoundingClientRect: () => ({ top: 650, bottom: 690 }), scrollIntoView: () => scrolled++ };
window.visualViewport.height = 420; syncViewport();
assert.ok(document.body.classList.contains("mobile-keyboard-open")); assert.equal(typingResets, 1); assert.equal(scrolled, 1);
assert.equal(document.body.style.getPropertyValue("--mobile-vv-height"), "420px");
assert.equal(document.body.style.getPropertyValue("--input-shift"), "");
assert.equal(document.body.classList.contains("input-shift-active"), false);
document.activeElement = null; window.visualViewport.height = 844; syncViewport();
assert.equal(document.body.classList.contains("mobile-keyboard-open"), false);

const css = readFileSync(new URL("../web/mobile.css", import.meta.url), "utf8");
let cssDepth = 0;
for (const brace of css.replace(/\/\*[\s\S]*?\*\//g, "").match(/[{}]/g) || []) {
  cssDepth += brace === "{" ? 1 : -1; assert.ok(cssDepth >= 0);
}
assert.equal(cssDepth, 0, "mobile stylesheet blocks must be balanced");
assert.doesNotMatch(readFileSync(new URL("../web/styles.css", import.meta.url), "utf8"), /body\.mobile-ui\s+button\s*\{/,
  "legacy blanket button shrinking must not override the touch targets");
assert.match(css, /min-width: 44px; min-height: 44px/);
assert.match(css, /var\(--slot-primary-size\) \* var\(--mob-scale\)/);
assert.match(css, /var\(--slot-secondary-size\) \* var\(--mob-scale\)/);
assert.match(css, /grid-template-areas: "attack swap" "attack defend"/);
assert.match(css, /border: 0; border-radius: 50%; background: rgba\(0, 0, 0, \.32\)/);
for (const [, selector, body] of css.matchAll(/([^{}]+)\{([^{}]*)\}/g)) {
  if (/\.quick-actions\b|\.top-left-actions\b|\.chat-hint\b/.test(selector))
    assert.doesNotMatch(body, /display:\s*none/, `restored HUD hidden by ${selector.trim()}`);
}
const html = readFileSync(new URL("../web/index.html", import.meta.url), "utf8");
for (const id of ["quickBackpackBtn", "quickTalentBtn", "quickCraftBtn", "topSettingsBtn", "codexBtn", "chatHint"])
  assert.ok(html.includes(`id="${id}"`));
assert.match(html, /<span id="chatComposeChannel"/);
assert.doesNotMatch(html, /id="chatSendBtn"/);
assert.match(readFileSync(new URL("../web/src/app_context.js", import.meta.url), "utf8"), /slotPanelCollapsed: false/);
assert.match(css, /\.console-panel\.open\s*\{ top: var\(--mobile-vv-top/);
for (const locale of ["en", "zh-Hans", "zh-Hant"]) {
  const values = JSON.parse(readFileSync(new URL(`../web/locales/${locale}.json`, import.meta.url), "utf8")).ui.mobile;
  for (const key of Object.keys(zh.ui.mobile)) assert.ok(values[key], `${locale}: missing ui.mobile.${key}`);
}
ui.setEnabled(false); assert.equal(ui.isMobileMode(), false); assert.equal(input.attacking, false);
console.log("PASS restored corner/slot/chat HUD, simple A/B/X bindings, touch-only joystick, independent multi-touch, modal/cancel/resize release, long-press safety, keyboard viewport and translations");

// Retained cells and viewport-local card preparation. Inventory changes update
// counts, not hundreds of SVG/name nodes; offscreen cards wait until needed.
import { bindTouchLongPress } from "./touch_gestures.js";

export function createCraftMatrix({ root, createStack, visualKey, describe, formatCount,
  attachTooltip, clearTooltip, vanish, select, isOpen = () => true,
  onLongPress = item => select(item.petalType, item.rarity, { all: true }),
  document: doc = globalThis.document, Observer = globalThis.IntersectionObserver,
  schedule = fn => requestAnimationFrame(fn), clock = () => performance.now() }) {
  const matrix = doc.createElement("div");
  const records = new Map(), byElement = new WeakMap(), pending = new Set();
  let scheduled = false, mode = "";
  const observer = typeof Observer === "function" ? new Observer(entries => {
    for (const entry of entries) {
      const record = byElement.get(entry.target);
      if (!record || records.get(record.item.key) !== record) continue;
      record.visible = entry.isIntersecting;
      if (record.visible) queue(record); else pending.delete(record);
    }
  }, { root, rootMargin: "160px 0px" }) : null;

  function queue(record) {
    if (!record.visible || !record.item.count) return;
    pending.add(record);
    if (scheduled || !isOpen()) return;
    scheduled = true;
    schedule(() => {
      scheduled = false;
      if (!isOpen()) return;
      const start = clock();
      let count = 0;
      for (const record of pending) {
        pending.delete(record);
        if (records.get(record.item.key) !== record || !record.visible) continue;
        renderCard(record);
        if (++count >= 12 || clock() - start >= 2) break;
      }
      if (pending.size) queue(pending.values().next().value);
    });
  }

  function renderCard(record) {
    const { item, cell } = record;
    if (item.count <= 0) return;
    const key = visualKey(item);
    if (!record.stack || record.visualKey !== key || record.vanishing) {
      record.vanishing = false;
      record.stack = createStack(item);
      record.visualKey = key;
      cell.replaceChildren(record.stack);
      attachTooltip(cell, item.petalType, item.rarity);
      record.badge = record.stack.querySelector(".petal-count");
    }
    const text = formatCount(item);
    if (item.count > 1 && !record.badge) {
      record.badge = doc.createElement("span");
      record.badge.className = "petal-count";
      record.stack.appendChild(record.badge);
    }
    if (record.badge) record.badge.hidden = item.count <= 1;
    if (record.badge && record.badge.textContent !== text) record.badge.textContent = text;
    if (cell.title !== record.title) cell.title = record.title;
  }

  function update(record, item) {
    const previous = record.item;
    record.item = item;
    const className = `craft-matrix-cell${item.count <= 0 ? " empty" : ""}${item.lit ? " lit" : " locked"}`;
    if (record.cell.className !== className) record.cell.className = className;
    const tabIndex = item.count > 0 ? 0 : -1;
    if (record.cell.tabIndex !== tabIndex) record.cell.tabIndex = tabIndex;
    if (!previous || previous.count !== item.count || previous.name !== item.name || previous.forge !== item.forge)
      record.title = item.baseCount > 0 ? describe(item) : "";
    if (item.count > 0) {
      // Existing visible cards patch synchronously, so selection counts and
      // incoming authoritative inventory updates never lag behind input.
      if (record.stack && record.visible && isOpen()) renderCard(record);
      else queue(record);
    } else {
      pending.delete(record);
      clearTooltip(record.cell);
      record.cell.title = record.title;
      if (record.stack && !record.vanishing) {
        const stack = record.stack;
        if (record.visible && isOpen()) {
          record.vanishing = true;
          vanish(stack, { durationMs: 280, onFinish: () => {
            stack.remove();
            // A late fade completion must not erase a replenished/new card.
            if (record.stack === stack) {
              record.stack = record.badge = null;
              record.vanishing = false;
            }
          } });
        } else {
          stack.remove(); record.stack = record.badge = null;
        }
      }
    }
  }

  function render(items, { forge = false, columns = 9 } = {}) {
    const nextMode = `${forge}:${columns}`;
    if (mode !== nextMode) {
      mode = nextMode;
      matrix.className = `craft-matrix${forge ? " forge-matrix" : ""}`;
      matrix.style.setProperty("--craft-rarity-count", String(columns));
    }
    root.classList.toggle("hidden", items.length === 0);
    const live = new Set(), ordered = [];
    for (const item of items) {
      live.add(item.key);
      let record = records.get(item.key);
      if (!record) {
        const cell = doc.createElement("button");
        cell.type = "button"; cell.dataset.craftKey = item.key;
        record = { cell, stack: null, item: null, visible: !observer, visualKey: "", vanishing: false };
        records.set(item.key, record); byElement.set(cell, record);
        cell.onpointerdown = event => {
          if (event.button !== undefined && event.button !== 0) return;
          if (event.pointerType === "touch") {
            // Keep the list's native vertical scrolling available. The
            // long-press recognizer below will still consume a stationary
            // touch and add the whole available stack.
            event.stopPropagation();
            return;
          }
          event.preventDefault(); event.stopPropagation();
          if (record.item.lit) select(record.item.petalType, record.item.rarity, { all: event.shiftKey });
        };
        cell.addEventListener("click", event => {
          if (event.pointerType !== "touch") return;
          const item = record.item;
          if (item?.lit && item.count > 0) select(item.petalType, item.rarity);
        });
        bindTouchLongPress(cell, () => {
          const item = record.item;
          if (item?.lit && item.count > 0) onLongPress(item);
        });
        // Focusing an offscreen cell via keyboard must prepare it as well.
        cell.addEventListener("focus", () => { record.visible = true; renderCard(record); });
        observer?.observe(cell);
      }
      update(record, item); ordered.push(record.cell);
    }
    for (const [key, record] of records) if (!live.has(key)) {
      observer?.unobserve(record.cell); pending.delete(record); record.cell.remove(); records.delete(key);
    }
    // Do not append every existing child: even an unchanged append invalidates
    // grid layout and forces repaint of the entire scrolling matrix.
    let cursor = matrix.firstElementChild;
    for (const cell of ordered) {
      if (cell === cursor) cursor = cursor.nextElementSibling;
      else matrix.insertBefore(cell, cursor);
    }
    if (matrix.parentElement !== root) root.replaceChildren(matrix);
  }

  return { render, dispose() {
    observer?.disconnect(); pending.clear(); records.clear(); matrix.remove();
  } };
}

import { rarityColor, rarityName } from "./protocol.js";

// 分组和物品按身份复用；数量变化只更新角标，保留焦点、拖动捕获和现有图片。
export function createInventoryView({ groupInventoryItems, makePetalStack, effectivePetalCardCopy,
  getPetalDisplayName, formatPetalItem, formatCount = String, attachPetalInfoTooltip,
  beginDrag, shouldSuppressClick, equipFromInventory }) {
  const containers = new WeakMap();
  const variantsOf = item => Array.isArray(item?.typeStack) && item.typeStack.length ? item.typeStack : [item];

  function makeStack(variants) {
    if (variants.length === 1) {
      const item = variants[0];
      return makePetalStack(item.petalType, item.rarity, item.count);
    }
    const stack = document.createElement("span");
    stack.className = "inventory-type-stack";
    for (let depth = Math.min(variants.length, 3) - 1; depth >= 0; depth--) {
      const item = variants[depth], layer = makePetalStack(item.petalType, item.rarity, depth ? 0 : item.count);
      layer.classList.add("inventory-type-stack-layer");
      layer.style.setProperty("--type-stack-depth", String(depth));
      stack.appendChild(layer);
    }
    return stack;
  }

  function updateCard(record, item) {
    record.item = item;
    const variants = variantsOf(item), visible = variants.slice(0, 3);
    const key = visible.map(v => [v.petalType, v.rarity, effectivePetalCardCopy(v.petalType, v.rarity), getPetalDisplayName(v.petalType)].join(":")).join("|");
    const className = "inventory-item" + (variants.length > 1 ? " type-stacked" : "");
    if (record.button.className !== className) record.button.className = className;
    if (record.key !== key) {
      record.key = key;
      const stack = makeStack(variants);
      record.front = variants.length > 1 ? stack.lastElementChild : stack;
      record.button.replaceChildren(stack);
      attachPetalInfoTooltip(record.button, item.petalType, item.rarity);
      record.badge = record.front.querySelector(".petal-count");
    }
    const count = variants[0].count || 0;
    if (count > 1 && !record.badge) {
      record.badge = document.createElement("span");
      record.badge.className = "petal-count";
      record.front.appendChild(record.badge);
    }
    if (record.badge) {
      record.badge.hidden = count <= 1;
      const text = "x" + formatCount(count);
      if (record.badge.textContent !== text) record.badge.textContent = text;
    }
    const title = variants.map(v => formatPetalItem(v.petalType, v.rarity, v.count)).join("\n");
    if (record.button.title !== title) record.button.title = title;
  }

  function place(parent, children, cursor = parent.firstElementChild) {
    for (const child of children) {
      if (child === cursor) cursor = cursor.nextElementSibling;
      else parent.insertBefore(child, cursor);
    }
  }

  function render(container, items) {
    let cache = containers.get(container);
    if (!cache) { cache = new Map(); containers.set(container, cache); }
    const groups = groupInventoryItems(items), liveGroups = new Set(), sections = [];
    container.classList.toggle("hidden", groups.length === 0);
    for (const group of groups) {
      const key = group.showHeader ? "rarity:" + group.rarity : "stacked";
      liveGroups.add(key);
      let record = cache.get(key);
      if (!record) {
        const section = document.createElement("section"), grid = document.createElement("div");
        section.className = "inventory-group"; section.dataset.groupKey = key; grid.className = "inventory-grid";
        let header = null;
        if (group.showHeader) { header = document.createElement("div"); header.className = "inventory-group-title"; section.appendChild(header); }
        section.appendChild(grid);
        record = { section, grid, header, cards: new Map() }; cache.set(key, record);
      }
      if (record.header) {
        const name = rarityName(group.rarity);
        if (record.header.textContent !== name) record.header.textContent = name;
        record.header.style.color = rarityColor(group.rarity, 1);
      }
      const live = new Set(), buttons = [];
      for (const item of group.items) {
        const id = group.showHeader ? item.petalType + ":" + item.rarity : String(item.petalType);
        live.add(id);
        let card = record.cards.get(id);
        if (!card) {
          const button = document.createElement("button"); button.type = "button";
          card = { button, item, key: null, front: null, badge: null };
          button.addEventListener("pointerdown", event => beginDrag(event, button, "inventory", -1, card.item));
          button.addEventListener("click", () => { if (!shouldSuppressClick()) equipFromInventory(card.item); });
          record.cards.set(id, card);
        }
        updateCard(card, item); buttons.push(card.button);
      }
      for (const [id, card] of record.cards) if (!live.has(id)) { card.button.remove(); record.cards.delete(id); }
      place(record.grid, buttons); sections.push(record.section);
    }
    for (const [id, group] of cache) if (!liveGroups.has(id)) { group.section.remove(); cache.delete(id); }
    // 自定义滚动条不属于物品列表，不能随库存刷新删除。
    place(container, sections, [...container.children].find(child => child.classList.contains("inventory-group")) || null);
  }

  return { render };
}

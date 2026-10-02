// Reusable rectangular mob/petal type picker. Rarity deliberately belongs
// to the calling form and is never mixed into the type catalogue.
export function createDebugTypePicker({ host, getIcon, getName = entry => entry.name }) {
  let pending = null;
  const overlay = document.createElement("div"); overlay.className = "dev-picker-shade"; overlay.hidden = true;
  const panel = document.createElement("section"); panel.className = "dev-picker";
  panel.setAttribute("role", "dialog"); panel.setAttribute("aria-label", "选择生物或花瓣类型");
  const heading = document.createElement("h3");
  const search = document.createElement("input"); search.type = "search"; search.placeholder = "搜索名称 / type ID";
  const grid = document.createElement("div"); grid.className = "dev-type-grid";
  const close = document.createElement("button"); close.type = "button"; close.textContent = "取消";
  panel.append(heading, search, grid, close); overlay.append(panel); host.append(overlay);
  let entries = [];
  function finish(value) {
    const resolve = pending; pending = null; overlay.hidden = true; grid.replaceChildren();
    resolve?.(value);
  }
  function render() {
    const query = search.value.trim().toLocaleLowerCase();
    grid.replaceChildren();
    for (const entry of entries) {
      const label = getName(entry) || entry.name;
      if (query && !`${entry.id} ${entry.name} ${label}`.toLocaleLowerCase().includes(query)) continue;
      const cell = document.createElement("button"); cell.type = "button"; cell.className = "dev-type-cell";
      const src = getIcon?.(entry);
      if (src) {
        const image = document.createElement("img"); image.src = src; image.alt = ""; image.draggable = false;
        image.addEventListener("error", () => image.remove(), { once: true }); cell.append(image);
      }
      const title = document.createElement("span"); title.textContent = label;
      const id = document.createElement("small"); id.textContent = `#${entry.id}`;
      cell.title = `${entry.name} · type ${entry.id}`; cell.append(title, id);
      cell.addEventListener("click", () => finish(entry)); grid.append(cell);
    }
  }
  search.addEventListener("input", render);
  close.addEventListener("click", () => finish(null));
  overlay.addEventListener("pointerdown", event => { if (event.target === overlay) finish(null); });
  overlay.addEventListener("keydown", event => {
    if (event.key === "Escape") { event.preventDefault(); event.stopPropagation(); finish(null); }
  });
  return {
    isOpen: () => !overlay.hidden,
    close: () => finish(null),
    open(kind, catalogue) {
      finish(null); entries = catalogue.filter(entry => entry.kind === kind)
        .sort((a, b) => String(getName(a)).localeCompare(String(getName(b))) || a.id - b.id);
      heading.textContent = kind === "mob" ? "选择生物 type" : "选择花瓣 type";
      search.value = ""; overlay.hidden = false; render(); search.focus();
      return new Promise(resolve => { pending = resolve; });
    },
  };
}

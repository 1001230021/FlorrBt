// Exercise the actual codex view with fake DOM/dependencies; no browser or
// app bootstrap. The existing UI module still belongs to a legacy cycle.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import * as ids from "../web/src/game_ids.js";

function element(tag = "div") {
  const attrs = new Map();
  const style = { setProperty(key, value) { this[key] = String(value); } };
  const node = {
    tagName: tag.toUpperCase(), children: [], parentElement: null, className: "", dataset: {}, style, textContent: "",
    offsetWidth: 260, offsetHeight: 150, scrollTop: 0, replacements: 0,
    setAttribute: (key, value) => attrs.set(key, value),
    getAttribute: key => attrs.get(key),
    appendChild(child) { child.remove(); this.children.push(child); child.parentElement = this; return child; },
    insertBefore(child, cursor) {
      child.remove(); const index = cursor ? this.children.indexOf(cursor) : this.children.length;
      assert.ok(index >= 0); this.children.splice(index, 0, child); child.parentElement = this;
    },
    replaceChildren(...children) { this.replacements++; for (const child of [...this.children]) child.remove(); for (const child of children) this.appendChild(child); },
    remove() { if (!this.parentElement) return; const list = this.parentElement.children; list.splice(list.indexOf(this), 1); this.parentElement = null; },
    querySelector(selector) { return this.children.find(child => child.classList.contains(selector.slice(1))) || null; },
    closest(selector) { return this.classList.contains(selector.slice(1)) ? this : this.parentElement?.closest(selector); },
    getBoundingClientRect: () => ({ left: 50, right: 106, top: 100, bottom: 156 }),
    get firstElementChild() { return this.children[0] || null; },
    get nextElementSibling() { const list = this.parentElement?.children || []; return list[list.indexOf(this) + 1] || null; },
  };
  node.classList = {
    contains: value => node.className.split(/\s+/).includes(value),
    toggle(value, enabled) {
      const set = new Set(node.className.split(/\s+/).filter(Boolean));
      if (enabled ?? !set.has(value)) set.add(value); else set.delete(value);
      node.className = [...set].join(" ");
    },
  };
  return node;
}
const dom = Object.fromEntries(["codexBtn", "codexDetail", "codexGrid", "codexPanel"].map(key => [key, element()]));
const scrollbar = element();
scrollbar.className = "custom-scrollbar";
dom.codexGrid.appendChild(scrollbar);
const state = { authenticated: true, codexOpen: true, codex: new Map(), codexLoot: new Map() };
let language = "en";
const scope = {
  ...ids, dom, state, document: { createElement: element }, window: { innerWidth: 1920, innerHeight: 1080 },
  MobNames: {}, mobTypeName: type => `${language} Mob ${type}`, rarityName: rarity => `R${rarity}`,
  rarityColor: () => "#ffff00", translationSlug: name => name.toLowerCase(), t: key => key,
  assetImage: src => ({ src, complete: false }), petalBaseImage: rarity => ({ src: `rarity_${rarity}.svg` }),
  makePetalStack: () => element(), formatShortCount: String,
  toggleBackpack() {}, toggleCraft() {}, toggleSettings() {}, toggleTalent() {}, updateQuickActionButtons() {},
  runMotionPhase() {}, setAnimatedVisibility: (el, visible) => el.classList.toggle("hidden", !visible),
};
const source = readFileSync(new URL("../web/src/ui_codex.js", import.meta.url), "utf8")
  .replace(/^import[\s\S]*?from "[^"]+";\r?\n/gm, "").replace(/^export /gm, "");
const codex = vm.runInNewContext(`${source}\n;({renderCodexPanel, codexKillCount, codexKey, moveCodexDetail, toggleCodex, codexRarityOrder})`, scope);
const common = ids.rarityDisplayOrder[0], mob = ids.beetleType;
const key = codex.codexKey(mob, common);
state.codex.set(key, 1);
codex.renderCodexPanel();
assert.equal(dom.codexGrid.children.length, codex.codexRarityOrder.length + 1);
assert.equal(scrollbar.parentElement, dom.codexGrid, "rendering must not delete the managed scrollbar");
assert.equal(dom.codexGrid.style["--codex-columns"], String(codex.codexRarityOrder.length));
const cell = dom.codexGrid.children.find(cell => cell.dataset.rarity === String(common));
assert.ok(cell.querySelector(".card-icon-layer"), "first-load SVG must mount even before decoding");
const image = cell.querySelector(".card-bg-layer");
dom.codexGrid.scrollTop = 90;
state.codex.set(key, 4); codex.renderCodexPanel();
assert.equal(dom.codexGrid.children.find(el => el === cell), cell);
assert.equal(cell.querySelector(".card-bg-layer"), image);
assert.equal(dom.codexGrid.scrollTop, 90);
assert.equal(cell.querySelector(".petal-count").textContent, "x4");
assert.equal(scrollbar.parentElement, dom.codexGrid);
state.codex.set(codex.codexKey(mob, ids.rarityExotic), 3);
assert.equal(codex.codexKillCount(mob, common), 7);
assert.equal(codex.codexKillCount(mob, ids.rarityExotic), 3, "exotic queries must not double themselves");
// Add and remove a whole row, not only a count: reordering must preserve
// both existing cards and the overlay scrollbar alongside the grid cells.
const otherKey = codex.codexKey(ids.beeType, common);
state.codex.set(otherKey, 3); codex.renderCodexPanel();
assert.equal(dom.codexGrid.children.length, codex.codexRarityOrder.length * 2 + 1);
assert.equal(cell.querySelector(".card-bg-layer"), image);
assert.equal(scrollbar.parentElement, dom.codexGrid);
state.codex.delete(otherKey); codex.renderCodexPanel();
assert.equal(dom.codexGrid.children.length, codex.codexRarityOrder.length + 1);
assert.equal(scrollbar.parentElement, dom.codexGrid);
codex.moveCodexDetail({ target: cell });
const rebuilds = dom.codexDetail.replacements;
for (let i = 0; i < 30; i++) codex.moveCodexDetail({ target: cell });
assert.equal(dom.codexDetail.replacements, rebuilds, "pointer movement must not rebuild loot cards");
language = "zh"; codex.moveCodexDetail({ target: cell });
assert.equal(dom.codexDetail.replacements, rebuilds + 1);
codex.toggleCodex(false);
assert.ok(dom.codexDetail.classList.contains("hidden"));
state.codex.clear(); state.codexOpen = true; codex.renderCodexPanel();
assert.equal(dom.codexGrid.children.length, 1);
assert.equal(dom.codexGrid.children[0], scrollbar, "clearing kills must remove only cards");
state.codex.set(key, 8); codex.renderCodexPanel();
assert.equal(dom.codexGrid.children.length, codex.codexRarityOrder.length + 1);
assert.equal(scrollbar.parentElement, dom.codexGrid, "gallery must keep scrolling after becoming empty and refilling");

const css = readFileSync(new URL("../web/styles.css", import.meta.url), "utf8");
const rule = selector => css.slice(css.indexOf(`${selector} {`)).split("}", 1)[0];
assert.match(rule(".codex-cell"), /overflow:\s*visible\s*;/, "rotated kill counts must not be clipped by their card");
assert.match(rule(".codex-grid"), /padding:\s*10px 16px 6px 4px\s*;/, "first-row/last-column counts need scroll-edge clearance");
assert.match(rule(".codex-grid"), /grid-template-columns:\s*repeat\(var\(--codex-columns/);
assert.match(rule(".codex-cell > .petal-count"), /top:\s*2px\s*;/);
assert.match(rule(".codex-cell > .card-bg-layer"), /width:\s*110%\s*;/, "card SVG transparent margins must not shrink occupied cells");
assert.match(rule(".codex-cell > .card-bg-layer"), /clip-path:\s*inset\(/, "round the artwork, not the count container");
const panel = rule(".game-panel.codex-panel");
assert.match(panel, /--game-panel-background:\s*var\(--florr-yellow-surface\)\s*;/);
assert.match(css, /--florr-yellow-surface:\s*#[0-9a-f]{6}\s*;/i);
assert.match(panel, /--game-panel-well:\s*var\(--game-panel-background\)\s*;/, "no second gold/brown panel behind the grid");
assert.match(rule(".game-panel.codex-panel .panel-title > span"), /-webkit-text-stroke:\s*2px #202020/);
assert.match(rule(".codex-grid .custom-scrollbar-thumb"), /box-shadow:\s*none/);
console.log("PASS codex count clearance, reference palette/card geometry, persistent scrollbar, node reuse and tooltip caching");

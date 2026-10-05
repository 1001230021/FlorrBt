// Reference geometry and responsive contracts. No browser/game/account used.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { calculateUiLayout, getHudPortraitLayout, writeUiLayoutVariables } from "../web/src/ui_layout.js";

const near = (a, b, epsilon = 1e-6) => assert.ok(Math.abs(a - b) < epsilon, `${a} != ${b}`);
const reference = calculateUiLayout();
near(reference.scale, 1);
near(reference.loadout.primary * 100 / 110, 70);
near(reference.loadout.secondary * 100 / 110, 50);
near(reference.loadout.x + reference.loadout.width / 2, 955);
near(884 - reference.loadout.bottom - reference.loadout.height, 725);
near(884 - reference.quick.bottom - 3 * reference.quick.size - 2 * reference.quick.gap, 662);
near(reference.panel.left, 92);
near(884 - reference.panel.bottom - reference.panel.craftHeight, 168);
near(884 - reference.panel.bottom - reference.panel.talentHeight, 212);
assert.deepEqual(reference.minimap, { x: 1790, y: 18, size: 100 });
assert.deepEqual(reference.chat, { x: 100, width: 420, bottom: 20 });
const portrait = getHudPortraitLayout(reference);
assert.deepEqual(portrait.flower, { x: 60, y: 108 });
assert.deepEqual(portrait.health, { x: 100, y: 90, width: 195, height: 36 });
near(portrait.radius, 32);
near(getHudPortraitLayout(reference, 0).flower.y, 208);
near(getHudPortraitLayout(reference, 1).flower.y, 270);

for (const [width, height] of [[1910, 884], [1366, 768], [1280, 720], [960, 540], [2560, 1440], [3840, 2160], [390, 844], [844, 390], [320, 568]]) {
  for (const mobile of [false, true]) for (const primaryCount of [5, 10, 16, 24]) for (const collapsed of [false, true]) {
    const layout = calculateUiLayout({ width, height, mobile, primaryCount, secondaryCount: primaryCount, collapsed,
      insets: mobile ? { top: 24, bottom: 16, left: 6, right: 6 } : {} });
    const slots = layout.loadout;
    assert.ok(slots.x >= 0 && slots.x + slots.width <= width, "no slots outside the screen");
    assert.ok(slots.bottom >= layout.safe.bottom);
    assert.ok(slots.height + slots.bottom < height);
    assert.ok(layout.chat.x >= 0 && layout.chat.x + layout.chat.width <= width);
    assert.ok(layout.chat.bottom + 32 * layout.scale < height);
    for (const rect of [layout.minimap, layout.fullMinimap]) {
      assert.ok(rect.x >= 0 && rect.x + rect.size <= width - layout.safe.right);
      assert.ok(rect.y >= layout.safe.top && rect.y + rect.size < height - layout.safe.bottom);
    }
    near(slots.primary / slots.secondary, 77 / 55);
    if (mobile) {
      assert.ok(layout.top.size >= 40 && layout.quick.size >= 44);
      assert.ok(layout.chat.bottom >= slots.bottom + slots.height);
      assert.ok(slots.primary >= 26);
      if (width < height) assert.ok(layout.actionBottom > slots.bottom + slots.height);
    }
    const owner = getHudPortraitLayout(layout);
    assert.ok(owner.flower.y - owner.radius >= layout.top.y + layout.top.size);
    assert.ok(owner.health.x + owner.health.width < width);
    assert.ok(owner.experience.y > owner.health.y + owner.health.height);
  }
}

const properties = new Map(); let writes = 0;
const root = { style: { getPropertyValue: name => properties.get(name),
  setProperty(name, value) { writes++; properties.set(name, value); } } };
writeUiLayoutVariables(root, reference);
const firstWrites = writes;
writeUiLayoutVariables(root, reference);
assert.equal(writes, firstWrites, "no repeat style writes for identical state");
assert.equal(properties.get("--slot-primary-size"), "77px");
assert.equal(properties.get("--craft-height"), "698px");
const portraitMobile = calculateUiLayout({ mobile: true, width: 390, height: 844 });
const collapsedMobile = calculateUiLayout({ mobile: true, width: 390, height: 844, collapsed: true });
assert.ok(collapsedMobile.loadout.height < portraitMobile.loadout.height);
assert.ok(collapsedMobile.chat.bottom < portraitMobile.chat.bottom);

const html = readFileSync(new URL("../web/index.html", import.meta.url), "utf8");
const css = readFileSync(new URL("../web/layout.css", import.meta.url), "utf8");
const game = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
const settings = readFileSync(new URL("../web/src/settings_ui.js", import.meta.url), "utf8");
assert.ok(html.indexOf('href="./layout.css"') > html.indexOf('href="./mobile.css"'));
assert.ok(html.indexOf('href="./layout.css"') < html.indexOf('href="./dev_debug.css"'));
assert.match(game, /getHudPortraitLayout\(getGameUiLayout\(\)\)/);
assert.match(game, /mode === "full" \? layout.fullMinimap : layout.minimap/);
assert.match(game, /onLayoutChange:[^\n]*syncUiLayout/);
assert.match(settings, /row.dataset.settingType = definition.type/);
assert.match(css, /toggle"\] \.settings-square-toggle \{ grid-column: 1; grid-row: 1/);
assert.match(css, /\.craft-stage\.success \.craft-stage-slot::before \{ background: transparent/);
assert.match(css, /inset: 4\.54545%/);
assert.doesNotMatch(css, /\.craft-matrix-cell\.selected\s*\{/);
assert.doesNotMatch(css, /body\s*\{[^}]*transform:/);
assert.doesNotMatch(css, /task-card|quest-card/);
let braces = 0;
for (const char of css.replace(/\/\*[\s\S]*?\*\//g, "")) {
  if (char === "{") braces++;
  if (char === "}") braces--;
  assert.ok(braces >= 0);
}
assert.equal(braces, 0);
console.log("PASS reference HUD/panel anchors, 70/50 card silhouettes, desktop/mobile safe areas, slot counts, collapse, checkbox order and shared CSS/Canvas geometry");

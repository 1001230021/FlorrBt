import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
const read = name => readFileSync(new URL(`../${name}`, import.meta.url), "utf8");
const game = read("web/src/game.js");
const scene = game.slice(game.indexOf("export function drawScene("), game.indexOf("function makeBench", game.indexOf("export function drawScene(")));
const layer = scene.indexOf("ui.mapRenderer?.drawShortcutSurface(now)");
assert(layer > 0);
for (const pass of ["ground", "underlay", "world", "petals", "overlays"])
  assert(scene.indexOf(`drawEntityPass(passes.${pass}`) >= 0 && scene.indexOf(`drawEntityPass(passes.${pass}`) < layer);
assert(layer < scene.indexOf("drawEntityWithHallucinationGhosts(passes.owner"));
assert(scene.indexOf("drawEntityWithHallucinationGhosts(passes.owner") < scene.indexOf("drawShortcutCover(now)"));
const map = read("web/src/map_renderer.js");
assert(!map.slice(map.indexOf("function drawMapLayer("), map.indexOf("function drawShortcutPass(")).includes("drawShortcutPass("),
  "terrain pass must not draw the shortcut a second time below entities");
const debug = read("web/src/dev_debug_ui.js");
assert(debug.indexOf("renderEntities?.(ctx") < debug.indexOf("mapRenderer.drawShortcutSurface?.(now)"));
assert(debug.indexOf("mapRenderer.drawShortcutSurface?.(now)") < debug.indexOf("mapRenderer.drawShortcutCover?.(now)"));
for (const path of ["web/ui_theme.css", "web/styles.css", "web/dev_debug.css"]) {
  const css = read(path).replace(/\/\*[\s\S]*?\*\//g, "");
  for (const block of css.matchAll(/([^{}]+)\{([^{}]*)\}/g)) {
    if (!block[1].includes(":focus")) continue;
    const outline = /outline\s*:\s*([^;]+)/.exec(block[2])?.[1].trim();
    const shadow = /box-shadow\s*:\s*([^;]+)/.exec(block[2])?.[1].trim();
    assert(!outline || /^(none|0(?:px)?)(\s|$)/.test(outline), `${path}: focus outline must be removed`);
    assert(!shadow || shadow === "none", `${path}: focus halo must be removed`);
  }
}
console.log("PASS entity/shortcut/owner/cover ordering, debug parity, no duplicate shortcut pass and removed focus rings");
if (process.argv.includes("--browser")) {
  const { runSceneLayersBrowserCheck } = await import("./scene_layers_browser.mjs");
  await runSceneLayersBrowserCheck();
}

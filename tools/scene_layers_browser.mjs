import assert from "node:assert/strict";
import { readFile, mkdir, writeFile } from "node:fs/promises";
import { resolve, extname, sep } from "node:path";
import { fileURLToPath } from "node:url";

export async function runSceneLayersBrowserCheck() {
  const { chromium } = await import("../build/render-qa/node_modules/playwright-core/index.mjs");
  const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
  const tileset = JSON.parse(await readFile(resolve(root, "data/tiles/tileset.tsj"), "utf8"));
  const map = { width: 1, height: 1, tilewidth: 512, tileheight: 512,
    tilesets: [{ firstgid: 1, source: "../tiles/tileset.tsj" }],
    layers: ["grass", "shortcut", "shortcut_cover"].map((name, index) => ({ type: "tilelayer", name, width: 1, height: 1,
      visible: true, data: [[1], [127], [25]][index] })) };
  const browser = await chromium.launch({ headless: true,
    executablePath: process.env.FLORRBT_BROWSER_EXECUTABLE || "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" });
  try {
    const page = await browser.newPage({ viewport: { width: 1000, height: 700 } });
    const errors = [];
    page.on("pageerror", error => errors.push(String(error)));
    await page.route("**/*", async route => {
      const url = new URL(route.request().url());
      if (url.hostname !== "scene-tests.local") return route.abort();
      if (url.pathname === "/") return route.fulfill({ contentType: "text/html", body: `<!doctype html><html><head>
        <link rel="stylesheet" href="/styles.css"><link rel="stylesheet" href="/mobile.css"><link rel="stylesheet" href="/layout.css">
        <link rel="stylesheet" href="/dev_debug.css"><link rel="stylesheet" href="/ui_theme.css"></head><body class="florr-ui">
        <canvas id="game" width="256" height="256"></canvas><div style="position:relative;z-index:200">
        <button id="backpack" class="quick-action quick-action-backpack">Bag</button>
        <button id="talents" class="quick-action quick-action-talent">Talents</button>
        <button id="craft" class="quick-action quick-action-craft">Craft</button>
        <button id="settings" class="top-action top-action-settings">Settings</button>
        <button id="gallery" class="top-action top-action-codex">Gallery</button>
        <button id="node" class="talent-node" style="position:static;translate:none">Node</button>
        <input id="search" class="ui-text-input inventory-search"><a id="link" href="#">Link</a><select id="select"><option>A</option></select>
        <div class="chat-composer"><input id="chat" class="ui-text-input ui-text-input--embedded"></div>
        <div class="dev-debug" style="position:static;display:block"><button id="debug">Debug</button><input id="debugInput"></div>
        </div></body></html>` });
      if (url.pathname === "/data/maps/layers.tmj") return route.fulfill({ json: map });
      if (url.pathname === "/data/tiles/tileset.tsj") return route.fulfill({ json: { ...tileset, tiles: tileset.tiles.filter(t => [0, 126, 24].includes(t.id)) } });
      const base = url.pathname.startsWith("/data/") ? root : resolve(root, "web");
      const path = resolve(base, "." + decodeURIComponent(url.pathname));
      if (!path.startsWith(base + sep)) return route.abort();
      try { return route.fulfill({ body: await readFile(path), contentType: ({ ".js":"application/javascript", ".css":"text/css", ".svg":"image/svg+xml", ".json":"application/json", ".ttf":"font/ttf" })[extname(path)] }); }
      catch { return route.fulfill({ status:404, body:"Missing resource" }); }
    });
    await page.goto("http://scene-tests.local/");
    await page.keyboard.press("Tab");
    const focus = await page.evaluate(() => [...document.querySelectorAll("button,input,a,select")].map(element => {
      element.focus(); const style = getComputedStyle(element);
      return { id:element.id, visible:element.matches(":focus-visible"), outline:style.outlineStyle,
        width:style.outlineWidth, shadow:style.boxShadow, parentShadow:getComputedStyle(element.parentElement).boxShadow };
    }));
    assert(focus.every(item => item.visible), "exercise keyboard-style focus, not only mouse focus");
    for (const item of focus) {
      assert(item.outline === "none" || item.width === "0px", item.id);
      assert.equal(item.shadow, "none", item.id);
      assert.equal(item.parentShadow, "none", item.id);
    }
    const pixels = await page.evaluate(async () => {
      const { createMapRenderer } = await import('/src/map_renderer.js');
      const { flagShortcut } = await import('/src/game_ids.js');
      const { initI18n } = await import('/src/i18n.js'); await initI18n('en');
      const canvas = document.querySelector('canvas'), ctx = canvas.getContext('2d');
      const owner = { snapshot: { flags:0, pos:{x:128,y:128} }, renderPos:{x:128,y:128} };
      const state = { entities:new Map([[1,owner]]), ownerEntityId:1, map:null, mapLoadToken:0,
        mapLoadPath:'', mapLoadPromise:null, camera:{x:256,y:256}, dpr:1, canvasWidth:256, canvasHeight:256 };
      const renderer = createMapRenderer({ ctx, state, worldScale:()=>.5, worldToScreen:p=>({x:p.x*.5,y:p.y*.5}) });
      if (!await renderer.loadMap('layers')) throw new Error('Layer fixture failed to load');
      const pixel = (x,y) => [...ctx.getImageData(x,y,1,1).data];
      renderer.drawGrid(1); const terrain = pixel(64,64);
      renderer.drawShortcutSurface(1); const solid = pixel(64,64);
      renderer.drawGrid(2); ctx.fillStyle='#ff0000'; ctx.fillRect(0,0,120,120);
      renderer.drawShortcutSurface(2); const hiddenOther = pixel(64,64);
      owner.snapshot.flags=flagShortcut;
      renderer.drawGrid(3); ctx.fillStyle='#ff0000'; ctx.fillRect(0,0,120,120);
      renderer.drawShortcutSurface(3); const revealedOther = pixel(64,64);
      ctx.fillStyle='#0000ff'; ctx.fillRect(60,60,10,10); const visibleOwner = pixel(64,64);
      renderer.drawShortcutCover(3); const coveredOwner = pixel(64,64);
      renderer.dispose();
      return { terrain, solid, hiddenOther, revealedOther, visibleOwner, coveredOwner };
    });
    assert.deepEqual(pixels.hiddenOther, pixels.solid, "opaque shortcut occludes the other entity");
    assert(pixels.revealedOther[0] > pixels.solid[0] + 40 && pixels.revealedOther[1] > 15,
      "inside shortcut: other entity remains under the 50% surface");
    assert.deepEqual(pixels.visibleOwner,[0,0,255,255], "local owner is drawn above the surface");
    assert(pixels.coveredOwner[2] >= 242 && pixels.coveredOwner[0] < 20 && pixels.coveredOwner[1] < 20,
      "cover retains the existing 95% reveal around the local owner");
    assert.deepEqual(errors, []);
    const out = resolve(root, "artifacts/shortcut-layers"); await mkdir(out,{recursive:true});
    await writeFile(resolve(out,"browser-results.json"),JSON.stringify({focus,pixels},null,2));
    console.log(`PASS Chromium: ${focus.length} focused controls without outer rings; actual foliage/cover pixels preserve layer and reveal ordering`);
  } finally { await browser.close(); }
}

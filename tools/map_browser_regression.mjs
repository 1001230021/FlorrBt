// Run through: node tools/test_map_seams.mjs --browser
// Test-only dependencies: npm --prefix build/render-qa install --no-save playwright-core
// Uses a headless browser, intercepted local assets, no game connection/accounts.
import assert from "node:assert/strict";
import { readFile, mkdir, access, writeFile } from "node:fs/promises";
import { resolve, extname, sep } from "node:path";
import { fileURLToPath } from "node:url";

export async function runMapBrowserRegression() {
  const { chromium } = await import("../build/render-qa/node_modules/playwright-core/index.mjs");
  const root = fileURLToPath(new URL("..", import.meta.url)).replace(/[\\/]$/, "");
  let executablePath = process.env.FLORRBT_BROWSER_EXECUTABLE;
  if (!executablePath && process.platform === "win32") {
    for (const candidate of ["C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
      "C:/Program Files/Google/Chrome/Application/chrome.exe"]) {
      try { await access(candidate); executablePath = candidate; break; } catch { /* Try the next installed browser. */ }
    }
  }
  const output = resolve(root, "artifacts/map-render");
  await mkdir(output, { recursive: true });
  const browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  try {
    const page = await browser.newPage({ viewport: { width: 1200, height: 800 } });
    const errors = [];
    page.on("pageerror", error => errors.push(String(error)));
    page.on("console", message => { if (message.type() === "error") console.error(message.text()); });
    await page.route("**/*", async route => {
      const url = new URL(route.request().url());
      if (url.hostname !== "map-tests.local") return route.abort();
      if (url.pathname === "/") return route.fulfill({ contentType: "text/html",
        body: '<style>body{margin:0;background:#ddd}canvas{display:block}</style><canvas id="game"></canvas>' });
      const base = url.pathname.startsWith("/data/") ? root : resolve(root, "web");
      const file = resolve(base, `.${decodeURIComponent(url.pathname)}`);
      if (!file.startsWith(base + sep)) return route.abort();
      try {
        return route.fulfill({ contentType: ({ ".js": "application/javascript", ".svg": "image/svg+xml",
          ".tmj": "application/json", ".tsj": "application/json", ".json": "application/json", ".png": "image/png" })[extname(file)],
        body: await readFile(file) });
      } catch { console.error(`Missing browser fixture: ${url.pathname}`); return route.fulfill({ status: 404, body: "Missing fixture" }); }
    });
    await page.goto("http://map-tests.local/");
    const result = await page.evaluate(async () => {
      const { loadVisualImage, whenVisualReady } = await import("/src/visual_assets.js");
      const { createMapChunkCache, getMapRasterProfile } = await import("/src/map_chunk_cache.js");
      const { drawTileImage, createMapRenderer } = await import("/src/map_renderer.js");
      const { RasterBudget } = await import("/src/raster_budget.js");
      const { initI18n } = await import("/src/i18n.js");
      await initI18n("en");
      const createCanvas = (width, height) => {
        const c = document.createElement("canvas"); c.width = width; c.height = height; return c;
      };
      const pixels = c => c.getContext("2d").getImageData(0, 0, c.width, c.height).data;
      const failures = [];
      let svgCases = 0, seamCases = 0, textureCases = 0;
      // Real assets only have a viewBox, without explicit intrinsic dimensions.
      // Unlike a bitmap-only canvas test, this catches Chromium's source-crop
      // interpretation and checks Tiled's eight orientations as well.
      for (const name of ["dirt_tl_0.svg", "dirt_l_0.svg", "dirt_t_0.svg", "dirt_tri_0.svg", "grass_c_0.svg", "water_l_0.svg"]) {
        const image = loadVisualImage(`/data/tiles/${name}`);
        if (!await whenVisualReady(image)) throw new Error(`Cannot decode ${name}`);
        for (const size of [32, 64, 128, 256, 512]) for (let flags = 0; flags < 8; flags++) {
          const actual = createCanvas(size, size), expected = createCanvas(size, size);
          const h = !!(flags & 1), v = !!(flags & 2), d = !!(flags & 4);
          const raw = ((h ? 0x80000000 : 0) | (v ? 0x40000000 : 0) | (d ? 0x20000000 : 0)) >>> 0;
          drawTileImage(image, { x: 0, y: 0 }, size, size, raw, actual.getContext("2d"));
          const ref = expected.getContext("2d");
          ref.translate(h ? size : 0, v ? size : 0); ref.scale(h ? -1 : 1, v ? -1 : 1);
          if (d) ref.transform(0, 1, 1, 0, 0, 0);
          ref.drawImage(image, 0, 0, size, size);
          const a = pixels(actual), e = pixels(expected);
          let maxError = 0;
          for (let i = 0; i < a.length; i++) maxError = Math.max(maxError, Math.abs(a[i] - e[i]));
          if (maxError > 0) failures.push({ kind: "SVG crop/orientation", name, size, flags, maxError });
          svgCases++;
          actual.width = actual.height = expected.width = expected.height = 0;
        }
      }
      const image = loadVisualImage("/data/tiles/grass_c_0.svg"); await whenVisualReady(image);
      for (const dpr of [0.75, 1, 1.25, 1.5, 2, 3, 4]) for (const scale of [0.237, 0.51, 1.03]) {
        for (const transparent of [false, true]) {
          const tileWidth = 128, side = 64, profile = getMapRasterProfile(scale, dpr, tileWidth, tileWidth, side);
          const center = tileWidth * profile.span * 2;
          const map = { width: side, height: side, tileWidth, tileHeight: tileWidth,
            tileImages: new Map([[1, image]]), layers: [{ visible: true, width: side, height: side,
              opacity: transparent ? 0.6 : 1, tiles: Array(side * side).fill(1) }] };
          const state = { canvasWidth: 181, canvasHeight: 137, dpr, camera: { x: center, y: center } };
          let drawScale = scale;
          const project = p => ({ x: (p.x - state.camera.x) * drawScale + state.canvasWidth / 2,
            y: (p.y - state.camera.y) * drawScale + state.canvasHeight / 2 });
          const budget = new RasterBudget(64 * 1024 * 1024);
          const cache = createMapChunkCache({ map, state, worldScale: () => drawScale, worldToScreen: project,
            createCanvas, drawTile: drawTileImage, isActive: () => true, requestDraw() {}, transparent,
            budget, clock: () => 0, yieldWork: async () => {}, schedule() {} });
          // One continuous raster is the reference for phase across chunk
          // boundaries. Alpha-only assertions cannot detect texture jumps.
          const span = profile.span * tileWidth;
          const reference = createCanvas(Math.round(2 * span * profile.density), Math.round(2 * span * profile.density));
          const ref = reference.getContext("2d");
          if (!transparent) { ref.fillStyle = "#1ea761"; ref.fillRect(0, 0, reference.width, reference.height); }
          ref.globalAlpha = transparent ? 0.6 : 1;
          for (let y = 0; y < profile.span * 2; y++) for (let x = 0; x < profile.span * 2; x++) {
            ref.drawImage(image, x * tileWidth * profile.density, y * tileWidth * profile.density,
              tileWidth * profile.density, tileWidth * profile.density);
          }
          const check = (mode, pan) => {
            state.camera.x = center + pan; state.camera.y = center + pan * 0.7;
            const actual = createCanvas(Math.round(state.canvasWidth * dpr), Math.round(state.canvasHeight * dpr));
            const ctx = actual.getContext("2d"); ctx.scale(dpr, dpr);
            // An overview is present even when the detailed cache misses.
            ctx.fillStyle = transparent ? "rgba(30,167,97,0.6)" : "#1ea761";
            ctx.fillRect(0, 0, actual.width / dpr, actual.height / dpr);
            cache.draw(ctx, seamCases++, false);
            const a = pixels(actual); let maxAlphaError = 0;
            for (let i = 3; i < a.length; i += 4) maxAlphaError = Math.max(maxAlphaError, Math.abs(a[i] - (transparent ? 153 : 255)));
            if (maxAlphaError > 1) failures.push({ kind: "seam", mode, dpr, scale: drawScale, pan, transparent, maxAlphaError });
            if (mode !== "zoom fallback") {
              const expected = createCanvas(actual.width, actual.height), target = expected.getContext("2d");
              target.scale(dpr, dpr);
              const from = project({ x: center - span, y: center - span });
              target.drawImage(reference, from.x, from.y, 2 * span * scale, 2 * span * scale);
              const e = pixels(expected); let error = 0;
              for (let i = 0; i < a.length; i++) error += Math.abs(a[i] - e[i]);
              if (error / a.length > 0.2) failures.push({ kind: "texture phase", mode, dpr, scale, pan, transparent, error: error / a.length });
              textureCases++;
              expected.width = expected.height = 0;
            }
            actual.width = actual.height = 0;
          };
          if (profile.span <= 4) for (const pan of [0.13, 0.51, 0.83]) check("cold", pan);
          await cache.warm();
          for (const pan of [0, 0.13, 0.37, 0.51, 0.83, 1.07]) check("warm", pan);
          for (const factor of [0.5, 0.125]) {
            drawScale = scale * factor;
            for (const pan of [0.13, 0.51]) check("zoom fallback", pan);
          }
          cache.dispose();
          reference.width = reference.height = 0;
          if (budget.bytes !== 0) failures.push({ kind: "cache leak", bytes: budget.bytes });
        }
      }
      // Exercise the complete production loader and real Garden layers, not a
      // second renderer. Save its dirt/grass boundary for visual inspection.
      const canvas = document.getElementById("game"); canvas.width = 1200; canvas.height = 800;
      const mapState = { map: null, mapLoadToken: 0, mapLoadPromise: null, mapLoadPath: "", dpr: 1,
        canvasWidth: 1200, canvasHeight: 800, camera: { x: 51200.37, y: 41984.51 }, entities: new Map() };
      const scale = 0.237;
      const loadMessages = [];
      const renderer = createMapRenderer({ ctx: canvas.getContext("2d"), state: mapState, worldScale: () => scale,
        addConsoleLine: text => loadMessages.push(text),
        worldToScreen: p => ({ x: (p.x - mapState.camera.x) * scale + 600, y: (p.y - mapState.camera.y) * scale + 400 }) });
      const garden = await renderer.loadMap("garden", { showLoading: false });
      if (!garden) throw new Error(`Real Garden map could not be loaded: ${loadMessages.join("; ")}`);
      renderer.drawGrid(1); renderer.drawShortcutSurface(1); renderer.drawShortcutCover(1);
      const gardenImage = canvas.toDataURL("image/png");
      renderer.dispose();
      return { svgCases, seamCases, textureCases, failures, gardenImage };
    });
    console.log(JSON.stringify({ browser: await browser.version(), svgCases: result.svgCases,
      seamCases: result.seamCases, textureCases: result.textureCases, failures: result.failures.slice(0, 15), failureCount: result.failures.length }));
    await writeFile(resolve(output, "garden-after.png"), Buffer.from(result.gardenImage.split(",")[1], "base64"));
    delete result.gardenImage;
    await writeFile(resolve(output, "browser-results.json"), JSON.stringify(result, null, 2));
    assert.deepEqual(errors, [], "browser runtime errors");
    assert.equal(result.failures.length, 0, "real SVG/browser rendering regressions");
  } finally { await browser.close(); }
}

import assert from "node:assert/strict";
import { createMapChunkCache, getMapRasterProfile } from "../web/src/map_chunk_cache.js";
import { pixelCoverage, drawRasterImage } from "../web/src/map_raster_geometry.js";
import { RasterBudget } from "../web/src/raster_budget.js";

// Integer coverage boundaries may move one pixel, but textures must never be
// rescaled independently to fit those boundaries while the camera moves.
for (const dpr of [1, 1.25, 2, 3]) for (const pan of [0, 0.01, 0.25, 0.51, 0.99]) {
  const a = { x: -10.1 - pan, y: 0.2 }, b = { x: 103.7 - pan, y: 40.3 }, c = { x: 217.5 - pan, y: 40.3 };
  const left = pixelCoverage(a, b, dpr), right = pixelCoverage({ x: b.x, y: a.y }, c, dpr);
  assert.ok(Math.abs(left.x + left.width - right.x) < 1e-10);
  const calls = [], image = { width: 1028, height: 1028 };
  drawRasterImage({ drawImage: (...args) => calls.push(args) }, image, a.x, a.y, 249.7, 249.7);
  assert.equal(calls[0][5], a.x); assert.equal(calls[0][7], 249.7);
}

if (process.argv.includes("--pixels")) {
  const { createCanvas } = await import("../build/render-qa/node_modules/@napi-rs/canvas/index.js");
  let comparisons = 0;
  for (const dpr of [1, 1.25, 2]) for (const scale of [0.237, 0.51, 1.03]) for (const transparent of [false, true]) {
    const tileWidth = 128;
    const profile = getMapRasterProfile(scale, dpr, tileWidth, tileWidth, 128);
    const side = profile.span * 2;
    const extent = side * tileWidth;
    const tile = createCanvas(tileWidth, tileWidth), tileContext = tile.getContext("2d");
    tileContext.fillStyle = "#338855"; tileContext.fillRect(0, 0, tileWidth, tileWidth);
    tileContext.strokeStyle = "#eecb77"; tileContext.lineWidth = 7;
    for (let x = -128; x <= 256; x += 32) {
      tileContext.beginPath(); tileContext.moveTo(x, 0); tileContext.lineTo(x - 128, 128); tileContext.stroke();
    }
    // The cache accepts decoded images; adapt the native canvas to that view.
    const image = { complete: true, naturalWidth: tileWidth, native: tile };
    const map = { width: side, height: side, tileWidth, tileHeight: tileWidth,
      layers: [{ visible: true, width: side, height: side, opacity: transparent ? 0.6 : 1,
        tiles: Array(side * side).fill(1) }], tileImages: new Map([[1, image]]) };
    const state = { canvasWidth: 160, canvasHeight: 120, dpr, camera: { x: extent / 2, y: extent / 2 } };
    let drawScale = scale;
    const project = p => ({ x: (p.x - state.camera.x) * drawScale + state.canvasWidth / 2,
      y: (p.y - state.camera.y) * drawScale + state.canvasHeight / 2 });
    const budget = new RasterBudget(64 * 1024 * 1024);
    const cache = createMapChunkCache({ map, state, worldScale: () => drawScale, worldToScreen: project,
      createCanvas, transparent, drawTile: (img, p, w, h, raw, ctx, padding) => drawRasterImage(ctx, img.native, p.x, p.y, w, h, padding),
      isActive: () => true, requestDraw() {}, budget, clock: () => 0, yieldWork: async () => {}, schedule() {} });
    await cache.warm();
    const reference = createCanvas(Math.round(extent * profile.density), Math.round(extent * profile.density));
    const ref = reference.getContext("2d");
    if (!transparent) { ref.fillStyle = "#1ea761"; ref.fillRect(0, 0, reference.width, reference.height); }
    ref.globalAlpha = transparent ? 0.6 : 1;
    for (let y = 0; y < side; y++) for (let x = 0; x < side; x++)
      ref.drawImage(tile, x * tileWidth * profile.density, y * tileWidth * profile.density,
        tileWidth * profile.density, tileWidth * profile.density);
    for (const pan of [0, 0.13, 0.37, 0.51, 0.83, 1.07]) {
      state.camera.x = extent / 2 + pan; state.camera.y = extent / 2 + pan * 0.7;
      const expected = createCanvas(Math.round(160 * dpr), Math.round(120 * dpr));
      const actual = createCanvas(expected.width, expected.height);
      for (const destination of [expected, actual]) {
        const target = destination.getContext("2d"); target.scale(dpr, dpr);
        const from = project({ x: 0, y: 0 });
        // Simulate the overview underneath the chunks, including transparency.
        target.drawImage(reference, from.x, from.y, extent * scale, extent * scale);
      }
      cache.draw(actual.getContext("2d"), comparisons++, false);
      const a = actual.getContext("2d").getImageData(0, 0, actual.width, actual.height).data;
      const e = expected.getContext("2d").getImageData(0, 0, expected.width, expected.height).data;
      let error = 0, maxAlphaError = 0;
      for (let i = 0; i < a.length; i++) {
        error += Math.abs(a[i] - e[i]);
        if (i % 4 === 3) maxAlphaError = Math.max(maxAlphaError, Math.abs(a[i] - e[i]));
      }
      assert.ok(error / a.length < 0.2, `texture phase changed: dpr=${dpr}, scale=${scale}, pan=${pan}, alpha=${transparent}, error=${error / a.length}`);
      assert.ok(maxAlphaError <= 1, `gap/overlap at chunk seam: alpha error ${maxAlphaError}`);
      expected.width = expected.height = actual.width = actual.height = 0;
    }
    // Retain old rasters during a rapid zoom-out. Their small gutters must
    // still cover the snapped device-pixel ownership boundaries completely.
    state.canvasWidth = state.canvasHeight = 64;
    for (const factor of [0.5, 0.125]) {
      drawScale = scale * factor;
      const output = createCanvas(Math.round(64 * dpr), Math.round(64 * dpr));
      const context = output.getContext("2d"); context.scale(dpr, dpr);
      const from = project({ x: 0, y: 0 });
      context.drawImage(reference, from.x, from.y, extent * drawScale, extent * drawScale);
      cache.draw(context, comparisons++, false);
      const pixels = context.getImageData(0, 0, output.width, output.height).data;
      const alpha = transparent ? 153 : 255;
      for (let i = 3; i < pixels.length; i += 4) assert.ok(Math.abs(pixels[i] - alpha) <= 1,
        `fallback seam: alpha=${pixels[i]}, expected=${alpha}, factor=${factor}, dpr=${dpr}, scale=${scale}`);
      output.width = output.height = 0;
    }
    if (profile.span <= 4) {
      // Force uncached vector fallback and exercise the same per-tile mapping.
      cache.dispose(); drawScale = scale;
      const direct = createMapChunkCache({ map, state, worldScale: () => drawScale, worldToScreen: project,
        createCanvas, transparent, drawTile: (img, p, w, h, raw, ctx, padding) => drawRasterImage(ctx, img.native, p.x, p.y, w, h, padding),
        isActive: () => true, requestDraw() {}, budget, schedule() {} });
      const output = createCanvas(Math.round(64 * dpr), Math.round(64 * dpr));
      const context = output.getContext("2d"); context.scale(dpr, dpr);
      const from = project({ x: 0, y: 0 });
      context.drawImage(reference, from.x, from.y, extent * drawScale, extent * drawScale);
      direct.draw(context, comparisons++, false);
      const pixels = context.getImageData(0, 0, output.width, output.height).data;
      for (let i = 3; i < pixels.length; i += 4) assert.ok(Math.abs(pixels[i] - (transparent ? 153 : 255)) <= 1, "direct tile seam");
      direct.dispose(); output.width = output.height = 0;
    }
    cache.dispose(); reference.width = reference.height = tile.width = tile.height = 0;
    assert.equal(budget.bytes, 0);
  }
  console.log(`PASS ${comparisons} raster pixel comparisons: fractional movement/zoom/DPR, opaque and transparent seams`);
}
console.log("PASS shared tile coverage and continuous texture transforms");

// Offline: real map loader, cache and compositor, instrumented canvas. No game
// server, browser session, credentials, sockets or persisted account data.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { createMapChunkCache } from "../web/src/map_chunk_cache.js";
import { mapLayerRole, defaultPlayerLightRadius, shortcutReveal } from "../web/src/terrain_rules.js";
import { flagShortcut, flagDead } from "../web/src/game_ids.js";
import { drawRasterImage } from "../web/src/map_raster_geometry.js";

const tileset = JSON.parse(readFileSync(new URL("../data/tiles/tileset.tsj", import.meta.url), "utf8"));
const garden = JSON.parse(readFileSync(new URL("../data/maps/garden.tmj", import.meta.url), "utf8"));
const shapes = new Map();
for (const [id, quarters] of [[125, 3], [126, 2], [127, 1]]) {
  const tile = tileset.tiles.find(tile => tile.id === id);
  assert.equal(tile.objectgroup.objects.length, 1, "merged outline must not create internal cell seams");
  const object = tile.objectgroup.objects[0];
  const points = object.polygon || [{ x: 0, y: 0 }, { x: object.width, y: 0 },
    { x: object.width, y: object.height }, { x: 0, y: object.height }];
  let area = 0;
  for (let i = 0; i < points.length; i++) {
    const a = points[i], b = points[(i + 1) % points.length];
    assert.equal(a.x % 128, 0); assert.equal(a.y % 128, 0);
    area += a.x * b.y - a.y * b.x;
  }
  assert.equal(Math.abs(area) / 2, quarters * 128 * 128);
  shapes.set(id, points);
}
assert.ok(garden.tilesets.every(ref => !/^[a-z]:/i.test(ref.source || "")), "map cannot depend on a desktop path");
assert.equal(defaultPlayerLightRadius({ tileWidth: garden.tilewidth, tileHeight: garden.tileheight }), 512);

let canvasId = 0;
const canvases = [], scheduled = [];
function canvas() {
  const result = { id: ++canvasId, width: 0, height: 0 };
  const stack = [], commands = [];
  const context = new Proxy({
    canvas: result, commands, globalAlpha: 1, globalCompositeOperation: "source-over",
    save() { stack.push({ globalAlpha: this.globalAlpha, globalCompositeOperation: this.globalCompositeOperation }); },
    restore() { Object.assign(this, stack.pop()); },
    drawImage(image, ...args) { commands.push({ kind: "image", image, args, alpha: this.globalAlpha, composite: this.globalCompositeOperation }); },
    fill() { commands.push({ kind: "fill", alpha: this.globalAlpha, composite: this.globalCompositeOperation }); },
    fillRect(...args) { commands.push({ kind: "rect", args, alpha: this.globalAlpha }); },
    clearRect(...args) { commands.push({ kind: "clear", args }); },
    arc(...args) { commands.push({ kind: "arc", args }); },
    getImageData(_x, _y, width, height) { return { data: new Uint8ClampedArray(width * height * 4) }; },
  }, { get: (object, key) => key in object ? object[key] : () => {} });
  result.getContext = () => context; canvases.push(result); return result;
}
const target = canvas();
const mapFixture = { width: 3, height: 1, tilewidth: 256, tileheight: 256,
  tilesets: [{ firstgid: 1, source: "../tiles/tileset.tsj" }],
  layers: [
    { type: "tilelayer", name: "grass", width: 3, height: 1, data: [1, 1, 1] },
    { type: "tilelayer", name: "shortcut", width: 3, height: 1, data: [0, 127, 0] },
    { type: "tilelayer", name: "shortcut_cover", width: 3, height: 1, data: [0, 25, 0] },
  ] };
const resources = new Map([
  ["/data/maps/shortcuts.tmj", mapFixture],
  ["/data/tiles/tileset.tsj", { ...tileset, tiles: tileset.tiles.filter(tile => [0, 24, 126].includes(tile.id)) }],
]);
const source = readFileSync(new URL("../web/src/map_renderer.js", import.meta.url), "utf8")
  .replace(/^import[\s\S]*?from "[^"]+";\r?\n/gm, "").replace(/^export /gm, "");
const state = { map: null, mapLoadToken: 0, mapLoadPromise: null, mapLoadPath: "", ownerEntityId: 7,
  entities: new Map(), camera: { x: 384, y: 128 }, canvasWidth: 512, canvasHeight: 256, dpr: 1 };
const scope = { createMapChunkCache, mapLayerRole, shortcutReveal, drawRasterImage, appState: state, dom: { ctx: target.getContext() },
  clientRuntimeConfig: { minimapTileSubdivisions: 2, minimapTextureSampleSizePx: 4,
    minimapTextureCoverageThreshold: 0.002, minimapCollisionSamplesPerAxis: 2, minimapCollisionCoverageThreshold: 0.08 },
  clamp: (value, lo, hi) => Math.max(lo, Math.min(hi, value)), t: key => key,
  document: { createElement: () => canvas() }, performance: { now: () => 0 }, window: { setTimeout },
  loadVisualImage: src => ({ src, complete: true, naturalWidth: 256, naturalHeight: 256 }),
  whenVisualReady: async () => true,
  fetch: async path => ({ ok: resources.has(path), status: 404, json: async () => structuredClone(resources.get(path)) }),
};
const { createMapRenderer } = vm.runInNewContext(`${source}\n;({createMapRenderer})`, scope);
const errors = [];
const renderer = createMapRenderer({ state, ctx: target.getContext(), worldScale: () => 1,
  worldToScreen: point => ({ x: point.x - 128, y: point.y }), addConsoleLine: (...args) => errors.push(args) });
const previousAnimationFrame = globalThis.requestAnimationFrame;
globalThis.requestAnimationFrame = fn => scheduled.push(fn);
try {
  const map = await renderer.loadMap("shortcuts");
  assert.ok(map, JSON.stringify(errors));
  assert.equal(map.collisionWalls.length, 1, "visual cover must not generate a second solid wall");
  assert.equal(map.collisionWalls[0].shortcut, true);
  assert.equal(map.renderLayers.length, 1);
  assert.equal(map.overlayPasses.length, 2);
  for (const pass of map.overlayPasses) {
    assert.ok(pass.renderLayers.every(layer => layer.role === pass.role));
    for (const entry of pass.chunkCache.values()) {
      const calls = entry.canvas.getContext().commands;
      assert.ok(!calls.some(call => call.kind === "rect"), "overlay chunks must not be filled with opaque grass");
      const images = calls.filter(call => call.kind === "image");
      assert.ok(images.every(call => call.image.src?.includes(pass.role === "shortcut" ? "foliage" : "dirt")));
    }
  }
  const predicted = renderer.resolvePredictedCircleMotion({ x: 220, y: 64 }, { x: 400, y: 64 }, 10);
  assert.equal(predicted.pos.x, 400); assert.equal(predicted.normals.length, 0);
  const owner = { snapshot: { flags: 0, pos: { x: 384, y: 128 }, horizon: 99999 }, renderPos: { x: 380, y: 120 } };
  state.entities.set(7, owner);
  renderer.drawGrid(1000); renderer.drawShortcutCover(1000);
  assert.ok(!canvases.some(item => item.getContext().commands.some(call => call.composite === "destination-out")));
  owner.snapshot.flags = flagShortcut;
  const before = new Set(canvases);
  renderer.drawGrid(1001); renderer.drawShortcutCover(1001);
  const erasures = canvases.flatMap(item => item.getContext().commands).filter(call => call.kind === "fill" && call.composite === "destination-out");
  assert.deepEqual(erasures.map(call => call.alpha), [0.5, 0.95]);
  const circles = canvases.flatMap(item => item.getContext().commands).filter(call => call.kind === "arc");
  assert.ok(circles.every(call => call.args[0] === 252 && call.args[1] === 120 && call.args[2] === 256));
  assert.ok(!target.getContext().commands.some(call => call.composite === "destination-out"), "mask must never erase the world/entities surface");
  assert.ok(canvases.every(item => before.has(item)), "moving/revealing cannot rebuild terrain caches");
  owner.snapshot.flags |= flagDead; assert.equal(shortcutReveal(state, map), null);
  owner.snapshot.flags = flagShortcut; assert.equal(shortcutReveal(state, { ...map }), null);
  state.mapLoadPromise = Promise.resolve();
  assert.equal(shortcutReveal(state, map), null, "new-scene snapshots cannot reveal the old map during asynchronous loading");
  state.mapLoadPromise = null;
  state.entities.set(7, { snapshot: { flags: 0, pos: owner.snapshot.pos, entityGeneration: "new" } });
  assert.equal(shortcutReveal(state, map), null, "a reused owner id must not keep the previous actor's reveal");
  renderer.applyTerrainDelta(2, 0, 1, 0);
  assert.equal(map.overlayPasses[1].layers[0].tiles[1], 0, "passes share authoritative tile arrays, not stale copies");
  renderer.dispose();
  for (const pass of map.overlayPasses) assert.equal(pass.overviewCanvas.width, 0);
} finally { globalThis.requestAnimationFrame = previousAnimationFrame; }
console.log("PASS quarter-tile outlines, layer roles, player prediction, independent alpha masks, base light radius, scene identity and cache lifetime");

// Offline only: evaluate the real loader with fake fetch/images/canvas.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { createMapChunkCache } from "../web/src/map_chunk_cache.js";

const source = readFileSync(new URL("../web/src/map_renderer.js", import.meta.url), "utf8")
  .replace(/^import[\s\S]*?from "[^"]+";\r?\n/gm, "").replace(/^export /gm, "");
const requests = [], imagePaths = [], resources = new Map();
const canvasContext = new Proxy({
  getImageData: (_x, _y, width, height) => ({ data: new Uint8ClampedArray(width * height * 4) }),
}, { get: (target, key) => key in target ? target[key] : () => {} });
class FakeImage {
  complete = true;
  naturalWidth = 16;
  naturalHeight = 16;
  set src(value) { imagePaths.push(value); queueMicrotask(() => this.onload?.()); }
  async decode() {}
}
const scope = {
  createMapChunkCache,
  loadVisualImage: src => { const image = new FakeImage(); image.src = src; return image; },
  whenVisualReady: async image => image.complete && image.naturalWidth > 0,
  appState: {}, dom: { ctx: canvasContext },
  clientRuntimeConfig: { minimapTileSubdivisions: 2, minimapTextureSampleSizePx: 4,
    minimapTextureCoverageThreshold: 0.002, minimapCollisionSamplesPerAxis: 2, minimapCollisionCoverageThreshold: 0.08 },
  clamp: (value, min, max) => Math.max(min, Math.min(max, value)),
  t: (key, args) => `${key} ${JSON.stringify(args)}`,
  document: { createElement: () => ({ getContext: () => canvasContext }) },
  Image: FakeImage, performance: { now: () => 0 }, window: { setTimeout },
  fetch: async path => {
    requests.push(path);
    const data = resources.get(path);
    return { ok: data !== undefined, status: data === undefined ? 404 : 200, json: async () => structuredClone(data) };
  },
};
const { createMapRenderer, normalizeMapPath } = vm.runInNewContext(`${source}\n;({createMapRenderer, normalizeMapPath})`, scope);
for (const [input, expected] of [
  [undefined, ""], ["", ""], ["   ", ""],
  ["garden", "/data/maps/garden.tmj"], ["garden.tmj", "/data/maps/garden.tmj"],
  ["data/maps/garden", "/data/maps/garden.tmj"], ["/data/maps/garden.tmj", "/data/maps/garden.tmj"],
  ["maps/eou_small", "/data/maps/eou_small.tmj"], [".\\data\\maps\\ant_hel.tmj", "/data/maps/ant_hel.tmj"],
  ["custom/garden", "/data/maps/custom/garden.tmj"], ["maps/custom/garden.json", "/data/maps/custom/garden.json"],
  ["garden?v=2", "/data/maps/garden.tmj?v=2"], ["garden.tmj?v=2", "/data/maps/garden.tmj?v=2"],
]) assert.equal(normalizeMapPath(input), expected);

const mapFixture = { width: 1, height: 1, tilewidth: 16, tileheight: 16,
  tilesets: [{ firstgid: 1, source: "../tiles/tileset.tsj" }],
  layers: [{ type: "tilelayer", name: "ground", width: 1, height: 1, data: [1] }] };
resources.set("/data/maps/garden.tmj", mapFixture);
resources.set("/data/tiles/tileset.tsj", { tiles: [{ id: 0, image: "grass.svg", imagewidth: 16, imageheight: 16 }] });
const newState = () => ({ map: null, mapLoadToken: 0, mapLoadPromise: null, mapLoadPath: "" });
const state = newState(), messages = [], loading = [];
const renderer = createMapRenderer({ state, addConsoleLine: (text, level) => messages.push({ text, level }),
  setLoadingVisible: value => loading.push(value), worldScale: () => 1, worldToScreen: point => point });
const loaded = await renderer.loadMap("garden");
assert.ok(loaded, JSON.stringify(messages));
assert.equal(loaded.path, "/data/maps/garden.tmj");
assert.deepEqual(requests, ["/data/maps/garden.tmj", "/data/tiles/tileset.tsj"]);
assert.deepEqual(imagePaths, ["/data/tiles/grass.svg"]);
assert.equal(state.map, loaded);
assert.equal(state.mapLoadPromise, null);
assert.deepEqual(loading, [true, false]);
await renderer.loadMap("data/maps/garden.tmj");
assert.equal(requests.length, 2, "short names and explicit paths must use the same cached map");

assert.equal(await renderer.loadMap("missing"), null);
assert.match(messages.at(-1).text, /status\.mapLoadFailed.*\/data\/maps\/missing\.tmj.*404/);
assert.equal(state.mapLoadPromise, null, "failed requests cannot leave an in-flight promise blocking a retry");
resources.set("/data/maps/missing.tmj", { ...mapFixture, tilesets: [] });
assert.ok(await renderer.loadMap("missing"));
assert.equal(state.map.path, "/data/maps/missing.tmj");

// Separate observers may load the same map without replacing the game map.
const observerState = newState();
const observer = createMapRenderer({ state: observerState, worldScale: () => 1, worldToScreen: point => point });
await observer.loadMap("garden");
assert.equal(observerState.map.path, "/data/maps/garden.tmj");
assert.equal(state.map.path, "/data/maps/missing.tmj");
console.log("PASS legacy/full map paths, relative tileset/images, cached aliases, failed-load retry and isolated observer state");

// Missing terrain must not publish a map with permanent transparent holes.
const brokenState = newState(), brokenScope = { ...scope,
  whenVisualReady: async () => false };
const { createMapRenderer: createBrokenRenderer } = vm.runInNewContext(`${source}\n;({createMapRenderer})`, brokenScope);
const brokenRenderer = createBrokenRenderer({ state: brokenState, worldScale: () => 1, worldToScreen: p => p });
assert.equal(await brokenRenderer.loadMap("garden"), null);
assert.equal(brokenState.map, null);
assert.equal(brokenState.mapLoadPromise, null);
observer.dispose(); renderer.dispose();
assert.equal(observerState.map, null); assert.equal(state.map, null);
console.log("PASS failed terrain cannot publish, map/observer disposal releases retained resources");

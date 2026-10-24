// Offline rendering-policy checks. No sockets, browser or persisted accounts.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { RasterBudget } from "../web/src/raster_budget.js";
import { createMapChunkCache, getMapRasterProfile } from "../web/src/map_chunk_cache.js";
import { getRenderFrameTiming, stepRenderZoom, takeRenderPreparationCost } from "../web/src/render_timing.js";

const budget = new RasterBudget(100), evicted = [];
const a = {}, b = {}, c = {};
assert.ok(budget.store(a, 60, () => evicted.push("a")));
budget.beginFrame(1); budget.touch(a);
assert.equal(budget.store(b, 60, () => evicted.push("b")), false, "current frame cannot be evicted");
budget.beginFrame(2);
assert.ok(budget.store(b, 60, () => evicted.push("b")));
assert.deepEqual(evicted, ["a"]);
assert.equal(budget.store(c, 101, () => {}), false);
assert.equal(budget.bytes, 60);
budget.remove(b); assert.equal(budget.bytes, 0);

for (const scale of [0.002, 0.06, 0.12, 0.5, 1, 2, 4, 8]) {
  for (const dpr of [0.75, 1, 2, 4]) {
    const p = getMapRasterProfile(scale, dpr, 512, 512);
    assert.ok(p.density >= scale * dpr, `${scale} ${dpr} must not upscale`);
    assert.ok(p.span * 512 * p.density <= 1024);
    assert.ok(Number.isInteger(Math.log2(p.span)), "spatial parents remain aligned");
  }
}
assert.ok(getMapRasterProfile(1, 1, 16, 16, 1).span <= 1, "tiny maps do not build huge repeated regions");

let now = 0, scale = 0.24;
const scheduled = [], created = [], drawn = [], tileDraws = [];
const state = { canvasWidth: 256, canvasHeight: 256, camera: { x: 500, y: 500 }, dpr: 1 };
const map = { width: 16, height: 16, tileWidth: 128, tileHeight: 128,
  layers: [
    { name: "floor", width: 16, height: 16, visible: true, tiles: Array(256).fill(1) },
    { name: "wall", width: 16, height: 16, visible: true, tiles: Array(256).fill(2) },
    { name: "hidden", width: 16, height: 16, visible: false, tiles: Array(256).fill(3) },
  ], tileImages: new Map([1, 2, 3].map(id => [id, { id, complete: true, naturalWidth: 16 }])) };
const renderBudget = new RasterBudget(32 * 1024 * 1024);
const ctx = { drawImage: (...args) => drawn.push(args), save() {}, restore() {}, beginPath() {}, rect() {}, clip() {}, fillRect() {} };
const createCanvas = (width, height) => {
  const image = { width, height, calls: [], getContext: () => context };
  const context = { fillRect() {}, image };
  created.push(image); return image;
};
const cache = createMapChunkCache({ map, state, worldScale: () => scale,
  worldToScreen: pos => ({ x: (pos.x - state.camera.x) * scale + 128, y: (pos.y - state.camera.y) * scale + 128 }),
  createCanvas, drawTile: (image, _pos, _w, _h, _raw, target) => { tileDraws.push(image.id); target.image?.calls.push(image.id); },
  isActive: () => true, requestDraw() {}, budget: renderBudget,
  clock: () => now += 0.01, yieldWork: async () => {}, schedule: fn => scheduled.push(fn),
});
await cache.warm();
assert.ok(map.chunkCache.size);
for (const entry of map.chunkCache.values()) {
  assert.ok(entry.canvas.calls.includes(1) && entry.canvas.calls.includes(2), "publish full floor + wall together");
  assert.ok(!entry.canvas.calls.includes(3));
  assert.equal(entry.canvas.calls.lastIndexOf(1) < entry.canvas.calls.indexOf(2), true, "layer order stays intact");
  assert.equal(entry.canvas.width, entry.pixelWidth + 4, "sample neighbours in a real gutter");
}
cache.draw(ctx, 100);
const stableVisibleKeys = map.visibleChunkKeys;
cache.draw(ctx, 108);
assert.equal(map.visibleChunkKeys, stableVisibleKeys, "stationary frames reuse the chunk plan");
const oldCanvases = new Set(drawn.map(call => call[0]));
drawn.length = 0; scale = 0.26; // density 1/4 -> 1/2, new spatial keys
cache.draw(ctx, 116);
assert.ok(drawn.some(call => oldCanvases.has(call[0])), "keep old quality until new chunks finish");
for (const call of drawn) {
  assert.equal(call.length, 9); assert.equal(call[1], 0); assert.equal(call[2], 0);
  assert.equal(call[3], call[0].width); assert.equal(call[4], call[0].height);
}
let work = 0;
while (scheduled.length && work++ < 160) await scheduled.shift()();
drawn.length = 0; cache.draw(ctx, 132);
assert.ok([...map.chunkCache.values()].some(e => e.profile.density === 0.5));
assert.ok([...map.chunkCache.values()].some(e => e.profile.density === 0.25));

// Turning/panning must replace queued work rather than finish obsolete areas.
state.camera = { x: 90000, y: 90000 };
cache.draw(ctx, 148);
assert.ok(map.chunkBuildQueue.every(job => job.left > 80000 && job.top > 80000));
assert.ok(map.chunkBuildQueue.length <= 160);
cache.invalidate(0, 0);
assert.ok(![...map.chunkCache.values()].some(e => e.left <= 0 && e.right > 0 && e.top <= 0 && e.bottom > 0));
// Queue length is a scheduling cap, never a cap on visible coverage.
state.canvasWidth = 4800; state.canvasHeight = 2700; state.dpr = 4; scale = 2;
let regions = 0;
let depth = 0;
ctx.save = () => { depth++; }; ctx.restore = () => { depth--; };
ctx.rect = () => { if (depth === 1) regions++; };
cache.draw(ctx, 164);
assert.ok(map.visibleChunkKeys.size > 160);
assert.equal(regions, map.visibleChunkKeys.size, "high-DPI viewport cannot drop the cells beyond the queue cap");
assert.ok(map.chunkBuildQueue.length <= 160);
cache.dispose();
assert.equal(renderBudget.bytes, 0);
while (scheduled.length) await scheduled.shift()();
assert.equal(map.chunkCache.size, 0, "disposed maps cannot publish late work");

// A terrain edit during a yielded build rejects that entire composite.
let releaseSlice;
const pausedMap = { ...map, chunkCache: undefined };
const pendingCache = createMapChunkCache({ map: pausedMap, state: { ...state, camera: { x: 100, y: 100 } },
  worldScale: () => 0.03, worldToScreen: p => p, createCanvas,
  drawTile() {}, isActive: () => true, requestDraw() {}, budget: renderBudget,
  clock: () => now += 2, yieldWork: () => new Promise(resolve => { releaseSlice = resolve; }) });
const preparing = pendingCache.warm();
await Promise.resolve(); assert.equal(typeof releaseSlice, "function");
pendingCache.dispose(); releaseSlice();
// warm's final yield is also controllable, release it once.
await Promise.resolve(); await Promise.resolve(); releaseSlice();
await preparing;
assert.equal(pausedMap.chunkCache.size, 0);

for (const fps of [30, 60, 120]) {
  let zoom = 100;
  for (let i = 0; i < fps; i++) zoom = stepRenderZoom(zoom, 25, 1 / fps);
  assert.ok(Math.abs(zoom - 25) < 0.01);
  const halfway = stepRenderZoom(100, 25, 0.04);
  assert.ok(halfway > 25 && halfway < 100);
  assert.ok(stepRenderZoom(halfway, 100, 0.02) > halfway, "reversal starts from current pose");
}
const hitch = getRenderFrameTiming(0.12);
assert.equal(hitch.dt, 0.12); assert.ok(hitch.steps <= 8 && hitch.step <= 1 / 30);
assert.ok(Math.abs(hitch.step * hitch.steps - 0.12) < 1e-8);
assert.equal(getRenderFrameTiming(10).dt, 0.25, "background-tab resume is bounded");
assert.equal(getRenderFrameTiming(NaN).dt, 0);
takeRenderPreparationCost();

// Exercise the real sprite cache with fake canvases. Bitmap creation must
// happen in the preparation queue, not in drawSpriteImage's miss path.
const spriteSource = readFileSync(new URL("../web/src/sprite_raster_cache.js", import.meta.url), "utf8")
  .replace(/^import[^\n]+\r?\n/gm, "").replace(/^export /gm, "");
const spriteBudget = new RasterBudget(16 * 1024 * 1024), spriteFrames = [], spriteDraws = [];
let rasterCount = 0, spriteClock = 0;
class FakeSprite { complete = true; naturalWidth = 110; src = "body.svg"; }
const spriteScope = { state: { dpr: 2 }, sceneRasterBudget: spriteBudget,
  HTMLImageElement: FakeSprite, recordRenderPreparationCost() {}, requestRedraw() {},
  requestAnimationFrame: fn => spriteFrames.push(fn), setTimeout,
  performance: { now: () => spriteClock += 0.01 },
  document: { createElement() { rasterCount++; return { width: 0, height: 0,
    getContext: () => ({ drawImage() {}, fillRect() {} }) }; } } };
const sprites = vm.runInNewContext(`${spriteSource}\n;({drawSpriteImage,setSpriteRasterFrameTime,setSpriteHitTint})`, spriteScope);
const spriteContext = { drawImage: (...args) => spriteDraws.push(args) }, sprite = new FakeSprite();
sprites.setSpriteRasterFrameTime(1);
sprites.drawSpriteImage(spriteContext, sprite, 0, 0, 80, 80);
assert.equal(spriteDraws.at(-1)[0], sprite); assert.equal(rasterCount, 0);
assert.equal(spriteFrames.length, 1);
spriteFrames.shift()(2);
assert.equal(rasterCount, 1);
sprites.setSpriteRasterFrameTime(2);
sprites.drawSpriteImage(spriteContext, sprite, 0, 0, 80, 80);
const bitmap = spriteDraws.at(-1)[0];
assert.notEqual(bitmap, sprite); assert.ok(bitmap.width >= 160);
const beforeZoomOut = rasterCount;
sprites.drawSpriteImage(spriteContext, sprite, 0, 0, 40, 40);
assert.equal(spriteDraws.at(-1)[0], bitmap, "zoom-out reuses sharper rasters rather than rerasterizing SVGs inline");
assert.equal(rasterCount, beforeZoomOut);
sprites.drawSpriteImage(spriteContext, sprite, 0, 0, 80, 80);
sprites.setSpriteHitTint(true);
sprites.drawSpriteImage(spriteContext, sprite, 0, 0, 80, 80);
assert.notEqual(spriteDraws.at(-1)[0], bitmap); assert.ok(spriteBudget.bytes > bitmap.width * bitmap.height * 4);
sprites.setSpriteHitTint(false);
sprites.drawSpriteImage(spriteContext, sprite, 0, 0, 2000, 2000);
assert.equal(spriteDraws.at(-1)[0], sprite, "giant SVG must remain vector beyond the raster cap");
const beforeTiny = spriteDraws.length;
sprites.drawSpriteImage(spriteContext, sprite, 0, 0, 0.1, 0.1);
assert.equal(spriteDraws.length, beforeTiny + 1, "subpixel bodies still draw");
sprite.src = "changed.svg";
sprites.drawSpriteImage(spriteContext, sprite, 0, 0, 80, 80);
assert.equal(bitmap.width, 0, "source replacement releases old bitmap and tint");
assert.equal(spriteDraws.at(-1)[0], sprite);
while (spriteFrames.length) spriteFrames.shift()(3);
assert.ok(spriteBudget.bytes <= spriteBudget.limitBytes);

// Fetch/transform/decode readiness with fake Image and fetch; never networking.
const originals = new Map(["Image", "fetch", "document"].map(key => [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
const fetches = [], revoked = [];
const createURL = URL.createObjectURL, revokeURL = URL.revokeObjectURL;
let nextUrl = 0, rejectFetch = false, allowDecode = null;
class FakeImage {
  complete = false; naturalWidth = 0;
  set src(value) { this._src = value; queueMicrotask(() => { this.complete = true; this.naturalWidth = 64; this.onload?.(); }); }
  get src() { return this._src; }
  decode() { return allowDecode ? new Promise(resolve => { allowDecode.resolve = resolve; }) : Promise.resolve(); }
}
try {
  Object.defineProperty(globalThis, "document", { configurable: true, value: { baseURI: "http://offline.invalid/" } });
  Object.defineProperty(globalThis, "Image", { configurable: true, value: FakeImage });
  Object.defineProperty(globalThis, "fetch", { configurable: true, value: async src => {
    fetches.push(src); if (rejectFetch) throw Error("offline failure");
    return { ok: true, text: async () => "<svg/>" };
  } });
  URL.createObjectURL = () => `blob:offline-${++nextUrl}`;
  URL.revokeObjectURL = value => revoked.push(value);
  const { loadVisualImage, prepareSvgImage, whenVisualReady, assignVisualImage } = await import("../web/src/visual_assets.js");
  let transformations = 0;
  const image = loadVisualImage("./assets/unit.svg");
  const derived = prepareSvgImage("http://offline.invalid/assets/unit.svg", "body", text => { transformations++; return text; });
  await Promise.all([whenVisualReady(image), whenVisualReady(derived)]);
  assert.equal(fetches.length, 1, "original and variants fetch source once");
  assert.equal(transformations, 1);
  assert.equal(prepareSvgImage("./assets/unit.svg", "body"), derived);
  assert.equal(revoked.length, 0, "codex/DOM reuse must not receive a revoked URL");
  const card = {};
  assignVisualImage(card, derived); assert.equal(card.src, derived.src);

  rejectFetch = true;
  const bad = loadVisualImage("./bad.svg");
  assert.equal(await whenVisualReady(bad), false, "fetch failure settles, never hangs loading");
  rejectFetch = false;
  const retry = loadVisualImage("./bad.svg", { retry: true });
  assert.notEqual(retry, bad); assert.equal(await whenVisualReady(retry), true);
  const invalid = prepareSvgImage("./assets/unit.svg", "invalid", () => { throw Error("bad transform"); });
  assert.equal(await whenVisualReady(invalid), false);
  allowDecode = {};
  const decoding = loadVisualImage("./decode.svg");
  for (let i = 0; i < 20 && !allowDecode.resolve; i++) await Promise.resolve();
  assert.equal(decoding.visualState, "loading", "load event alone is not readiness");
  assert.equal(typeof allowDecode.resolve, "function");
  allowDecode.resolve(); allowDecode = null;
  assert.equal(await whenVisualReady(decoding), true);
} finally {
  for (const [key, descriptor] of originals) {
    if (descriptor) Object.defineProperty(globalThis, key, descriptor);
    else delete globalThis[key];
  }
  URL.createObjectURL = createURL; URL.revokeObjectURL = revokeURL;
}

const game = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
for (const name of ["prepareBeeSprites", "prepareBeetleSprites", "prepareSpiderSprites", "prepareGardenSprites", "prepareSoldierAntSprites"])
  assert.match(game, new RegExp(`${name}\\(\\)`));
assert.doesNotMatch(game, /if \((?:size|drawSize) < 0\.5\) return/);
assert.doesNotMatch(game, /drawPixelEntity|detailMinRadiusForEntity/);
assert.doesNotMatch(game, /isEntityInRenderView: \(\) => true/);
assert.ok(game.indexOf("setSpriteRasterFrameTime(now);", game.indexOf("export function drawScene")) <
  game.indexOf("if (devDebugUi?.isOpen())", game.indexOf("export function drawScene")), "debug gets its own frame token too");
console.log("PASS decoded variants, failure/retry, shared bytes/pins, complete layers, retained zoom levels, gutters, stale-work cancellation, subpixel bodies and frame-rate-independent zoom/catch-up");

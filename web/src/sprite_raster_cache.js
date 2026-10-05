import { state } from "./app_context.js";
import { sceneRasterBudget } from "./raster_budget.js";
import { recordRenderPreparationCost } from "./render_timing.js";
import { requestRedraw } from "./render_invalidate.js";

// SVG-backed <img> sources re-rasterize the whole vector on every drawImage
// whose destination size misses the browser's internal cache. Entity sprites
// draw at per-entity, per-zoom sizes, so in practice every mob paid a full
// SVG rasterization every frame — the dominant draw cost of the world pass.
//
// This cache rasterizes each image once per quantized device-pixel bucket
// into a plain canvas and blits that instead: bitmap-to-bitmap draws are
// cheap and stay cheap at any entity count. Buckets grow in 1.25x steps and
// always round up, so the blit only ever downscales (≤20%, visually clean).
const spriteRasterCache = new Map(); // image -> { src, buckets: Map(key -> entry) }
// Larger drawings fall back to the decoded SVG, not an enlarged low-res
// bitmap. Map composites and all raster/tint variants share a byte budget.
const maxBucketPx = 2048;
const minBucketPx = 8;
const bucketLogRatio = Math.log(1.25);
const pixelBuckets = new Uint16Array(maxBucketPx + 1);
const pendingRasters = new Map();
const rasterJobs = [];
let rasterPumpScheduled = false;

// The cache is touched by many entities per frame. Let the scene provide one
// shared frame token instead of calling performance.now() for every draw
// and every LRU touch. Both game and debug renderers enter here.
export function setSpriteRasterFrameTime(now = performance.now()) {
  sceneRasterBudget.beginFrame(Number.isFinite(now) ? now : performance.now());
}

// White-silhouette variants power the hit flash: instead of re-drawing a
// flashing entity through ctx.filter (which forces intermediate surfaces and
// stalled frames whenever a whole pack got hit), the entity is re-blitted
// from pre-tinted copies of the cached rasters at flash alpha. While the
// tint mode is active drawSpriteImage substitutes the white variant.
let whiteTintActive = false;
const whiteSourceVariants = new WeakMap(); // non-<img> sources (pattern canvases)

export function setSpriteHitTint(active) {
  whiteTintActive = !!active;
}

function bucketPx(sizePx) {
  const clamped = Math.ceil(Math.max(minBucketPx, Math.min(maxBucketPx, sizePx)));
  if (pixelBuckets[clamped]) return pixelBuckets[clamped];
  const power = Math.ceil(
    Math.log(clamped / minBucketPx) / bucketLogRatio - 1e-6,
  );
  return pixelBuckets[clamped] = Math.min(
    maxBucketPx,
    Math.ceil(minBucketPx * Math.pow(1.25, Math.max(0, power))),
  );
}

export function drawSpriteImage(ctx, image, dx, dy, dw, dh) {
  if (!(dw > 0) || !(dh > 0)) return;
  if (
    typeof HTMLImageElement !== "undefined" &&
    image instanceof HTMLImageElement
  ) {
    // SVGs remain vectors outside the bounded cache. Stretching a 2048px
    // bitmap at 4K zoom was the reason large bodies became blurry.
    if (Math.max(dw, dh) * (state.dpr || 1) > maxBucketPx && !whiteTintActive) {
      ctx.drawImage(image, dx, dy, dw, dh);
      return;
    }
    const entry = rasterizedSprite(image, dw, dh);
    if (entry) {
      const source = whiteTintActive
        ? whiteVariantForEntry(entry) || entry.canvas
        : entry.canvas;
      ctx.drawImage(source, dx, dy, dw, dh);
      return;
    }
    // Not-yet-loaded images: nothing sensible to tint, and the normal path
    // keeps the caller's existing fallback behavior.
    if (whiteTintActive) return;
    ctx.drawImage(image, dx, dy, dw, dh);
    return;
  }
  // Canvas/bitmap sources are already fast; tinting uses a per-source white
  // copy (kept in a WeakMap so per-entity pattern canvases can be collected).
  if (whiteTintActive) {
    const white = whiteVariantForSource(image);
    if (white) ctx.drawImage(white, dx, dy, dw, dh);
    return;
  }
  ctx.drawImage(image, dx, dy, dw, dh);
}

function rasterizedSprite(image, dw, dh, prepare = false) {
  if (!image.complete || image.naturalWidth <= 0) return null;

  let cached = spriteRasterCache.get(image);
  const src = image.currentSrc || image.src || "";
  if (cached && cached.src !== src) {
    // The <img> swapped sources after load (blob URL replaced the original):
    // drop the stale rasters.
    for (const entry of cached.buckets.values()) sceneRasterBudget.remove(entry);
    cached = null;
  }
  if (!cached) {
    cached = { src, buckets: new Map() };
    spriteRasterCache.set(image, cached);
  }

  const dpr = state.dpr || 1;
  const bucketW = bucketPx(dw * dpr);
  const bucketH = bucketPx(dh * dpr);
  const key = bucketW * 100000 + bucketH;
  let entry = cached.buckets.get(key);
  if (!entry) {
    if (!prepare) {
      queueSpriteRaster(image, dw, dh, key, src);
      // Reuse a sharper already-decoded bitmap during zoom-out while the
      // exact bucket is queued. Never upscale a coarse raster or drop bodies.
      let fallback = null;
      for (const candidate of cached.buckets.values()) {
        if (candidate.canvas.width >= bucketW && candidate.canvas.height >= bucketH &&
            (!fallback || candidate.bytes < fallback.bytes)) fallback = candidate;
      }
      if (fallback) { sceneRasterBudget.touch(fallback); return fallback; }
      return null; // Draw the decoded vector until its bitmap is ready.
    }
    const canvas = document.createElement("canvas");
    canvas.width = bucketW;
    canvas.height = bucketH;
    const context = canvas.getContext("2d");
    if (!context) return null;
    try {
      context.drawImage(image, 0, 0, bucketW, bucketH);
    } catch {
      return null;
    }
    entry = { canvas, white: null, bytes: bucketW * bucketH * 4 };
    const owner = cached, created = entry;
    if (!sceneRasterBudget.store(entry, entry.bytes, () => {
      if (created.white) sceneRasterBudget.remove(created.white);
      owner.buckets.delete(key);
      if (!owner.buckets.size && spriteRasterCache.get(image) === owner) spriteRasterCache.delete(image);
      canvas.width = canvas.height = 0;
    })) {
      canvas.width = canvas.height = 0;
      return null;
    }
    // store() can evict the last older bucket owned by this image.
    spriteRasterCache.set(image, cached);
    cached.buckets.set(key, entry);
  }
  sceneRasterBudget.touch(entry);
  return entry;
}

function queueSpriteRaster(image, dw, dh, key, src) {
  let pending = pendingRasters.get(image);
  const existing = pending?.get(key);
  if (existing) { existing.frame = sceneRasterBudget.frame; return; }
  if (rasterJobs.length >= 128) return;
  if (!pending) { pending = new Map(); pendingRasters.set(image, pending); }
  const job = { image, dw, dh, key, src, frame: sceneRasterBudget.frame };
  pending.set(key, job); rasterJobs.push(job);
  scheduleRasterPump();
}

function scheduleRasterPump() {
  if (rasterPumpScheduled || !rasterJobs.length) return;
  rasterPumpScheduled = true;
  requestAnimationFrame(now => {
    rasterPumpScheduled = false;
    sceneRasterBudget.beginFrame(now);
    const start = performance.now();
    let count = 0;
    while (rasterJobs.length && count < 2 && performance.now() - start < 1.5) {
      const job = rasterJobs.shift(), pending = pendingRasters.get(job.image);
      pending?.delete(job.key);
      if (!pending?.size) pendingRasters.delete(job.image);
      if (sceneRasterBudget.frame - job.frame > 3 || (job.image.currentSrc || job.image.src || "") !== job.src) continue;
      rasterizedSprite(job.image, job.dw, job.dh, true);
      count++;
    }
    recordRenderPreparationCost(performance.now() - start);
    if (count) requestRedraw();
    scheduleRasterPump();
  });
}

export async function primeSpriteRasters(images) {
  let started = performance.now();
  for (const image of images) {
    if (!image?.complete || !image.naturalWidth || image.failed) continue;
    rasterizedSprite(image, 64, 64, true);
    if (performance.now() - started >= 1.5) {
      await new Promise(resolve => setTimeout(resolve, 0));
      started = performance.now();
    }
  }
}

function whitenedCanvas(source, width, height) {
  if (!(width > 0) || !(height > 0)) return null;
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  if (!context) return null;
  try {
    context.drawImage(source, 0, 0, width, height);
  } catch {
    return null;
  }
  context.globalCompositeOperation = "source-in";
  // The reference's yellow mobs flash orange/red, not opaque white. Keep
  // the cached silhouette approach (one extra blit, no canvas filter).
  context.fillStyle = "#ff7954";
  context.fillRect(0, 0, width, height);
  return canvas;
}

function whiteVariantForEntry(entry) {
  if (entry.white) { sceneRasterBudget.touch(entry.white); return entry.white; }
  const white = whitenedCanvas(
    entry.canvas,
    entry.canvas.width,
    entry.canvas.height,
  );
  if (!white) return null;
  if (!sceneRasterBudget.store(white, entry.bytes, () => {
    if (entry.white === white) entry.white = null;
    white.width = white.height = 0;
  })) { white.width = white.height = 0; return null; }
  entry.white = white;
  sceneRasterBudget.touch(white);
  return white;
}

function whiteVariantForSource(source) {
  const width = Math.floor(source.width || source.naturalWidth || 0);
  const height = Math.floor(source.height || source.naturalHeight || 0);
  if (!(width > 0) || !(height > 0)) return null;
  const cached = whiteSourceVariants.get(source);
  if (cached && cached.width === width && cached.height === height) {
    sceneRasterBudget.touch(cached.canvas);
    return cached.canvas;
  }
  if (cached) sceneRasterBudget.remove(cached.canvas);
  const canvas = whitenedCanvas(source, width, height);
  if (!canvas) return null;
  if (!sceneRasterBudget.store(canvas, width * height * 4, () => {
    if (whiteSourceVariants.get(source)?.canvas === canvas) whiteSourceVariants.delete(source);
    canvas.width = canvas.height = 0;
  })) { canvas.width = canvas.height = 0; return null; }
  whiteSourceVariants.set(source, { canvas, width, height });
  sceneRasterBudget.touch(canvas);
  return canvas;
}

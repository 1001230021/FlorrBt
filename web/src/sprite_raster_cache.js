import { state } from "./app_context.js";

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
let totalRasterBytes = 0;
const maxRasterBytes = 96 * 1024 * 1024;
const maxBucketPx = 1024;
const minBucketPx = 8;
const bucketLogRatio = Math.log(1.25);

// White-silhouette variants power the hit flash: instead of re-drawing a
// flashing entity through ctx.filter (which forces intermediate surfaces and
// stalled frames whenever a whole pack got hit), the entity is re-blitted
// from pre-tinted copies of the cached rasters at flash alpha. While the
// tint mode is active drawSpriteImage substitutes the white variant.
let whiteTintActive = false;
const whiteSourceVariants = new WeakMap(); // non-<img> sources (pattern canvases)

export function setSpriteWhiteTint(active) {
  whiteTintActive = !!active;
}

function bucketPx(sizePx) {
  const clamped = Math.max(minBucketPx, Math.min(maxBucketPx, sizePx));
  const power = Math.ceil(
    Math.log(clamped / minBucketPx) / bucketLogRatio - 1e-6,
  );
  return Math.min(
    maxBucketPx,
    Math.ceil(minBucketPx * Math.pow(1.25, Math.max(0, power))),
  );
}

function entryBytes(entry) {
  return entry.bytes * (entry.white ? 2 : 1);
}

export function drawSpriteImage(ctx, image, dx, dy, dw, dh) {
  if (!(dw > 0) || !(dh > 0)) return;
  if (
    typeof HTMLImageElement !== "undefined" &&
    image instanceof HTMLImageElement
  ) {
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

function rasterizedSprite(image, dw, dh) {
  if (!image.complete || image.naturalWidth <= 0) return null;

  let cached = spriteRasterCache.get(image);
  const src = image.currentSrc || image.src || "";
  if (cached && cached.src !== src) {
    // The <img> swapped sources after load (blob URL replaced the original):
    // drop the stale rasters.
    for (const entry of cached.buckets.values())
      totalRasterBytes -= entryBytes(entry);
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
    entry = { canvas, white: null, bytes: bucketW * bucketH * 4, lastUsed: 0 };
    cached.buckets.set(key, entry);
    totalRasterBytes += entry.bytes;
    if (totalRasterBytes > maxRasterBytes) evictOldSpriteRasters();
  }
  entry.lastUsed = performance.now();
  return entry;
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
  context.fillStyle = "#ffffff";
  context.fillRect(0, 0, width, height);
  return canvas;
}

function whiteVariantForEntry(entry) {
  if (entry.white) return entry.white;
  const white = whitenedCanvas(
    entry.canvas,
    entry.canvas.width,
    entry.canvas.height,
  );
  if (!white) return null;
  entry.white = white;
  totalRasterBytes += entry.bytes;
  if (totalRasterBytes > maxRasterBytes) evictOldSpriteRasters();
  return white;
}

function whiteVariantForSource(source) {
  const width = Math.floor(source.width || source.naturalWidth || 0);
  const height = Math.floor(source.height || source.naturalHeight || 0);
  if (!(width > 0) || !(height > 0)) return null;
  const cached = whiteSourceVariants.get(source);
  if (cached && cached.width === width && cached.height === height)
    return cached.canvas;
  const canvas = whitenedCanvas(source, width, height);
  if (!canvas) return null;
  whiteSourceVariants.set(source, { canvas, width, height });
  return canvas;
}

function evictOldSpriteRasters() {
  const all = [];
  for (const [image, cached] of spriteRasterCache) {
    for (const [key, entry] of cached.buckets)
      all.push({ image, cached, key, entry });
  }
  all.sort((a, b) => a.entry.lastUsed - b.entry.lastUsed);
  for (const item of all) {
    if (totalRasterBytes <= maxRasterBytes * 0.8) break;
    item.cached.buckets.delete(item.key);
    totalRasterBytes -= entryBytes(item.entry);
    if (item.cached.buckets.size === 0) spriteRasterCache.delete(item.image);
  }
}

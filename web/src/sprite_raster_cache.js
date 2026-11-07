import { state } from "./app_context.js";
import { sceneRasterBudget } from "./raster_budget.js";
import { recordRenderPreparationCost } from "./render_timing.js";
import { requestRedraw } from "./render_invalidate.js";

// SVG 按设备像素尺寸分桶缓存到画布，避免每实体每帧重新栅格化。
// 桶按 1.25 倍递增并向上取整，绘制只轻微缩小。
const spriteRasterCache = new Map();
// 超过缓存尺寸时回退矢量图，避免放大低分辨率位图；全部变体共享字节预算。
const maxBucketPx = 2048;
const minBucketPx = 8;
const bucketLogRatio = Math.log(1.25);
const pixelBuckets = new Uint16Array(maxBucketPx + 1);
const pendingRasters = new Map();
const rasterJobs = [];
let rasterPumpScheduled = false;

// 场景提供共用帧标记，避免每次绘制及缓存访问都读取时钟。
export function setSpriteRasterFrameTime(now = performance.now()) {
  sceneRasterBudget.beginFrame(Number.isFinite(now) ? now : performance.now());
}

// 受击闪光使用预着色缓存轮廓，避免画布滤镜创建临时表面。
let whiteTintActive = false;
const whiteSourceVariants = new WeakMap(); // 非图片源的弱引用着色缓存。

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
    // 超大尺寸使用矢量源，避免拉伸有限尺寸缓存造成模糊。
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
    // 未加载图像无法着色，保留普通绘制回退。
    if (whiteTintActive) return;
    ctx.drawImage(image, dx, dy, dw, dh);
    return;
  }
  // 画布/位图直接绘制；着色副本以弱映射保存，允许按实体创建的画布回收。
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
    // 图像源被替换后丢弃旧栅格。
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
      // 缩小时可复用更清晰缓存等待目标桶，不能放大粗糙缓存或漏绘。
      let fallback = null;
      for (const candidate of cached.buckets.values()) {
        if (candidate.canvas.width >= bucketW && candidate.canvas.height >= bucketH &&
            (!fallback || candidate.bytes < fallback.bytes)) fallback = candidate;
      }
      if (fallback) { sceneRasterBudget.touch(fallback); return fallback; }
      return null; // 位图就绪前直接绘制已解码矢量图。
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
    // 存入新桶可能淘汰该图像最后一个旧桶。
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
  // 受击轮廓使用暖色，仍只额外位图绘制一次。
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

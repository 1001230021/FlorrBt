import { sceneRasterBudget } from "./raster_budget.js";
import { recordRenderPreparationCost } from "./render_timing.js";
import { pixelCoverage, drawRasterImage } from "./map_raster_geometry.js";

const gutter = 2;
const chunkPixelLimit = 1024;
const sliceMs = 1.5;
const maxQueuedChunks = 160;
const overlaps = (a, b) => a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;

export function getMapRasterProfile(scale, dpr, tileWidth, tileHeight, mapExtent = 128) {
  const requested = Math.max(1 / 256, Math.min(32, (scale || 1) * (dpr || 1)));
  const density = 2 ** Math.ceil(Math.log2(requested));
  // 区域按二次幂对齐，跨缩放档位时可复用父子块；小于瓦片的区域用于近景。
  const span = Math.max(1 / 64, Math.min(128, 2 ** Math.floor(Math.log2(Math.max(1, mapExtent))),
    2 ** Math.floor(Math.log2(chunkPixelLimit / (Math.max(tileWidth, tileHeight) * density)))));
  return { density, span, key: `${span}:${density}`, label: `${span}@${density}x` };
}

export function getMapRasterBounds(state, scale) {
  const x = state.camera?.x || 0, y = state.camera?.y || 0;
  const halfW = Math.max(1, state.canvasWidth || 1) / (2 * scale);
  const halfH = Math.max(1, state.canvasHeight || 1) / (2 * scale);
  return { left: x - halfW, right: x + halfW, top: y - halfH, bottom: y + halfH };
}

function expand(bounds, amount) {
  return { left: bounds.left - amount, right: bounds.right + amount,
    top: bounds.top - amount, bottom: bounds.bottom + amount };
}

// 每块包含当前绘制通道的完整图层。透明通道独立合成并替换覆盖，避免总览和父子块重复叠加透明度。
export function createMapChunkCache({ map, state, worldScale, worldToScreen,
  createCanvas, drawTile, isActive, requestDraw, budget = sceneRasterBudget,
  clock = () => performance.now(), yieldWork = () => new Promise(resolve => setTimeout(resolve, 0)),
  schedule = run => requestAnimationFrame(run), layers = map.layers, transparent = false }) {
  const entries = new Map();
  let wanted = new Map(), queue = [], busy = false, scheduled = false, disposed = false;
  let revision = 0, previousCamera = null, inFlight = null;
  let planKey = "", queueDirty = false;
  let directCanvas = null;
  map.chunkCache = entries;
  map.chunkBuildQueue = queue;

  function refreshQueue() {
    queue = [...wanted.values()].filter(job => !entries.has(job.key) && job.key !== inFlight).slice(0, maxQueuedChunks);
    map.chunkBuildQueue = queue;
    queueDirty = false;
  }

  function plan(now) {
    const scale = Math.max(1e-6, worldScale());
    const profile = getMapRasterProfile(scale, state.dpr, map.tileWidth, map.tileHeight, Math.max(map.width, map.height));
    const visible = getMapRasterBounds(state, scale);
    const cx = (visible.left + visible.right) / 2, cy = (visible.top + visible.bottom) / 2;
    const dt = previousCamera ? Math.max(1 / 240, Math.min(0.25, (now - previousCamera.now) / 1000)) : 0;
    const width = visible.right - visible.left, height = visible.bottom - visible.top;
    const vx = dt ? (cx - previousCamera.x) / dt : 0, vy = dt ? (cy - previousCamera.y) / dt : 0;
    const leadX = Math.max(-width * 0.5, Math.min(width * 0.5, vx * 0.25));
    const leadY = Math.max(-height * 0.5, Math.min(height * 0.5, vy * 0.25));
    previousCamera = { x: cx, y: cy, now };
    const ahead = expand(visible, Math.min(width, height) * 0.18);
    ahead.left += Math.min(0, leadX); ahead.right += Math.max(0, leadX);
    ahead.top += Math.min(0, leadY); ahead.bottom += Math.max(0, leadY);
    const w = profile.span * map.tileWidth, h = profile.span * map.tileHeight;
    const x0 = Math.floor(ahead.left / w), x1 = Math.ceil(ahead.right / w);
    const y0 = Math.floor(ahead.top / h), y1 = Math.ceil(ahead.bottom / h);
    const nextKey = `${profile.key}:${revision}:${x0}:${x1}:${y0}:${y1}:` +
      `${Math.floor(visible.left / w)}:${Math.ceil(visible.right / w)}:${Math.floor(visible.top / h)}:${Math.ceil(visible.bottom / h)}`;
    // 相机未跨块时复用描述、优先级和队列，避免每帧分配排序。
    if (nextKey === planKey) {
      if (queueDirty) refreshQueue();
      return visible;
    }
    planKey = nextKey;
    const jobs = [];
    for (let y = y0; y < y1; y++) {
      for (let x = x0; x < x1; x++) {
        const bounds = { left: x * w, top: y * h, right: (x + 1) * w, bottom: (y + 1) * h };
        const visibleNow = overlaps(bounds, visible);
        jobs.push({ key: `${profile.key}:${x}:${y}`, ...bounds, profile, revision, visible: visibleNow,
          priority: (visibleNow ? 0 : 1e12) + ((x + 0.5) * w - cx) ** 2 + ((y + 0.5) * h - cy) ** 2 });
      }
    }
    jobs.sort((a, b) => a.priority - b.priority);
    const coverage = jobs.filter((job, index) => job.visible || index < maxQueuedChunks);
    wanted = new Map(coverage.map(job => [job.key, job]));
    refreshQueue();
    map.visibleChunkKeys = new Set(jobs.filter(job => job.visible).map(job => job.key));
    map.chunkProfileLabel = profile.label;
    return visible;
  }

  async function build(job, warming = false) {
    const { density } = job.profile;
    const width = Math.round((job.right - job.left) * density);
    const height = Math.round((job.bottom - job.top) * density);
    const canvas = createCanvas(width + gutter * 2, height + gutter * 2);
    const context = canvas.getContext("2d");
    if (!context) return null;
    const valid = () => !disposed && isActive() && job.revision === revision && (warming || wanted.has(job.key));
    const fail = () => { canvas.width = canvas.height = 0; return null; };
    if (!transparent) {
      context.fillStyle = map.backgroundColor || "#1ea761";
      context.fillRect(0, 0, canvas.width, canvas.height);
    }
    const tw = map.tileWidth, th = map.tileHeight;
    const x0 = Math.floor((job.left - gutter / density) / tw);
    const y0 = Math.floor((job.top - gutter / density) / th);
    const x1 = Math.ceil((job.right + gutter / density) / tw);
    const y1 = Math.ceil((job.bottom + gutter / density) / th);
    let started = clock(), tiles = 0;
    for (const layer of layers) {
      if (layer.visible === false || !layer.tiles?.length || !(layer.width > 0 && layer.height > 0)) continue;
      context.globalAlpha = layer.opacity ?? 1;
      for (let y = y0; y < y1; y++) {
        const sy = Math.max(0, Math.min(layer.height - 1, y));
        for (let x = x0; x < x1; x++) {
          const sx = Math.max(0, Math.min(layer.width - 1, x));
          const raw = layer.tiles[sy * layer.width + sx] >>> 0;
          const image = map.tileImages.get(raw & 0x1fffffff);
          if (image?.complete && image.naturalWidth > 0 && !image.failed) {
            drawTile(image, { x: (x * tw - job.left) * density + gutter,
              y: (y * th - job.top) * density + gutter }, tw * density, th * density, raw, context);
          }
          if (++tiles >= 256 || clock() - started >= sliceMs) {
            recordRenderPreparationCost(clock() - started);
            await yieldWork();
            if (!valid()) return fail();
            started = clock(); tiles = 0;
          }
        }
      }
    }
    recordRenderPreparationCost(clock() - started);
    if (!valid()) return fail();
    return { ...job, canvas, pixelWidth: width, pixelHeight: height };
  }

  function publish(entry) {
    if (!entry) return false;
    if (!budget.store(entry, entry.canvas.width * entry.canvas.height * 4, () => {
      if (entries.get(entry.key) === entry) entries.delete(entry.key);
      queueDirty = true;
      entry.canvas.width = entry.canvas.height = 0;
    })) { entry.canvas.width = entry.canvas.height = 0; return false; }
    entries.set(entry.key, entry);
    return true;
  }

  function pump() {
    if (scheduled || busy || disposed || !isActive() || !queue.length) return;
    scheduled = true;
    schedule(async () => {
      scheduled = false;
      if (disposed || busy || !isActive()) return;
      const job = queue.shift();
      if (!job) return;
      busy = true; map.chunkBuilding = true; inFlight = job.key;
      try { if (!entries.has(job.key) && publish(await build(job))) requestDraw(); }
      catch (error) { console.warn("Map chunk preparation failed", error); }
      finally { busy = false; map.chunkBuilding = false; inFlight = null; queueDirty = true; pump(); }
    });
  }

  function buildDirect(job) {
    // 缺失块也须按世界坐标采样。直接缩放 SVG 的边缘抗锯齿会留下缝隙，整数裁剪无法补齐；
    // 因此复用有界临时画布，并绘入真实相邻像素。
    const { density } = job.profile;
    const width = Math.round((job.right - job.left) * density);
    const height = Math.round((job.bottom - job.top) * density);
    if (!directCanvas) directCanvas = createCanvas(width + gutter * 2, height + gutter * 2);
    if (directCanvas.width !== width + gutter * 2) directCanvas.width = width + gutter * 2;
    if (directCanvas.height !== height + gutter * 2) directCanvas.height = height + gutter * 2;
    const context = directCanvas.getContext("2d");
    if (!context) return null;
    context.globalAlpha = 1;
    if (transparent) context.clearRect(0, 0, directCanvas.width, directCanvas.height);
    else {
      context.fillStyle = map.backgroundColor || "#1ea761";
      context.fillRect(0, 0, directCanvas.width, directCanvas.height);
    }
    const tw = map.tileWidth, th = map.tileHeight;
    const x0 = Math.floor((job.left - gutter / density) / tw);
    const y0 = Math.floor((job.top - gutter / density) / th);
    const x1 = Math.ceil((job.right + gutter / density) / tw);
    const y1 = Math.ceil((job.bottom + gutter / density) / th);
    for (const layer of layers) {
      if (layer.visible === false || !layer.tiles?.length || !(layer.width > 0 && layer.height > 0)) continue;
      context.globalAlpha = layer.opacity ?? 1;
      for (let ty = y0; ty < y1; ty++) {
        for (let tx = x0; tx < x1; tx++) {
          const sy = Math.max(0, Math.min(layer.height - 1, ty)), sx = Math.max(0, Math.min(layer.width - 1, tx));
          const raw = layer.tiles[sy * layer.width + sx] >>> 0, image = map.tileImages.get(raw & 0x1fffffff);
          if (!image?.complete || !image.naturalWidth || image.failed) continue;
          drawTile(image, { x: (tx * tw - job.left) * density + gutter,
            y: (ty * th - job.top) * density + gutter }, tw * density, th * density, raw, context);
        }
      }
    }
    return { ...job, canvas: directCanvas, pixelWidth: width, pixelHeight: height };
  }

  function draw(ctx, now, prepare = true) {
    if (disposed) return;
    budget.beginFrame(now);
    const visible = plan(now);
    let candidates = null;
    const dpr = Math.max(0.75, state.dpr || 1);
    const contains = (entry, job) => entry.left <= job.left && entry.top <= job.top &&
      entry.right >= job.right && entry.bottom >= job.bottom;
    const drawEntry = entry => {
      if (!entry) return;
      budget.touch(entry);
      const from = worldToScreen({ x: entry.left, y: entry.top });
      const to = worldToScreen({ x: entry.right, y: entry.bottom });
      const coverage = pixelCoverage(from, to, dpr);
      if (coverage.width <= 0 || coverage.height <= 0) return;
      const pixelScale = worldScale() / entry.profile.density;
      const padding = gutter * pixelScale;
      ctx.save(); ctx.beginPath(); ctx.rect(coverage.x, coverage.y, coverage.width, coverage.height); ctx.clip();
      if (transparent) ctx.clearRect(coverage.x, coverage.y, coverage.width, coverage.height);
      drawRasterImage(ctx, entry.canvas, from.x - padding, from.y - padding,
        entry.canvas.width * pixelScale, entry.canvas.height * pixelScale);
      ctx.restore();
    };
    for (const job of wanted.values()) {
      if (!job.visible) continue;
      ctx.save();
      const from = worldToScreen({ x: job.left, y: job.top }), to = worldToScreen({ x: job.right, y: job.bottom });
      const x = Math.round(from.x * dpr) / dpr, y = Math.round(from.y * dpr) / dpr;
      ctx.beginPath(); ctx.rect(x, y, Math.round(to.x * dpr) / dpr - x, Math.round(to.y * dpr) / dpr - y); ctx.clip();
      const exact = entries.get(job.key);
      if (exact) {
        drawEntry(exact);
      } else {
        // 仅缓存缺失时搜索并排序旧档位覆盖，稳定视野只做精确查找。
        candidates ??= [...entries.values()].filter(entry => overlaps(entry, visible))
          .sort((a, b) => a.profile.density - b.profile.density);
        const parent = candidates.findLast(entry => contains(entry, job));
        if (parent) drawEntry(parent);
        // 缩小时先保留已覆盖该区域的精细子块，待粗粒度替换块完成。
        const children = candidates.filter(entry => overlaps(entry, job) &&
          !contains(entry, job) && (!parent || entry.profile.density > parent.profile.density));
        if (!parent && job.profile.span <= 4) {
          // 极高 DPI 可能超过预算；近景缺失块使用不超过 1028px 的临时画布，远景沿用旧档位等待异步完成。
          drawEntry(buildDirect(job));
        }
        for (const child of children) drawEntry(child);
      }
      ctx.restore();
    }
    // 只固定选中的覆盖块，避免旧档位长期占满预算而无法装入清晰替换块。
    if (prepare) pump();
  }

  async function warm() {
    // 准备期间相机可能已随新快照移动，发布前再次检查当前视野。
    for (let pass = 0; pass < 2; pass++) {
      plan(clock());
      for (const job of [...wanted.values()].filter(job => job.visible && !entries.has(job.key))) {
        if (disposed || !isActive()) return;
        publish(await build(job, true));
        await yieldWork();
      }
    }
    queue = queue.filter(job => !entries.has(job.key));
    map.chunkBuildQueue = queue;
  }

  function invalidate(row, col) {
    revision += 1;
    planKey = "";
    const tile = { left: col * map.tileWidth, top: row * map.tileHeight,
      right: (col + 1) * map.tileWidth, bottom: (row + 1) * map.tileHeight };
    for (const entry of entries.values()) {
      // 相邻块的采样边缘也可能包含该瓦片，需要一并失效。
      if (overlaps(expand(entry, gutter / entry.profile.density), tile)) budget.remove(entry);
    }
  }

  function dispose() {
    disposed = true; revision += 1; queue.length = 0; wanted.clear();
    for (const entry of entries.values()) budget.remove(entry);
    if (directCanvas) directCanvas.width = directCanvas.height = 0;
    directCanvas = null;
  }

  return { draw, warm, invalidate, dispose };
}

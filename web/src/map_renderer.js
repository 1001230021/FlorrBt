import { clamp } from "./protocol.js";
import { dom, state } from "./app_context.js";

const mapChunkTileSpan = 4;
const mapChunkCacheLimit = 192;
const mapPrewarmBudgetMs = 8;

export function createMapRenderer({
  ctx = dom.ctx,
  defaultMapName = "garden.tmj",
  addConsoleLine = () => {},
  requestDraw = () => {},
  worldScale,
  worldToScreen,
} = {}) {
  async function loadMap(mapName) {
    const path = normalizeMapPath(mapName);
    if (!path) return null;
    if (path === state.mapName && state.map) return state.map;
    if (path === state.mapName && state.mapLoadPromise) return state.mapLoadPromise;

    const token = ++state.mapLoadToken;
    state.mapName = path;
    state.mapDisplayName = String(mapName || path);
    addConsoleLine(`Loading map: ${path}`);
    state.mapLoadPromise = loadMapData(path, token);
    return state.mapLoadPromise;
  }

  async function loadMapData(path, token) {
    try {
      const response = await fetch(path, { cache: "no-cache" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const tmj = await response.json();
      const tileImages = new Map();
      const collidableGids = new Set();
      const tileLoadPromises = [];
      for (const tilesetRef of tmj.tilesets || []) {
        const tilesetPath = joinAssetPath(path, tilesetRef.source || "");
        if (!tilesetPath) continue;
        const tsjResponse = await fetch(tilesetPath, { cache: "no-cache" });
        if (!tsjResponse.ok) throw new Error(`tileset HTTP ${tsjResponse.status}`);
        const tileset = await tsjResponse.json();
        for (const tile of tileset.tiles || []) {
          if (!tile.image) continue;
          const gid = (tilesetRef.firstgid || 1) + (tile.id || 0);
          if (tileHasCollision(tile)) collidableGids.add(gid);
          const image = new Image();
          const tileSrc = joinAssetPath(tilesetPath, tile.image);
          tileLoadPromises.push(loadMapTileImage(image, tileSrc).then((ok) => {
            if (!ok && token === state.mapLoadToken) addConsoleLine(`Tile image failed: ${tileSrc}`, "error");
          }));
          tileImages.set(gid, image);
        }
      }
      await Promise.all(tileLoadPromises);

      const layers = [];
      for (const layer of tmj.layers || []) {
        if (layer.type !== "tilelayer") continue;
        layers.push({
          name: layer.name || "",
          width: layer.width || tmj.width || 0,
          height: layer.height || tmj.height || 0,
          tiles: await decodeTileLayer(layer),
        });
      }
      const decorations = await loadMapDecorations(tmj, path, token);

      if (token !== state.mapLoadToken) return;
      state.map = {
        path,
        width: tmj.width || 0,
        height: tmj.height || 0,
        tileWidth: tmj.tilewidth || 512,
        tileHeight: tmj.tileheight || 512,
        layers,
        decorations,
        tileImages,
        collidableGids,
        collisionTiles: buildCollisionTiles(layers, collidableGids),
        chunkCache: new Map(),
        chunkBuildQueue: [],
        chunkBuildQueued: new Set(),
        chunkBuildPumpScheduled: false,
        chunkUseTick: 0,
      };
      addConsoleLine(`Map loaded: ${path} (${layers.length} layers, ${tileImages.size} tiles)`);
      startMapChunkPrewarm(state.map, token);
      return state.map;
    } catch (error) {
      if (token === state.mapLoadToken) {
        state.map = null;
        addConsoleLine(`Map load failed: ${path} (${error.message})`, "error");
      }
      return null;
    } finally {
      if (token === state.mapLoadToken) state.mapLoadPromise = null;
    }
  }

  async function loadMapTileImage(image, src) {
    return new Promise((resolve) => {
      image.decoding = "async";
      image.onload = async () => {
        if (image.decode) {
          try {
            await image.decode();
          } catch {
            // Some browsers reject SVG decode even after load; the image is still drawable.
          }
        }
        resolve(true);
      };
      image.onerror = () => resolve(false);
      image.src = src;
    });
  }

  async function decodeTileLayer(layer) {
    if (Array.isArray(layer.data)) return layer.data.map((value) => Number(value) || 0);
    if (typeof layer.data !== "string") return [];

    const compressed = Uint8Array.from(atob(layer.data), (char) => char.charCodeAt(0));
    let bytes = compressed;
    if (layer.compression === "gzip") {
      if (typeof DecompressionStream === "undefined") throw new Error("gzip map layers need DecompressionStream");
      const stream = new Blob([compressed]).stream().pipeThrough(new DecompressionStream("gzip"));
      bytes = new Uint8Array(await new Response(stream).arrayBuffer());
    } else if (layer.compression) {
      throw new Error(`unsupported map compression: ${layer.compression}`);
    }

    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const out = [];
    for (let offset = 0; offset + 4 <= bytes.byteLength; offset += 4)
      out.push(view.getUint32(offset, true));
    return out;
  }

  function startMapChunkPrewarm(map, token) {
    if (!map?.layers?.length) return;

    const jobs = [];
    const profile = mapChunkProfile(1);
    map.activeChunkProfileKey = mapChunkProfileKey(profile);
    for (let layerIndex = 0; layerIndex < map.layers.length; layerIndex += 1) {
      const layer = map.layers[layerIndex];
      if (!layer.width || !layer.height || !layer.tiles || layer.tiles.length <= 0) continue;

      const chunkCols = Math.ceil(layer.width / profile.span);
      const chunkRows = Math.ceil(layer.height / profile.span);
      for (let chunkY = 0; chunkY < chunkRows; chunkY += 1) {
        for (let chunkX = 0; chunkX < chunkCols; chunkX += 1) {
          jobs.push({ layer, layerIndex, chunkX, chunkY, profile });
        }
      }
    }
    if (!jobs.length) return;
    if (jobs.length > mapChunkCacheLimit) {
      addConsoleLine(`Map tiles ready: ${map.path} (${jobs.length} chunks on demand)`);
      return;
    }

    for (const job of jobs) {
      if (token !== state.mapLoadToken || state.map !== map) return;
      queueMapChunkBuild(map, job.layer, job.layerIndex, job.chunkX, job.chunkY, job.profile);
    }
  }

  function drawGrid() {
    ctx.fillStyle = state.map ? "#20aa63" : "#edf3f7";
    ctx.fillRect(0, 0, state.canvasWidth, state.canvasHeight);
    drawMapTiles();
    drawMapDecorations();

    if (!state.map) {
      ctx.save();
      ctx.font = "13px Segoe UI, Microsoft YaHei, sans-serif";
      ctx.textAlign = "left";
      ctx.textBaseline = "top";
      ctx.fillStyle = "rgba(57, 70, 88, 0.72)";
      ctx.fillText(`Loading map: ${state.mapDisplayName || defaultMapName}`, 14, 72);
      ctx.restore();
    }

    if (!state.debugGrid) return;

    const scale = worldScale();
    drawTileDebugGrid(scale);

    let spacingWorld = 64;
    while (spacingWorld * scale < 42) spacingWorld *= 2;
    while (spacingWorld * scale > 130 && spacingWorld > 8) spacingWorld *= 0.5;
    const spacing = spacingWorld * scale;
    const startX = ((-state.camera.x * scale + state.canvasWidth * 0.5) % spacing + spacing) % spacing;
    const startY = ((-state.camera.y * scale + state.canvasHeight * 0.5) % spacing + spacing) % spacing;
    ctx.lineWidth = 1;
    ctx.strokeStyle = "rgba(184, 198, 214, 0.55)";
    ctx.beginPath();
    for (let x = startX; x < state.canvasWidth; x += spacing) {
      ctx.moveTo(x, 0);
      ctx.lineTo(x, state.canvasHeight);
    }
    for (let y = startY; y < state.canvasHeight; y += spacing) {
      ctx.moveTo(0, y);
      ctx.lineTo(state.canvasWidth, y);
    }
    ctx.stroke();

    const origin = worldToScreen({ x: 0, y: 0 });
    ctx.strokeStyle = "rgba(118, 136, 158, 0.42)";
    ctx.beginPath();
    ctx.moveTo(origin.x, 0);
    ctx.lineTo(origin.x, state.canvasHeight);
    ctx.moveTo(0, origin.y);
    ctx.lineTo(state.canvasWidth, origin.y);
    ctx.stroke();
  }

  function drawMapTiles() {
    const map = state.map;
    if (!map || !map.layers || !map.tileImages) return;

    const scale = worldScale();
    const profile = mapChunkProfile(scale);
    resetMapChunkBuildQueueForProfile(map, profile);
    map.chunkProfileLabel = profile.label;
    const tileW = map.tileWidth;
    const tileH = map.tileHeight;
    if (tileW <= 0 || tileH <= 0) return;

    const visibleBounds = {
      left: state.camera.x - state.canvasWidth * 0.5 / scale - tileW,
      top: state.camera.y - state.canvasHeight * 0.5 / scale - tileH,
      right: state.camera.x + state.canvasWidth * 0.5 / scale + tileW,
      bottom: state.camera.y + state.canvasHeight * 0.5 / scale + tileH,
    };
    const preloadBounds = mapPreloadBounds(visibleBounds, tileW, tileH);
    const visibleChunkKeys = new Set();

    for (let layerIndex = 0; layerIndex < map.layers.length; layerIndex += 1) {
      const layer = map.layers[layerIndex];
      if (!layer.width || !layer.height || !layer.tiles || layer.tiles.length <= 0) continue;
      const chunkBounds = normalizeChunkBounds(chunkBoundsForWorldBounds(visibleBounds, tileW, tileH, profile.span));

      for (let chunkY = chunkBounds.minY; chunkY <= chunkBounds.maxY; chunkY += 1) {
        for (let chunkX = chunkBounds.minX; chunkX <= chunkBounds.maxX; chunkX += 1) {
          visibleChunkKeys.add(mapChunkKey(layerIndex, chunkX, chunkY, profile));
        }
      }
    }
    map.visibleChunkKeys = visibleChunkKeys;

    for (let layerIndex = 0; layerIndex < map.layers.length; layerIndex += 1) {
      const layer = map.layers[layerIndex];
      if (!layer.width || !layer.height || !layer.tiles || layer.tiles.length <= 0) continue;
      const chunkBounds = normalizeChunkBounds(chunkBoundsForWorldBounds(visibleBounds, tileW, tileH, profile.span));
      queueMapChunksForBounds(map, layer, layerIndex, preloadBounds, profile);

      for (let chunkY = chunkBounds.minY; chunkY <= chunkBounds.maxY; chunkY += 1) {
        for (let chunkX = chunkBounds.minX; chunkX <= chunkBounds.maxX; chunkX += 1) {
          const chunk = getMapChunk(map, layer, layerIndex, chunkX, chunkY, { buildNow: false, profile });
          if (!chunk || chunk.empty) continue;
          const pos = worldToScreen({ x: chunk.worldX, y: chunk.worldY });
          const bleed = 0.75;
          ctx.drawImage(
            chunk.canvas,
            pos.x - bleed,
            pos.y - bleed,
            chunk.width * scale + bleed * 2,
            chunk.height * scale + bleed * 2,
          );
        }
      }
    }
  }

  function drawMapDecorations() {
    const map = state.map;
    if (!map?.decorations?.length) return;

    const scale = worldScale();
    const visibleBounds = {
      left: state.camera.x - state.canvasWidth * 0.5 / scale,
      top: state.camera.y - state.canvasHeight * 0.5 / scale,
      right: state.camera.x + state.canvasWidth * 0.5 / scale,
      bottom: state.camera.y + state.canvasHeight * 0.5 / scale,
    };

    for (const decor of map.decorations) {
      if (!imageReady(decor.image)) continue;
      if (!rectsOverlap(decor, visibleBounds)) continue;

      const pos = worldToScreen({ x: decor.x, y: decor.y });
      const frame = decorationFrame(decor);
      ctx.save();
      ctx.globalAlpha *= decor.opacity;
      if (decor.rotation) {
        ctx.translate(pos.x + decor.width * scale * 0.5, pos.y + decor.height * scale * 0.5);
        ctx.rotate(decor.rotation);
        drawDecorationImage(decor, frame, -decor.width * scale * 0.5, -decor.height * scale * 0.5, decor.width * scale, decor.height * scale);
      } else {
        drawDecorationImage(decor, frame, pos.x, pos.y, decor.width * scale, decor.height * scale);
      }
      ctx.restore();
    }
  }

  function decorationFrame(decor) {
    if (!decor || decor.frameCount <= 1 || decor.frameWidth <= 0 || decor.frameHeight <= 0) return null;
    const index = Math.floor(performance.now() / decor.frameMs) % decor.frameCount;
    return {
      sx: (index % decor.frameColumns) * decor.frameWidth,
      sy: Math.floor(index / decor.frameColumns) * decor.frameHeight,
      sw: decor.frameWidth,
      sh: decor.frameHeight,
    };
  }

  function drawDecorationImage(decor, frame, x, y, width, height) {
    if (frame) ctx.drawImage(decor.image, frame.sx, frame.sy, frame.sw, frame.sh, x, y, width, height);
    else ctx.drawImage(decor.image, x, y, width, height);
  }

  function drawTileDebugGrid(scale) {
    const map = state.map;
    if (!map || !map.layers) return;

    const tileW = map.tileWidth;
    const tileH = map.tileHeight;
    if (tileW <= 0 || tileH <= 0) return;

    const width = (map.width || map.layers[0]?.width || 0) * tileW;
    const height = (map.height || map.layers[0]?.height || 0) * tileH;
    if (width <= 0 || height <= 0) return;

    const worldLeft = state.camera.x - state.canvasWidth * 0.5 / scale;
    const worldTop = state.camera.y - state.canvasHeight * 0.5 / scale;
    const worldRight = state.camera.x + state.canvasWidth * 0.5 / scale;
    const worldBottom = state.camera.y + state.canvasHeight * 0.5 / scale;
    const minX = Math.max(0, Math.floor(worldLeft / tileW));
    const minY = Math.max(0, Math.floor(worldTop / tileH));
    const maxX = Math.min(Math.ceil(width / tileW), Math.ceil(worldRight / tileW));
    const maxY = Math.min(Math.ceil(height / tileH), Math.ceil(worldBottom / tileH));

    ctx.save();
    ctx.lineWidth = 1;
    ctx.strokeStyle = "rgba(98, 116, 138, 0.28)";
    ctx.beginPath();
    for (let x = minX; x <= maxX; x += 1) {
      const sx = worldToScreen({ x: x * tileW, y: 0 }).x;
      ctx.moveTo(sx, 0);
      ctx.lineTo(sx, state.canvasHeight);
    }
    for (let y = minY; y <= maxY; y += 1) {
      const sy = worldToScreen({ x: 0, y: y * tileH }).y;
      ctx.moveTo(0, sy);
      ctx.lineTo(state.canvasWidth, sy);
    }
    ctx.stroke();
    ctx.restore();
  }

  function getMapChunk(map, layer, layerIndex, chunkX, chunkY, options = {}) {
    const buildNow = options.buildNow !== false;
    const profile = normalizeMapChunkProfile(options.profile || mapChunkProfile(1));
    if (!map.chunkCache) map.chunkCache = new Map();
    const key = mapChunkKey(layerIndex, chunkX, chunkY, profile);
    let chunk = map.chunkCache.get(key);
    if (!chunk) {
      if (!buildNow) {
        queueMapChunkBuild(map, layer, layerIndex, chunkX, chunkY, profile);
        return null;
      }
      chunk = buildMapChunk(map, layer, layerIndex, chunkX, chunkY, profile);
      if (!chunk) return null;
      map.chunkCache.set(key, chunk);
      pruneMapChunkCache(map);
    }
    chunk.lastUsed = ++map.chunkUseTick;
    return chunk;
  }

  function queueMapChunksForBounds(map, layer, layerIndex, bounds, profile) {
    const chunkBounds = normalizeChunkBounds(chunkBoundsForWorldBounds(bounds, map.tileWidth, map.tileHeight, profile.span));
    for (let chunkY = chunkBounds.minY; chunkY <= chunkBounds.maxY; chunkY += 1) {
      for (let chunkX = chunkBounds.minX; chunkX <= chunkBounds.maxX; chunkX += 1) {
        queueMapChunkBuild(map, layer, layerIndex, chunkX, chunkY, profile);
      }
    }
  }

  function queueMapChunkBuild(map, layer, layerIndex, chunkX, chunkY, profile = mapChunkProfile(1)) {
    const chunkProfile = normalizeMapChunkProfile(profile);
    if (!map.chunkCache) map.chunkCache = new Map();
    if (!map.chunkBuildQueue) map.chunkBuildQueue = [];
    if (!map.chunkBuildQueued) map.chunkBuildQueued = new Set();

    const key = mapChunkKey(layerIndex, chunkX, chunkY, chunkProfile);
    if (map.chunkCache.has(key) || map.chunkBuildQueued.has(key)) return;

    map.chunkBuildQueued.add(key);
    const job = { key, layer, layerIndex, chunkX, chunkY, profile: chunkProfile };
    if (map.visibleChunkKeys?.has(key)) map.chunkBuildQueue.unshift(job);
    else map.chunkBuildQueue.push(job);
    scheduleMapChunkBuildPump(map);
  }

  function scheduleMapChunkBuildPump(map) {
    if (!map || map.chunkBuildPumpScheduled) return;
    map.chunkBuildPumpScheduled = true;

    const run = () => {
      map.chunkBuildPumpScheduled = false;
      pumpMapChunkBuildQueue(map);
    };
    if (window.requestIdleCallback) window.requestIdleCallback(run, { timeout: 80 });
    else requestAnimationFrame(run);
  }

  function pumpMapChunkBuildQueue(map) {
    if (!map || state.map !== map || !map.chunkBuildQueue?.length) return;

    let builtAny = false;
    const started = performance.now();
    while (map.chunkBuildQueue.length && performance.now() - started < mapPrewarmBudgetMs) {
      const job = map.chunkBuildQueue.shift();
      map.chunkBuildQueued?.delete(job.key);
      if (map.chunkCache?.has(job.key)) continue;

      const chunk = buildMapChunk(map, job.layer, job.layerIndex, job.chunkX, job.chunkY, job.profile);
      if (!chunk) continue;
      map.chunkCache.set(job.key, chunk);
      pruneMapChunkCache(map);
      builtAny = true;
    }

    if (builtAny) requestDraw();
    if (map.chunkBuildQueue.length) scheduleMapChunkBuildPump(map);
  }

  return {
    drawGrid,
    loadMap,
  };
}

async function loadMapDecorations(tmj, mapPath, token) {
  const decorations = [];
  const loadPromises = [];

  for (const layer of tmj.layers || []) {
    if (layer.type !== "objectgroup") continue;
    const layerName = String(layer.name || "").toLowerCase();
    if (layerName !== "img" && layerName !== "images" && layerName !== "decor") continue;

    const layerOpacity = Number.isFinite(layer.opacity) ? layer.opacity : 1;
    for (const obj of layer.objects || []) {
      if (obj.visible === false) continue;
      const imagePath = objectProperty(obj, "image") || objectProperty(obj, "src");
      if (!imagePath) continue;

      const width = Math.max(1, Number(obj.width) || 1);
      const height = Math.max(1, Number(obj.height) || 1);
      const anchor = String(objectProperty(obj, "anchor") || "top-left").toLowerCase();
      const x = Number(obj.x) || 0;
      const y = Number(obj.y) || 0;
      const decor = {
        image: new Image(),
        src: joinAssetPath(mapPath, imagePath),
        x: anchor === "center" ? x - width * 0.5 : x,
        y: anchor === "center" ? y - height * 0.5 : y,
        width,
        height,
        frameWidth: objectNumberProperty(obj, "frame_width", 0),
        frameHeight: objectNumberProperty(obj, "frame_height", 0),
        frameCount: Math.max(1, Math.floor(objectNumberProperty(obj, "frame_count", 1))),
        frameColumns: Math.max(1, Math.floor(objectNumberProperty(obj, "frame_columns", 1))),
        frameMs: Math.max(1, objectNumberProperty(obj, "frame_ms", 100)),
        rotation: ((Number(obj.rotation) || 0) * Math.PI) / 180,
        opacity: clamp(layerOpacity * objectNumberProperty(obj, "opacity", 1), 0, 1),
      };
      loadPromises.push(loadMapDecorationImage(decor.image, decor.src).then((ok) => {
        if (!ok && token === state.mapLoadToken) decor.image.failed = true;
      }));
      decorations.push(decor);
    }
  }

  await Promise.all(loadPromises);
  decorations.sort((a, b) => (a.y + a.height) - (b.y + b.height));
  return decorations;
}

async function loadMapDecorationImage(image, src) {
  return new Promise((resolve) => {
    image.decoding = "async";
    image.onload = async () => {
      if (image.decode) {
        try {
          await image.decode();
        } catch {
          // Animated formats can reject decode while still being drawable.
        }
      }
      resolve(true);
    };
    image.onerror = () => resolve(false);
    image.src = src;
  });
}

function objectProperty(obj, name) {
  for (const prop of obj?.properties || []) {
    if (String(prop?.name || "").toLowerCase() === name) return prop.value;
  }
  return undefined;
}

function objectNumberProperty(obj, name, fallback = 0) {
  const value = objectProperty(obj, name);
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function rectsOverlap(a, b) {
  return a.x <= b.right && a.x + a.width >= b.left && a.y <= b.bottom && a.y + a.height >= b.top;
}

function normalizeMapPath(path) {
  const normalized = String(path || "").replace(/\\/g, "/").replace(/^\/+/, "");
  if (!normalized) return "";
  if (normalized.startsWith("data/")) return `/${normalized}`;
  if (normalized.startsWith("maps/") || normalized.startsWith("tiles/")) return `/data/${normalized}`;
  return `/data/maps/${normalized}`;
}

function joinAssetPath(basePath, relativePath) {
  const relative = String(relativePath || "").replace(/\\/g, "/");
  if (!relative) return "";
  if (relative.startsWith("/") || /^[a-z]+:\/\//i.test(relative)) return relative;
  const base = String(basePath || "").replace(/\\/g, "/");
  const parts = base.split("/");
  parts.pop();
  for (const part of relative.split("/")) {
    if (!part || part === ".") continue;
    if (part === "..") parts.pop();
    else parts.push(part);
  }
  return parts.join("/");
}

function imageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

function canonicalTileGid(raw) {
  return (raw >>> 0) & 0x1fffffff;
}

function tileHasCollision(tile) {
  if (tile?.objectgroup?.objects?.length > 0) return true;
  for (const prop of tile?.properties || []) {
    const name = String(prop?.name || "").toLowerCase();
    if ((name === "collision" || name === "collidable" || name === "solid" || name === "wall") && prop.value !== false)
      return true;
  }
  return false;
}

function layerLooksCollidable(layer) {
  const name = String(layer?.name || "").toLowerCase();
  return /\b(wall|collision|collidable|solid|block)\b/.test(name);
}

function buildCollisionTiles(layers, collidableGids) {
  const byTile = new Map();
  for (const layer of layers || []) {
    if (!layer.width || !layer.height || !layer.tiles?.length) continue;
    const layerCollidable = layerLooksCollidable(layer);
    for (let y = 0; y < layer.height; y += 1) {
      for (let x = 0; x < layer.width; x += 1) {
        const gid = canonicalTileGid(layer.tiles[y * layer.width + x] || 0);
        if (!gid) continue;
        if (!layerCollidable && !collidableGids.has(gid)) continue;
        byTile.set(`${x}:${y}`, { x, y });
      }
    }
  }
  return [...byTile.values()];
}

function transformTilePoint(x, y, width, height, flipH, flipV, flipD) {
  if (flipD) {
    const oldX = x;
    x = y;
    y = oldX;
  }
  if (flipH) x = width - x;
  if (flipV) y = height - y;
  return { x, y };
}

function drawTileImage(image, pos, width, height, raw, targetCtx) {
  const flipH = (raw & 0x80000000) !== 0;
  const flipV = (raw & 0x40000000) !== 0;
  const flipD = (raw & 0x20000000) !== 0;

  targetCtx.save();
  targetCtx.translate(pos.x, pos.y);
  if (flipH || flipV || flipD) {
    const origin = transformTilePoint(0, 0, width, height, flipH, flipV, flipD);
    const xAxis = transformTilePoint(width, 0, width, height, flipH, flipV, flipD);
    const yAxis = transformTilePoint(0, height, width, height, flipH, flipV, flipD);
    targetCtx.transform(
      (xAxis.x - origin.x) / width,
      (xAxis.y - origin.y) / width,
      (yAxis.x - origin.x) / height,
      (yAxis.y - origin.y) / height,
      origin.x,
      origin.y,
    );
  }
  const bleed = 0.75;
  targetCtx.drawImage(image, -bleed, -bleed, width + bleed * 2, height + bleed * 2);
  targetCtx.restore();
}

function createMapRenderCanvas(width, height) {
  if (typeof OffscreenCanvas !== "undefined") return new OffscreenCanvas(width, height);
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  return canvas;
}

function pruneMapChunkCache(map) {
  if (!map?.chunkCache || map.chunkCache.size <= mapChunkCacheLimit) return;
  const protectedKeys = map.visibleChunkKeys || new Set();
  const dynamicLimit = Math.max(mapChunkCacheLimit, protectedKeys.size + 128);
  if (map.chunkCache.size <= dynamicLimit) return;
  const entries = [...map.chunkCache.entries()].sort((a, b) => (a[1].lastUsed || 0) - (b[1].lastUsed || 0));
  let removeCount = Math.max(0, entries.length - dynamicLimit);
  for (const [key] of entries) {
    if (removeCount <= 0) break;
    if (protectedKeys.has(key)) continue;
    map.chunkCache.delete(key);
    removeCount -= 1;
  }
}

function mapChunkProfile(scale) {
  if (!Number.isFinite(scale) || scale <= 0) return { span: mapChunkTileSpan, renderScale: 1, label: "4@1x" };
  if (scale < 0.004) return { span: 128, renderScale: 1 / 64, label: "128@1/64" };
  if (scale < 0.009) return { span: 64, renderScale: 1 / 32, label: "64@1/32" };
  if (scale < 0.012) return { span: 32, renderScale: 1 / 16, label: "32@1/16" };
  if (scale < 0.026) return { span: 16, renderScale: 1 / 8, label: "16@1/8" };
  if (scale < 0.06) return { span: 8, renderScale: 1 / 4, label: "8@1/4" };
  if (scale < 0.12) return { span: 4, renderScale: 1 / 2, label: "4@1/2" };
  return { span: mapChunkTileSpan, renderScale: 1, label: "4@1x" };
}

function normalizeMapChunkProfile(profile) {
  const span = Math.max(1, Math.floor(profile?.span || mapChunkTileSpan));
  const renderScale = clamp(Number(profile?.renderScale) || 1, 1 / 64, 1);
  const label = profile?.label || `${span}@${Math.round(renderScale * 1024)}/1024`;
  return { span, renderScale, label };
}

function mapChunkProfileKey(profile) {
  const normalized = normalizeMapChunkProfile(profile);
  return `${normalized.span}:${Math.round(normalized.renderScale * 1024)}`;
}

function mapChunkKey(layerIndex, chunkX, chunkY, profile) {
  return `${mapChunkProfileKey(profile)}:${layerIndex}:${chunkX}:${chunkY}`;
}

function buildMapChunk(map, layer, layerIndex, chunkX, chunkY, profile = mapChunkProfile(1)) {
  const chunkProfile = normalizeMapChunkProfile(profile);
  const tileW = map.tileWidth;
  const tileH = map.tileHeight;
  const startX = chunkX * chunkProfile.span;
  const startY = chunkY * chunkProfile.span;
  const widthTiles = chunkProfile.span;
  const heightTiles = chunkProfile.span;
  const width = Math.max(1, widthTiles * tileW);
  const height = Math.max(1, heightTiles * tileH);
  const pixelTileW = Math.max(1, tileW * chunkProfile.renderScale);
  const pixelTileH = Math.max(1, tileH * chunkProfile.renderScale);
  const pixelWidth = Math.max(1, Math.ceil(width * chunkProfile.renderScale));
  const pixelHeight = Math.max(1, Math.ceil(height * chunkProfile.renderScale));
  const maxTileX = Math.max(0, layer.width - 1);
  const maxTileY = Math.max(0, layer.height - 1);
  const drawTiles = [];

  for (let localY = 0; localY < heightTiles; localY += 1) {
    const tileY = startY + localY;
    const sampleY = clamp(tileY, 0, maxTileY);
    for (let localX = 0; localX < widthTiles; localX += 1) {
      const tileX = startX + localX;
      const sampleX = clamp(tileX, 0, maxTileX);
      const raw = layer.tiles[sampleY * layer.width + sampleX] >>> 0;
      const gid = canonicalTileGid(raw);
      if (!gid) continue;

      const image = map.tileImages.get(gid);
      if (!imageReady(image)) return null;
      drawTiles.push({ image, raw, x: localX * pixelTileW, y: localY * pixelTileH });
    }
  }

  const key = mapChunkKey(layerIndex, chunkX, chunkY, chunkProfile);
  if (!drawTiles.length) {
    return {
      canvas: null,
      worldX: startX * tileW,
      worldY: startY * tileH,
      width,
      height,
      empty: true,
      lastUsed: ++map.chunkUseTick,
      key,
      profileKey: mapChunkProfileKey(chunkProfile),
    };
  }

  const canvas = createMapRenderCanvas(pixelWidth, pixelHeight);
  const chunkCtx = canvas.getContext("2d");
  if (!chunkCtx) return null;

  for (const tile of drawTiles) {
    drawTileImage(tile.image, { x: tile.x, y: tile.y }, pixelTileW, pixelTileH, tile.raw, chunkCtx);
  }

  return {
    canvas,
    worldX: startX * tileW,
    worldY: startY * tileH,
    width,
    height,
    empty: false,
    lastUsed: ++map.chunkUseTick,
    key,
    profileKey: mapChunkProfileKey(chunkProfile),
  };
}

function resetMapChunkBuildQueueForProfile(map, profile) {
  const profileKey = mapChunkProfileKey(profile);
  if (map.activeChunkProfileKey === profileKey) return;
  if (!map.activeChunkProfileKey) {
    map.activeChunkProfileKey = profileKey;
    return;
  }
  map.activeChunkProfileKey = profileKey;
  map.chunkBuildQueue = [];
  map.chunkBuildQueued = new Set();
}

function chunkBoundsForWorldBounds(bounds, tileW, tileH, span) {
  return {
    minX: Math.floor(Math.floor(bounds.left / tileW) / span),
    minY: Math.floor(Math.floor(bounds.top / tileH) / span),
    maxX: Math.floor(Math.ceil(bounds.right / tileW) / span),
    maxY: Math.floor(Math.ceil(bounds.bottom / tileH) / span),
  };
}

function normalizeChunkBounds(bounds) {
  return {
    minX: Math.min(bounds.minX, bounds.maxX),
    minY: Math.min(bounds.minY, bounds.maxY),
    maxX: Math.max(bounds.minX, bounds.maxX),
    maxY: Math.max(bounds.minY, bounds.maxY),
  };
}

function mapPreloadBounds(visibleBounds, tileW, tileH) {
  const last = state.map?.lastPreloadCamera || state.camera;
  const dx = state.camera.x - last.x;
  const dy = state.camera.y - last.y;
  state.map.lastPreloadCamera = { x: state.camera.x, y: state.camera.y };

  const owner = state.entities.get(state.ownerEntityId);
  const snapshotMove = owner?.snapshotMove || 0;
  const cameraMove = Math.hypot(dx, dy);
  const speedHint = Math.max(cameraMove, snapshotMove);
  const tileSize = Math.max(1, Math.min(tileW, tileH));
  const sidePadding = clamp(tileSize + speedHint * 4, tileSize, Math.max(tileSize, state.viewRadius * 0.3));
  const lead = clamp(tileSize + speedHint * 12, tileSize * 1.5, Math.max(tileSize * 2, state.viewRadius * 0.85));
  const length = Math.hypot(dx, dy);
  const nx = length > 0.001 ? dx / length : 0;
  const ny = length > 0.001 ? dy / length : 0;

  return {
    left: visibleBounds.left - sidePadding + Math.min(0, nx * lead),
    top: visibleBounds.top - sidePadding + Math.min(0, ny * lead),
    right: visibleBounds.right + sidePadding + Math.max(0, nx * lead),
    bottom: visibleBounds.bottom + sidePadding + Math.max(0, ny * lead),
  };
}

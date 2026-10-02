import { clamp } from "./protocol.js";
import { dom, state as appState } from "./app_context.js";
import { clientRuntimeConfig } from "./client_config.js";
import { t } from "./i18n.js";
import { createMapChunkCache } from "./map_chunk_cache.js";
import { loadVisualImage, whenVisualReady } from "./visual_assets.js";

const mapPrewarmBudgetMs = 1.5;
const mapOverviewMaxDimensionPx = 2048;
const mapTransitionDurationMs = 360;
const {
  minimapTileSubdivisions,
  minimapTextureSampleSizePx,
  minimapTextureCoverageThreshold,
  minimapCollisionSamplesPerAxis,
  minimapCollisionCoverageThreshold,
} = clientRuntimeConfig;
const checkpointObjectTypes = new Set([
  "checkpoint",
  "respawn_area",
  "new_players",
]);
const manualMinimapTileMatrices = Object.freeze({
  l: Object.freeze([
    Object.freeze([1, 0]),
    Object.freeze([1, 0]),
  ]),
  tl: Object.freeze([
    Object.freeze([1, 0]),
    Object.freeze([0, 0]),
  ]),
  tri: Object.freeze([
    Object.freeze([1, 0]),
    Object.freeze([1, 1]),
  ]),
  c: Object.freeze([
    Object.freeze([1, 1]),
    Object.freeze([1, 1]),
  ]),
});

export function createMapRenderer({
  ctx = dom.ctx,
  state = appState,
  defaultMapName = "garden.tmj",
  addConsoleLine = () => {},
  requestDraw = () => {},
  setLoadingVisible = () => {},
  worldScale,
  worldToScreen,
} = {}) {
  let mapTransition = null;
  let transitionCanvas = null;

  async function loadMap(mapName) {
    const path = normalizeMapPath(mapName);
    if (!path) return null;
    if (state.map?.path === path) {
      if (state.mapLoadPromise && state.mapLoadPath !== path) {
        state.mapLoadToken += 1;
        state.mapLoadPromise = null;
        state.mapLoadPath = "";
        setLoadingVisible(false);
      }
      return state.map;
    }
    if (path === state.mapLoadPath && state.mapLoadPromise)
      return state.mapLoadPromise;

    const token = ++state.mapLoadToken;
    state.mapLoadPath = path;
    state.mapDisplayName = String(mapName || path);
    addConsoleLine(t("status.mapLoading", { path }));
    setLoadingVisible(true);
    const loadPromise = loadMapData(path, token);
    state.mapLoadPromise = loadPromise;
    try {
      return await loadPromise;
    } finally {
      if (token === state.mapLoadToken) {
        state.mapLoadPromise = null;
        state.mapLoadPath = "";
        setLoadingVisible(false);
      }
    }
  }

  async function loadMapData(path, token) {
    try {
      const response = await fetch(path, { cache: "no-cache" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const tmj = await response.json();
      const tileImages = new Map();
      const minimapTileDefinitions = new Map();
      const collisionDefs = new Map();
      const tileLoadPromises = [];
      const failedTiles = [];
      for (const tilesetRef of tmj.tilesets || []) {
        const tilesetPath = joinAssetPath(path, tilesetRef.source || "");
        let tileset = tilesetRef;
        if (tilesetPath) {
          const tsjResponse = await fetch(tilesetPath, { cache: "no-cache" });
          if (!tsjResponse.ok)
            throw new Error(`tileset HTTP ${tsjResponse.status}`);
          tileset = await tsjResponse.json();
        }
        for (const tile of tileset.tiles || []) {
          const gid = (tilesetRef.firstgid || 1) + (tile.id || 0);
          const collisionDef = buildTileCollisionDefinition(tile, tileset);
          if (collisionDef) collisionDefs.set(gid, collisionDef);
          if (!tile.image) continue;
          const tileSrc = joinAssetPath(tilesetPath || path, tile.image);
          const image = loadVisualImage(tileSrc, { retry: true });
          minimapTileDefinitions.set(
            gid,
            parseMinimapTileDefinition(tile.image),
          );
          tileLoadPromises.push(
            whenVisualReady(image).then((ok) => {
              if (!ok) failedTiles.push(tileSrc);
              if (!ok && token === state.mapLoadToken)
                addConsoleLine(
                  t("status.tileImageFailed", { path: tileSrc }),
                  "error",
                );
            }),
          );
          tileImages.set(gid, image);
        }
      }
      await Promise.all(tileLoadPromises);
      if (failedTiles.length) throw new Error(`Tile preparation failed: ${failedTiles.join(", ")}`);
      const minimapSubdivisions = Math.max(
        1,
        Math.floor(minimapTileSubdivisions),
      );
      const minimapTileMasks = buildMinimapTileMasks(
        tileImages,
        minimapTileDefinitions,
        collisionDefs,
        minimapSubdivisions,
      );

      const layers = [];
      for (const layer of tmj.layers || []) {
        if (layer.type !== "tilelayer") continue;
        layers.push({
          name: layer.name || "",
          visible: layer.visible !== false,
          opacity: Number.isFinite(layer.opacity) ? clamp(layer.opacity, 0, 1) : 1,
          width: layer.width || tmj.width || 0,
          height: layer.height || tmj.height || 0,
          tiles: await decodeTileLayer(layer),
        });
      }
      const decorations = await loadMapDecorations(tmj, path, token, state);
      const mapWidth = tmj.width || layers[0]?.width || 0;
      const mapHeight = tmj.height || layers[0]?.height || 0;
      const tileWidth = tmj.tilewidth || 512;
      const tileHeight = tmj.tileheight || 512;
      const collisionWalls = buildMapCollisionWalls(tmj, layers, collisionDefs);
      const minimapCollisionCells = buildMinimapCollisionCells(
        collisionWalls,
        mapWidth,
        mapHeight,
        tileWidth,
        tileHeight,
        minimapSubdivisions,
      );
      const minimapCellColumns = mapWidth * minimapSubdivisions;
      const minimapCollisionCellIndices = new Set(
        minimapCollisionCells.map(
          (cell) => cell.y * minimapCellColumns + cell.x,
        ),
      );
      const checkpointZones = buildMapCheckpointZones(tmj);
      const collisionGids = new Set(collisionDefs.keys());
      const spawnDifficultyCells = buildSpawnDifficultyCells(
        tmj,
        buildMapSpawnZones(tmj),
        minimapCollisionCellIndices,
        mapWidth,
        mapHeight,
        minimapSubdivisions,
      );

      if (token !== state.mapLoadToken) return null;
      const map = {
        path,
        width: mapWidth,
        height: mapHeight,
        tileWidth,
        tileHeight,
        backgroundColor: tmj.backgroundcolor || "#1ea761",
        layers,
        decorations,
        tileImages,
        minimapTileDefinitions,
        collisionGids,
        collisionWalls,
        minimapSubdivisions,
        minimapTileMasks,
        minimapCollisionCells,
        checkpointZones,
        spawnDifficultyCells,
        collisionWallBuckets: buildCollisionWallBuckets(
          collisionWalls,
          tileWidth,
          tileHeight,
        ),
        chunkCache: new Map(),
        chunkBuildQueue: [],
      };
      await buildFullMapOverview(map, token);
      if (token !== state.mapLoadToken) { releaseMapRasters(map); return null; }

      map.rasterCache = createMapChunkCache({ map, state, worldScale, worldToScreen,
        createCanvas: createMapRenderCanvas, drawTile: drawTileImage, requestDraw,
        isActive: () => token === state.mapLoadToken || state.map === map });
      // Publish only after the initial visible region has complete layers,
      // not merely an 8px-per-tile overview. Ordinary movement remains async.
      await map.rasterCache.warm();
      if (token !== state.mapLoadToken) { releaseMapRasters(map); return null; }

      const previousMap = state.map;
      if (mapTransition?.from && mapTransition.from !== previousMap)
        releaseMapRasters(mapTransition.from);
      state.map = map;
      state.mapName = path;
      // Do not allocate an extra hundreds-of-MB framebuffer at 4K/4x DPR.
      // In that case publish the already-prepared map sharply instead.
      const transitionBytes = (state.canvasWidth || 1) * (state.canvasHeight || 1) * (state.dpr || 1) ** 2 * 4;
      if (previousMap && previousMap !== map && transitionBytes <= 32 * 1024 * 1024) {
        mapTransition = {
          from: previousMap,
          to: map,
          startedAt: performance.now(),
        };
      } else {
        if (previousMap && previousMap !== map) releaseMapRasters(previousMap);
        mapTransition = null;
      }
      addConsoleLine(
        t("status.mapLoaded", {
          path,
          layers: layers.length,
          tiles: tileImages.size,
        }),
      );
      requestDraw();
      return map;
    } catch (error) {
      if (token === state.mapLoadToken) {
        addConsoleLine(
          t("status.mapLoadFailed", { path, error: error.message }),
          "error",
        );
      }
      return null;
    }
  }



  async function decodeTileLayer(layer) {
    if (Array.isArray(layer.data))
      return layer.data.map((value) => Number(value) || 0);
    if (typeof layer.data !== "string") return [];

    const compressed = Uint8Array.from(atob(layer.data), (char) =>
      char.charCodeAt(0),
    );
    let bytes = compressed;
    if (layer.compression === "gzip") {
      if (typeof DecompressionStream === "undefined")
        throw new Error("gzip map layers need DecompressionStream");
      const stream = new Blob([compressed])
        .stream()
        .pipeThrough(new DecompressionStream("gzip"));
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

  async function buildFullMapOverview(map, token) {
    const worldWidth = Math.max(1, map.width * map.tileWidth);
    const worldHeight = Math.max(1, map.height * map.tileHeight);
    const overviewScale = Math.min(
      1,
      mapOverviewMaxDimensionPx / Math.max(worldWidth, worldHeight),
    );
    const canvas = createMapRenderCanvas(
      Math.max(1, Math.ceil(worldWidth * overviewScale)),
      Math.max(1, Math.ceil(worldHeight * overviewScale)),
    );
    const overviewCtx = canvas.getContext("2d");
    if (!overviewCtx) throw new Error("map overview canvas unavailable");

    const tileWidth = map.tileWidth * overviewScale;
    const tileHeight = map.tileHeight * overviewScale;
    if (!map.overviewTileImagesCache) {
      map.overviewTileImagesCache = buildOverviewTileImages(
        map.tileImages,
        tileWidth,
        tileHeight,
      );
    }
    const overviewTileImages = map.overviewTileImagesCache;
    overviewCtx.imageSmoothingEnabled = true;
    let sliceStarted = performance.now();

    for (const layer of map.layers || []) {
      if (layer.visible === false || !layer?.width || !layer?.height || !layer.tiles?.length) continue;
      overviewCtx.globalAlpha = layer.opacity ?? 1;
      for (let tileY = 0; tileY < layer.height; tileY += 1) {
        for (let tileX = 0; tileX < layer.width; tileX += 1) {
          const raw = layer.tiles[tileY * layer.width + tileX] >>> 0;
          const image = overviewTileImages.get(canonicalTileGid(raw));
          if (image) {
            drawTileImage(
              image,
              { x: tileX * tileWidth, y: tileY * tileHeight },
              tileWidth,
              tileHeight,
              raw,
              overviewCtx,
            );
          }

          if (performance.now() - sliceStarted < mapPrewarmBudgetMs) continue;
          await yieldMapPreparation();
          if (token !== state.mapLoadToken) return;
          sliceStarted = performance.now();
        }
      }
    }

    map.overviewCanvas = canvas;
    map.overviewWorldWidth = worldWidth;
    map.overviewWorldHeight = worldHeight;
    map.overviewScale = overviewScale;
  }

  function buildOverviewTileImages(tileImages, tileWidth, tileHeight) {
    const rasterWidth = Math.max(1, Math.ceil(tileWidth));
    const rasterHeight = Math.max(1, Math.ceil(tileHeight));
    const result = new Map();
    for (const [gid, image] of tileImages || []) {
      if (!imageReady(image)) continue;
      const canvas = createMapRenderCanvas(rasterWidth, rasterHeight);
      const rasterCtx = canvas.getContext("2d");
      if (!rasterCtx) continue;
      rasterCtx.imageSmoothingEnabled = true;
      rasterCtx.drawImage(image, 0, 0, rasterWidth, rasterHeight);
      result.set(gid, canvas);
    }
    return result;
  }

  function yieldMapPreparation() {
    return new Promise((resolve) => window.setTimeout(resolve, 0));
  }

  function drawGrid(now = performance.now()) {
    const transition = mapTransition;
    let progress = 1;
    if (transition) {
      const linearProgress = clamp(
        (now - transition.startedAt) / mapTransitionDurationMs,
        0,
        1,
      );
      progress = linearProgress * linearProgress * (3 - 2 * linearProgress);
      if (progress >= 1) {
        releaseMapRasters(transition.from);
        mapTransition = null;
        if (transitionCanvas) transitionCanvas.width = transitionCanvas.height = 0;
        transitionCanvas = null;
      }
    }

    // Match the original Garden green more closely. This is only the
    // fallback under loaded tile layers; the map assets remain authoritative.
    ctx.fillStyle = state.map ? "#1ea761" : "#edf3f7";
    ctx.fillRect(0, 0, state.canvasWidth, state.canvasHeight);

    if (transition && progress < 1) {
      // The old map is already prepared, so only reuse its overview/chunks;
      // all chunk construction remains reserved for the new authoritative
      // map. This keeps the crossfade smooth even for large maps.
      drawMapLayer(transition.from, 1, false, now);
      drawMapLayer(transition.to, progress, true, now);
    } else {
      drawMapLayer(state.map, 1, true, now);
    }

    if (!state.map) {
      ctx.save();
      ctx.font = "13px Segoe UI, Microsoft YaHei, sans-serif";
      ctx.textAlign = "left";
      ctx.textBaseline = "top";
      ctx.fillStyle = "rgba(57, 70, 88, 0.72)";
      ctx.fillText(
        t("status.mapLoading", {
          path: state.mapDisplayName || defaultMapName,
        }),
        14,
        72,
      );
      ctx.restore();
    }

  }

  function drawMapLayer(map, alpha, prepare, now) {
    if (!map || alpha <= 0) return;
    // Apply transition alpha once to the complete map, not once per layer.
    const output = ctx;
    if (alpha < 1) {
      const dpr = state.dpr || 1;
      const width = Math.max(1, Math.round(state.canvasWidth * dpr));
      const height = Math.max(1, Math.round(state.canvasHeight * dpr));
      if (!transitionCanvas) transitionCanvas = createMapRenderCanvas(width, height);
      if (transitionCanvas.width !== width) transitionCanvas.width = width;
      if (transitionCanvas.height !== height) transitionCanvas.height = height;
      ctx = transitionCanvas.getContext("2d");
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, state.canvasWidth, state.canvasHeight);
    }
    try {
      ctx.save();
      ctx.fillStyle = map.backgroundColor || "#1ea761";
      ctx.fillRect(0, 0, state.canvasWidth, state.canvasHeight);
      drawMapOverview(map, worldScale());
      map.rasterCache?.draw(ctx, now, prepare);
      drawMapDecorations(map);
      ctx.restore();
    } finally { ctx = output; }
    if (alpha < 1) {
      ctx.save(); ctx.globalAlpha *= alpha;
      ctx.drawImage(transitionCanvas, 0, 0, transitionCanvas.width, transitionCanvas.height,
        0, 0, state.canvasWidth, state.canvasHeight);
      ctx.restore();
    }
  }

  function drawMapOverview(map, scale) {
    if (!map?.overviewCanvas || scale <= 0) return;
    const halfWidth = state.canvasWidth / (2 * scale);
    const halfHeight = state.canvasHeight / (2 * scale);
    const left = clamp(state.camera.x - halfWidth, 0, map.overviewWorldWidth);
    const top = clamp(state.camera.y - halfHeight, 0, map.overviewWorldHeight);
    const right = clamp(state.camera.x + halfWidth, 0, map.overviewWorldWidth);
    const bottom = clamp(
      state.camera.y + halfHeight,
      0,
      map.overviewWorldHeight,
    );
    if (right <= left || bottom <= top) return;

    const pos = worldToScreen({ x: left, y: top });
    const overviewScale = map.overviewScale || 1;
    ctx.save();
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(
      map.overviewCanvas,
      left * overviewScale,
      top * overviewScale,
      (right - left) * overviewScale,
      (bottom - top) * overviewScale,
      pos.x,
      pos.y,
      (right - left) * scale,
      (bottom - top) * scale,
    );
    ctx.restore();
  }

  function drawMapDecorations(map = state.map) {
    if (!map?.decorations?.length) return;

    const scale = worldScale();
    const visibleBounds = {
      left: state.camera.x - (state.canvasWidth * 0.5) / scale,
      top: state.camera.y - (state.canvasHeight * 0.5) / scale,
      right: state.camera.x + (state.canvasWidth * 0.5) / scale,
      bottom: state.camera.y + (state.canvasHeight * 0.5) / scale,
    };

    for (const decor of map.decorations) {
      if (!imageReady(decor.image)) continue;
      if (!rectsOverlap(decor, visibleBounds)) continue;

      const pos = worldToScreen({ x: decor.x, y: decor.y });
      const frame = decorationFrame(decor);
      ctx.save();
      ctx.globalAlpha *= decor.opacity;
      if (decor.rotation) {
        ctx.translate(
          pos.x + decor.width * scale * 0.5,
          pos.y + decor.height * scale * 0.5,
        );
        ctx.rotate(decor.rotation);
        drawDecorationImage(
          decor,
          frame,
          -decor.width * scale * 0.5,
          -decor.height * scale * 0.5,
          decor.width * scale,
          decor.height * scale,
        );
      } else {
        drawDecorationImage(
          decor,
          frame,
          pos.x,
          pos.y,
          decor.width * scale,
          decor.height * scale,
        );
      }
      ctx.restore();
    }
  }

  function decorationFrame(decor) {
    if (
      !decor ||
      decor.frameCount <= 1 ||
      decor.frameWidth <= 0 ||
      decor.frameHeight <= 0
    )
      return null;
    const index =
      Math.floor(performance.now() / decor.frameMs) % decor.frameCount;
    return {
      sx: (index % decor.frameColumns) * decor.frameWidth,
      sy: Math.floor(index / decor.frameColumns) * decor.frameHeight,
      sw: decor.frameWidth,
      sh: decor.frameHeight,
    };
  }

  function drawDecorationImage(decor, frame, x, y, width, height) {
    if (frame)
      ctx.drawImage(
        decor.image,
        frame.sx,
        frame.sy,
        frame.sw,
        frame.sh,
        x,
        y,
        width,
        height,
      );
    else ctx.drawImage(decor.image, x, y, width, height);
  }


  function resolvePredictedCircleMotion(start, end, radius) {
    const map = state.map;
    if (!map?.collisionWalls?.length)
      return { pos: { x: end.x, y: end.y }, normals: [] };
    return resolveCircleMotion(
      map,
      start,
      end,
      Math.max(0, Number(radius) || 0),
    );
  }

  function applyTerrainDelta(layerIndex, row, col, gid) {
    const map = state.map;
    if (!map) return false;
    const layer = map.layers[layerIndex];
    if (!layer || !layer.tiles || !layer.width || !layer.height) return false;
    const index = row * layer.width + col;
    if (index < 0 || index >= layer.tiles.length) return false;
    if (layer.tiles[index] === gid) return false;
    layer.tiles[index] = gid;

    map.rasterCache?.invalidate(row, col);

    // If the new tile's image just became available, drop the raster cache so
    // the next overview rebuild includes it.
    if (gid && map.overviewTileImagesCache) {
      const canonicalGid = canonicalTileGid(gid);
      if (
        !map.overviewTileImagesCache.has(canonicalGid) &&
        imageReady(map.tileImages.get(canonicalGid))
      )
        map.overviewTileImagesCache = null;
    }

    // The fallback must change with the terrain, not show a removed wall
    // for another half second while a full-map rebuild is debounced.
    updateOverviewTile(map, row, col);
    requestDraw();
    return true;
  }

  function updateOverviewTile(map, row, col) {
    const target = map.overviewCanvas?.getContext("2d");
    if (!target) return;
    const width = map.tileWidth * map.overviewScale, height = map.tileHeight * map.overviewScale;
    const x = col * width, y = row * height;
    target.save(); target.beginPath(); target.rect(x, y, width, height); target.clip();
    target.clearRect(x, y, width, height);
    for (const layer of map.layers) {
      if (layer.visible === false || row >= layer.height || col >= layer.width) continue;
      const raw = layer.tiles[row * layer.width + col] >>> 0;
      const image = map.tileImages.get(canonicalTileGid(raw));
      if (!imageReady(image)) continue;
      target.globalAlpha = layer.opacity ?? 1;
      drawTileImage(image, { x, y }, width, height, raw, target);
    }
    target.restore();
  }

  function applyWallRemove(minPos, maxPos) {
    const map = state.map;
    if (!map || !map.collisionWalls) return false;
    const minX = Math.min(minPos.x, maxPos.x);
    const minY = Math.min(minPos.y, maxPos.y);
    const maxX = Math.max(minPos.x, maxPos.x);
    const maxY = Math.max(minPos.y, maxPos.y);
    const before = map.collisionWalls.length;
    map.collisionWalls = map.collisionWalls.filter(
      (wall) =>
        !(wall.maxX >= minX && wall.minX <= maxX && wall.maxY >= minY && wall.minY <= maxY),
    );
    if (map.collisionWalls.length === before) return false;
    map.collisionWallBuckets = buildCollisionWallBuckets(
      map.collisionWalls,
      map.tileWidth,
      map.tileHeight,
    );
    const minimapSubdivisions = map.minimapSubdivisions || 1;
    const minimapCellColumns = Math.max(1, map.width * minimapSubdivisions);
    map.minimapCollisionCells = buildMinimapCollisionCells(
      map.collisionWalls,
      map.width,
      map.height,
      map.tileWidth,
      map.tileHeight,
      minimapSubdivisions,
    );
    map.minimapCollisionCellIndices = new Set(
      map.minimapCollisionCells.map((cell) => cell.y * minimapCellColumns + cell.x),
    );
    requestDraw();
    return true;
  }

  return {
    drawGrid,
    loadMap,
    applyTerrainDelta,
    applyWallRemove,
    resolvePredictedCircleMotion,
    dispose() {
      state.mapLoadToken += 1;
      releaseMapRasters(state.map);
      if (mapTransition?.from !== state.map) releaseMapRasters(mapTransition?.from);
      state.map = null;
      state.mapLoadPromise = null;
      state.mapLoadPath = "";
      mapTransition = null;
      if (transitionCanvas) transitionCanvas.width = transitionCanvas.height = 0;
      transitionCanvas = null;
    },
  };
}

function releaseMapRasters(map) {
  if (!map) return;
  map.rasterCache?.dispose();
  if (map.overviewCanvas) map.overviewCanvas.width = map.overviewCanvas.height = 0;
  for (const canvas of map.overviewTileImagesCache?.values() || []) canvas.width = canvas.height = 0;
}

async function loadMapDecorations(tmj, mapPath, token, state = appState) {
  const decorations = [];
  const loadPromises = [];

  for (const layer of tmj.layers || []) {
    if (layer.type !== "objectgroup") continue;
    const layerName = String(layer.name || "").toLowerCase();
    if (layerName !== "img" && layerName !== "images" && layerName !== "decor")
      continue;

    const layerOpacity = Number.isFinite(layer.opacity) ? layer.opacity : 1;
    for (const obj of layer.objects || []) {
      if (obj.visible === false) continue;
      const imagePath =
        objectProperty(obj, "image") || objectProperty(obj, "src");
      if (!imagePath) continue;

      const width = Math.max(1, Number(obj.width) || 1);
      const height = Math.max(1, Number(obj.height) || 1);
      const anchor = String(
        objectProperty(obj, "anchor") || "top-left",
      ).toLowerCase();
      const x = Number(obj.x) || 0;
      const y = Number(obj.y) || 0;
      const src = joinAssetPath(mapPath, imagePath);
      const decor = {
        image: loadVisualImage(src, { retry: true }),
        src,
        x: anchor === "center" ? x - width * 0.5 : x,
        y: anchor === "center" ? y - height * 0.5 : y,
        width,
        height,
        frameWidth: objectNumberProperty(obj, "frame_width", 0),
        frameHeight: objectNumberProperty(obj, "frame_height", 0),
        frameCount: Math.max(
          1,
          Math.floor(objectNumberProperty(obj, "frame_count", 1)),
        ),
        frameColumns: Math.max(
          1,
          Math.floor(objectNumberProperty(obj, "frame_columns", 1)),
        ),
        frameMs: Math.max(1, objectNumberProperty(obj, "frame_ms", 100)),
        rotation: ((Number(obj.rotation) || 0) * Math.PI) / 180,
        opacity: clamp(
          layerOpacity * objectNumberProperty(obj, "opacity", 1),
          0,
          1,
        ),
      };
      loadPromises.push(
        whenVisualReady(decor.image).then((ok) => {
          if (!ok && token === state.mapLoadToken) decor.image.failed = true;
        }),
      );
      decorations.push(decor);
    }
  }

  await Promise.all(loadPromises);
  decorations.sort((a, b) => a.y + a.height - (b.y + b.height));
  return decorations;
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

function buildMapSpawnZones(tmj) {
  const zones = [];
  for (const layer of tmj?.layers || []) {
    if (layer?.type !== "objectgroup") continue;
    for (const object of layer.objects || []) {
      const objectType = String(object?.class || object?.type || "")
        .trim()
        .toLowerCase();
      if (objectType !== "spawn_mobs") continue;

      const region = buildCollisionWallFromObject(object);
      if (!region?.closed || region.vertices.length < 3) continue;
      zones.push({
        ...region,
        difficulty: objectNumberProperty(object, "difficulty", 0),
      });
    }
  }
  return zones;
}

function buildMapCheckpointZones(tmj) {
  const zones = [];
  for (const layer of tmj?.layers || []) {
    if (layer?.type !== "objectgroup") continue;
    for (const object of layer.objects || []) {
      const objectType = String(object?.class || object?.type || "")
        .trim()
        .toLowerCase();
      if (!checkpointObjectTypes.has(objectType)) continue;

      const region = buildCollisionWallFromObject(object);
      if (!region?.closed || region.vertices.length < 3) continue;
      zones.push({ ...region, type: objectType });
    }
  }
  return zones;
}

function parseMinimapTileDefinition(source) {
  const normalized = String(source || "")
    .replaceAll("\\", "/")
    .toLowerCase();
  const filename = normalized.slice(normalized.lastIndexOf("/") + 1);
  const extensionMatch = filename.match(/\.([^.]+)$/);
  const extension = extensionMatch?.[1] || "";
  const basename = filename.replace(/\.[^.]+$/, "");
  const parts = basename.split("_");
  const hasVariant = /^\d+$/.test(parts.at(-1) || "");
  const variant = hasVariant ? Number(parts.pop()) : null;
  const shape = parts.length > 1 ? parts.pop() : "";
  return {
    material: parts.join("_"),
    shape,
    variant,
    extension,
  };
}

function buildManualMinimapTileMask(definition, subdivisions) {
  if (definition?.extension !== "svg" || definition.variant === null)
    return null;
  const matrix = manualMinimapTileMatrices[definition?.shape];
  if (!matrix || subdivisions !== 2) return null;

  let mask = 0;
  for (let y = 0; y < 2; y += 1) {
    for (let x = 0; x < 2; x += 1) {
      if (matrix[y][x]) mask |= 1 << (y * 2 + x);
    }
  }
  return mask;
}

function buildMinimapTileMasks(
  tileImages,
  minimapTileDefinitions,
  collisionDefs,
  subdivisions,
) {
  const masks = new Map();
  const fullMask = 2 ** (subdivisions * subdivisions) - 1;
  const requestedSampleSize = Math.max(
    subdivisions,
    Math.floor(minimapTextureSampleSizePx),
  );
  const sampleSize =
    Math.ceil(requestedSampleSize / subdivisions) * subdivisions;
  const canvas = createMapRenderCanvas(sampleSize, sampleSize);
  const context = canvas?.getContext("2d", { willReadFrequently: true });
  if (!context) {
    for (const gid of tileImages.keys()) {
      const manualMask = buildManualMinimapTileMask(
        minimapTileDefinitions?.get(gid),
        subdivisions,
      );
      masks.set(gid, manualMask ?? fullMask);
    }
    return masks;
  }

  const sampleCellSize = sampleSize / subdivisions;
  const sampleCellPixels = sampleCellSize * sampleCellSize;
  const threshold = clamp(minimapTextureCoverageThreshold, 0, 1);
  for (const [gid, image] of tileImages) {
    let mask = fullMask;
    const manualMask = buildManualMinimapTileMask(
      minimapTileDefinitions?.get(gid),
      subdivisions,
    );
    const collisionDef = collisionDefs?.get(gid);
    if (manualMask !== null) {
      mask = manualMask;
    } else if (collisionDef) {
      mask = buildMinimapCollisionTileMask(collisionDef, subdivisions);
    } else if (imageReady(image)) {
      try {
        context.clearRect(0, 0, sampleSize, sampleSize);
        context.drawImage(image, 0, 0, sampleSize, sampleSize);
        const pixels = context.getImageData(0, 0, sampleSize, sampleSize).data;
        mask = 0;
        for (let cellY = 0; cellY < subdivisions; cellY += 1) {
          for (let cellX = 0; cellX < subdivisions; cellX += 1) {
            let alpha = 0;
            const startX = cellX * sampleCellSize;
            const startY = cellY * sampleCellSize;
            for (let y = startY; y < startY + sampleCellSize; y += 1) {
              for (let x = startX; x < startX + sampleCellSize; x += 1) {
                alpha += pixels[(y * sampleSize + x) * 4 + 3];
              }
            }
            if (alpha / (sampleCellPixels * 255) >= threshold)
              mask |= 1 << (cellY * subdivisions + cellX);
          }
        }
      } catch {
        mask = fullMask;
      }
    }
    masks.set(gid, mask);
  }
  return masks;
}

function buildMinimapCollisionTileMask(definition, subdivisions) {
  const sampleCount = Math.max(
    1,
    Math.floor(minimapCollisionSamplesPerAxis),
  );
  const coverageThreshold = clamp(minimapCollisionCoverageThreshold, 0, 1);
  const cellWidth = definition.sourceWidth / subdivisions;
  const cellHeight = definition.sourceHeight / subdivisions;
  const sampleTotal = sampleCount * sampleCount;
  let mask = 0;

  for (let cellY = 0; cellY < subdivisions; cellY += 1) {
    for (let cellX = 0; cellX < subdivisions; cellX += 1) {
      const left = cellX * cellWidth;
      const top = cellY * cellHeight;
      const right = left + cellWidth;
      const bottom = top + cellHeight;
      const epsilon = Math.max(1e-6, Math.min(cellWidth, cellHeight) * 1e-6);
      const hasPolyline = (definition.walls || []).some(
        (wall) =>
          !wall.closed &&
          polylineIntersectsRectInterior(
            wall.vertices,
            left,
            top,
            right,
            bottom,
            epsilon,
          ),
      );
      let coveredSamples = 0;
      if (!hasPolyline) {
        for (let sampleY = 0; sampleY < sampleCount; sampleY += 1) {
          const y = top + ((sampleY + 0.5) / sampleCount) * cellHeight;
          for (let sampleX = 0; sampleX < sampleCount; sampleX += 1) {
            const x = left + ((sampleX + 0.5) / sampleCount) * cellWidth;
            if (
              (definition.walls || []).some(
                (wall) => wall.closed && pointInPolygon({ x, y }, wall.vertices),
              )
            )
              coveredSamples += 1;
          }
        }
      }

      if (hasPolyline || coveredSamples / sampleTotal >= coverageThreshold)
        mask |= 1 << (cellY * subdivisions + cellX);
    }
  }
  return mask;
}

function polylineIntersectsRectInterior(
  vertices,
  left,
  top,
  right,
  bottom,
  epsilon,
) {
  for (let index = 0; index + 1 < (vertices?.length || 0); index += 1) {
    if (
      segmentIntersectsRectInterior(
        vertices[index],
        vertices[index + 1],
        left,
        top,
        right,
        bottom,
        epsilon,
      )
    )
      return true;
  }
  return false;
}

function buildMinimapCollisionCells(
  walls,
  mapWidth,
  mapHeight,
  tileWidth,
  tileHeight,
  subdivisions,
) {
  const columns = mapWidth * subdivisions;
  const rows = mapHeight * subdivisions;
  if (columns <= 0 || rows <= 0) return [];

  const cellWidth = tileWidth / subdivisions;
  const cellHeight = tileHeight / subdivisions;
  const occupied = new Set();
  const addCell = (x, y) => {
    if (x < 0 || x >= columns || y < 0 || y >= rows) return;
    occupied.add(y * columns + x);
  };

  for (const wall of walls || []) {
    if (!wall?.vertices?.length) continue;
    if (!wall.closed) {
      addMinimapPolylineCells(
        wall.vertices,
        cellWidth,
        cellHeight,
        addCell,
      );
      continue;
    }
    if (
      wall.maxX <= 0 ||
      wall.maxY <= 0 ||
      wall.minX >= columns * cellWidth ||
      wall.minY >= rows * cellHeight
    )
      continue;

    const minCellX = clamp(
      Math.floor(wall.minX / cellWidth),
      0,
      columns - 1,
    );
    const minCellY = clamp(
      Math.floor(wall.minY / cellHeight),
      0,
      rows - 1,
    );
    const maxCellX = clamp(
      Math.ceil(wall.maxX / cellWidth) - 1,
      0,
      columns - 1,
    );
    const maxCellY = clamp(
      Math.ceil(wall.maxY / cellHeight) - 1,
      0,
      rows - 1,
    );

    for (let y = minCellY; y <= maxCellY; y += 1) {
      for (let x = minCellX; x <= maxCellX; x += 1) {
        const left = x * cellWidth;
        const top = y * cellHeight;
        if (
          closedWallOverlapsRect(
            wall.vertices,
            left,
            top,
            left + cellWidth,
            top + cellHeight,
          )
        )
          addCell(x, y);
      }
    }
  }

  return [...occupied]
    .sort((left, right) => left - right)
    .map((index) => ({
      x: index % columns,
      y: Math.floor(index / columns),
    }));
}

function addMinimapPolylineCells(vertices, cellWidth, cellHeight, addCell) {
  for (let index = 0; index + 1 < vertices.length; index += 1) {
    const start = vertices[index];
    const end = vertices[index + 1];
    const steps = Math.max(
      1,
      Math.ceil(
        Math.max(
          Math.abs(end.x - start.x) / cellWidth,
          Math.abs(end.y - start.y) / cellHeight,
        ) * 4,
      ),
    );
    for (let step = 0; step <= steps; step += 1) {
      const t = step / steps;
      addCell(
        Math.floor((start.x + (end.x - start.x) * t) / cellWidth),
        Math.floor((start.y + (end.y - start.y) * t) / cellHeight),
      );
    }
  }
}

function closedWallOverlapsRect(vertices, left, top, right, bottom) {
  const epsilon = Math.max(1e-6, Math.min(right - left, bottom - top) * 1e-6);
  const insetLeft = left + epsilon;
  const insetTop = top + epsilon;
  const insetRight = right - epsilon;
  const insetBottom = bottom - epsilon;
  const probes = [
    { x: (left + right) * 0.5, y: (top + bottom) * 0.5 },
    { x: insetLeft, y: insetTop },
    { x: insetRight, y: insetTop },
    { x: insetRight, y: insetBottom },
    { x: insetLeft, y: insetBottom },
  ];
  if (probes.some((point) => pointInPolygon(point, vertices))) return true;

  for (const point of vertices) {
    if (
      point.x > insetLeft &&
      point.x < insetRight &&
      point.y > insetTop &&
      point.y < insetBottom
    )
      return true;
  }

  for (let index = 0; index < vertices.length; index += 1) {
    if (
      segmentIntersectsRectInterior(
        vertices[index],
        vertices[(index + 1) % vertices.length],
        left,
        top,
        right,
        bottom,
        epsilon,
      )
    )
      return true;
  }
  return false;
}

function segmentIntersectsRectInterior(
  start,
  end,
  left,
  top,
  right,
  bottom,
  epsilon,
) {
  const dx = end.x - start.x;
  const dy = end.y - start.y;
  let minT = 0;
  let maxT = 1;
  for (const [direction, distance] of [
    [-dx, start.x - left],
    [dx, right - start.x],
    [-dy, start.y - top],
    [dy, bottom - start.y],
  ]) {
    if (Math.abs(direction) <= epsilon) {
      if (distance < -epsilon) return false;
      continue;
    }
    const t = distance / direction;
    if (direction < 0) minT = Math.max(minT, t);
    else maxT = Math.min(maxT, t);
    if (minT > maxT) return false;
  }

  const middleT = (minT + maxT) * 0.5;
  const middleX = start.x + dx * middleT;
  const middleY = start.y + dy * middleT;
  return (
    middleX > left + epsilon &&
    middleX < right - epsilon &&
    middleY > top + epsilon &&
    middleY < bottom - epsilon
  );
}

function buildSpawnDifficultyCells(
  tmj,
  zones,
  blockedCellIndices,
  mapWidth,
  mapHeight,
  subdivisions,
) {
  const tileWidth = Math.max(1, Number(tmj?.tilewidth) || 512);
  const tileHeight = Math.max(1, Number(tmj?.tileheight) || 512);
  const cellWidth = tileWidth / subdivisions;
  const cellHeight = tileHeight / subdivisions;
  const columns = mapWidth * subdivisions;
  const rows = mapHeight * subdivisions;
  if (columns <= 0 || rows <= 0 || !zones?.length) return [];

  const difficultyByCell = new Map();
  for (const zone of zones) {
    const minCellX = clamp(Math.floor(zone.minX / cellWidth), 0, columns - 1);
    const minCellY = clamp(
      Math.floor(zone.minY / cellHeight),
      0,
      rows - 1,
    );
    const maxCellX = clamp(
      Math.ceil(zone.maxX / cellWidth) - 1,
      0,
      columns - 1,
    );
    const maxCellY = clamp(
      Math.ceil(zone.maxY / cellHeight) - 1,
      0,
      rows - 1,
    );

    for (let y = minCellY; y <= maxCellY; y += 1) {
      for (let x = minCellX; x <= maxCellX; x += 1) {
        const index = y * columns + x;
        if (blockedCellIndices?.has(index)) continue;
        const center = {
          x: (x + 0.5) * cellWidth,
          y: (y + 0.5) * cellHeight,
        };
        if (!pointInPolygon(center, zone.vertices)) continue;

        const previous = difficultyByCell.get(index);
        if (previous === undefined || zone.difficulty > previous)
          difficultyByCell.set(index, zone.difficulty);
      }
    }
  }

  return [...difficultyByCell.entries()]
    .sort(([left], [right]) => left - right)
    .map(([index, difficulty]) => ({
      x: index % columns,
      y: Math.floor(index / columns),
      difficulty,
    }));
}

function rectsOverlap(a, b) {
  return (
    a.x <= b.right &&
    a.x + a.width >= b.left &&
    a.y <= b.bottom &&
    a.y + a.height >= b.top
  );
}

function normalizeMapPath(path) {
  let normalized = String(path || "").trim()
    .replace(/\\/g, "/")
    .replace(/^(?:\.\/|\/)+/, "");
  if (!normalized) return "";
  // Older debug servers send a display name ("garden"), not a map filename.
  // Keep explicit filenames intact, and resolve legacy names to Tiled JSON.
  const suffixIndex = normalized.search(/[?#]/);
  const suffix = suffixIndex < 0 ? "" : normalized.slice(suffixIndex);
  if (suffixIndex >= 0) normalized = normalized.slice(0, suffixIndex);
  if (!normalized) return "";
  if (!/\.[^/]+$/.test(normalized)) normalized += ".tmj";
  normalized += suffix;
  if (normalized.startsWith("data/")) return `/${normalized}`;
  if (normalized.startsWith("maps/") || normalized.startsWith("tiles/"))
    return `/data/${normalized}`;
  return `/data/maps/${normalized}`;
}

function joinAssetPath(basePath, relativePath) {
  const relative = String(relativePath || "").replace(/\\/g, "/");
  if (!relative) return "";
  if (relative.startsWith("/") || /^[a-z]+:\/\//i.test(relative))
    return relative;
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

function buildTileCollisionDefinition(tile, tileset = {}) {
  const sourceWidth = Math.max(
    1,
    Number(tile?.imagewidth || tileset?.tilewidth) || 256,
  );
  const sourceHeight = Math.max(
    1,
    Number(tile?.imageheight || tileset?.tileheight) || 256,
  );
  const walls = [];
  for (const object of tile?.objectgroup?.objects || []) {
    const wall = buildCollisionWallFromObject(object);
    if (wall) walls.push(wall);
  }
  if (!walls.length) {
    const hasSolidProperty = (tile?.properties || []).some((prop) => {
      const name = String(prop?.name || "").toLowerCase();
      return (
        (name === "collision" ||
          name === "collidable" ||
          name === "solid" ||
          name === "wall") &&
        prop.value !== false
      );
    });
    if (hasSolidProperty) {
      walls.push(
        finalizeCollisionWall(
          [
            { x: 0, y: 0 },
            { x: sourceWidth, y: 0 },
            { x: sourceWidth, y: sourceHeight },
            { x: 0, y: sourceHeight },
          ],
          true,
        ),
      );
    }
  }
  return walls.length ? { sourceWidth, sourceHeight, walls } : null;
}

function buildMapCollisionWalls(tmj, layers, collisionDefs) {
  const walls = [];
  const tileWidth = Math.max(1, Number(tmj?.tilewidth) || 512);
  const tileHeight = Math.max(1, Number(tmj?.tileheight) || 512);

  for (const layer of layers || []) {
    if (!layer.width || !layer.height || !layer.tiles?.length) continue;
    for (let y = 0; y < layer.height; y += 1) {
      for (let x = 0; x < layer.width; x += 1) {
        const raw = (layer.tiles[y * layer.width + x] || 0) >>> 0;
        const def = collisionDefs.get(canonicalTileGid(raw));
        if (!def) continue;

        const flipH = (raw & 0x80000000) !== 0;
        const flipV = (raw & 0x40000000) !== 0;
        const flipD = (raw & 0x20000000) !== 0;
        for (const sourceWall of def.walls) {
          const vertices = sourceWall.vertices.map((point) => {
            const scaledX = (point.x / def.sourceWidth) * tileWidth;
            const scaledY = (point.y / def.sourceHeight) * tileHeight;
            const transformed = transformTilePoint(
              scaledX,
              scaledY,
              tileWidth,
              tileHeight,
              flipH,
              flipV,
              flipD,
            );
            return {
              x: x * tileWidth + transformed.x,
              y: y * tileHeight + transformed.y,
            };
          });
          walls.push(finalizeCollisionWall(vertices, sourceWall.closed));
        }
      }
    }
  }

  for (const layer of tmj?.layers || []) {
    if (layer?.type !== "objectgroup") continue;
    for (const object of layer.objects || []) {
      if (!isMapWallObject(layer, object)) continue;
      const wall = buildCollisionWallFromObject(object);
      if (wall) walls.push(wall);
    }
  }
  return walls.filter(Boolean);
}

function isMapWallObject(layer, object) {
  const layerName = String(layer?.name || "").toLowerCase();
  if (
    layerName === "collision" ||
    layerName === "collisions" ||
    layerName === "wall" ||
    layerName === "walls"
  )
    return true;
  const type = String(object?.type || "").toLowerCase();
  const name = String(object?.name || "").toLowerCase();
  return (
    type === "wall" ||
    type === "collision" ||
    type === "collisions" ||
    name === "wall" ||
    name === "collision" ||
    name === "collisions"
  );
}

function buildCollisionWallFromObject(object) {
  if (!object) return null;
  let localVertices = [];
  let closed = true;
  if (Array.isArray(object.polygon)) {
    localVertices = object.polygon;
  } else if (Array.isArray(object.polyline)) {
    localVertices = object.polyline;
    closed = false;
  } else {
    const width = Number(object.width) || 0;
    const height = Number(object.height) || 0;
    if (width <= 0 || height <= 0) return null;
    localVertices = [
      { x: 0, y: 0 },
      { x: width, y: 0 },
      { x: width, y: height },
      { x: 0, y: height },
    ];
  }

  if (localVertices.length < (closed ? 3 : 2)) return null;
  const originX = Number(object.x) || 0;
  const originY = Number(object.y) || 0;
  const angle = ((Number(object.rotation) || 0) * Math.PI) / 180;
  const sin = Math.sin(angle);
  const cos = Math.cos(angle);
  const vertices = localVertices.map((point) => {
    const localX = Number(point.x) || 0;
    const localY = Number(point.y) || 0;
    return {
      x: originX + localX * cos - localY * sin,
      y: originY + localX * sin + localY * cos,
    };
  });
  return finalizeCollisionWall(vertices, closed);
}

function finalizeCollisionWall(vertices, closed) {
  if (!vertices?.length) return null;
  let minX = vertices[0].x;
  let minY = vertices[0].y;
  let maxX = minX;
  let maxY = minY;
  for (const point of vertices) {
    minX = Math.min(minX, point.x);
    minY = Math.min(minY, point.y);
    maxX = Math.max(maxX, point.x);
    maxY = Math.max(maxY, point.y);
  }
  return { vertices, closed, minX, minY, maxX, maxY };
}

function buildCollisionWallBuckets(walls, tileWidth, tileHeight) {
  const size = Math.max(
    128,
    Math.min(
      1024,
      Math.max(Number(tileWidth) || 512, Number(tileHeight) || 512),
    ),
  );
  const buckets = new Map();
  for (let id = 0; id < (walls?.length || 0); id += 1) {
    const wall = walls[id];
    const minX = Math.floor(wall.minX / size);
    const minY = Math.floor(wall.minY / size);
    const maxX = Math.floor(wall.maxX / size);
    const maxY = Math.floor(wall.maxY / size);
    for (let y = minY; y <= maxY; y += 1) {
      for (let x = minX; x <= maxX; x += 1) {
        const key = `${x}:${y}`;
        let bucket = buckets.get(key);
        if (!bucket) buckets.set(key, (bucket = []));
        bucket.push(id);
      }
    }
  }
  return { size, buckets };
}

function collisionWallsInBounds(map, minX, minY, maxX, maxY) {
  const index = map?.collisionWallBuckets;
  if (!index?.buckets) return map?.collisionWalls || [];
  const ids = new Set();
  const firstX = Math.floor(minX / index.size);
  const firstY = Math.floor(minY / index.size);
  const lastX = Math.floor(maxX / index.size);
  const lastY = Math.floor(maxY / index.size);
  for (let y = firstY; y <= lastY; y += 1) {
    for (let x = firstX; x <= lastX; x += 1) {
      for (const id of index.buckets.get(`${x}:${y}`) || []) ids.add(id);
    }
  }
  return [...ids].map((id) => map.collisionWalls[id]);
}

function resolveCircleMotion(map, start, end, radius) {
  const motion = { x: end.x - start.x, y: end.y - start.y };
  const travel = Math.hypot(motion.x, motion.y);
  const maxStep = Math.max(2, radius * 0.5);
  const steps = Math.max(1, Math.ceil(travel / maxStep));
  const step = { x: motion.x / steps, y: motion.y / steps };
  const pos = { x: start.x, y: start.y };
  const normals = [];

  for (let i = 0; i < steps; i += 1) {
    const previous = { x: pos.x, y: pos.y };
    pos.x += step.x;
    pos.y += step.y;
    for (let pass = 0; pass < 4; pass += 1) {
      let pushed = false;
      const walls = collisionWallsInBounds(
        map,
        Math.min(previous.x, pos.x) - radius - 1,
        Math.min(previous.y, pos.y) - radius - 1,
        Math.max(previous.x, pos.x) + radius + 1,
        Math.max(previous.y, pos.y) + radius + 1,
      );
      for (const wall of walls) {
        const normal = pushCircleOutOfWall(pos, radius, wall, step);
        if (!normal) continue;
        pushed = true;
        normals.push(normal);
      }
      if (!pushed) break;
    }
  }
  return { pos, normals };
}

function pushCircleOutOfWall(pos, radius, wall, motion) {
  if (
    !wall ||
    pos.x + radius < wall.minX ||
    pos.x - radius > wall.maxX ||
    pos.y + radius < wall.minY ||
    pos.y - radius > wall.maxY
  )
    return null;

  const count = wall.closed ? wall.vertices.length : wall.vertices.length - 1;
  let bestDistanceSq = Number.POSITIVE_INFINITY;
  let closest = null;
  let edgeA = null;
  let edgeB = null;
  for (let i = 0; i < count; i += 1) {
    const a = wall.vertices[i];
    const b = wall.vertices[(i + 1) % wall.vertices.length];
    const point = closestPointOnSegment(pos, a, b);
    const dx = pos.x - point.x;
    const dy = pos.y - point.y;
    const distanceSq = dx * dx + dy * dy;
    if (distanceSq >= bestDistanceSq) continue;
    bestDistanceSq = distanceSq;
    closest = point;
    edgeA = a;
    edgeB = b;
  }
  if (!closest) return null;

  const inside = wall.closed && pointInPolygon(pos, wall.vertices);
  const distance = Math.sqrt(Math.max(0, bestDistanceSq));
  if (!inside && distance >= radius) return null;

  let normal;
  if (distance > 0.000001) {
    const scale = 1 / distance;
    normal = inside
      ? { x: (closest.x - pos.x) * scale, y: (closest.y - pos.y) * scale }
      : { x: (pos.x - closest.x) * scale, y: (pos.y - closest.y) * scale };
  } else {
    const edgeX = edgeB.x - edgeA.x;
    const edgeY = edgeB.y - edgeA.y;
    const edgeLength = Math.hypot(edgeX, edgeY) || 1;
    normal = { x: -edgeY / edgeLength, y: edgeX / edgeLength };
    if (wall.closed) {
      const probe = { x: pos.x + normal.x, y: pos.y + normal.y };
      if (pointInPolygon(probe, wall.vertices))
        normal = { x: -normal.x, y: -normal.y };
    } else if (normal.x * motion.x + normal.y * motion.y > 0) {
      normal = { x: -normal.x, y: -normal.y };
    }
  }

  const penetration = inside ? radius + distance : radius - distance;
  const skin = 0.5;
  pos.x += normal.x * (penetration + skin);
  pos.y += normal.y * (penetration + skin);
  return normal;
}

function closestPointOnSegment(point, a, b) {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const lengthSq = dx * dx + dy * dy;
  if (lengthSq <= 0.000001) return { x: a.x, y: a.y };
  const t = clamp(
    ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSq,
    0,
    1,
  );
  return { x: a.x + dx * t, y: a.y + dy * t };
}

function pointInPolygon(point, vertices) {
  let inside = false;
  for (let i = 0, j = vertices.length - 1; i < vertices.length; j = i, i += 1) {
    const a = vertices[i];
    const b = vertices[j];
    const crosses =
      a.y > point.y !== b.y > point.y &&
      point.x <
        ((b.x - a.x) * (point.y - a.y)) / (b.y - a.y || Number.EPSILON) + a.x;
    if (crosses) inside = !inside;
  }
  return inside;
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
    const xAxis = transformTilePoint(
      width,
      0,
      width,
      height,
      flipH,
      flipV,
      flipD,
    );
    const yAxis = transformTilePoint(
      0,
      height,
      width,
      height,
      flipH,
      flipV,
      flipD,
    );
    targetCtx.transform(
      (xAxis.x - origin.x) / width,
      (xAxis.y - origin.y) / width,
      (yAxis.x - origin.x) / height,
      (yAxis.y - origin.y) / height,
      origin.x,
      origin.y,
    );
  }
  targetCtx.drawImage(
    image,
    0,
    0,
    width,
    height,
  );
  targetCtx.restore();
}

function createMapRenderCanvas(width, height) {
  if (typeof OffscreenCanvas !== "undefined")
    return new OffscreenCanvas(width, height);
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  return canvas;
}

const SVG_VIEWBOX_SIZE = 110;
const SVG_CLIP_X = 9.167;
const SVG_CLIP_SIZE = 91.667;
const ANT_BASE_FACE_ANGLE = -Math.PI * 0.75;
const ROCK_FILL = "#777";
const ROCK_STROKE = "#606060";
const ROCK_EDGE_WIDTH = 5.2;
const ROCK_VISUAL_SCALE = 1.18;
const SANDSTORM_VISUAL_SCALE = 1.2;
const PORTAL_VISUAL_SCALE = 2.15;
const ANT_EGG_VISUAL_SCALE = 4.4;
const FULL_ANT_VISUAL_SCALE = 5.65;
const BABY_ANT_VISUAL_SCALE = 7.37;
const OVERMIND_VISUAL_SCALE = 4.1;
const LEAF_PIECE_VISUAL_SCALE = 3.1;
const DANDELION_VISUAL_SCALE = 4;
const DANDELION_BASE_IMAGE_ANGLE_OFFSET = Math.PI * 0.5;
const BASE_CACHE = new Map();
const PART_CACHE = new Map();
const ROCK_SHAPE_CACHE = new Map();

export function drawRock(ctx, pos, radius, entityId, worldRadius = radius) {
  if (radius < 0.5) return;
  const points = rockShapePoints(entityId, worldRadius);
  const visualRadius = radius * ROCK_VISUAL_SCALE;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  ctx.lineJoin = "miter";
  ctx.lineCap = "butt";
  ctx.miterLimit = 2.6;
  ctx.lineWidth = Math.max(1.2, Math.min(ROCK_EDGE_WIDTH, visualRadius * 0.38));
  ctx.fillStyle = ROCK_FILL;
  ctx.strokeStyle = ROCK_STROKE;
  ctx.beginPath();
  points.forEach((point, index) => {
    const x = point.x * visualRadius;
    const y = point.y * visualRadius;
    if (index === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
  ctx.restore();
}

function rockShapePoints(entityId, worldRadius) {
  const radiusKey = Math.max(1, Math.round((worldRadius || 1) * 10));
  const cacheKey = `${entityId || 0}:${radiusKey}`;
  const cached = ROCK_SHAPE_CACHE.get(cacheKey);
  if (cached) return cached;

  const seed = (((entityId || 0) + 1) * 2654435761 + radiusKey * 1597334677) >>> 0;
  const rng = seededRng(seed);
  const size01 = clamp01((Math.max(0, worldRadius || 0) - 28) / 240);
  const pointCount = 10 + Math.floor(size01 * 12) + Math.floor(rng() * (3 + size01 * 4));
  const angleOffset = rng() * Math.PI * 2;
  const angleJitter = 0.04 + size01 * 0.13;
  const radialJitter = 0.045 + size01 * 0.18;
  const points = [];

  for (let i = 0; i < pointCount; i += 1) {
    const t = i / pointCount;
    let angle = angleOffset + t * Math.PI * 2 + (rng() - 0.5) * angleJitter;
    if (size01 > 0.25 && rng() < size01 * 0.35) {
      const snap = Math.PI / (4 + Math.floor(rng() * 3));
      angle = Math.round(angle / snap) * snap + (rng() - 0.5) * 0.025;
    }

    const wave = Math.sin(t * Math.PI * (4 + Math.floor(size01 * 4)) + seed * 0.000001) * (0.025 + size01 * 0.035);
    let radial = 0.92 + wave + (rng() - 0.5) * radialJitter;
    if (rng() < size01 * 0.28) radial -= rng() * (0.08 + size01 * 0.08);
    if (rng() < size01 * 0.18) radial += rng() * 0.045;
    radial = Math.max(0.72 + (1 - size01) * 0.12, Math.min(0.995, radial));
    points.push({ x: Math.cos(angle) * radial, y: Math.sin(angle) * radial });
  }

  ROCK_SHAPE_CACHE.set(cacheKey, points);
  if (ROCK_SHAPE_CACHE.size > 640) ROCK_SHAPE_CACHE.delete(ROCK_SHAPE_CACHE.keys().next().value);
  return points;
}

export function drawBabyAnt(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/baby_ant.svg",
    sizeScale: BABY_ANT_VISUAL_SCALE,
    stripWings: false,
    forelimbAmplitude: 0.18,
  });
}

export function drawWorkerAnt(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/worker_ant.svg",
    sizeScale: 5.3,
    stripWings: false,
    forelimbAmplitude: 0.18,
  });
}

export function drawQueenAnt(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/queen_ant.svg",
    sizeScale: 4.3,
    stripWings: true,
    layeredWings: true,
    forelimbAmplitude: 0.28,
  });
}

export function drawAntEggMob(ctx, pos, radius, variant = "normal") {
  const src = variant === "fire"
    ? "./assets/fire_ant_egg.svg"
    : (variant === "termite" ? "./assets/termite_egg.svg" : "./assets/ant_egg.svg");
  drawSvgMob(ctx, src, pos, radius, undefined, { sizeScale: ANT_EGG_VISUAL_SCALE });
}

export function drawLeafPiece(ctx, pos, radius, entityId, angle) {
  if (radius < 0.5) return;
  const variant = ((stableHash(entityId || 0) % 4) + 1);
  const image = baseImage(`./assets/leaf_piece_${variant}.svg`, "raw", "raw");
  const size = Math.max(1, radius * LEAF_PIECE_VISUAL_SCALE);
  const half = size * 0.5;
  const fallbackAngle = (stableHash((entityId || 0) + 0x9e3779b9) / 0xffffffff) * Math.PI * 2;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  ctx.rotate(Number.isFinite(angle) ? angle : fallbackAngle);
  if (isImageReady(image)) {
    ctx.drawImage(image, -half, -half, size, size);
  } else {
    drawFallbackLeafPiece(ctx, radius);
  }
  ctx.restore();
}

export function drawBabyFireAnt(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/baby_fire_ant.svg",
    sizeScale: 5.65,
    stripWings: false,
    forelimbAmplitude: 0.18,
  });
}

export function drawWorkerFireAnt(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/worker_fire_ant.svg",
    sizeScale: 5.3,
    stripWings: false,
    forelimbAmplitude: 0.18,
  });
}

export function drawFireQueenAnt(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/fire_queen_ant.svg",
    sizeScale: 4.3,
    stripWings: true,
    layeredWings: true,
    forelimbAmplitude: 0.28,
  });
}

export function drawBabyTermite(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/baby_termite.svg",
    sizeScale: 5.65,
    stripWings: false,
    forelimbAmplitude: 0.18,
  });
}

export function drawWorkerTermite(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/worker_termite.svg",
    sizeScale: 5.3,
    stripWings: false,
    forelimbAmplitude: 0.18,
  });
}

export function drawSoldierFireAntMob(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/soldier_fire_ant.svg",
    sizeScale: FULL_ANT_VISUAL_SCALE,
    forelimbAmplitude: 0.22,
  });
}

export function drawSoldierTermite(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/worker_termite.svg",
    sizeScale: 5.45,
    stripWings: false,
    forelimbAmplitude: 0.2,
  });
}

export function drawTermiteOvermind(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/termite_overmind.svg",
    sizeScale: OVERMIND_VISUAL_SCALE,
    stripWings: true,
    layeredWings: true,
    forelimbAmplitude: 0.3,
  });
}

export function drawAntHole(ctx, pos, radius) {
  const image = baseImage("./assets/ant_hole.svg", "plain", false);
  const size = Math.max(1, radius * 3.05);

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (isImageReady(image)) {
    ctx.drawImage(image, -size * 0.5, -size * 0.5, size, size);
  } else {
    ctx.fillStyle = "#946d00";
    ctx.strokeStyle = "#6b4f00";
    ctx.lineWidth = Math.max(2, radius * 0.14);
    ctx.beginPath();
    ctx.arc(0, 0, radius * 0.95, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
  }
  ctx.restore();
}

function drawSvgMob(ctx, src, pos, radius, angle, options = {}) {
  const image = baseImage(src, "raw", "raw");
  const sizeScale = Number.isFinite(options.sizeScale) ? options.sizeScale : 4;
  const size = Math.max(1, radius * sizeScale);
  const half = size * 0.5;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle) && Number.isFinite(options.faceAngle)) ctx.rotate(angle - options.faceAngle);
  if (isImageReady(image)) {
    ctx.drawImage(image, -half, -half, size, size);
  } else {
    drawFallbackAnt(ctx, radius);
  }
  ctx.restore();
}

function drawFallbackLeafPiece(ctx, radius) {
  const size = radius * 1.85;
  ctx.fillStyle = "#38b148";
  ctx.strokeStyle = "#2e933c";
  ctx.lineWidth = Math.max(1.8, radius * 0.22);
  ctx.beginPath();
  ctx.rect(-size * 0.5, -size * 0.5, size, size);
  ctx.fill();
  ctx.stroke();
}

export function drawDandelion(ctx, pos, radius, _entityId, angle, options = {}) {
  const image = baseImage("./assets/dandelion_base.svg", "dandelion-base", "raw");
  const visualScale = Number.isFinite(options.scale) ? options.scale : DANDELION_VISUAL_SCALE;
  const angleOffset = Number.isFinite(options.angle) ? options.angle : DANDELION_BASE_IMAGE_ANGLE_OFFSET;
  const offsetX = Number.isFinite(options.x) ? options.x : 0;
  const offsetY = Number.isFinite(options.y) ? options.y : 0;
  const size = Math.max(1, radius * visualScale);
  const facing = Number.isFinite(angle) ? angle : 0;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  ctx.rotate(facing);
  ctx.translate(radius * offsetX, radius * offsetY);
  ctx.rotate(angleOffset);
  if (isImageReady(image)) {
    ctx.drawImage(image, -size * 0.5, -size * 0.5, size, size);
  } else {
    drawFallbackDandelion(ctx, radius);
  }
  ctx.restore();
}

export function drawSandstorm(ctx, pos, radius, entityId, time) {
  if (radius < 0.5) return;

  const image = baseImage("./assets/sandstorm.svg", "sandstorm", "plain");
  const basePhase = (entityId || 0) * 0.41;
  const t = time || 0;
  const layers = [
    { scale: 2.75, alpha: 0.42, speed: 3.75, phase: 0.2 },
    { scale: 2.1, alpha: 0.62, speed: -7.5, phase: 1.3 },
    { scale: 1.48, alpha: 0.92, speed: 15.0, phase: 2.1 },
  ];

  ctx.save();
  ctx.translate(pos.x, pos.y);
  for (const layer of layers) {
    const size = Math.max(1, radius * layer.scale * SANDSTORM_VISUAL_SCALE);
    ctx.save();
    ctx.rotate(basePhase + layer.phase + t * layer.speed);
    ctx.globalAlpha *= layer.alpha;
    if (isImageReady(image)) {
      ctx.drawImage(image, -size * 0.5, -size * 0.5, size, size);
    } else {
      drawFallbackSandstorm(ctx, radius, layer.scale);
    }
    ctx.restore();
  }
  ctx.restore();
}

export function drawPortal(ctx, pos, radius, entityId, time, angle = 0) {
  if (radius < 0.5) return;

  const image = baseImage("./assets/portal.svg", "portal", "raw");
  const basePhase = (entityId || 0) * 0.37 + (Number.isFinite(angle) ? angle : 0);
  const t = time || 0;
  const layers = [
    { scale: 1.3, alpha: 0.5, speed: 1.8, phase: 0.4 },
    { scale: 1.0, alpha: 0.72, speed: -3.2, phase: 1.7 },
    { scale: 0.72, alpha: 0.95, speed: 5.8, phase: 2.5 },
  ];

  ctx.save();
  ctx.translate(pos.x, pos.y);
  for (const layer of layers) {
    const size = Math.max(1, radius * layer.scale * PORTAL_VISUAL_SCALE);
    ctx.save();
    ctx.rotate(basePhase + layer.phase + t * layer.speed);
    ctx.globalAlpha *= layer.alpha;
    if (isImageReady(image)) {
      ctx.drawImage(image, -size * 0.5, -size * 0.5, size, size);
    } else {
      drawFallbackPortal(ctx, radius, layer.scale);
    }
    ctx.restore();
  }
  ctx.restore();
}

function drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, options) {
  const spriteSize = Math.max(1, radius * options.sizeScale);
  const spriteHalf = spriteSize * 0.5;
  const forelimbs = svgParts(options.src, "forelimbs");
  const wings = svgParts(options.src, "wings");
  const animation = antAnimation(entityId, motion || 0, time || 0, options.forelimbAmplitude);

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle)) ctx.rotate(angle - ANT_BASE_FACE_ANGLE);

  if (options.layeredWings) {
    const back = baseImage(options.src, "queen-back", "queen-back");
    const front = baseImage(options.src, "queen-front", "queen-front");
    if (isImageReady(back)) {
      ctx.drawImage(back, -spriteHalf, -spriteHalf, spriteSize, spriteSize);
    } else {
      drawFallbackAnt(ctx, radius);
    }
    drawAnimatedParts(ctx, wings, spriteSize, animation.wing, "fill");
    drawAnimatedParts(ctx, forelimbs, spriteSize, animation.forelimb, "stroke");
    if (isImageReady(front)) {
      ctx.drawImage(front, -spriteHalf, -spriteHalf, spriteSize, spriteSize);
    }
    ctx.restore();
    return;
  }

  const base = baseImage(options.src, options.stripWings ? "no-limbs-wings" : "no-limbs", options.stripWings);
  drawAnimatedParts(ctx, forelimbs, spriteSize, animation.forelimb, "stroke");
  if (isImageReady(base)) {
    ctx.drawImage(base, -spriteHalf, -spriteHalf, spriteSize, spriteSize);
  } else {
    drawFallbackAnt(ctx, radius);
  }
  if (!options.stripWings) drawAnimatedParts(ctx, wings, spriteSize, animation.wing, "fill");
  ctx.restore();
}

function antAnimation(entityId, motion, time, forelimbAmplitude = 0.28) {
  const move = clamp01(motion);
  const activeMove = move < 0.12 ? 0 : move;
  const forelimbPhase = time * (4.5 + activeMove * 30) + entityId * 0.73;
  const wingPhase = time * (4.5 + move * 20) + entityId * 0.47;
  return {
    forelimb: Math.sin(forelimbPhase) * activeMove * forelimbAmplitude,
    wing: Math.sin(wingPhase) * Math.max(0.22, move) * 0.16,
  };
}

function drawAnimatedParts(ctx, parts, spriteSize, amount, mode) {
  if (!parts || parts.length === 0) return;

  const scale = spriteSize / SVG_VIEWBOX_SIZE;
  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-SVG_VIEWBOX_SIZE * 0.5, -SVG_VIEWBOX_SIZE * 0.5);
  clipSvgBounds(ctx);

  for (const part of parts) {
    const swing = amount * part.direction;
    ctx.save();
    ctx.translate(part.pivot.x, part.pivot.y);
    ctx.rotate(swing);
    ctx.translate(-part.pivot.x, -part.pivot.y);
    if (mode === "stroke") {
      ctx.strokeStyle = part.stroke || "#292929";
      ctx.lineWidth = part.strokeWidth || 5.833;
      ctx.lineCap = "round";
      ctx.lineJoin = "round";
      ctx.stroke(part.path);
    } else {
      ctx.fillStyle = part.fill || "#eee";
      ctx.globalAlpha = part.opacity ?? 0.498;
      ctx.fill(part.path);
      ctx.globalAlpha = 1;
    }
    ctx.restore();
  }
  ctx.restore();
}

function baseImage(src, key, stripWings) {
  const cacheKey = `${key}:${src}`;
  let entry = BASE_CACHE.get(cacheKey);
  if (entry) return entry.image;

  const image = new Image();
  image.decoding = "async";
  entry = { image, url: "" };
  BASE_CACHE.set(cacheKey, entry);

  fetch(src)
    .then((response) => {
      if (!response.ok) throw new Error(`Garden mob SVG failed: ${src}`);
      return response.text();
    })
    .then((svgText) => {
      const blob = new Blob([stripAnimatedSvg(svgText, stripWings)], { type: "image/svg+xml" });
      const url = URL.createObjectURL(blob);
      entry.url = url;
      image.src = url;
    })
    .catch(() => {
      image.failed = true;
    });

  return image;
}

function svgParts(src, kind) {
  const cacheKey = `${kind}:${src}`;
  let entry = PART_CACHE.get(cacheKey);
  if (entry) return entry.parts;

  entry = { parts: [] };
  PART_CACHE.set(cacheKey, entry);
  if (typeof Path2D === "undefined") return entry.parts;

  fetch(src)
    .then((response) => {
      if (!response.ok) throw new Error(`Garden mob parts failed: ${src}`);
      return response.text();
    })
    .then((svgText) => {
      entry.parts = extractSvgParts(svgText, kind);
    })
    .catch(() => {
      entry.parts = [];
    });
  return entry.parts;
}

function stripAnimatedSvg(svgText, stripWings) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  const mode = typeof stripWings === "string" ? stripWings : (stripWings ? "no-limbs-wings" : "no-limbs");
  if (mode === "raw") return svgText;
  const paths = Array.from(root.querySelectorAll("path"));
  const firstWing = paths.findIndex((path) => isWingPath(path));
  const lastForelimb = paths.reduce((last, path, index) => (isForelimbPath(path) ? index : last), -1);

  paths.forEach((path, index) => {
    const forelimb = isForelimbPath(path);
    const wing = isWingPath(path);
    if (mode === "queen-back") {
      if (forelimb || wing || (lastForelimb >= 0 && index > lastForelimb)) path.remove();
      return;
    }
    if (mode === "queen-front") {
      if (forelimb || wing || index <= lastForelimb || (lastForelimb < 0 && firstWing >= 0 && index <= firstWing)) path.remove();
      return;
    }
    if (forelimb || (mode === "no-limbs-wings" && wing)) path.remove();
  });
  return new XMLSerializer().serializeToString(root);
}

function extractSvgParts(svgText, kind) {
  if (typeof DOMParser === "undefined" || typeof Path2D === "undefined") return [];

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return [];

  const nodes = Array.from(root.querySelectorAll("path")).filter((path) =>
    kind === "forelimbs" ? isForelimbPath(path) : isWingPath(path));
  return nodes.map((node, index) => {
    const d = node.getAttribute("d") || "";
    const pivot = parseMovePivot(d);
    return {
      path: new Path2D(d),
      pivot,
      direction: index % 2 === 0 ? -1 : 1,
      stroke: node.getAttribute("stroke") || "#292929",
      strokeWidth: Number.parseFloat(node.getAttribute("stroke-width") || "5.833"),
      fill: node.getAttribute("fill") || "#eee",
      opacity: Number.parseFloat(node.getAttribute("fill-opacity") || "0.498"),
    };
  });
}

function isForelimbPath(path) {
  return (path.getAttribute("stroke") || "").trim().toLowerCase() === "#292929";
}

function isWingPath(path) {
  const fill = (path.getAttribute("fill") || "").trim().toLowerCase();
  return fill === "#eee" || fill === "#eeeeee";
}

function parseMovePivot(d) {
  const match = /M\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)/i.exec(d);
  if (!match) return { x: SVG_VIEWBOX_SIZE * 0.5, y: SVG_VIEWBOX_SIZE * 0.5 };
  return { x: Number.parseFloat(match[1]), y: Number.parseFloat(match[2]) };
}

function clipSvgBounds(ctx) {
  if (typeof ctx.roundRect === "function") {
    ctx.beginPath();
    ctx.roundRect(SVG_CLIP_X, SVG_CLIP_X, SVG_CLIP_SIZE, SVG_CLIP_SIZE, 4.167);
    ctx.clip();
    return;
  }

  ctx.beginPath();
  ctx.rect(SVG_CLIP_X, SVG_CLIP_X, SVG_CLIP_SIZE, SVG_CLIP_SIZE);
  ctx.clip();
}

function seededRng(seed) {
  let value = seed >>> 0;
  return () => {
    value ^= value << 13;
    value ^= value >>> 17;
    value ^= value << 5;
    return ((value >>> 0) / 4294967296);
  };
}

function stableHash(seed) {
  let value = seed >>> 0;
  value ^= value >>> 16;
  value = Math.imul(value, 2246822507) >>> 0;
  value ^= value >>> 13;
  value = Math.imul(value, 3266489909) >>> 0;
  value ^= value >>> 16;
  return value >>> 0;
}

function isImageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

function clamp01(value) {
  return Math.max(0, Math.min(1, value));
}

function drawFallbackAnt(ctx, radius) {
  ctx.fillStyle = "#454545";
  ctx.beginPath();
  ctx.arc(radius * 0.42, radius * 0.42, radius * 0.95, 0, Math.PI * 2);
  ctx.arc(-radius * 0.26, -radius * 0.26, radius * 0.72, 0, Math.PI * 2);
  ctx.fill();
}

function drawFallbackDandelion(ctx, radius) {
  ctx.fillStyle = "#f3d851";
  ctx.strokeStyle = "#a48e2a";
  ctx.lineWidth = Math.max(1.4, radius * 0.12);
  ctx.beginPath();
  ctx.arc(0, 0, radius * 1.25, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();

  ctx.fillStyle = "#7fb96b";
  ctx.beginPath();
  ctx.arc(0, radius * 0.08, radius * 0.38, 0, Math.PI * 2);
  ctx.fill();
}

function drawFallbackSandstorm(ctx, radius, scale) {
  const size = radius * scale * 0.34;
  ctx.strokeStyle = "#cabb99";
  ctx.lineWidth = Math.max(1.2, radius * 0.13);
  ctx.lineJoin = "round";
  ctx.beginPath();
  for (let i = 0; i < 6; i += 1) {
    const a = i * Math.PI / 3;
    const x = Math.cos(a) * size;
    const y = Math.sin(a) * size;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.closePath();
  ctx.stroke();
}

function drawFallbackPortal(ctx, radius, scale) {
  const size = radius * scale * 0.58;
  ctx.strokeStyle = "#7a5dff";
  ctx.lineWidth = Math.max(1.4, radius * 0.12);
  ctx.beginPath();
  ctx.ellipse(0, 0, size * 0.9, size * 0.55, 0, 0, Math.PI * 2);
  ctx.stroke();
  ctx.strokeStyle = "#55e7ff";
  ctx.lineWidth *= 0.65;
  ctx.beginPath();
  ctx.arc(0, 0, size * 0.35, 0, Math.PI * 2);
  ctx.stroke();
}

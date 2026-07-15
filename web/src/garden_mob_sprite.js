const SVG_VIEWBOX_SIZE = 110;
const SVG_CLIP_X = 9.167;
const SVG_CLIP_SIZE = 91.667;
const ANT_BASE_FACE_ANGLE = -Math.PI * 0.75;
const ROCK_FILL = "#777";
const ROCK_STROKE = "#606060";
const BASE_CACHE = new Map();
const PART_CACHE = new Map();

export function drawRock(ctx, pos, radius, entityId) {
  const rng = seededRng((entityId + 1) * 2654435761 + Math.round(radius * 997));
  const pointCount = 9 + Math.floor(rng() * 8);
  const smoothness = rng();
  const angleOffset = rng() * Math.PI * 2;

  const points = [];
  for (let i = 0; i < pointCount; i += 1) {
    const t = i / pointCount;
    const baseAngle = angleOffset + t * Math.PI * 2;
    const jag = smoothness < 0.45 ? 0.18 : 0.08;
    const radial = radius * (0.82 + rng() * jag + Math.sin(t * Math.PI * 4 + entityId) * 0.035);
    points.push({ x: Math.cos(baseAngle) * radial, y: Math.sin(baseAngle) * radial });
  }

  ctx.save();
  ctx.translate(pos.x, pos.y);
  ctx.lineJoin = "round";
  ctx.lineCap = "round";
  ctx.lineWidth = Math.max(2, radius * 0.22);
  ctx.fillStyle = ROCK_FILL;
  ctx.strokeStyle = ROCK_STROKE;
  ctx.beginPath();
  points.forEach((point, index) => {
    if (index === 0) ctx.moveTo(point.x, point.y);
    else ctx.lineTo(point.x, point.y);
  });
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
  ctx.restore();
}

export function drawBabyAnt(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/baby_ant.svg",
    sizeScale: 5.0,
    stripWings: false,
  });
}

export function drawWorkerAnt(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/worker_ant.svg",
    sizeScale: 5.3,
    stripWings: false,
  });
}

export function drawQueenAnt(ctx, pos, radius, entityId, angle, motion, time) {
  drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/queen_ant.svg",
    sizeScale: 4.3,
    stripWings: true,
    layeredWings: true,
  });
}

export function drawAntHole(ctx, pos, radius) {
  const image = baseImage("./assets/ant_hole.svg", "plain", false);
  const size = Math.max(1, radius * 2.55);

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

function drawGardenAnt(ctx, pos, radius, entityId, angle, motion, time, options) {
  const spriteSize = Math.max(1, radius * options.sizeScale);
  const spriteHalf = spriteSize * 0.5;
  const forelimbs = svgParts(options.src, "forelimbs");
  const wings = options.stripWings ? svgParts(options.src, "wings") : [];
  const animation = antAnimation(entityId, motion || 0, time || 0);

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
  drawAnimatedParts(ctx, wings, spriteSize, animation.wing, "fill");
  ctx.restore();
}

function antAnimation(entityId, motion, time) {
  const move = clamp01(motion);
  const activeMove = move < 0.12 ? 0 : move;
  const forelimbPhase = time * (8 + activeMove * 16) + entityId * 0.73;
  const wingPhase = time * (7.5 + move * 10) + entityId * 0.47;
  return {
    forelimb: Math.sin(forelimbPhase) * activeMove * 0.28,
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

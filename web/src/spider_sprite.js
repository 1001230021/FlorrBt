const SPIDER_ASSET = "./assets/spider.svg";
const SPIDER_VIEWBOX = 110;
const SPIDER_CLIP_X = 9.167;
const SPIDER_CLIP_SIZE = 91.667;
const SPIDER_BASE_FACE_ANGLE = -Math.PI * 0.75;
const SPIDER_SPRITE_SCALE = 7.7;
const SPIDER_LEG_GROUP_A = new Set([1, 3, 5, 7]);
const SPIDER_CACHE = {
  started: false,
  failed: false,
  body: null,
  legs: [],
};

export function drawSpider(ctx, pos, radius, entityId, angle, motion, time) {
  const cache = spiderAsset();
  const move = clamp01(motion || 0);
  const spriteSize = Math.max(1, radius * SPIDER_SPRITE_SCALE);
  const spriteHalf = spriteSize * 0.5;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle)) ctx.rotate(angle - SPIDER_BASE_FACE_ANGLE);

  drawSpiderLegs(ctx, cache.legs, spriteSize, entityId || 0, move, time || 0);
  if (isImageReady(cache.body)) {
    ctx.drawImage(cache.body, -spriteHalf, -spriteHalf, spriteSize, spriteSize);
  } else {
    drawSpiderFallback(ctx, radius);
  }
  ctx.restore();
}

export function drawSpiderWeb(ctx, pos, radius, entityId, time, options = {}) {
  const spokes = 10;
  const rings = [0.27, 0.45, 0.64, 0.84];
  const seed = ((entityId || 1) * 2654435761) >>> 0;
  const phase = seededUnit(seed) * Math.PI * 2;
  const shimmer = 0.55 + Math.sin((time || 0) * 2.2 + seed * 0.000001) * 0.08;
  const color = options.playerOwned ? "255, 214, 70" : "255, 255, 255";

  ctx.save();
  ctx.translate(pos.x, pos.y);
  ctx.rotate(phase);
  ctx.strokeStyle = `rgba(${color}, ${shimmer})`;
  ctx.lineWidth = Math.max(1, radius * 0.035);
  ctx.lineCap = "round";
  ctx.lineJoin = "round";

  const spokePoints = [];
  for (let i = 0; i < spokes; i += 1) {
    const angle = (i / spokes) * Math.PI * 2;
    const jitter = 0.9 + seededUnit(seed + i * 97) * 0.16;
    const outer = radius * jitter;
    const x = Math.cos(angle) * outer;
    const y = Math.sin(angle) * outer;
    spokePoints.push({ angle, outer, x, y });

    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(x, y);
    ctx.stroke();
  }

  for (let ringIndex = 0; ringIndex < rings.length; ringIndex += 1) {
    ctx.beginPath();
    for (let i = 0; i <= spokes; i += 1) {
      const spoke = spokePoints[i % spokes];
      const wobble = 0.92 + seededUnit(seed + ringIndex * 613 + i * 41) * 0.15;
      const dist = spoke.outer * rings[ringIndex] * wobble;
      const x = Math.cos(spoke.angle) * dist;
      const y = Math.sin(spoke.angle) * dist;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }
  ctx.restore();
}

function drawSpiderLegs(ctx, legs, spriteSize, entityId, motion, time) {
  if (!legs || legs.length === 0) return;

  const scale = spriteSize / SPIDER_VIEWBOX;
  const stepRate = 4.8 + motion * 22;
  const step = Math.sin(time * stepRate + entityId * 0.43);
  const lift = Math.max(0, Math.sin(time * stepRate * 2 + entityId * 0.43));
  const swingAmp = 0.18 * motion;
  const slideAmp = 1.8 * motion;

  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-SPIDER_VIEWBOX * 0.5, -SPIDER_VIEWBOX * 0.5);
  clipSpiderBounds(ctx);

  for (const leg of legs) {
    const groupStep = (SPIDER_LEG_GROUP_A.has(leg.index) ? step : -step);
    const side = leg.end.x < SPIDER_VIEWBOX * 0.5 ? -1 : 1;
    const swing = groupStep * side * swingAmp;
    const slide = groupStep * slideAmp;
    const width = leg.strokeWidth * (1 + lift * 0.035);

    ctx.save();
    ctx.translate(leg.pivot.x, leg.pivot.y);
    ctx.rotate(swing);
    ctx.translate(-leg.pivot.x + leg.forward.x * slide, -leg.pivot.y + leg.forward.y * slide);
    ctx.strokeStyle = leg.stroke;
    ctx.lineWidth = width;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.stroke(leg.path);
    ctx.restore();
  }
  ctx.restore();
}

function spiderAsset() {
  if (SPIDER_CACHE.started) return SPIDER_CACHE;
  SPIDER_CACHE.started = true;

  const image = new Image();
  image.decoding = "async";
  image.addEventListener("error", () => {
    SPIDER_CACHE.failed = true;
  }, { once: true });
  SPIDER_CACHE.body = image;

  fetch(SPIDER_ASSET)
    .then((response) => {
      if (!response.ok) throw new Error(`Spider SVG failed: ${SPIDER_ASSET}`);
      return response.text();
    })
    .then((svgText) => {
      SPIDER_CACHE.legs = extractSpiderLegs(svgText);
      const blob = new Blob([stripSpiderLegs(svgText)], { type: "image/svg+xml" });
      image.src = URL.createObjectURL(blob);
    })
    .catch(() => {
      SPIDER_CACHE.failed = true;
    });

  return SPIDER_CACHE;
}

function extractSpiderLegs(svgText) {
  if (typeof DOMParser === "undefined" || typeof Path2D === "undefined") return [];

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return [];

  return Array.from(root.querySelectorAll("path"))
    .filter(isSpiderLegPath)
    .map((node, index) => {
      const d = node.getAttribute("d") || "";
      const pivot = parseMovePivot(d);
      const end = parseQuadraticEnd(d, pivot);
      const len = Math.hypot(end.x - pivot.x, end.y - pivot.y) || 1;
      return {
        index,
        path: new Path2D(d),
        pivot,
        end,
        forward: { x: (end.x - pivot.x) / len, y: (end.y - pivot.y) / len },
        stroke: node.getAttribute("stroke") || "#333",
        strokeWidth: Number.parseFloat(node.getAttribute("stroke-width") || "4.167"),
      };
    });
}

function stripSpiderLegs(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  Array.from(root.querySelectorAll("path")).forEach((path) => {
    if (isSpiderLegPath(path)) path.remove();
  });
  return new XMLSerializer().serializeToString(root);
}

function isSpiderLegPath(path) {
  const fill = (path.getAttribute("fill") || "").trim().toLowerCase();
  return fill === "none" && !!path.getAttribute("stroke");
}

function parseMovePivot(d) {
  const match = /M\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)/i.exec(d);
  if (!match) return { x: SPIDER_VIEWBOX * 0.5, y: SPIDER_VIEWBOX * 0.5 };
  return { x: Number.parseFloat(match[1]), y: Number.parseFloat(match[2]) };
}

function parseQuadraticEnd(d, pivot) {
  const match = /q\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)/i.exec(d);
  if (!match) return pivot;
  return {
    x: pivot.x + Number.parseFloat(match[3]),
    y: pivot.y + Number.parseFloat(match[4]),
  };
}

function clipSpiderBounds(ctx) {
  if (typeof ctx.roundRect === "function") {
    ctx.beginPath();
    ctx.roundRect(SPIDER_CLIP_X, SPIDER_CLIP_X, SPIDER_CLIP_SIZE, SPIDER_CLIP_SIZE, 4.167);
    ctx.clip();
    return;
  }

  ctx.beginPath();
  ctx.rect(SPIDER_CLIP_X, SPIDER_CLIP_X, SPIDER_CLIP_SIZE, SPIDER_CLIP_SIZE);
  ctx.clip();
}

function drawSpiderFallback(ctx, radius) {
  ctx.fillStyle = "#403525";
  ctx.strokeStyle = "#333";
  ctx.lineWidth = Math.max(2, radius * 0.18);
  ctx.beginPath();
  ctx.arc(0, 0, radius * 0.78, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
}

function isImageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

function clamp01(value) {
  return Math.max(0, Math.min(1, value));
}

function seededUnit(seed) {
  let value = seed >>> 0;
  value ^= value << 13;
  value ^= value >>> 17;
  value ^= value << 5;
  return ((value >>> 0) / 4294967296);
}

import { drawSpriteImage } from "./sprite_raster_cache.js";
import { parseSvgPathEndpoints, prepareSvgImage, whenVisualReady } from "./visual_assets.js";

const JELLYFISH_ASSET = "./assets/jellyfish.svg";
const JELLYFISH_VIEWBOX = 110;
const JELLYFISH_SPRITE_SCALE = 4.8;
const JELLY_CACHE = { started: false, body: null, tentacles: [] };

// Jellyfish do not turn their body toward their movement target.  Keeping the
// uploaded SVG at one world-facing angle also prevents server angle updates
// from making the bell and tentacles snap while it changes targets.
export function drawJellyfish(ctx, pos, radius, entityId, time) {
  const cache = jellyfishAsset();
  const spriteSize = Math.max(1, radius * JELLYFISH_SPRITE_SCALE);
  const half = spriteSize * 0.5;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  drawTentacles(ctx, cache.tentacles, spriteSize, entityId || 0, time || 0);
  if (isImageReady(cache.body)) {
    drawSpriteImage(ctx, cache.body, -half, -half, spriteSize, spriteSize);
  } else {
    drawJellyfishFallback(ctx, radius);
  }
  ctx.restore();
}

export function prepareJellyfishSprites() {
  return whenVisualReady(jellyfishAsset().body);
}

function jellyfishAsset() {
  if (JELLY_CACHE.started) return JELLY_CACHE;
  JELLY_CACHE.started = true;
  JELLY_CACHE.body = prepareSvgImage(JELLYFISH_ASSET, "jellyfish-body", (svgText) => {
    JELLY_CACHE.tentacles = extractTentacles(svgText);
    return stripTentacles(svgText);
  });
  return JELLY_CACHE;
}

function drawTentacles(ctx, tentacles, spriteSize, entityId, time) {
  if (!tentacles.length) return;
  const scale = spriteSize / JELLYFISH_VIEWBOX;
  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-JELLYFISH_VIEWBOX * 0.5, -JELLYFISH_VIEWBOX * 0.5);

  for (const tentacle of tentacles) {
    // Keep every root attached to the bell, then bend the middle and tip
    // independently.  Rotating the original path as one rigid piece made the
    // limbs look like clock hands rather than soft tentacles.
    const motionSeed = entitySeed(tentacle.index, entityId);
    const speedUnit = unitSeed(motionSeed);
    const phaseUnit = unitSeed(entitySeed(tentacle.index + 29, entityId));
    const amount = 0.55 + unitSeed(entitySeed(tentacle.index + 61, entityId)) * 0.45;
    const direction = (motionSeed & 1) === 0 ? -1 : 1;
    const speed = direction * (0.66 + speedUnit * 0.58);
    const phase = time * speed + phaseUnit * Math.PI * 2;

    const primaryWave = Math.sin(phase);
    const secondaryWave = Math.sin(phase * 0.63 + tentacle.secondaryPhase);
    const middleWave = Math.sin(phase - 0.72);
    const stretch = Math.sin(phase * 0.79 + 0.95) * tentacle.length * 0.018;
    const middleSway = tentacle.length * (0.105 + amount * 0.05) * middleWave;
    const outerSway = tentacle.length * (0.18 + amount * 0.075) * primaryWave;
    const tipSway = tentacle.length * (0.25 + amount * 0.105) *
      (primaryWave * 0.78 + secondaryWave * 0.22);

    const firstControl = pointAlongTentacle(tentacle, tentacle.length * 0.3, middleSway * 0.35);
    const secondControl = pointAlongTentacle(tentacle, tentacle.length * 0.7 + stretch, outerSway);
    const tip = pointAlongTentacle(tentacle, tentacle.length + stretch * 1.4, tipSway);

    ctx.save();
    ctx.globalAlpha *= tentacle.opacity;
    ctx.strokeStyle = tentacle.stroke;
    ctx.lineWidth = tentacle.strokeWidth;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.beginPath();
    ctx.moveTo(tentacle.pivot.x, tentacle.pivot.y);
    ctx.bezierCurveTo(
      firstControl.x,
      firstControl.y,
      secondControl.x,
      secondControl.y,
      tip.x,
      tip.y,
    );
    ctx.stroke();
    ctx.restore();
  }
  ctx.restore();
}

function pointAlongTentacle(tentacle, forwardDistance, sidewaysDistance) {
  return {
    x: tentacle.pivot.x + tentacle.forward.x * forwardDistance + tentacle.normal.x * sidewaysDistance,
    y: tentacle.pivot.y + tentacle.forward.y * forwardDistance + tentacle.normal.y * sidewaysDistance,
  };
}

function extractTentacles(svgText) {
  if (typeof DOMParser === "undefined") return [];
  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return [];
  return Array.from(root.querySelectorAll("[data-jelly-limb]"))
    .map((node, index) => {
      const d = node.getAttribute("d") || "";
      const { start: pivot, end } = parseSvgPathEndpoints(d, { x: 55, y: 55 });
      const length = Math.hypot(end.x - pivot.x, end.y - pivot.y) || 1;
      const forward = { x: (end.x - pivot.x) / length, y: (end.y - pivot.y) / length };
      return {
        index,
        pivot,
        length,
        forward,
        normal: { x: -forward.y, y: forward.x },
        secondaryPhase: unitSeed(entitySeed(index + 101, 0)) * Math.PI * 2,
        stroke: node.getAttribute("stroke") || "#684080",
        strokeWidth: Number.parseFloat(node.getAttribute("stroke-width") || "4.2"),
        opacity: Math.max(0, Math.min(1, Number.parseFloat(node.getAttribute("stroke-opacity") || "1"))),
      };
    });
}

function stripTentacles(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return svgText;
  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;
  root.querySelectorAll("[data-jelly-limb]").forEach((node) => node.remove());
  return new XMLSerializer().serializeToString(root);
}

function entitySeed(index, entityId = 0) {
  let value = ((entityId >>> 0) ^ Math.imul(index + 1, 2654435761)) >>> 0;
  value ^= value >>> 16;
  value = Math.imul(value, 2246822507);
  value ^= value >>> 13;
  return value >>> 0;
}

function unitSeed(value) {
  return (((value * 1664525 + 1013904223) >>> 0) / 4294967296);
}

function drawJellyfishFallback(ctx, radius) {
  ctx.fillStyle = "#b66bd8";
  ctx.strokeStyle = "#6d418e";
  ctx.lineWidth = Math.max(1.4, radius * 0.1);
  ctx.beginPath();
  ctx.arc(0, -radius * 0.1, radius * 0.78, Math.PI, 0);
  ctx.lineTo(radius * 0.78, radius * 0.2);
  ctx.quadraticCurveTo(0, radius * 0.48, -radius * 0.78, radius * 0.2);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
}

function isImageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

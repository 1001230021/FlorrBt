import { drawSpriteImage } from "./sprite_raster_cache.js";
import { getLimbMotionAmount, getSpritePhase } from "./sprite_motion.js";
import { parseSvgPathEndpoints, prepareSvgImage, whenVisualReady } from "./visual_assets.js";

const CRAB_ASSET = "./assets/crab.svg";
const CRAB_VIEWBOX = 110;
// The supplied artwork points toward the upper-left (its two claws), so use
// that diagonal as the source-facing direction before rotating to server aim.
const CRAB_BASE_FACE_ANGLE = -Math.PI * 0.75;
const CRAB_SPRITE_SCALE = 4.9;
const CRAB_CACHE = { started: false, body: null, limbs: [] };

export function drawCrab(ctx, pos, radius, entityId, angle, motion, time) {
  const cache = crabAsset();
  const spriteSize = Math.max(1, radius * CRAB_SPRITE_SCALE);
  const half = spriteSize * 0.5;
  const move = getLimbMotionAmount(motion);

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle)) ctx.rotate(angle - CRAB_BASE_FACE_ANGLE);
  drawCrabLimbs(ctx, cache.limbs, spriteSize, entityId || 0, move, time || 0);
  if (isImageReady(cache.body)) {
    drawSpriteImage(ctx, cache.body, -half, -half, spriteSize, spriteSize);
  } else {
    drawCrabFallback(ctx, radius);
  }
  ctx.restore();
}

export function prepareCrabSprites() {
  return whenVisualReady(crabAsset().body);
}

function crabAsset() {
  if (CRAB_CACHE.started) return CRAB_CACHE;
  CRAB_CACHE.started = true;
  CRAB_CACHE.body = prepareSvgImage(CRAB_ASSET, "crab-body", (svgText) => {
    CRAB_CACHE.limbs = extractCrabLimbs(svgText);
    return stripCrabLimbs(svgText);
  });
  return CRAB_CACHE;
}

function drawCrabLimbs(ctx, limbs, spriteSize, entityId, motion, time) {
  if (!limbs.length) return;
  const scale = spriteSize / CRAB_VIEWBOX;
  const phase = getSpritePhase(entityId, "crab-legs", time, 3.8 + motion * 18, entityId * 0.29);
  const step = Math.sin(phase);
  const lift = Math.max(0, Math.sin(phase * 2));

  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-CRAB_VIEWBOX * 0.5, -CRAB_VIEWBOX * 0.5);
  for (const limb of limbs) {
    const left = limb.pivot.x < CRAB_VIEWBOX * 0.5;
    const pairStep = (limb.index % 2 === 0 ? step : -step) * (left ? -1 : 1);
    const isClaw = limb.kind === "claw";
    const swing = pairStep * (isClaw ? 0.055 : 0.13) * (0.35 + motion);
    const slide = pairStep * (isClaw ? 0.8 : 1.9) * motion;
    ctx.save();
    ctx.translate(limb.pivot.x, limb.pivot.y);
    ctx.rotate(swing);
    ctx.translate(-limb.pivot.x + limb.forward.x * slide, -limb.pivot.y + limb.forward.y * slide);
    ctx.strokeStyle = limb.stroke;
    ctx.lineWidth = limb.strokeWidth * (1 + lift * 0.04);
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.stroke(limb.path);
    ctx.restore();
  }
  ctx.restore();
}

function extractCrabLimbs(svgText) {
  if (typeof DOMParser === "undefined" || typeof Path2D === "undefined") return [];
  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return [];
  return Array.from(root.querySelectorAll("[data-crab-limb]"))
    .map((node, index) => {
      const d = node.getAttribute("d") || "";
      const { start: pivot, end } = parseSvgPathEndpoints(d, { x: 55, y: 55 });
      const length = Math.hypot(end.x - pivot.x, end.y - pivot.y) || 1;
      return {
        index,
        kind: node.getAttribute("data-crab-limb") || "leg",
        path: new Path2D(d),
        pivot,
        forward: { x: (end.x - pivot.x) / length, y: (end.y - pivot.y) / length },
        stroke: node.getAttribute("stroke") || "#5a3432",
        strokeWidth: Number.parseFloat(node.getAttribute("stroke-width") || "5.2"),
      };
    });
}

function stripCrabLimbs(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return svgText;
  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;
  root.querySelectorAll("[data-crab-limb]").forEach((node) => node.remove());
  return new XMLSerializer().serializeToString(root);
}

function drawCrabFallback(ctx, radius) {
  ctx.strokeStyle = "#5a3432";
  ctx.fillStyle = "#bd554a";
  ctx.lineWidth = Math.max(1.5, radius * 0.12);
  ctx.beginPath();
  ctx.ellipse(0, 0, radius * 0.72, radius, 0, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
}

function isImageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

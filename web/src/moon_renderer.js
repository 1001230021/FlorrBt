import { drawSpriteImage } from "./sprite_raster_cache.js";
import { prepareSvgImage, whenVisualReady } from "./visual_assets.js";

// Moon art is sourced from the card SVG, but its three visual scales are
// intentionally independent in world rendering:
//   - body radius follows rarity;
//   - crater texture stays at the Mythic reference size;
//   - rim width stays constant in world units (and therefore changes only
//     with camera zoom, not rarity).
export const moonSvgGeometry = Object.freeze({
  source: "./assets/petals/moon.svg",
  viewBoxSize: 110,
  artRadius: 25.5,
  liveRadiusFactor: 0.16,
  baseRadiusUnits: 20,
  radiusStepUnits: 10,
  textureReferenceLevel: 6,
  // The Common SVG's rim thickness carried into every rarity. Expressing it
  // against the Mythic reference lets us recover screen width from zoom.
  rimWidthAtReferenceRatio: 0.042,
});

const moonCache = { started: false, texture: null };

export function moonSvgDrawSize(targetScreenRadius) {
  if (!(targetScreenRadius > 0) || !Number.isFinite(targetScreenRadius)) return 0;
  return targetScreenRadius * moonSvgGeometry.viewBoxSize / moonSvgGeometry.artRadius;
}

export function prepareMoonSurface() {
  return whenVisualReady(moonTextureImage());
}

export function drawMoonSurface(ctx, targetScreenRadius, rarityLevel = 1) {
  if (!(targetScreenRadius > 0) || !Number.isFinite(targetScreenRadius)) return false;

  const level = Math.max(1, Math.min(10, Math.floor(Number(rarityLevel) || 1)));
  const currentRadiusUnits =
    moonSvgGeometry.baseRadiusUnits + moonSvgGeometry.radiusStepUnits * (level - 1);
  const referenceRadiusUnits =
    moonSvgGeometry.baseRadiusUnits +
    moonSvgGeometry.radiusStepUnits * (moonSvgGeometry.textureReferenceLevel - 1);
  const referenceScreenRadius =
    targetScreenRadius * referenceRadiusUnits / Math.max(1, currentRadiusUnits);
  const rimWidth = Math.max(
    0,
    referenceScreenRadius * moonSvgGeometry.rimWidthAtReferenceRatio,
  );
  const innerRadius = Math.max(0, targetScreenRadius - rimWidth);

  ctx.fillStyle = "#6d6d6d";
  ctx.beginPath();
  ctx.arc(0, 0, targetScreenRadius, 0, Math.PI * 2);
  ctx.fill();

  ctx.fillStyle = "#878787";
  ctx.beginPath();
  ctx.arc(0, 0, innerRadius, 0, Math.PI * 2);
  ctx.fill();

  ctx.save();
  ctx.beginPath();
  ctx.arc(0, 0, innerRadius, 0, Math.PI * 2);
  ctx.clip();

  const texture = moonTextureImage();
  const drawSize = moonSvgDrawSize(referenceScreenRadius);
  if (isImageReady(texture)) {
    drawSpriteImage(ctx, texture, -drawSize * 0.5, -drawSize * 0.5, drawSize, drawSize);
    ctx.restore();
    return true;
  }

  drawMoonFallbackTexture(ctx, referenceScreenRadius);
  ctx.restore();
  return false;
}

function moonTextureImage() {
  if (moonCache.started) return moonCache.texture;
  moonCache.started = true;
  moonCache.texture = prepareSvgImage(
    moonSvgGeometry.source,
    "moon-world-texture",
    extractMoonTexture,
  );
  return moonCache.texture;
}

function extractMoonTexture(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return svgText;
  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;
  // The first path is the dark outer body and the second is the inner base.
  // Every following path is SVG-authored crater/highlight texture.
  Array.from(root.querySelectorAll("path")).slice(0, 2).forEach((node) => node.remove());
  return new XMLSerializer().serializeToString(root);
}

function drawMoonFallbackTexture(ctx, radius) {
  ctx.fillStyle = "#7c7c7c";
  ctx.beginPath();
  ctx.arc(radius * 0.2, -radius * 0.12, radius * 0.35, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = "#999999";
  ctx.beginPath();
  ctx.arc(-radius * 0.25, radius * 0.3, radius * 0.17, 0, Math.PI * 2);
  ctx.fill();
}

function isImageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

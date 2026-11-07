import { drawSpriteImage } from "./sprite_raster_cache.js";
import { prepareSvgImage, whenVisualReady } from "./visual_assets.js";

// Moon 身体按稀有度缩放，陨坑保持 Mythic 参考大小，边缘宽度固定世界单位。
export const moonSvgGeometry = Object.freeze({
  source: "./assets/petals/moon.svg",
  viewBoxSize: 110,
  artRadius: 25.5,
  liveRadiusFactor: 0.16,
  baseRadiusUnits: 20,
  radiusStepUnits: 10,
  textureReferenceLevel: 6,
  // 边缘厚度以 Mythic 参考换算屏幕宽度，只随相机缩放。
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
  // 前两条路径分别为外轮廓和内底色，后续为陨坑/高光纹理。
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

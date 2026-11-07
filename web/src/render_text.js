import { UI_FONT_FAMILY } from "./ui_typography.js";
import { dom, state } from "./app_context.js";

// 文字按内容/样式及量化像素尺寸缓存，减少逐实体字形栅格化。
// 字体尚未加载时直接绘制，避免缓存回退字体。
const outlinedTextCache = new Map();
let outlinedTextCacheBytes = 0;
const outlinedTextCacheMaxBytes = 48 * 1024 * 1024;
const outlinedTextSizeLogRatio = Math.log(1.12);

function outlinedTextRaster(text, font, fill, stroke, lineWidthRatio, baseline) {
  const sizeMatch = /([\d.]+)px/.exec(font);
  if (!sizeMatch) return null;
  const cssSize = parseFloat(sizeMatch[1]);
  const deviceSize = Math.max(4, Math.min(220, cssSize * (state.dpr || 1)));
  const bucket = Math.ceil(
    Math.log(deviceSize) / outlinedTextSizeLogRatio - 1e-6,
  );
  const rasterSize = Math.exp(bucket * outlinedTextSizeLogRatio);
  const family = font.slice(sizeMatch.index + sizeMatch[0].length);
  const key = `${bucket}|${fill}|${stroke}|${lineWidthRatio}|${baseline}|${font.slice(0, sizeMatch.index)}|${family}|${text}`;
  let entry = outlinedTextCache.get(key);
  if (entry) {
    entry.lastUsed = performance.now();
    return entry;
  }

  const rasterFont = `${font.slice(0, sizeMatch.index)}${rasterSize.toFixed(2)}px${font.slice(sizeMatch.index + sizeMatch[1].length + 2)}`;
  const lineWidth = lineWidthRatio * rasterSize;
  const measure = document.createElement("canvas").getContext("2d");
  if (!measure) return null;
  measure.font = rasterFont;
  measure.textBaseline = baseline;
  const metrics = measure.measureText(text);
  const ascent = metrics.actualBoundingBoxAscent;
  const descent = metrics.actualBoundingBoxDescent;
  const bearing = metrics.actualBoundingBoxLeft;
  if (![ascent, descent, bearing, metrics.actualBoundingBoxRight].every(Number.isFinite)) return null;
  const pad = Math.ceil(lineWidth + rasterSize * 0.08) + 2;
  const width = Math.ceil(Math.max(1, bearing + metrics.actualBoundingBoxRight) + pad * 2);
  const height = Math.ceil(Math.max(1, ascent + descent) + pad * 2);

  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  if (!context) return null;
  context.font = rasterFont;
  context.textAlign = "left";
  context.textBaseline = baseline;
  context.lineJoin = "round";
  context.lineWidth = lineWidth;
  context.strokeStyle = stroke;
  context.fillStyle = fill;
  context.strokeText(text, pad + bearing, pad + ascent);
  context.fillText(text, pad + bearing, pad + ascent);

  entry = {
    canvas,
    pad,
    ascent,
    descent,
    bearing,
    textWidth: Math.max(1, metrics.width),
    rasterSize,
    bytes: width * height * 4,
    lastUsed: performance.now(),
  };
  outlinedTextCache.set(key, entry);
  outlinedTextCacheBytes += entry.bytes;
  if (outlinedTextCacheBytes > outlinedTextCacheMaxBytes)
    evictOutlinedTextRasters();
  return entry;
}

function evictOutlinedTextRasters() {
  const entries = Array.from(outlinedTextCache.entries()).sort(
    (a, b) => a[1].lastUsed - b[1].lastUsed,
  );
  for (const [key, entry] of entries) {
    if (outlinedTextCacheBytes <= outlinedTextCacheMaxBytes * 0.75) break;
    outlinedTextCache.delete(key);
    outlinedTextCacheBytes -= entry.bytes;
  }
}

function clearTextRasters() {
  outlinedTextCache.clear();
  outlinedTextCacheBytes = 0;
}
document.fonts?.addEventListener('loadingdone', clearTextRasters);

export function drawOutlinedText(
  text,
  x,
  y,
  {
    font = `700 14px ${UI_FONT_FAMILY}`,
    fill = "#fff",
    stroke = "#050506",
    lineWidth = 4,
    align = "center",
    baseline = "middle",
  } = {},
  ctx = dom.ctx,
) {
  const label = String(text ?? "");
  if (!label) return;

  if (document.fonts?.status === "loaded" && ctx.direction !== "rtl") {
    const sizeMatch = /([\d.]+)px/.exec(font);
    const cssSize = sizeMatch ? parseFloat(sizeMatch[1]) : 0;
    if (cssSize > 0) {
      const lineWidthRatio =
        Math.round(((lineWidth || 0) / cssSize) * 20) / 20;
      const entry = outlinedTextRaster(label, font, fill, stroke, lineWidthRatio, baseline);
      if (entry) {
        // 从缓存尺寸还原到实际请求尺寸。
        const scale = cssSize / entry.rasterSize;
        const padCss = entry.pad * scale;
        const textWidthCss = entry.textWidth * scale;
        const ascentCss = entry.ascent * scale;
        let left = x - padCss - entry.bearing * scale;
        if (align === "center") left -= textWidthCss * 0.5;
        else if (align === "right" || align === "end") left -= textWidthCss;
        const top = y - padCss - ascentCss;
        ctx.drawImage(
          entry.canvas,
          left,
          top,
          entry.canvas.width * scale,
          entry.canvas.height * scale,
        );
        return;
      }
    }
  }

  ctx.font = font;
  ctx.textAlign = align;
  ctx.textBaseline = baseline;
  ctx.lineJoin = "round";
  ctx.lineWidth = lineWidth;
  ctx.strokeStyle = stroke;
  ctx.fillStyle = fill;
  ctx.strokeText(text, x, y);
  ctx.fillText(text, x, y);
}

import { dom, state } from "./app_context.js";

// Outlined-text raster cache. strokeText/fillText shape and rasterize glyphs
// on every call — with a name and a level label above every mob that was the
// single largest per-entity draw cost. Text/style combos repeat heavily, so
// each combo is rendered once per quantized device-pixel size and blitted
// afterwards. Falls back to direct text while the game fonts still load so a
// fallback-font raster never sticks in the cache.
const outlinedTextCache = new Map();
let outlinedTextCacheBytes = 0;
const outlinedTextCacheMaxBytes = 48 * 1024 * 1024;
const outlinedTextSizeLogRatio = Math.log(1.12);

function outlinedTextRaster(text, font, fill, stroke, lineWidthRatio) {
  const sizeMatch = /([\d.]+)px/.exec(font);
  if (!sizeMatch) return null;
  const cssSize = parseFloat(sizeMatch[1]);
  const deviceSize = Math.max(4, Math.min(220, cssSize * (state.dpr || 1)));
  const bucket = Math.ceil(
    Math.log(deviceSize) / outlinedTextSizeLogRatio - 1e-6,
  );
  const rasterSize = Math.exp(bucket * outlinedTextSizeLogRatio);
  const key = `${bucket}|${fill}|${stroke}|${lineWidthRatio}|${font.slice(0, sizeMatch.index)}|${text}`;
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
  const metrics = measure.measureText(text);
  const ascent = Math.max(
    metrics.actualBoundingBoxAscent || rasterSize * 0.8,
    rasterSize * 0.6,
  );
  const descent = Math.max(
    metrics.actualBoundingBoxDescent || rasterSize * 0.25,
    rasterSize * 0.05,
  );
  const pad = Math.ceil(lineWidth + rasterSize * 0.08) + 2;
  const width = Math.ceil(Math.max(1, metrics.width) + pad * 2);
  const height = Math.ceil(ascent + descent + pad * 2);

  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  if (!context) return null;
  context.font = rasterFont;
  context.textAlign = "left";
  context.textBaseline = "alphabetic";
  context.lineJoin = "round";
  context.lineWidth = lineWidth;
  context.strokeStyle = stroke;
  context.fillStyle = fill;
  context.strokeText(text, pad, pad + ascent);
  context.fillText(text, pad, pad + ascent);

  entry = {
    canvas,
    pad,
    ascent,
    descent,
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

let uiFontsReady = false;

function outlinedTextFontsReady() {
  if (uiFontsReady) return true;
  uiFontsReady = !!document.fonts && document.fonts.status === "loaded";
  return uiFontsReady;
}

export function drawOutlinedText(
  text,
  x,
  y,
  {
    font = "700 14px Florr UI, Sarasa SC, Segoe UI, Microsoft YaHei, sans-serif",
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

  if (outlinedTextFontsReady()) {
    const sizeMatch = /([\d.]+)px/.exec(font);
    const cssSize = sizeMatch ? parseFloat(sizeMatch[1]) : 0;
    if (cssSize > 0) {
      const lineWidthRatio =
        Math.round(((lineWidth || 0) / cssSize) * 20) / 20;
      const entry = outlinedTextRaster(label, font, fill, stroke, lineWidthRatio);
      if (entry) {
        // Scale from the raster bucket back to the exact requested size.
        const scale = cssSize / entry.rasterSize;
        const padCss = entry.pad * scale;
        const textWidthCss = entry.textWidth * scale;
        const ascentCss = entry.ascent * scale;
        const descentCss = entry.descent * scale;
        let left = x - padCss;
        if (align === "center") left -= textWidthCss * 0.5;
        else if (align === "right" || align === "end") left -= textWidthCss;
        let top = y - padCss;
        if (baseline === "middle") top -= (ascentCss + descentCss) * 0.5;
        else if (baseline === "bottom") top -= ascentCss + descentCss;
        else if (baseline === "alphabetic") top -= ascentCss;
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

// Keep texture coordinates on one continuous world -> screen transform.
// Pixel rounding belongs only to coverage/clip boundaries, never image scale.
export function pixelCoverage(from, to, dpr) {
  const x = Math.round(from.x * dpr) / dpr, y = Math.round(from.y * dpr) / dpr;
  return { x, y, width: Math.round(to.x * dpr) / dpr - x, height: Math.round(to.y * dpr) / dpr - y };
}

// Preserve the full raster, including any sampling gutters. Its destination
// remains fractional; disjoint integer clips assign each screen pixel once.
export function drawRasterImage(ctx, image, x, y, width, height) {
  const sw = image.naturalWidth || image.width, sh = image.naturalHeight || image.height;
  ctx.drawImage(image, 0, 0, sw, sh, x, y, width, height);
}

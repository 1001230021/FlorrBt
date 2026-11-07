// 纹理使用连续的世界到屏幕变换；仅覆盖和裁剪边界取整，缩放比例不取整。
export function pixelCoverage(from, to, dpr) {
  const x = Math.round(from.x * dpr) / dpr, y = Math.round(from.y * dpr) / dpr;
  return { x, y, width: Math.round(to.x * dpr) / dpr - x, height: Math.round(to.y * dpr) / dpr - y };
}

// 保留含采样边缘的整张图像；目标位置保持小数，用不相交的整数裁剪避免重复覆盖。
export function drawRasterImage(ctx, image, x, y, width, height) {
  // 仅有 viewBox 的 SVG 固有尺寸由浏览器决定，不能据此裁剪源图，否则会放大局部或切掉瓦片。
  ctx.drawImage(image, x, y, width, height);
}

function clampUnit(value) {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.min(1, value));
}

function capsulePath(ctx, x, y, rectangleLength, width) {
  const diameter = Math.max(0, width);
  const radius = diameter * 0.5;
  const bodyLength = Math.max(0, rectangleLength);
  const centerY = y + radius;
  const leftCenterX = x + radius;
  const rightCenterX = leftCenterX + bodyLength;

  ctx.beginPath();
  ctx.arc(leftCenterX, centerY, radius, Math.PI * 0.5, Math.PI * 1.5);
  ctx.lineTo(rightCenterX, y);
  ctx.arc(rightCenterX, centerY, radius, -Math.PI * 0.5, Math.PI * 0.5);
  ctx.closePath();
}

export function traceSolidProgressBarPath(
  ctx,
  { x, y, maxLength, width, progress = 1 },
) {
  if (!ctx) return null;

  const safeLength = Math.max(0, Number(maxLength) || 0);
  const safeWidth = Math.min(safeLength, Math.max(0, Number(width) || 0));
  const amount = clampUnit(progress);
  if (safeLength <= 0 || safeWidth <= 0 || amount <= 0) return null;

  const rawRectangleLength = safeLength * amount - safeWidth;
  const rectangleLength = Math.max(0, rawRectangleLength);
  const capAlpha =
    rawRectangleLength < 0
      ? clampUnit(1 + rawRectangleLength / safeWidth)
      : 1;
  capsulePath(ctx, x, y, rectangleLength, safeWidth);
  return { alpha: capAlpha, rectangleLength, width: safeWidth };
}

export function drawSolidProgressBar(
  ctx,
  {
    x,
    y,
    maxLength,
    width,
    progress = 1,
    color = "#fff",
    alpha = 1,
  },
) {
  if (!ctx) return;

  const baseAlpha = clampUnit(alpha);
  if (baseAlpha <= 0) return;

  ctx.save();
  const geometry = traceSolidProgressBarPath(ctx, {
    x,
    y,
    maxLength,
    width,
    progress,
  });
  if (!geometry) {
    ctx.restore();
    return;
  }
  ctx.globalAlpha *= baseAlpha * geometry.alpha;
  ctx.fillStyle = color;
  ctx.fill();
  ctx.restore();
}

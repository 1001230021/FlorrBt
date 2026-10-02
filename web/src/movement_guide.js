export function drawMovementGuide(ctx, center, pointer, radius) {
  const dx = pointer.x - center.x, dy = pointer.y - center.y;
  const distance = Math.hypot(dx, dy);
  if (!(distance > radius + 18)) return;
  const length = Math.min(260, distance - 14);
  const start = Math.max(12, radius * 1.3);
  if (length <= start + 10) return;
  ctx.save();
  ctx.translate(center.x, center.y);
  ctx.rotate(Math.atan2(dy, dx));
  ctx.globalAlpha *= Math.min(0.13, (distance - radius - 18) / 800);
  ctx.strokeStyle = "#17140f";
  ctx.lineWidth = 12;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.beginPath();
  ctx.moveTo(start, 0); ctx.lineTo(length, 0);
  ctx.moveTo(length - 24, -17); ctx.lineTo(length, 0); ctx.lineTo(length - 24, 17);
  ctx.stroke(); ctx.restore();
}

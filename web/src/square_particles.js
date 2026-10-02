// Shared craft/burrow particle appearance. Coordinates and size use the
// caller's units; crafting supplies DPR, the world supplies a camera matrix.
export function drawSquareParticle(ctx, particle, scale = 1) {
  if (!(particle.life > 0) || !(scale > 0)) return;
  const progress = Math.max(0, Math.min(1, particle.age / particle.life));
  if (progress >= 1) return;
  const size = particle.size * (1 - progress * 0.45) * scale;
  if (!(size > 0)) return;
  ctx.save();
  ctx.translate(particle.x * scale, particle.y * scale);
  ctx.rotate(particle.rot);
  ctx.globalAlpha *= (1 - progress) * (1 - progress);
  ctx.fillStyle = particle.color;
  ctx.fillRect(-size / 2, -size / 2, size, size);
  ctx.restore();
}

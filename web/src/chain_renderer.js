const CHAIN_PREFIX = "chain:";

export function parseChainSnapshot(name) {
  const parts = String(name || "").split(":");
  if (parts.length !== 8 || parts[0] !== "chain") return null;
  const values = parts.slice(1).map(Number);
  if (values.some((value) => !Number.isFinite(value))) return null;
  const [aId, aGeneration, bId, bGeneration, maxLength, integrity, tension] = values;
  if (aId < 0 || bId < 0 || aGeneration <= 0 || bGeneration <= 0) return null;
  return { aId, aGeneration, bId, bGeneration, maxLength, integrity, tension };
}

export function isChainSnapshot(name) {
  return String(name || "").startsWith(CHAIN_PREFIX);
}

export function drawChain(ctx, first, second, tension = 0) {
  if (!first || !second) return;
  const a = first.pos;
  const b = second.pos;
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const distance = Math.hypot(dx, dy);
  if (!(distance > 1)) return;

  ctx.save();
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.strokeStyle = tension > 0.65 ? "#f0f2f2" : "#d8dddd";
  ctx.lineWidth = 3.2;
  ctx.beginPath();
  ctx.moveTo(a.x, a.y);
  ctx.lineTo(b.x, b.y);
  ctx.stroke();
  ctx.restore();
}

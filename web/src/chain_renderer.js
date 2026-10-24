const CHAIN_PREFIX = "chain:";

export function parseChainSnapshot(name) {
  const parts = String(name || "").split(":");
  if ((parts.length !== 8 && parts.length !== 9) || parts[0] !== "chain") return null;
  const values = parts.slice(1).map(Number);
  if (values.some((value) => !Number.isFinite(value))) return null;
  const [aId, aGeneration, bId, bGeneration, maxLength, integrity, tension] = values;
  const elasticity = values.length >= 8 ? values[7] : -1;
  if (!Number.isSafeInteger(aId) || !Number.isSafeInteger(bId) || aId < 0 || bId < 0 ||
      aId > 0xffffffff || bId > 0xffffffff || !/^\d+$/.test(parts[2]) || !/^\d+$/.test(parts[4])) return null;
  const aGenerationExact = BigInt(parts[2]), bGenerationExact = BigInt(parts[4]);
  if (aGenerationExact <= 0n || bGenerationExact <= 0n ||
      aGenerationExact > 0xffffffffffffffffn || bGenerationExact > 0xffffffffffffffffn) return null;
  return { aId, aGeneration, bId, bGeneration, maxLength, integrity, tension, elasticity,
    aGenerationKey: aGenerationExact.toString(16).padStart(16, "0"),
    bGenerationKey: bGenerationExact.toString(16).padStart(16, "0") };
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

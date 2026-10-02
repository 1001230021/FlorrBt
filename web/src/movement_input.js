// Analog input helpers. Axis length is the requested fraction of full speed;
// the existing signed-byte wire axes carry it without a protocol extension.
export function clampMoveIntent(x, y) {
  if (!Number.isFinite(x) || !Number.isFinite(y)) return { x: 0, y: 0 };
  const length = Math.hypot(x, y);
  const scale = length > 1 ? 1 / length : 1;
  return { x: x * scale, y: y * scale };
}

export function getPointerMoveIntent(dx, dy, fullSpeedRadius, deadzone = 0) {
  if (!Number.isFinite(fullSpeedRadius) || fullSpeedRadius <= 0) return { x: 0, y: 0 };
  const length = Math.hypot(dx, dy);
  const inner = Math.max(0, Math.min(deadzone, fullSpeedRadius * 0.5));
  if (!Number.isFinite(length) || length <= inner) return { x: 0, y: 0 };
  // Continuous from the edge of the dead zone; no jump from zero to 10%.
  const strength = Math.min(1, (length - inner) / (fullSpeedRadius - inner));
  return { x: dx / length * strength, y: dy / length * strength };
}

export function quantizeMoveIntent(x, y) {
  const intent = clampMoveIntent(x, y);
  // Use the same quantized intent for local prediction and the server.
  return {
    x: Math.round(intent.x * 127) / 127,
    y: Math.round(intent.y * 127) / 127,
  };
}

let preparationMs = 0;

export function recordRenderPreparationCost(ms) {
  if (Number.isFinite(ms) && ms > 0) preparationMs += ms;
}

export function takeRenderPreparationCost() {
  const value = preparationMs;
  preparationMs = 0;
  return value;
}

export function stepRenderZoom(current, target, dt) {
  if (!Number.isFinite(current)) return target;
  if (!(dt > 0)) return current;
  const result = current + (target - current) * -Math.expm1(-18 * dt);
  return Math.abs(result - target) < 0.01 ? target : result;
}

export function getRenderFrameTiming(elapsed) {
  const dt = Number.isFinite(elapsed) ? Math.max(0, Math.min(0.25, elapsed)) : 0;
  const steps = Math.max(1, Math.min(8, Math.ceil(dt * 60 - 1e-6)));
  return { dt, steps, step: dt / steps };
}

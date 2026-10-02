// Unified petal-card vanish animation.
//
// Every place a petal card disappears routes through the same pose function:
// over the given duration the card spins one full turn (2π) while fading to
// transparent and shrinking to nothing. Consumers:
//   - canvas world drop cards (game.js drawEntityVisual, dying drops), driven
//     by the entity appearance/exit timeline (entity_motion.js);
//   - DOM cards leaving the UI (failed-craft losses, future card removals),
//     driven by animatePetalCardVanish below.

import { clientRuntimeConfig } from "./client_config.js";

/**
 * The single source of truth for the vanish motion. `progress` runs 0..1 over
 * the chosen duration. Smooth radial/angular endpoints keep the card
 * readable at first and prevent an abrupt spin start/stop; the turn is 2π.
 */
export function petalCardVanishPose(progress) {
  const p = Number.isFinite(progress) ? Math.max(0, Math.min(1, progress)) : 0;
  const eased = p * p * (3 - 2 * p);
  return {
    rotation: eased * Math.PI * 2,
    scale: 1 - eased,
    alpha: 1 - p * p,
  };
}

const activeVanishes = new WeakMap();

function prefersReducedMotion() {
  return !!globalThis.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches;
}

/**
 * Drive the unified vanish pose on a DOM card element via inline transform
 * and opacity. `baseTransform` preserves the element's layout centering
 * (petal stacks center themselves with translate(-50%, -50%)).
 */
export function animatePetalCardVanish(
  element,
  {
    durationMs = clientRuntimeConfig.petalCardVanishDurationMs ?? 700,
    baseTransform = "translate(-50%, -50%)",
    onFinish,
  } = {},
) {
  if (!element) {
    onFinish?.();
    return;
  }
  const previous = activeVanishes.get(element);
  if (previous) previous.cancel();

  const applyPose = (pose) => {
    element.style.transform =
      `${baseTransform} rotate(${pose.rotation}rad) ` +
      `scale(${Math.max(0, pose.scale)})`;
    element.style.opacity = String(Math.max(0, pose.alpha));
  };

  if (prefersReducedMotion() || !(durationMs > 0)) {
    applyPose(petalCardVanishPose(1));
    activeVanishes.delete(element);
    onFinish?.();
    return { cancel() {} };
  }

  const startTime = performance.now();
  const record = { frame: 0, cancelled: false, cancel() {
    this.cancelled = true;
    cancelAnimationFrame(this.frame);
    if (activeVanishes.get(element) === this) activeVanishes.delete(element);
  } };
  activeVanishes.set(element, record);
  const tick = (now) => {
    if (record.cancelled) return;
    const progress = (now - startTime) / durationMs;
    applyPose(petalCardVanishPose(progress));
    if (progress < 1) {
      record.frame = requestAnimationFrame(tick);
      return;
    }
    activeVanishes.delete(element);
    onFinish?.();
  };
  record.frame = requestAnimationFrame(tick);
  return record;
}

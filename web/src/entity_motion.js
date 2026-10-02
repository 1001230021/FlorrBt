import { petalCardVanishPose } from "./card_motion.js";
import { clientRuntimeConfig } from "./client_config.js";

const rest = Object.freeze({ alpha: 1, scale: 1, rotation: 0 });
const clamp01 = value => Math.max(0, Math.min(1, value));

export function getEntityAppearance(entity) {
  const motion = entity?.visualMotion;
  if (!motion) return rest;
  const p = clamp01(motion.age / motion.duration);
  const eased = p * p * (3 - 2 * p);
  if (motion.kind === "exit") {
    const growth = motion.mob ? clientRuntimeConfig.deathScaleBoost : motion.defeated ? 0.12 : 0;
    const pose = motion.card ? petalCardVanishPose(p)
      : { alpha: 1 - eased, scale: 1 + growth * eased, rotation: 0 };
    return { alpha: motion.from.alpha * pose.alpha, scale: motion.from.scale * pose.scale,
      rotation: motion.from.rotation + pose.rotation };
  }
  const settle = 1 - (1 - p) ** 3;
  return {
    alpha: motion.from.alpha + (1 - motion.from.alpha) * settle,
    scale: motion.from.scale + (1 - motion.from.scale) * settle,
    // Finish the partial turn rather than reversing direction on reappearance.
    rotation: motion.from.rotation + (motion.targetRotation - motion.from.rotation) * settle,
  };
}

export function beginEntityAppearance(entity, { immediate = false, card = false } = {}) {
  entity.visualMotion = immediate ? null : {
    kind: "enter", age: 0, duration: card ? 0.18 : 0.12,
    from: { alpha: 0, scale: card ? 0.88 : 0.97, rotation: 0 }, targetRotation: 0,
  };
}

export function beginEntityExit(entity, now, { card = false, defeated = false, mob = false } = {}) {
  if (entity.dying) return;
  const from = getEntityAppearance(entity);
  entity.visualMotion = { kind: "exit", age: 0,
    duration: card ? 0.3 : mob ? clientRuntimeConfig.deathFadeDuration : defeated ? 0.24 : 0.16,
    from: { ...from }, card, defeated, mob };
  // A retiring moving mob carries its last trajectory through the short
  // fade. Cards keep their established in-place vanish; stale samples and
  // teleports must never fling scenery across the screen.
  const velocity = entity.snapshotVelocity;
  const fresh = now - (entity.snapshotAt ?? now) < 0.2;
  if (!card && fresh && entity.renderPos && Number.isFinite(velocity?.x) && Number.isFinite(velocity?.y)) {
    const speed = Math.hypot(velocity.x, velocity.y);
    const duration = entity.visualMotion.duration;
    const maxDistance = Math.min(80, Math.max(4, (entity.snapshot?.radius || 0) * 0.65));
    const scale = speed > 0 ? Math.min(1, maxDistance * 6 / (speed * (1 - Math.exp(-6 * duration)))) : 0;
    entity.visualMotion.coast = { origin: { ...entity.renderPos }, velocity: { x: velocity.x * scale, y: velocity.y * scale } };
  }
  entity.dying = true;
  entity.deathAge = 0;
  // Body parts stop receiving wall-clock motion when the entity retires.
  entity.retirementTime = now;
}

export function resumeEntityAppearance(entity) {
  if (!entity.dying) return;
  const from = getEntityAppearance(entity);
  entity.visualMotion = { kind: "enter", age: 0, duration: 0.12, from: { ...from },
    targetRotation: Math.ceil(from.rotation / (Math.PI * 2)) * Math.PI * 2 };
  entity.dying = false;
  entity.deathAge = 0;
  entity.retirementTime = null;
}

// Returns true only when an exiting entity is ready for visual retirement.
export function stepEntityAppearance(entity, dt) {
  if (!Number.isFinite(dt) || dt < 0) return false;
  if (entity.dying) entity.deathAge = (entity.deathAge || 0) + dt;
  const motion = entity.visualMotion;
  if (!motion) return entity.dying && entity.deathAge >= 0.25;
  motion.age = Math.min(motion.duration, motion.age + dt);
  if (motion.coast) {
    const displacementTime = -Math.expm1(-6 * motion.age) / 6;
    entity.renderPos = {
      x: motion.coast.origin.x + motion.coast.velocity.x * displacementTime,
      y: motion.coast.origin.y + motion.coast.velocity.y * displacementTime,
    };
  }
  if (motion.age < motion.duration) return false;
  if (motion.kind === "exit") return true;
  entity.visualMotion = null;
  return false;
}

export function getDropCardTilt(entityId, time) {
  return Math.sin(entityId * 2.39996 + time * 1.3) * 0.08;
}

// Restore the old 20Hz displacement response (0.16 deadzone / 1.25 ramp),
// but derive that displacement from velocity so packet rate cannot change
// stride strength. Dividing by body radius made giant mobs barely animate.
export function stepEntityGait(entity, dt, now, snapshotInterval = 0.05) {
  const age = Math.max(0, now - (entity.snapshotAt ?? now));
  const grace = Math.max(0.12, snapshotInterval * 2.5);
  const freshness = clamp01(1 - Math.max(0, age - grace) / 0.15);
  const speed = Math.hypot(entity.snapshotVelocity?.x || 0, entity.snapshotVelocity?.y || 0);
  const referenceMove = speed * 0.05;
  const target = clamp01((referenceMove - 0.16) / 1.25) * freshness;
  const current = entity.motionBlend || 0;
  entity.motionBlend = current + (target - current) * (1 - Math.exp(-(target > current ? 18 : 14) * dt));
}

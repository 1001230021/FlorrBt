// Death presentation is separate from authoritative position / physics. Use
// the last observed velocity once, then an analytic exponential coast; never
// integrate dead input, collisions or a new snapshot's zero velocity.
const finitePoint = value => !!value && Number.isFinite(value.x) && Number.isFinite(value.y);
const damping = 5.5;
const coastSeconds = 0.8;

export function beginDeathMotion(entity, now, { velocity = null, resting = false } = {}) {
  if (entity.deathMotion) return entity.deathMotion;
  const anchor = finitePoint(entity.snapshot?.pos) ? entity.snapshot.pos : { x: 0, y: 0 };
  const radius = Math.max(1, Number(entity.snapshot?.radius) || 1);
  const visible = finitePoint(entity.renderPos) ? entity.renderPos : anchor;
  // A discontinuous world transfer is not a velocity sample.
  const continuous = Math.hypot(visible.x - anchor.x, visible.y - anchor.y) <= Math.max(160, radius * 6);
  const origin = continuous ? visible : anchor;
  const source = finitePoint(velocity) ? velocity : entity.snapshotVelocity;
  let vx = !resting && continuous && finitePoint(source) ? source.x : 0;
  let vy = !resting && continuous && finitePoint(source) ? source.y : 0;
  const speed = Math.hypot(vx, vy);
  const maxSpeed = Math.min(320, radius * 3) * damping;
  if (speed > maxSpeed) { vx *= maxSpeed / speed; vy *= maxSpeed / speed; }
  const roll = vx < -1 ? -0.45 : 0.35;
  const motion = {
    started: now, age: resting ? coastSeconds : 0,
    origin: { ...origin }, anchor: { ...anchor }, velocity: { x: vx, y: vy },
    roll, radius,
  };
  entity.deathMotion = motion;
  return motion;
}

export function stepDeathMotion(entity, dt) {
  const motion = entity.deathMotion;
  if (!motion) return false;
  const delta = Number.isFinite(dt) ? Math.max(0, dt) : 0;
  motion.age = Math.min(coastSeconds, motion.age + delta);
  const distanceTime = -Math.expm1(-damping * motion.age) / damping;
  entity.renderPos = {
    x: motion.origin.x + motion.velocity.x * distanceTime,
    y: motion.origin.y + motion.velocity.y * distanceTime,
  };
  entity.serverRenderPos = { ...motion.anchor };
  return true;
}

export function getDeathTilt(entity) {
  const motion = entity?.deathMotion;
  return motion ? motion.roll * (1 - Math.exp(-8 * motion.age)) : 0;
}

export function clearDeathMotion(entity) {
  entity.deathMotion = null;
}

export function stepDeathViewFactor(current, dead, dt, target = 0.6) {
  const from = Number.isFinite(current) && current > 0 ? current : 1;
  const to = dead ? Math.max(0.45, Math.min(1, target)) : 1;
  const next = from + (to - from) * (1 - Math.exp(-(dead ? 6 : 12) * Math.max(0, Number(dt) || 0)));
  return Math.abs(next - to) < 0.0001 ? to : next;
}

export function captureDeathLoadout(snap) {
  return (snap?.primarySlots || []).filter(slot => slot?.petalType > 0 && slot.rarity > 0)
    .slice(0, 16).map(slot => ({
      petalType: slot.petalType, rarity: slot.rarity,
      copies: Array.isArray(slot.copies) ? slot.copies.map(copy => ({ ...copy })) : [],
    }));
}

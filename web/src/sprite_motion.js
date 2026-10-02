// Integrate angular velocity instead of multiplying wall-clock time by the
// latest speed. A speed change must never teleport an insect's legs/wings.
// The base, hit-flash and hallucination passes share a pose for a given time.
const TAU = Math.PI * 2;
const phases = new Map();
const MAX_ENTITIES = 2048;

export function getSpritePhase(entityId, channel, time, rate, offset = 0) {
  if (!Number.isFinite(time) || !Number.isFinite(rate)) return offset;
  let channels = phases.get(entityId);
  if (!channels) {
    if (phases.size >= MAX_ENTITIES) phases.delete(phases.keys().next().value);
    channels = new Map();
    phases.set(entityId, channels);
  }
  let record = channels.get(channel);
  if (!record || time < record.time) {
    record = { time, rate: Math.max(0, rate), phase: ((offset % TAU) + TAU) % TAU };
    channels.set(channel, record);
  } else if (time > record.time) {
    const dt = Math.min(0.1, time - record.time);
    record.phase = (record.phase + dt * (record.rate + Math.max(0, rate)) * 0.5) % TAU;
    record.time = time;
    record.rate = Math.max(0, rate);
  }
  return record.phase;
}

export function getLimbMotionAmount(motion) {
  const value = Math.max(0, Math.min(1, Number(motion) || 0));
  // Original limb deadzone/amplitude; phase integration above only prevents
  // speed changes from jumping the pose and must not damp the movement.
  return value < 0.12 ? 0 : value;
}

export function clearSpriteMotion(entityId) { phases.delete(entityId); }
export function resetSpriteMotion() { phases.clear(); }

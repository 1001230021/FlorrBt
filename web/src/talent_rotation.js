const TAU = Math.PI * 2;
export const talentRotationConfig = Object.freeze({ follow: 18, friction: 9, maxVelocity: 4, wheelScale: 0.0026 });

export function createTalentRotation(angle = 0) {
  return { angle, target: angle, velocity: 0 };
}

export function stopTalentRotation(motion) {
  motion.target = motion.angle;
  motion.velocity = 0;
}

export function talentDragAngle(pivot, previous, next) {
  const a = { x: previous.x - pivot.x, y: previous.y - pivot.y };
  const b = { x: next.x - pivot.x, y: next.y - pivot.y };
  const dx = b.x - a.x, dy = b.y - a.y, length2 = dx * dx + dy * dy;
  const t = length2 > 0 ? Math.max(0, Math.min(1, -(a.x * dx + a.y * dy) / length2)) : 0;
  // 靠近圆心时角度不稳定，使用短水平弧，避免越过中心突然翻转半圈。
  if (Math.hypot(a.x + dx * t, a.y + dy * t) < 32)
    return Math.max(-0.5, Math.min(0.5, (next.x - previous.x) / 160));
  return Math.atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y);
}

export function releaseTalentRotation(motion, velocity) {
  motion.velocity = Number.isFinite(velocity)
    ? Math.max(-talentRotationConfig.maxVelocity, Math.min(talentRotationConfig.maxVelocity, velocity)) : 0;
}

// 同时解析积分惯性和跟随，保持30/60/120fps下相同的转角与减速距离。
export function stepTalentRotation(motion, seconds, reducedMotion = false) {
  if (reducedMotion) { motion.angle = motion.target; motion.velocity = 0; return false; }
  const dt = Number.isFinite(seconds) ? Math.max(0, seconds) : 0;
  const { follow: k, friction: f } = talentRotationConfig;
  const a = Math.exp(-k * dt), b = Math.exp(-f * dt), v = motion.velocity;
  const target = motion.target;
  motion.angle = target + (motion.angle - target) * a + v / f * ((1 - a) - k / (k - f) * (b - a));
  motion.target += v / f * (1 - b);
  motion.velocity *= b;
  const moving = Math.abs(motion.target - motion.angle) > 0.0001 || Math.abs(motion.velocity) > 0.001;
  if (!moving) {
    motion.angle = motion.target; motion.velocity = 0;
    // 整圈等价，闲置时归约，避免长时间滚动积累超大角度。
    const turns = Math.round(motion.angle / TAU) * TAU;
    motion.angle -= turns; motion.target -= turns;
  }
  return moving;
}

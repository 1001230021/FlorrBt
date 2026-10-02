import { flagDead, stateDigging } from "./game_ids.js";
import { drawSquareParticle } from "./square_particles.js";

const TAU = Math.PI * 2;
const palette = Object.freeze(["#795039", "#a47148", "#c7955c", "#ddba83"]);
const isDiggingState = state => state?.type === stateDigging;
const digging = snap => !!snap?.states?.some(isDiggingState);
const validPoint = point => Number.isFinite(point?.x) && Number.isFinite(point?.y);
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

export function getBurrowTransition(previous, next, continuous = true) {
  // Only compare consecutive observations of the same living entity. Joining
  // mid-burrow, visibility changes, respawns and teleports aren't emergence.
  if (!continuous || !previous || !next || previous.entityId !== next.entityId ||
      previous.entityType !== next.entityType || ((previous.flags | next.flags) & flagDead)) return null;
  if (previous.generation !== next.generation) return null;
  const before = digging(previous), after = digging(next);
  if (before === after || !validPoint(previous.pos) || !validPoint(next.pos)) return null;
  const radius = Math.max(1, Number(previous.radius) || 0, Number(next.radius) || 0);
  if (Math.hypot(next.pos.x - previous.pos.x, next.pos.y - previous.pos.y) > Math.max(200, radius * 6)) return null;
  return after ? "enter" : "exit";
}

export function createBurrowParticles({ random = Math.random, isVisible = () => true,
  reducedMotion = () => !!globalThis.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches } = {}) {
  const particles = [];
  const limit = 192, burstBudget = 96, recharge = 160;
  let budget = burstBudget;
  return {
    observe(previous, next, continuous = true, position = next?.pos) {
      const kind = getBurrowTransition(previous, next, continuous);
      if (!kind || !validPoint(position) || !isVisible(next, position)) return 0;
      const reduced = reducedMotion();
      const requested = reduced ? (kind === "enter" ? 6 : 10) : (kind === "enter" ? 18 : 28);
      const count = Math.min(requested, Math.floor(budget), limit - particles.length);
      if (count < 4) return 0;
      budget -= count;
      const radius = clamp(Number(next.radius) || 1, 1, 2000);
      const entering = kind === "enter";
      const speed = clamp(radius * (entering ? 2.4 : 3.4), 20, 260) * (reduced ? 0.55 : 1);
      for (let i = 0; i < count; i++) {
        const angle = (i + random()) / count * TAU;
        const ring = radius * (entering ? 0.65 + random() * 0.3 : 0.3 + random() * 0.4);
        const velocity = speed * (0.6 + random() * 0.6) * (entering ? -1 : 1);
        particles.push({
          x: position.x + Math.cos(angle) * ring, y: position.y + Math.sin(angle) * ring,
          vx: Math.cos(angle) * velocity, vy: Math.sin(angle) * velocity,
          rot: random() * TAU, vrot: reduced ? 0 : (random() - 0.5) * 10,
          size: clamp(radius * 0.17, 3, 18) * (0.7 + random() * 0.6),
          life: (reduced ? 0.3 : entering ? 0.44 : 0.62) * (0.85 + random() * 0.3),
          age: 0, color: palette[Math.floor(random() * palette.length) % palette.length],
        });
      }
      return count;
    },
    step(dt) {
      if (!Number.isFinite(dt) || dt <= 0) return;
      budget = Math.min(burstBudget, budget + dt * recharge);
      const decay = Math.exp(-3.2 * dt), displacement = (1 - decay) / 3.2;
      let alive = 0;
      for (const particle of particles) {
        particle.age += dt;
        if (particle.age >= particle.life) continue;
        // Analytic drag keeps travel and fade independent of frame rate.
        particle.x += particle.vx * displacement; particle.y += particle.vy * displacement;
        particle.vx *= decay; particle.vy *= decay; particle.rot += particle.vrot * dt;
        particles[alive++] = particle;
      }
      particles.length = alive;
    },
    draw(ctx, scale, origin) {
      if (!particles.length || !Number.isFinite(scale) || scale <= 0 || !validPoint(origin)) return;
      ctx.save(); ctx.globalCompositeOperation = "source-over";
      ctx.translate(origin.x, origin.y); ctx.scale(scale, scale);
      for (const particle of particles) drawSquareParticle(ctx, particle);
      ctx.restore();
    },
    clear() { particles.length = 0; budget = burstBudget; },
    count() { return particles.length; },
  };
}

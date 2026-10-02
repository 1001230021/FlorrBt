// Square-particle layer for the craft stage (florr-style crafting sparks).
//
// A dedicated transparent canvas overlays the craft stage; bursts spawn small
// rotating squares that fly outward, slow down and fade. The rAF loop only
// runs while particles are alive, so an idle craft panel costs nothing.

import { drawSquareParticle } from "./square_particles.js";

const TWO_PI = Math.PI * 2;

/**
 * Create the particle layer for `stageEl` (position: relative container).
 * burst() coordinates are stage-local CSS pixels.
 */
export function createCraftParticleLayer(stageEl) {
  let canvas = null;
  let ctx = null;
  let particles = [];
  let frame = 0;
  let lastTime = 0;
  let widthCss = 1, heightCss = 1;
  let resizeNeeded = true;
  const particleLimit = 192;
  const resizeObserver = typeof ResizeObserver === "function"
    ? new ResizeObserver(() => { resizeNeeded = true; }) : null;
  if (stageEl) resizeObserver?.observe(stageEl);

  const ensureCanvas = () => {
    if (canvas) return !!ctx;
    if (!stageEl) return false;
    canvas = document.createElement("canvas");
    canvas.className = "craft-particle-canvas";
    stageEl.appendChild(canvas);
    ctx = canvas.getContext("2d");
    return !!ctx;
  };

  const syncSize = () => {
    if (resizeNeeded) {
      // Local layout pixels, not an animated ancestor's scaled bounding box.
      widthCss = Math.max(1, stageEl.clientWidth);
      heightCss = Math.max(1, stageEl.clientHeight);
      resizeNeeded = false;
    }
    const dpr = Math.max(1, Math.min(3, globalThis.devicePixelRatio || 1));
    const width = Math.max(1, Math.round(widthCss * dpr));
    const height = Math.max(1, Math.round(heightCss * dpr));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    return dpr;
  };

  const step = (now) => {
    frame = 0;
    const dt = Math.min(0.05, Math.max(0.001, (now - lastTime) / 1000));
    lastTime = now;

    const dpr = syncSize();
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    let alive = 0;
    for (const particle of particles) {
      particle.age += dt;
      if (particle.age >= particle.life) continue;
      const drag = Math.max(0, 1 - particle.drag * dt);
      particle.vx *= drag;
      particle.vy *= drag;
      particle.x += particle.vx * dt;
      particle.y += particle.vy * dt;
      particle.rot += particle.vrot * dt;

      drawSquareParticle(ctx, particle, dpr);

      particles[alive] = particle;
      alive += 1;
    }
    particles.length = alive;

    if (alive > 0) frame = requestAnimationFrame(step);
    else ctx.clearRect(0, 0, canvas.width, canvas.height);
  };

  const start = () => {
    if (frame) return;
    lastTime = performance.now();
    frame = requestAnimationFrame(step);
  };

  return {
    /**
     * Spawn a square-particle burst at stage-local (x, y).
     * options: count, color, speed (px/s), size (px), life (s), spread
     * (radians of the emission arc; default full circle).
     */
    burst(x, y, color, { count = 14, speed = 150, size = 7, life = 0.55, spread = TWO_PI, baseAngle = 0 } = {}) {
      if (!ensureCanvas()) return;
      resizeNeeded = true;
      const spawnCount = Math.max(0, Math.min(Math.floor(count), particleLimit));
      if (particles.length + spawnCount > particleLimit)
        particles.splice(0, particles.length + spawnCount - particleLimit);
      for (let i = 0; i < spawnCount; i += 1) {
        const angle = baseAngle + (spread >= TWO_PI ? Math.random() * TWO_PI : (Math.random() - 0.5) * spread);
        const velocity = speed * (0.45 + Math.random() * 0.9);
        particles.push({
          x,
          y,
          vx: Math.cos(angle) * velocity,
          vy: Math.sin(angle) * velocity,
          rot: Math.random() * TWO_PI,
          vrot: (Math.random() - 0.5) * 10,
          size: size * (0.7 + Math.random() * 0.7),
          life: life * (0.7 + Math.random() * 0.6),
          age: 0,
          drag: 3.2,
          color,
        });
      }
      start();
    },

    clear() {
      particles.length = 0;
      if (frame) cancelAnimationFrame(frame);
      frame = 0;
      if (ctx && canvas) ctx.clearRect(0, 0, canvas.width, canvas.height);
    },
  };
}

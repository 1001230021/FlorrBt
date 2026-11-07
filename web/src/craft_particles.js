// 合成面板独立透明画布绘制方形粒子；仅有活跃粒子时运行动画循环。

import { drawSquareParticle } from "./square_particles.js";

const TWO_PI = Math.PI * 2;

/* 粒子画布附加到相对定位容器，爆发坐标为舞台内 CSS 像素。 */
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
      // 使用本地布局像素，不使用祖先动画缩放后的包围盒。
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
    /* 生成方形粒子；速度为像素/秒、寿命为秒，发射角范围为弧度。 */
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

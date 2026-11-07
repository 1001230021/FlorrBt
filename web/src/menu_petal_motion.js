import { normalizeMenuInventory, sampleMenuPetal } from './menu_inventory.js';

// 参考录屏：水平穿过画面、约 20px 上下轻摆，长条花瓣约 3rad/s 旋转。
export const menuPetalMotion = Object.freeze({ minSpeed: 70, maxSpeed: 200,
  minSpin: 2.4, maxSpin: 3.6, minBirthInterval: .4, maxBirthInterval: .6, limit: 64 });

export function createMenuPetalField({ random = Math.random, limit = menuPetalMotion.limit } = {}) {
  const capacity = Math.max(1, Math.min(menuPetalMotion.limit, Math.floor(limit) || menuPetalMotion.limit));
  let inventory = [], signature = '', particles = [], clock = 0, nextBirth = 0, sequence = 0;
  let viewport = { width: 1910, height: 884, scale: 1 };
  const unit = () => Math.max(0, Math.min(1 - Number.EPSILON, Number(random()) || 0));
  const between = (min, max) => min + (max - min) * unit();

  function pose(particle) {
    particle.y = particle.yRatio * viewport.height + Math.sin(particle.age + particle.phase) * particle.amplitude * viewport.scale;
    particle.angle = particle.initialAngle + particle.angularSpeed * particle.age;
  }

  function spawn() {
    if (particles.length >= capacity) return;
    const item = sampleMenuPetal(inventory, unit());
    if (!item) return;
    const size = between(86, 140);
    const particle = { ...item, id: ++sequence, age: 0, size,
      x: -size * viewport.scale / 2 - 2,
      yRatio: unit(), speed: between(menuPetalMotion.minSpeed, menuPetalMotion.maxSpeed),
      angularSpeed: between(menuPetalMotion.minSpin, menuPetalMotion.maxSpin), initialAngle: unit() * Math.PI * 2,
      phase: unit() * Math.PI * 2, amplitude: between(16, 24), alpha: between(.7, .95) };
    pose(particle); particles.push(particle);
  }

  function advance(target) {
    const dt = Math.max(0, target - clock);
    for (const particle of particles) {
      particle.age += dt; particle.x += particle.speed * viewport.scale * dt; pose(particle);
    }
    particles = particles.filter(particle => particle.x - particle.size * viewport.scale / 2 <= viewport.width);
    clock = target;
  }

  return {
    setInventory(items) {
      const next = normalizeMenuInventory(items);
      const key = next.map(item => `${item.petalType}:${item.rarity}:${item.count}`).join(',');
      if (key === signature) return;
      const wasEmpty = inventory.length === 0;
      inventory = next; signature = key;
      const owned = new Set(next.map(item => `${item.petalType}:${item.rarity}`));
      particles = particles.filter(particle => owned.has(`${particle.petalType}:${particle.rarity}`));
      if (!next.length || wasEmpty) { particles = []; clock = 0; nextBirth = 0; sequence = 0; }
    },
    step(dt, { width, height, scale = 1, enabled = true }) {
      if (!enabled) {
        if (particles.length) particles = [];
        clock = 0; nextBirth = 0; sequence = 0;
        return;
      }
      if (!(width > 0 && height > 0 && scale > 0) || ![width, height, scale].every(Number.isFinite)) return;
      if (width !== viewport.width) for (const particle of particles) particle.x *= width / viewport.width;
      viewport = { width, height, scale };
      if (!inventory.length) return;
      const elapsed = Number.isFinite(dt) ? Math.max(0, Math.min(1, dt)) : 0;
      const target = clock + elapsed;
      const density = Math.max(.5, Math.min(2, height / (884 * scale)));
      // 生成发生在精确时间点，刷新率不会改变出生数量、位移或转角。
      while (nextBirth <= target + 1e-9) {
        advance(Math.min(nextBirth, target)); spawn();
        nextBirth += between(menuPetalMotion.minBirthInterval, menuPetalMotion.maxBirthInterval) / density;
      }
      advance(target);
    },
    get particles() { return particles; },
    get born() { return sequence; },
  };
}

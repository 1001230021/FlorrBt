// A deterministic, screen-space rain field.
//
// RimWorld's rain reads as a quiet, continuous sheet: many short, thin,
// similarly angled strokes over a subdued world. Keeping the field
// deterministic avoids the noisy spawn/cull rhythm of particle sprites and
// makes it cheap at high refresh rates.

const TAU = Math.PI * 2;
const DEFAULT_DROP_COUNT = 720;
const MIN_INTENSITY = 0.005;

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function wrap(value, size) {
  if (!(size > 0)) return 0;
  const result = value % size;
  return result < 0 ? result + size : result;
}

function makeRandom(seed) {
  let value = seed >>> 0;
  return () => {
    value = (value * 1664525 + 1013904223) >>> 0;
    return value / 0x100000000;
  };
}

function normalizedWindDirection(direction) {
  const degrees = Number.isFinite(direction) ? direction : 0;
  return (degrees * Math.PI) / 180;
}

export function getRainLean(windDirection = 0, windSpeed = 0) {
  // The small base lean is intentional: calm rain is not perfectly vertical.
  // The wire format uses 0° as north, matching the existing wind decoder.
  const windX = Math.sin(normalizedWindDirection(windDirection));
  const windStrength = clamp((Number(windSpeed) || 0) / 18, 0, 1);
  return clamp(0.14 + windX * windStrength * 0.18, -0.12, 0.34);
}

export function createRainField({ seed = 0x6d2b79f5, maxDrops = DEFAULT_DROP_COUNT } = {}) {
  const count = Math.max(64, Math.floor(maxDrops));
  const random = makeRandom(seed);
  const drops = Array.from({ length: count }, () => {
    const far = random() < 0.42;
    return {
      x: random(),
      y: random(),
      far,
      speed: far ? 480 + random() * 130 : 760 + random() * 260,
      length: far ? 8 + random() * 5 : 14 + random() * 8,
      width: far ? 0.75 + random() * 0.25 : 0.9 + random() * 0.35,
    };
  });
  let elapsed = 0;

  return {
    step(dt) {
      if (!Number.isFinite(dt) || dt <= 0) return;
      elapsed += Math.min(0.1, dt);
    },

    draw(ctx, width, height, intensity, windDirection = 0, windSpeed = 0) {
      if (!ctx || !(width > 0) || !(height > 0)) return 0;
      const amount = clamp(Number(intensity) || 0, 0, 1);
      if (amount <= MIN_INTENSITY) return 0;

      // A gentle response keeps drizzle sparse while heavy rain becomes a
      // sheet, without ever needing to allocate or splice during a frame.
      const activeCount = Math.min(
        drops.length,
        Math.max(1, Math.floor(drops.length * Math.pow(amount, 0.72))),
      );
      const lean = getRainLean(windDirection, windSpeed);
      const verticalPeriod = height + 48;
      const horizontalPeriod = width + 48;
      const farPath = [];
      const nearPath = [];

      for (let index = 0; index < activeCount; index += 1) {
        const drop = drops[index];
        const x = wrap(drop.x * horizontalPeriod + elapsed * drop.speed * lean, horizontalPeriod) - 24;
        const y = wrap(drop.y * verticalPeriod + elapsed * drop.speed, verticalPeriod) - 24;
        const path = drop.far ? farPath : nearPath;
        path.push(x, y, x + drop.length * lean, y + drop.length);
      }

      ctx.save();
      ctx.globalCompositeOperation = "source-over";
      ctx.lineCap = "butt";
      ctx.lineJoin = "miter";

      const drawPath = (path, far) => {
        if (!path.length) return;
        ctx.beginPath();
        for (let i = 0; i < path.length; i += 4) {
          ctx.moveTo(path[i], path[i + 1]);
          ctx.lineTo(path[i + 2], path[i + 3]);
        }
        ctx.lineWidth = far ? 0.8 : 1.15;
        ctx.strokeStyle = far
          ? `rgba(198, 211, 214, ${0.075 + amount * 0.07})`
          : `rgba(218, 226, 226, ${0.105 + amount * 0.105})`;
        ctx.stroke();
      };
      // Draw the distant veil first, then the slightly brighter short strokes.
      drawPath(farPath, true);
      drawPath(nearPath, false);
      ctx.restore();
      return activeCount;
    },

    reset() {
      elapsed = 0;
    },

    count() {
      return drops.length;
    },
  };
}

export const rainConstants = Object.freeze({
  defaultDropCount: DEFAULT_DROP_COUNT,
  minimumIntensity: MIN_INTENSITY,
  tau: TAU,
});

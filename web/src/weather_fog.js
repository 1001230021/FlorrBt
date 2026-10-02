// Low-resolution, block-preserving weather fog.
// The fog is rendered to a tiny logical canvas and nearest-neighbour scaled
// onto the game canvas. This gives a deliberate tile/block texture while
// keeping the per-frame work bounded.

const MIN_INTENSITY = 0.005;

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function smoothstep(value) {
  const t = clamp(value, 0, 1);
  return t * t * (3 - 2 * t);
}

function hashCell(x, y) {
  let value = Math.imul(x + 0x9e3779b9, 0x85ebca6b);
  value = Math.imul(value ^ (y + 0x7f4a7c15), 0xc2b2ae35);
  value ^= value >>> 16;
  return (value >>> 0) / 0x100000000;
}

export function createBlockFogField({ canvasFactory = null } = {}) {
  let surface = null;
  let surfaceContext = null;
  let columns = 0;
  let rows = 0;

  const createSurface = () => {
    if (canvasFactory) return canvasFactory();
    if (typeof OffscreenCanvas !== "undefined") return new OffscreenCanvas(1, 1);
    if (typeof document !== "undefined") return document.createElement("canvas");
    return null;
  };

  const ensureSurface = (width, height) => {
    // About 44 CSS pixels per block; cap the logical surface so fog never
    // becomes a high-resolution particle layer on a 4K display.
    const nextColumns = clamp(Math.ceil(width / 44), 18, 48);
    const nextRows = clamp(Math.ceil(height / 44), 14, 32);
    if (surface && nextColumns === columns && nextRows === rows) return true;
    surface = createSurface();
    surfaceContext = surface?.getContext?.("2d");
    if (!surfaceContext) return false;
    columns = nextColumns;
    rows = nextRows;
    surface.width = columns;
    surface.height = rows;
    return true;
  };

  return {
    draw(ctx, width, height, intensity, player = null) {
      const amount = clamp(Number(intensity) || 0, 0, 1);
      if (!ctx || amount <= MIN_INTENSITY || !(width > 0) || !(height > 0)) return 0;
      if (!ensureSurface(width, height)) return 0;

      const playerX = Number.isFinite(player?.x) ? player.x : width * 0.5;
      const playerY = Number.isFinite(player?.y) ? player.y : height * 0.5;
      const shortSide = Math.min(width, height);
      const revealRadius = clamp(shortSide * 0.42, 180, 480);
      const fadeRadius = Math.max(80, shortSide * 0.34);

      surfaceContext.save();
      surfaceContext.setTransform?.(1, 0, 0, 1, 0, 0);
      surfaceContext.globalCompositeOperation = "source-over";
      surfaceContext.clearRect(0, 0, columns, rows);
      let painted = 0;
      for (let row = 0; row < rows; row += 1) {
        for (let column = 0; column < columns; column += 1) {
          const centerX = ((column + 0.5) / columns) * width;
          const centerY = ((row + 0.5) / rows) * height;
          const distance = Math.hypot(centerX - playerX, centerY - playerY);
          const distanceFog = smoothstep((distance - revealRadius) / fadeRadius);
          if (distanceFog <= 0.001) continue;

          // Stable variation keeps neighboring blocks from reading as a
          // perfect checkerboard while preserving the intentionally hard block
          // boundaries.
          const variation = 0.78 + hashCell(column, row) * 0.22;
          const alpha = clamp(
            amount * (0.015 + distanceFog * 0.24) * variation,
            0,
            0.34,
          );
          if (alpha <= 0.001) continue;
          surfaceContext.fillStyle = `rgba(137, 149, 148, ${alpha})`;
          surfaceContext.fillRect(column, row, 1, 1);
          painted += 1;
        }
      }
      surfaceContext.restore();

      ctx.save();
      ctx.globalCompositeOperation = "source-over";
      ctx.imageSmoothingEnabled = false;
      ctx.globalAlpha = 1;
      ctx.drawImage(surface, 0, 0, columns, rows, 0, 0, width, height);
      ctx.restore();
      return painted;
    },

    reset() {
      if (!surfaceContext) return;
      surfaceContext.clearRect(0, 0, columns, rows);
    },

    dimensions() {
      return { columns, rows };
    },
  };
}

export const fogConstants = Object.freeze({
  minimumIntensity: MIN_INTENSITY,
  minColumns: 18,
  maxColumns: 48,
  minRows: 14,
  maxRows: 32,
});

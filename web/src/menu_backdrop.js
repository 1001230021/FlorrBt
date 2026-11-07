import { PETAL_IDS } from './generated_ids.js';
import { loadVisualImage } from './visual_assets.js';
import { createMenuPetalField } from './menu_petal_motion.js';

// 菜单装饰复用草地和花瓣素材，只由现有渲染循环驱动，不创建游戏实体。
export function createMenuBackdrop({ getPetalImage, isEnabled = () => true }) {
  const icons = new Map();
  const field = createMenuPetalField();
  let ground = null, pattern = null, patternScale = 0, previous = 0;

  function grass(context, scale) {
    ground ??= [0, 1, 2, 3].map(index => loadVisualImage(`/data/tiles/grass_c_${index}.svg`));
    if (!pattern && ground.every(image => image.visualState === 'ready')) {
      const tile = document.createElement('canvas'); tile.width = tile.height = 1024;
      const drawing = tile.getContext('2d');
      if (!drawing) return null;
      ground.forEach((image, index) => drawing.drawImage(image, (index % 2) * 512, Math.floor(index / 2) * 512, 512, 512));
      pattern = context.createPattern(tile, 'repeat');
    }
    if (pattern && patternScale !== scale) {
      pattern.setTransform(new DOMMatrix().scale(scale)); patternScale = scale;
    }
    return pattern;
  }

  function icon(type, rarity) {
    const key = `${type}:${rarity}`;
    if (!icons.has(key)) icons.set(key, { image: getPetalImage(type, rarity), raster: null });
    const entry = icons.get(key);
    if (!entry.raster && entry.image?.visualState === 'ready') {
      const raster = document.createElement('canvas'); raster.width = raster.height = 192;
      const drawing = raster.getContext('2d');
      if (!drawing) return null;
      drawing.drawImage(entry.image, 0, 0, 192, 192); entry.raster = raster;
    }
    return entry.raster;
  }

  return {
    setInventory(items) {
      field.setInventory(items);
      if (!field.particles.length) icons.clear();
    },
    draw(context, { width, height, dpr, scale, now }) {
      const dt = previous ? Math.min(.1, Math.max(0, (now - previous) / 1000)) : 0;
      previous = now;
      field.step(dt, { width, height, scale, enabled: isEnabled() });
      context.save();
      context.setTransform(dpr, 0, 0, dpr, 0, 0);
      context.globalAlpha = 1; context.globalCompositeOperation = 'source-over'; context.filter = 'none';
      context.fillStyle = grass(context, scale) || '#1ea761';
      context.fillRect(0, 0, width, height);
      context.fillStyle = 'rgba(0, 0, 0, .035)'; context.fillRect(0, 0, width, height);
      const liveKeys = new Set();
      for (const petal of field.particles) {
        liveKeys.add(`${petal.petalType}:${petal.rarity}`);
        const image = icon(petal.petalType, petal.rarity);
        if (!image) continue;
        const size = petal.size * scale * ([PETAL_IDS.plank, PETAL_IDS.bone].includes(petal.petalType) ? .65 : 1);
        context.save(); context.translate(petal.x, petal.y); context.rotate(petal.angle);
        context.globalAlpha = petal.alpha;
        context.drawImage(image, -size / 2, -size / 2, size, size); context.restore();
      }
      for (const key of icons.keys()) if (!liveKeys.has(key)) icons.delete(key);
      context.restore();
    },
  };
}

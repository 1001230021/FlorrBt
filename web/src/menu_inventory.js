import { PETAL_IDS, RARITY_IDS } from './generated_ids.js';

const types = new Set(Object.values(PETAL_IDS));
const rarities = new Set(Object.values(RARITY_IDS));

export function normalizeMenuInventory(items) {
  const merged = new Map();
  for (const item of Array.isArray(items) ? items : []) {
    if (!item || !types.has(item.petalType) || item.petalType <= 0 || !rarities.has(item.rarity) || item.rarity <= 0 ||
        !Number.isSafeInteger(item.count) || item.count <= 0 || item.count > 0xffffffff) continue;
    const key = `${item.petalType}:${item.rarity}`, previous = merged.get(key);
    if (previous) previous.count += item.count;
    else merged.set(key, { petalType: item.petalType, rarity: item.rarity, count: item.count });
  }
  return [...merged.values()].sort((a, b) => a.petalType - b.petalType || a.rarity - b.rarity);
}

export function sampleMenuPetal(items, fraction) {
  const total = items.reduce((sum, item) => sum + item.count, 0);
  if (!items.length || !Number.isFinite(total) || total <= 0) return null;
  let cursor = Math.max(0, Math.min(1 - Number.EPSILON, Number.isFinite(fraction) ? fraction : 0)) * total;
  for (const item of items) {
    cursor -= item.count;
    if (cursor < 0) return { petalType: item.petalType, rarity: item.rarity };
  }
  const last = items.at(-1);
  return { petalType: last.petalType, rarity: last.rarity };
}

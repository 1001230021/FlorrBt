import { PETAL_IDS } from "./generated_ids.js";
import { PetalSlotVisualType, clamp } from "./protocol.js";

// 角标填色匹配实际卡片底色，不使用较暗的通用稀有度文字色。
const slotCounterColors = Object.freeze([
  "#7eef6d", "#ffe65d", "#4d52e3", "#861fde", "#de1f1f", "#1fdbde",
  "#ff2b75", "#2bffa3", "#eee", "#555", "#8a8a8a",
]);

export function getSlotCounterColor(rarity) {
  const index = clamp((rarity || 1) - 1, 0, slotCounterColors.length - 1);
  return slotCounterColors[index] || slotCounterColors[0];
}

// 普通卡返回空值，零计数仍需显示；副槽不因类型相同而借用主槽状态。
export function getSlotCounter(slot, runtime, kind) {
  if (!slot?.petalType || !slot.rarity) return null;
  // 旧服务器曾将闪电接触累计当作 Counter，该值不属于可展示资源。
  if (slot.petalType === PETAL_IDS.lightning) return null;
  const sameSlot = kind === "primary" && runtime?.petalType === slot.petalType && runtime?.rarity === slot.rarity;
  const counterType = slot.petalType === PETAL_IDS.battery || slot.petalType === PETAL_IDS.linker;
  if (sameSlot && runtime.visualType === PetalSlotVisualType.Counter) {
    return (runtime.copies || []).reduce((total, copy) =>
      total + (!runtime.banned && !copy.loading && Number.isFinite(copy.visual)
        ? Math.max(0, Math.floor(copy.visual)) : 0), 0);
  }
  // 尚无该槽快照时不编造实时计数。
  if (counterType) return kind === "secondary" ? 0 : undefined;
  return null;
}

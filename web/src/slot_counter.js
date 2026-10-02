import { PETAL_IDS } from "./generated_ids.js";
import { PetalSlotVisualType, clamp } from "./protocol.js";

// Match the actual card fills (assets/petals/0_0.svg through 0_10.svg).
// The general rarity text palette is darker for several rarities, so it
// would leave a visibly different-coloured tab on the card.
const slotCounterColors = Object.freeze([
  "#7eef6d", "#ffe65d", "#4d52e3", "#861fde", "#de1f1f", "#1fdbde",
  "#ff2b75", "#2bffa3", "#eee", "#555", "#8a8a8a",
]);

export function getSlotCounterColor(rarity) {
  const index = clamp((rarity || 1) - 1, 0, slotCounterColors.length - 1);
  return slotCounterColors[index] || slotCounterColors[0];
}

// Return null for an ordinary card; zero is a real displayed value. Side
// slots have no live petals/charge state, so they must not reuse a primary
// slot's counter merely because its type and rarity happen to match.
export function getSlotCounter(slot, runtime, kind) {
  if (!slot?.petalType || !slot.rarity) return null;
  // Older servers exposed Lightning's internal contact-hit accumulator as
  // Counter. It is not a charge/resource and must not create a badge, even
  // while connecting to a server that still sends that legacy payload.
  if (slot.petalType === PETAL_IDS.lightning) return null;
  const sameSlot = kind === "primary" && runtime?.petalType === slot.petalType && runtime?.rarity === slot.rarity;
  const counterType = slot.petalType === PETAL_IDS.battery;
  if (sameSlot && runtime.visualType === PetalSlotVisualType.Counter) {
    return (runtime.copies || []).reduce((total, copy) =>
      total + (!runtime.banned && !copy.loading && Number.isFinite(copy.visual)
        ? Math.max(0, Math.floor(copy.visual)) : 0), 0);
  }
  // Do not show a fabricated live count while awaiting this slot's snapshot.
  if (counterType) return kind === "secondary" ? 0 : undefined;
  return null;
}

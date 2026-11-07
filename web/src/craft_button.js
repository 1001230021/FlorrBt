import { rarityColor, RarityColors } from "./protocol.js";
import { RARITY_IDS } from "./generated_ids.js";

export function craftResultPalette(sourceRarity, forge = false) {
  if (!Number.isInteger(sourceRarity) || sourceRarity <= 0)
    return { rarity: 0, color: "#999999", border: "#777777" };
  const rarity = forge ? RARITY_IDS.unique : Math.min(RARITY_IDS.unique, sourceRarity + 1);
  const rgb = RarityColors[rarity];
  return { rarity, color: rarityColor(rarity), border: `rgb(${rgb.slice(0, 3).map(v => Math.round(v * .78)).join(", ")})` };
}

export function updateCraftButton(button, source, forge) {
  const palette = craftResultPalette(source?.rarity, forge);
  button.style.setProperty("--craft-target-color", palette.color);
  button.style.setProperty("--craft-target-border", palette.border);
  button.dataset.resultRarity = String(palette.rarity);
}

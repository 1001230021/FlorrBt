import { dom, state } from "./app_context.js";
import {
  antEggMobType,
  antHoleType,
  babyAntType,
  babyFireAntType,
  babyTermiteType,
  bandageBeetleType,
  beeType,
  beetleType,
  bubbleType,
  bumbleBeeType,
  dandelionType,
  dummyType,
  fireAntEggType,
  fireflyType,
  fireQueenAntType,
  hornetType,
  leafPieceType,
  leafcutterSoldierType,
  mechaFlowerType,
  normalFlowerType,
  normalLadybugType,
  queenAntEggType,
  queenAntType,
  queenFireAntEggType,
  rarityDisplayOrder,
  rockType,
  sandstormType,
  soldierAntType,
  soldierFireAntType,
  soldierTermiteType,
  spiderType,
  summonedBeetleType,
  summonedSoldierAntType,
  termiteEggType,
  termiteOvermindType,
  titanType,
  workerAntType,
  workerFireAntType,
  workerTermiteType,
} from "./game_ids.js";
import { rarityExotic } from "./game_ids.js";
import {
  MobNames,
  mobTypeName,
  rarityColor,
  rarityName,
  translationSlug,
} from "./protocol.js";
import { t } from "./i18n.js";
import {
  assetImage,
  formatShortCount,
  imageReady,
  makePetalStack,
  petalBaseImage,
} from "./petal_info.js";
import {
  toggleBackpack,
  toggleCraft,
  toggleSettings,
  toggleTalent,
  updateQuickActionButtons,
} from "./ui_settings.js";

const { codexBtn, codexDetail, codexGrid, codexPanel } = dom;

export const codexMobOrder = [
  beetleType, bandageBeetleType, normalLadybugType, mechaFlowerType, normalFlowerType,
  soldierAntType, soldierFireAntType, soldierTermiteType, summonedBeetleType,
  summonedSoldierAntType, beeType, hornetType, bumbleBeeType, rockType, bubbleType,
  babyAntType, workerAntType, queenAntType, antHoleType, spiderType, sandstormType, dummyType,
  dandelionType, antEggMobType, fireAntEggType, termiteEggType, queenAntEggType,
  queenFireAntEggType, babyFireAntType, workerFireAntType, fireQueenAntType,
  babyTermiteType, workerTermiteType, termiteOvermindType, leafPieceType,
  leafcutterSoldierType, titanType, fireflyType,
];

// No column is carved out for the exotic rarity: like on the server, exotic
// is the same tier as common, so common is the lowest column and exotic
// kills are tallied into it.
export const codexRarityOrder = rarityDisplayOrder.filter(
  (rarity) => rarity !== rarityExotic,
);

export function codexKillCount(mobType, rarity) {
  const count = state.codex.get(codexKey(mobType, rarity)) || 0;
  if (rarity === rarityExotic || rarity === rarityDisplayOrder[0])
    return count + (state.codex.get(codexKey(mobType, rarityExotic)) || 0);
  return count;
}

// Bestiary descriptions live in the locale files (codex.descriptions.<slug>)
// so they follow the display language like every other UI string.
export function codexMobDescription(mobType) {
  const fallback = MobNames[mobType] || "Mob" + mobType;
  return t(`codex.descriptions.${translationSlug(fallback)}`, {}, {
    defaultValue: "",
  });
}

export function codexKey(mobType, rarity) {
  return (mobType << 8) | rarity;
}

export function toggleCodex(force) {
  if (!state.authenticated && force !== false) return false;
  state.codexOpen = force !== undefined ? !!force : !state.codexOpen;
  codexPanel.classList.toggle("open", state.codexOpen);
  codexBtn?.setAttribute("aria-pressed", String(state.codexOpen));
  if (state.codexOpen) {
    toggleBackpack(false);
    toggleCraft(false);
    toggleTalent(false);
    toggleSettings(false);
    renderCodexPanel();
    hideCodexDetail();
  }
  updateQuickActionButtons();
  return state.codexOpen;
}

export function codexKilledMobTypes() {
  const types = new Set();
  for (const key of state.codex.keys()) types.add(key >> 8);
  return codexMobOrder.filter((type) => types.has(type));
}

export function renderCodexPanel() {
  if (!state.codexOpen || !codexGrid) return;
  codexGrid.replaceChildren();
  const mobTypes = codexKilledMobTypes().sort((a, b) =>
    mobTypeName(a).localeCompare(mobTypeName(b)),
  );
  if (mobTypes.length === 0) {
    codexGrid.style.gridTemplateColumns = "repeat(auto-fill, 56px)";
    return;
  }
  // x = rarity (exotic shares the common column; 11 columns total),
  // y = mob type (alphabetical, top to bottom). Skip rows whose mob has no
  // kills at all.
  codexGrid.style.gridTemplateColumns = `repeat(${codexRarityOrder.length}, 56px)`;
  for (const mobType of mobTypes) {
    let anyKill = false;
    for (const rarity of codexRarityOrder) {
      if (codexKillCount(mobType, rarity) > 0) {
        anyKill = true;
        break;
      }
    }
    if (!anyKill) continue;
    for (const rarity of codexRarityOrder) {
      const count = codexKillCount(mobType, rarity);
      const cell = document.createElement("button");
      cell.className = `codex-cell card-base${count > 0 ? "" : " empty"}`;
      cell.type = "button";
      cell.dataset.mobType = String(mobType);
      cell.dataset.rarity = String(rarity);
      if (count > 0) {
        // Rarity backdrop + mob svg on top, like a petal card.
        const base = petalBaseImage(rarity);
        if (base) {
          const baseLayer = document.createElement("img");
          baseLayer.src = base.src;
          baseLayer.className = "card-bg-layer";
          cell.appendChild(baseLayer);
        }
        const iconName = codexMobIcons[mobType];
        if (iconName) {
          const image = assetImage(`./assets/${iconName}`);
          if (image && imageReady(image)) {
            const iconLayer = document.createElement("img");
            iconLayer.src = image.src;
            iconLayer.className = "card-icon-layer";
            cell.appendChild(iconLayer);
          }
        }
        if (count > 1) {
          const badge = document.createElement("span");
          badge.className = "petal-count";
          badge.textContent = `x${formatShortCount(count)}`;
          cell.appendChild(badge);
        }
      }
      codexGrid.appendChild(cell);
    }
  }
}

export const codexMobIcons = {
  [beetleType]: "beetle.svg",
  [bandageBeetleType]: "bandage_beetle.svg",
  [normalLadybugType]: "normal_ladybug.svg",
  [mechaFlowerType]: "mecha_flower.svg",
  [normalFlowerType]: "player_flower.svg",
  [soldierAntType]: "soldier_ant.svg",
  [soldierFireAntType]: "soldier_fire_ant.svg",
  [soldierTermiteType]: "soldier_termite.svg",
  [summonedBeetleType]: "beetle.svg",
  [summonedSoldierAntType]: "soldier_ant.svg",
  [beeType]: "bee.svg",
  [hornetType]: "hornet.svg",
  [bumbleBeeType]: "bumblebee.svg",
  [babyAntType]: "baby_ant.svg",
  [workerAntType]: "worker_ant.svg",
  [queenAntType]: "queen_ant.svg",
  [antHoleType]: "ant_hole.svg",
  [bubbleType]: "bubble.svg",
  [spiderType]: "spider.svg",
  [sandstormType]: "sandstorm.svg",
  [dandelionType]: "dandelion.svg",
  [antEggMobType]: "ant_egg.svg",
  [fireAntEggType]: "fire_ant_egg.svg",
  [termiteEggType]: "termite_egg.svg",
  [babyFireAntType]: "baby_fire_ant.svg",
  [workerFireAntType]: "worker_fire_ant.svg",
  [fireQueenAntType]: "fire_queen_ant.svg",
  [babyTermiteType]: "baby_termite.svg",
  [workerTermiteType]: "worker_termite.svg",
  [termiteOvermindType]: "termite_overmind.svg",
  [leafPieceType]: "leaf_piece_1.svg",
  [leafcutterSoldierType]: "soldier_leafcutter_ant.svg",
  [fireflyType]: "firefly.svg",
  // rock / dummy / titan / queen eggs have no dedicated icon; their cells
  // keep the dark rarity backdrop and count only.
};

export function mobIconName(mobType) {
  const name = mobTypeName(mobType).toLowerCase().replace(/\s+/g, "_");
  return name || `mob_${mobType}`;
}

export function moveCodexDetail(event) {
  const cell = event.target.closest(".codex-cell");
  if (!cell || cell.classList.contains("empty")) {
    hideCodexDetail();
    return;
  }
  const mobType = Number(cell.dataset.mobType);
  const rarity = Number(cell.dataset.rarity);
  const count = state.codex.get(codexKey(mobType, rarity)) || 0;
  if (count <= 0) {
    hideCodexDetail();
    return;
  }

  codexDetail.replaceChildren();
  const title = document.createElement("div");
  title.className = "codex-detail-title";
  title.textContent = mobTypeName(mobType);
  codexDetail.appendChild(title);

  const rarityLine = document.createElement("div");
  rarityLine.className = "codex-detail-rarity";
  rarityLine.textContent = rarityName(rarity);
  rarityLine.style.color = rarityColor(rarity, 1);
  codexDetail.appendChild(rarityLine);

  const desc = document.createElement("div");
  desc.className = "codex-detail-desc";
  desc.textContent = codexMobDescription(mobType);
  codexDetail.appendChild(desc);

  const loot = state.codexLoot.get(codexKey(mobType, rarity)) || [];
  if (loot.length > 0) {
    // Group drops by petal type; one card row + one aligned rate row each.
    const byPetal = new Map();
    for (const entry of loot) {
      if (!byPetal.has(entry.petalType)) byPetal.set(entry.petalType, []);
      byPetal.get(entry.petalType).push(entry);
    }
    const lootSection = document.createElement("div");
    lootSection.className = "codex-loot";
    for (const entries of byPetal.values()) {
      entries.sort((a, b) => a.dropRarity - b.dropRarity);
      const columns = entries.length;
      const cardRow = document.createElement("div");
      cardRow.className = "codex-loot-row";
      cardRow.style.gridTemplateColumns = `repeat(${columns}, 39px)`;
      for (const entry of entries) {
        cardRow.appendChild(
          makePetalStack(entry.petalType, entry.dropRarity, 0),
        );
      }
      lootSection.appendChild(cardRow);

      const rateRow = document.createElement("div");
      rateRow.className = "codex-loot-rate-row";
      rateRow.style.gridTemplateColumns = `repeat(${columns}, 39px)`;
      for (const entry of entries) {
        const rate = document.createElement("span");
        rate.textContent = `${(entry.dropRate * 100).toFixed(1)}%`;
        rateRow.appendChild(rate);
      }
      lootSection.appendChild(rateRow);
    }
    codexDetail.appendChild(lootSection);
  }

  codexDetail.classList.remove("hidden");
  const rect = cell.getBoundingClientRect();
  const detailWidth = Math.min(420, codexDetail.offsetWidth || 420);
  let x = rect.right + 8;
  if (x + detailWidth > window.innerWidth - 8) x = Math.max(8, rect.left - detailWidth - 8);
  let y = rect.top;
  if (y + (codexDetail.offsetHeight || 120) > window.innerHeight - 8)
    y = Math.max(8, window.innerHeight - (codexDetail.offsetHeight || 120) - 8);
  codexDetail.style.left = `${x}px`;
  codexDetail.style.top = `${y}px`;
}

export function hideCodexDetail() {
  if (codexDetail) codexDetail.classList.add("hidden");
}


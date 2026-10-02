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
import { runMotionPhase, setAnimatedVisibility } from "./ui_motion.js";

const { codexBtn, codexDetail, codexGrid, codexPanel } = dom;
let detailContentKey = "";
let detailLoot = null;

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
  if (rarity === rarityDisplayOrder[0])
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
  const next = force !== undefined ? !!force : !state.codexOpen;
  if (next === state.codexOpen) return next;
  state.codexOpen = next;
  codexPanel.classList.toggle("open", state.codexOpen);
  codexPanel.setAttribute("aria-hidden", String(!state.codexOpen));
  runMotionPhase(codexPanel, state.codexOpen ? "entering" : "exiting", {
    classPrefix: "ui-panel",
    duration: state.codexOpen ? 260 : 110,
  });
  codexBtn?.setAttribute("aria-pressed", String(state.codexOpen));
  if (state.codexOpen) {
    toggleBackpack(false);
    toggleCraft(false);
    toggleTalent(false);
    toggleSettings(false);
    renderCodexPanel();
    hideCodexDetail();
  } else hideCodexDetail();
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
  // The scrollbar is managed by custom_scrollbar.js and also lives in this
  // element. Only reconcile codex cells; removing the scrollbar leaves its
  // registered observer attached to a detached, permanently invisible node.
  const previous = new Map(Array.from(codexGrid.children)
    .filter(cell => cell.classList.contains("codex-cell"))
    .map(cell => [cell.dataset.codexKey, cell]));
  codexGrid.style.setProperty("--codex-columns", String(codexRarityOrder.length));
  const ordered = [];
  const mobTypes = codexKilledMobTypes().sort((a, b) =>
    mobTypeName(a).localeCompare(mobTypeName(b)),
  );
  if (mobTypes.length === 0) {
    for (const cell of previous.values()) cell.remove();
    return;
  }
  // x = rarity (exotic shares the common column; 11 columns total),
  // y = mob type (alphabetical, top to bottom). Skip rows whose mob has no
  // kills at all.
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
      const key = `${mobType}:${rarity}`;
      let cell = previous.get(key);
      previous.delete(key);
      if (!cell) cell = document.createElement("button");
      cell.dataset.codexKey = key;
      ordered.push(cell);
      if (cell.dataset.killCount === String(count)) continue;
      cell.dataset.killCount = String(count);
      // Images stay mounted on kill updates so loading/hovering does not
      // flash the gold placeholder through the rarity-coloured background.
      if (count <= 0) cell.replaceChildren();
      cell.className = `codex-cell card-base${count > 0 ? "" : " empty"}`;
      cell.type = "button";
      cell.dataset.mobType = String(mobType);
      cell.dataset.rarity = String(rarity);
      if (count > 0) {
        // Rarity backdrop + mob svg on top, like a petal card.
        const base = petalBaseImage(rarity);
        if (base && !cell.querySelector(".card-bg-layer")) {
          const baseLayer = document.createElement("img");
          baseLayer.src = base.src;
          baseLayer.className = "card-bg-layer";
          baseLayer.alt = "";
          baseLayer.draggable = false;
          cell.appendChild(baseLayer);
        }
        const iconName = codexMobIcons[mobType];
        if (iconName && !cell.querySelector(".card-icon-layer")) {
          const image = assetImage(`./assets/${iconName}`);
          if (image) {
            const iconLayer = document.createElement("img");
            iconLayer.src = image.src;
            iconLayer.className = "card-icon-layer";
            iconLayer.alt = "";
            iconLayer.decoding = "async";
            iconLayer.draggable = false;
            cell.appendChild(iconLayer);
          }
        }
        if (count > 1) {
          const badge = cell.querySelector(".petal-count") || document.createElement("span");
          badge.className = "petal-count";
          badge.textContent = `x${formatShortCount(count)}`;
          cell.appendChild(badge);
        } else cell.querySelector(".petal-count")?.remove();
      }
    }
  }
  for (const cell of previous.values()) cell.remove();
  let cursor = codexGrid.firstElementChild;
  for (const cell of ordered) {
    if (cell === cursor) cursor = cursor.nextElementSibling;
    else codexGrid.insertBefore(cell, cursor);
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
  const count = codexKillCount(mobType, rarity);
  if (count <= 0) {
    hideCodexDetail();
    return;
  }

  const loot = state.codexLoot.get(codexKey(mobType, rarity)) || null;
  const name = mobTypeName(mobType);
  const description = codexMobDescription(mobType);
  const contentKey = `${mobType}:${rarity}:${count}:${name}:${rarityName(rarity)}:${description}`;
  // Moving the pointer over the same card only repositions the tooltip. The
  // old handler rebuilt all drop cards on every pointermove (up to 120Hz).
  if (contentKey !== detailContentKey || loot !== detailLoot) {
    detailContentKey = contentKey;
    detailLoot = loot;
    codexDetail.replaceChildren();
    const title = document.createElement("div");
    title.className = "codex-detail-title";
    title.textContent = name;
    codexDetail.appendChild(title);

    const rarityLine = document.createElement("div");
    rarityLine.className = "codex-detail-rarity";
    rarityLine.textContent = rarityName(rarity);
    rarityLine.style.color = rarityColor(rarity, 1);
    codexDetail.appendChild(rarityLine);

    const desc = document.createElement("div");
    desc.className = "codex-detail-desc";
    desc.textContent = description;
    codexDetail.appendChild(desc);

    if (loot?.length > 0) {
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
  }

  setAnimatedVisibility(codexDetail, true, { duration: 140 });
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
  setAnimatedVisibility(codexDetail, false, { duration: 120 });
}


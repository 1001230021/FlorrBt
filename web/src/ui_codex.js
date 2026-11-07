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
  crabType,
  jellyfishType,
  bumbleBeeType,
  dandelionType,
  dummyType,
  fireAntEggType,
  fireflyType,
  fireflyLarvaType,
  magicFireflyType,
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
import { setAnimatedVisibility } from "./ui_motion.js";
import { transitionPanel } from "./panel_motion.js";
import { assignVisualImage } from "./visual_assets.js";
import { drawRock } from "./garden_mob_sprite.js";
import { drawFlowerDisc, drawFlowerFace, drawTitanBody, getTitanCoreRadius, getFlowerPalette, stepFlowerFace } from "./flower_renderer.js";

const { codexBtn, codexDetail, codexGrid, codexPanel } = dom;
let detailContentKey = "";
let detailLoot = null;
let detailHideTimer = null;
const proceduralIcons = new Map();

function codexMobImage(mobType) {
  const iconName = codexMobIcons[mobType];
  if (iconName) return assetImage(`./assets/${iconName}`);
  if (![rockType, dummyType, titanType].includes(mobType)) return null;
  if (!proceduralIcons.has(mobType)) {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 128;
    const context = canvas.getContext('2d');
    if (!context) return null;
    // 直接复用场景绘制，避免图鉴另造一套外观。
    if (mobType === rockType) drawRock(context, { x: 64, y: 64 }, 52, 16, 52);
    else {
      context.translate(64, 64);
      const options = { titan: mobType === titanType, dummy: mobType === dummyType };
      const palette = getFlowerPalette(options);
      if (options.titan) drawTitanBody(context, 52, palette);
      else drawFlowerDisc(context, 52, palette);
      drawFlowerFace(context, options.titan ? getTitanCoreRadius(52) : 52,
        stepFlowerFace(null, {}, 0), palette, options);
    }
    proceduralIcons.set(mobType, assetImage(canvas.toDataURL()));
  }
  return proceduralIcons.get(mobType);
}

function keepCodexDetail() {
  clearTimeout(detailHideTimer);
  detailHideTimer = null;
}

function scheduleHideCodexDetail() {
  if (detailHideTimer === null) detailHideTimer = setTimeout(hideCodexDetail, 150);
}

export function bindCodexDetailEvents() {
  codexPanel.addEventListener('pointermove', moveCodexDetail);
  codexPanel.addEventListener('pointerdown', event => {
    if (event.pointerType === 'touch') moveCodexDetail(event);
  });
  codexPanel.addEventListener('pointerleave', scheduleHideCodexDetail);
  codexDetail.addEventListener('pointerenter', keepCodexDetail);
  codexDetail.addEventListener('pointerleave', scheduleHideCodexDetail);
  codexGrid.addEventListener('scroll', hideCodexDetail);
  window.addEventListener('resize', hideCodexDetail);
  document.addEventListener('pointerdown', event => {
    if (!codexPanel.contains(event.target) && !codexDetail.contains(event.target)) hideCodexDetail();
  });
}

export const codexMobOrder = [
  beetleType, bandageBeetleType, normalLadybugType, mechaFlowerType, normalFlowerType,
  soldierAntType, soldierFireAntType, soldierTermiteType, summonedBeetleType,
  summonedSoldierAntType, beeType, hornetType, bumbleBeeType, rockType, bubbleType,
  crabType,
  jellyfishType,
  babyAntType, workerAntType, queenAntType, antHoleType, spiderType, sandstormType, dummyType,
  dandelionType, antEggMobType, fireAntEggType, termiteEggType, queenAntEggType,
  queenFireAntEggType, babyFireAntType, workerFireAntType, fireQueenAntType,
  babyTermiteType, workerTermiteType, termiteOvermindType, leafPieceType,
  leafcutterSoldierType, titanType, fireflyType, magicFireflyType, fireflyLarvaType,
];

// Exotic 与 Common 同阶，击杀数合并到 Common 列。
export const codexRarityOrder = rarityDisplayOrder.filter(
  (rarity) => rarity !== rarityExotic,
);

export function codexKillCount(mobType, rarity) {
  const count = state.codex.get(codexKey(mobType, rarity)) || 0;
  if (rarity === rarityDisplayOrder[0])
    return count + (state.codex.get(codexKey(mobType, rarityExotic)) || 0);
  return count;
}

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
  transitionPanel(codexPanel, state.codexOpen);
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
  // 只更新图鉴格子，保留滚动条及其观察者，避免留下脱离文档的节点。
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
  // 横轴稀有度共 11 列，纵轴按名称排列生物，跳过无击杀记录的行。
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
      cell.setAttribute('aria-label', `${mobTypeName(mobType)} · ${rarityName(rarity)}`);
      ordered.push(cell);
      if (cell.dataset.killCount === String(count)) continue;
      cell.dataset.killCount = String(count);
      // 更新数量时保留图片节点，避免背景闪现。
      if (count <= 0) cell.replaceChildren();
      cell.className = `codex-cell card-base${count > 0 ? "" : " empty"}`;
      cell.type = "button";
      cell.dataset.mobType = String(mobType);
      cell.dataset.rarity = String(rarity);
      if (count > 0) {
        const base = petalBaseImage(rarity);
        if (base && !cell.querySelector(".card-bg-layer")) {
          const baseLayer = document.createElement("img");
          assignVisualImage(baseLayer, base);
          baseLayer.className = "card-bg-layer";
          baseLayer.alt = "";
          baseLayer.draggable = false;
          cell.appendChild(baseLayer);
        }
        if (!cell.querySelector(".card-icon-layer")) {
          const image = codexMobImage(mobType);
          if (image) {
            const iconLayer = document.createElement("img");
            assignVisualImage(iconLayer, image);
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
  [crabType]: "crab.svg",
  [jellyfishType]: "jellyfish.svg",
  [spiderType]: "spider.svg",
  [sandstormType]: "sandstorm.svg",
  [dandelionType]: "dandelion.svg",
  [antEggMobType]: "ant_egg.svg",
  [fireAntEggType]: "fire_ant_egg.svg",
  [termiteEggType]: "termite_egg.svg",
  [queenAntEggType]: "ant_egg.svg",
  [queenFireAntEggType]: "fire_ant_egg.svg",
  [babyFireAntType]: "baby_fire_ant.svg",
  [workerFireAntType]: "worker_fire_ant.svg",
  [fireQueenAntType]: "fire_queen_ant.svg",
  [babyTermiteType]: "baby_termite.svg",
  [workerTermiteType]: "worker_termite.svg",
  [termiteOvermindType]: "termite_overmind.svg",
  [leafPieceType]: "leaf_piece_1.svg",
  [leafcutterSoldierType]: "soldier_leafcutter_ant.svg",
  [fireflyType]: "firefly.svg",
  [magicFireflyType]: "firefly_cyan.svg",
  [fireflyLarvaType]: "firefly_larva.svg",
};

export function mobIconName(mobType) {
  const name = mobTypeName(mobType).toLowerCase().replace(/\s+/g, "_");
  return name || `mob_${mobType}`;
}

export function moveCodexDetail(event) {
  if (event.target.closest('.codex-detail')) {
    keepCodexDetail();
    return;
  }
  const cell = event.target.closest(".codex-cell");
  if (!cell || cell.classList.contains("empty")) {
    scheduleHideCodexDetail();
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
  keepCodexDetail();
  const name = mobTypeName(mobType);
  const description = codexMobDescription(mobType);
  const contentKey = `${mobType}:${rarity}:${count}:${name}:${rarityName(rarity)}:${description}`;
  // 同一卡片上移动只改提示位置，不重复重建全部掉落卡。
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
      const byPetal = new Map();
      for (const entry of loot) {
        if (!byPetal.has(entry.petalType)) byPetal.set(entry.petalType, []);
        byPetal.get(entry.petalType).push(entry);
      }
      const lootSection = document.createElement("div");
      lootSection.className = "codex-loot";
      for (const entries of byPetal.values()) {
        entries.sort((a, b) => rarityDisplayOrder.indexOf(a.dropRarity) - rarityDisplayOrder.indexOf(b.dropRarity));
        const group = document.createElement("div");
        group.className = "codex-loot-group";
        for (const entry of entries) {
          const item = document.createElement("div");
          item.className = "codex-loot-entry";
          item.appendChild(makePetalStack(entry.petalType, entry.dropRarity, 0));
          const rate = document.createElement("span");
          rate.className = "codex-loot-rate";
          rate.textContent = `${(entry.dropRate * 100).toFixed(1)}%`;
          item.appendChild(rate);
          group.appendChild(item);
        }
        lootSection.appendChild(group);
      }
      codexDetail.appendChild(lootSection);
    }
  }

  setAnimatedVisibility(codexDetail, true, { duration: 140 });
  const rect = cell.getBoundingClientRect();
  const detailWidth = codexDetail.offsetWidth || 340;
  let x = rect.right + 8;
  if (x + detailWidth > window.innerWidth - 8) x = Math.max(8, rect.left - detailWidth - 8);
  let y = rect.top;
  if (y + (codexDetail.offsetHeight || 120) > window.innerHeight - 8)
    y = Math.max(8, window.innerHeight - (codexDetail.offsetHeight || 120) - 8);
  codexDetail.style.left = `${x}px`;
  codexDetail.style.top = `${y}px`;
}

export function hideCodexDetail() {
  keepCodexDetail();
  setAnimatedVisibility(codexDetail, false, { duration: 120 });
}


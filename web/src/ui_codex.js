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
import { mobTypeName, rarityColor, rarityName } from "./protocol.js";
import {
  assetImage,
  formatShortCount,
  imageReady,
  makePetalStack,
} from "./petal_info.js";
import { petalBaseImage } from "./game.js";
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

export const codexRarityOrder = [
  rarityDisplayOrder[0], rarityDisplayOrder[1], rarityDisplayOrder[2],
  rarityDisplayOrder[3], rarityDisplayOrder[4], rarityDisplayOrder[5],
  rarityDisplayOrder[6], rarityDisplayOrder[7], rarityDisplayOrder[8],
  rarityDisplayOrder[9], rarityDisplayOrder[10], rarityDisplayOrder[11],
];

export const codexMobDescriptions = {
  [beetleType]: "最常见的敌人之一，缓慢爬行并造成撞击伤害。",
  [bandageBeetleType]: "裹着绷带的甲虫，比普通甲虫更耐打。",
  [normalLadybugType]: "圆滚滚的瓢虫，移动缓慢但攻击不低。",
  [mechaFlowerType]: "机械花朵，花园里的常客。",
  [normalFlowerType]: "普通花朵，被打倒后会掉落花瓣。",
  [soldierAntType]: "蚁群的士兵，成群出现并围攻玩家。",
  [soldierFireAntType]: "火蚁士兵，比普通兵蚁更强。",
  [soldierTermiteType]: "白蚁士兵，沙漠区域的威胁。",
  [summonedBeetleType]: "被召唤的甲虫，为它的主人而战。",
  [summonedSoldierAntType]: "被召唤的兵蚁，听从主人指挥。",
  [beeType]: "忙碌的蜜蜂，用尾针攻击。",
  [hornetType]: "凶猛的黄蜂，发射导弹攻击。",
  [bumbleBeeType]: "胖乎乎的大黄蜂，撞击伤害高。",
  [rockType]: "一块石头，挡路也伤人。",
  [bubbleType]: "漂浮的泡泡，轻巧但很难打破，高等级泡泡受击时会分裂。",
  [babyAntType]: "幼蚁，脆弱但数量多。",
  [workerAntType]: "工蚁，负责搬运与建巢。",
  [queenAntType]: "蚁后，蚁穴的核心与守护者。",
  [antHoleType]: "蚁穴，持续产出蚂蚁大军。",
  [spiderType]: "蜘蛛，吐出蛛网减速猎物。",
  [sandstormType]: "移动的沙暴，席卷一切。",
  [dummyType]: "训练假人，适合测试伤害。",
  [dandelionType]: "蒲公英，发射种子弹幕。",
  [antEggMobType]: "蚁卵，孵化出新蚂蚁。",
  [fireAntEggType]: "火蚁卵，孵化出火蚁。",
  [termiteEggType]: "白蚁卵，孵化出白蚁。",
  [queenAntEggType]: "蚁后卵，孵化出新的蚁后。",
  [queenFireAntEggType]: "火蚁后卵，孵化出新的火蚁后。",
  [babyFireAntType]: "幼火蚁，速度比普通幼蚁快。",
  [workerFireAntType]: "火工蚁，比普通工蚁更强。",
  [fireQueenAntType]: "火蚁后，火蚁群的核心。",
  [babyTermiteType]: "幼白蚁，脆弱但数量多。",
  [workerTermiteType]: "白蚁工蚁，负责建设巢穴。",
  [termiteOvermindType]: "白蚁心智，沙漠深处的首领。",
  [leafPieceType]: "被切下的叶片，切叶蚁的搬运物。",
  [leafcutterSoldierType]: "切叶蚁士兵，沙漠区域的威胁。",
  [titanType]: "泰坦，最强的存在之一。",
  [fireflyType]: "萤火虫，受到攻击时会用连锁闪电反击。",
};

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
  // x = rarity (fixed 12 columns), y = mob type (alphabetical, top to bottom).
  // Skip rows whose mob has no kills at all.
  codexGrid.style.gridTemplateColumns = `repeat(${codexRarityOrder.length}, 56px)`;
  for (const mobType of mobTypes) {
    let anyKill = false;
    for (const rarity of codexRarityOrder) {
      if ((state.codex.get(codexKey(mobType, rarity)) || 0) > 0) {
        anyKill = true;
        break;
      }
    }
    if (!anyKill) continue;
    for (const rarity of codexRarityOrder) {
      const count = state.codex.get(codexKey(mobType, rarity)) || 0;
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
  desc.textContent = codexMobDescriptions[mobType] || "";
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


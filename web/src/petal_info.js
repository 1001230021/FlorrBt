import { ASSET_MANIFEST } from "./asset_manifest.js";
import { PETAL_ICON_IDS } from "./generated_ids.js";
import { state } from "./app_context.js";
import {
  PetalIconIds,
  compassUltraIconMinRarity,
  nonStackPetalTypes,
  petalAirType,
  petalAmuletType,
  petalAntEggType,
  petalAntennaeType,
  petalBandageType,
  petalBasicType,
  petalBasilType,
  petalBatteryType,
  petalBeetleEggType,
  petalBlackFungusType,
  petalBloodSacrificeType,
  petalBoneType,
  petalBroccoliType,
  petalBrokenEggType,
  petalBrushType,
  petalBubbleType,
  petalBulbType,
  petalCactusType,
  petalCardIconScale,
  petalCarrotType,
  petalCogwheelType,
  petalCoinType,
  petalCompassType,
  petalCornType,
  petalCorruptionType,
  petalDahliaType,
  petalDandelionType,
  petalDouliType,
  petalDustType,
  petalEraserType,
  petalFasterType,
  petalFragmentType,
  petalGlassType,
  petalGoldenLeafType,
  petalHallucinogenicMushroomType,
  petalHeavyType,
  petalHoneyType,
  petalIrisType,
  petalLeafType,
  petalLentilType,
  petalLightType,
  petalLightningType,
  petalMagnetType,
  petalMimicType,
  petalMissileType,
  petalMoonType,
  petalNullificationType,
  petalOrangeType,
  petalPincerType,
  petalPlankType,
  petalPollenType,
  petalRelicType,
  petalRiceType,
  petalRockType,
  petalRoseType,
  petalSawbladeType,
  petalShovelType,
  petalSoilType,
  petalStingerType,
  petalThirdEyeType,
  petalTomatoType,
  petalTrapperType,
  petalTriangleType,
  petalWaxType,
  petalWebType,
  petalWhiteFungusType,
  petalWingType,
  petalYggdrasilType,
  petalYinYangType,
  petalYuccaType,
  rarityDisplayOrder,
  rarityExotic,
  raritySortRanks,
  stingerSplitIconMinRarity,
} from "./game_ids.js";
import {
  extractMimicLabelSvg,
  extractPetalLabelSvg,
  stripLivePetalLabel,
} from "./petal_svg.js";
import { requestRedraw } from "./render_invalidate.js";
import { getClientConfigValue } from "./client_config.js";
import { t } from "./i18n.js";
import {
  PetalNames,
  clamp,
  petalTypeFromEntity,
  petalTypeName,
  rarityColor,
  rarityName,
} from "./protocol.js";
import { TalentId } from "./talent_data.js";

export const PetalNameToType = new Map(
  PetalNames.map((name, index) => [normalizePetalName(name), index]),
);
export const assetImages = new Map();
export const livePetalImages = new Map();
export const bandageOverlayImages = new Map();
export const transformedPetalImages = new Map();
export const strippedPetalIconUrls = new Map();
export const petalLabelIconUrls = new Map();
export const mimicLabelIconUrls = new Map();

// Display-name map: multi-word petal names resolve to their most
// representative word. Entries here win over the automatic last-word rule;
// every entry also has its localized form in the locales (petals.*).
export const PETAL_DISPLAY_EN = {
  HallucinogenicMushroom: "Mushroom",
  BeetleEgg: "Egg",
  AntEgg: "Egg",
  BrokenEgg: "Egg",
  BloodSacrifice: "Blood",
  GoldenLeaf: "Leaf",
  WhiteFungus: "Fungus",
  BlackFungus: "Fungus",
  ThirdEye: "Eye",
  Nullification: "Null",
};

function displayNameSlug(petalType) {
  const full = petalTypeName(petalType);
  return full
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .toLowerCase();
}

function lastCamelWord(fullName) {
  const words = fullName
    .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
    .split(/[\s_]+/)
    .filter(Boolean);
  return words.length > 1 ? words[words.length - 1] : fullName;
}

export function petalDisplayName(petalType) {
  const full = petalTypeName(petalType);
  const en = PETAL_DISPLAY_EN[full] || lastCamelWord(full);
  if (state.locale === "en") return en;
  const slug = displayNameSlug(petalType);
  const localized = t(`petals.${slug}`, {}, { defaultValue: "" });
  if (localized && localized !== full) return localized;
  return en;
}

export function petalBaseCopy(petalType, rarity) {
  const level = rarityLevel(rarity);
  switch (petalType) {
    case petalAntEggType:
      return 4;
    case petalDustType:
      return 3;
    case petalDahliaType:
      return 3;
    case petalDandelionType:
      return level >= rarityLevel(8) ? 3 : level >= rarityLevel(6) ? 2 : 1;
    case petalOrangeType:
      return 3;
    case petalShovelType:
      return 1;
    case petalStingerType:
      return level >= rarityLevel(6) ? 3 : 1;
    case petalLightType:
      return level >= 6 ? 5 : level >= 4 ? 3 : level >= 2 ? 2 : 1;
    case petalPollenType:
      return level >= 4 ? 3 : level >= 2 ? 2 : 1;
    case 34:
      return rarity === 10 ? 1 : level >= 7 ? 3 : 1;
    case petalMimicType:
    case petalDouliType:
    case petalNullificationType:
    case petalThirdEyeType:
    case 1:
    case 3:
    case 18:
    case 24:
    case 25:
    case 26:
      return 0;
    default:
      return 1;
  }
}

export function petalHasMultiCopyVisual(petalType, rarity) {
  return effectivePetalCardCopy(petalType, rarity) > 1;
}

export function talentOwned(id, rarity, rank = 0) {
  return (state.talents || []).some(
    (talent) =>
      talent.id === id &&
      talent.rarity === rarity &&
      (talent.rank || 0) === rank,
  );
}

export function canDuplicatorAffect(petalType, copy) {
  return copy > 0 && !nonStackPetalTypes.has(petalType);
}

// ---- Server-derived copy counts ---------------------------------------------
// The owner snapshot carries one entry per copy for every primary slot
// (recharging copies included, see snapshot_service.cpp), so equipped cards
// render the exact copy count the server runs instead of predicting it with
// the local rules below. Counts seen live are remembered per type+rarity for
// surfaces without live data (inventory, craft, ground drops); the legacy
// formula only covers petals never observed live in this session.
const livePetalCopyCounts = new Map(); // "type:rarity" -> { count, leftmost }

export function recordLivePetalCopyCount(petalType, rarity, count, slotIndex) {
  if (!petalType || !rarity || !(count > 0)) return false;
  const key = `${petalType}:${rarity}`;
  const leftmost = slotIndex === 0;
  const previous = livePetalCopyCounts.get(key);
  // Leftmost-slot talents can grant slot 0 an extra copy: a non-leftmost
  // observation is the shared base value and always wins over one that may
  // carry that bonus.
  if (previous && !previous.leftmost && leftmost) return false;
  if (previous && previous.leftmost === leftmost && previous.count === count)
    return false;
  livePetalCopyCounts.set(key, { count, leftmost });
  return !previous || previous.count !== count;
}

export function livePetalCopyCount(petalType, rarity) {
  return livePetalCopyCounts.get(`${petalType}:${rarity}`)?.count || 0;
}

export function ownerLiveSlotCopyCount(slotIndex, petalType, rarity) {
  const runtime = (state.ownerSlotRuntime || [])[slotIndex];
  if (!runtime || runtime.petalType !== petalType || runtime.rarity !== rarity)
    return 0;
  return (runtime.copies || []).length;
}

export function effectivePetalCardCopy(petalType, rarity, options = {}) {
  if (options.kind === "primary" && Number.isFinite(options.slotIndex)) {
    const live = ownerLiveSlotCopyCount(options.slotIndex, petalType, rarity);
    if (live > 0) return live;
  }
  const observed = livePetalCopyCount(petalType, rarity);
  if (observed > 0) return observed;

  // Never seen live this session: predict with the legacy rules.
  let copy = petalBaseCopy(petalType, rarity);
  if (!canDuplicatorAffect(petalType, copy)) return copy;

  if (copy >= 2 && talentOwned(TalentId.PetalSplit, 5)) copy += 1;
  if (copy >= 2 && talentOwned(TalentId.PetalSplit, 7)) copy += 1;

  const inPrimarySlot =
    options.kind === "primary" && Number.isFinite(options.slotIndex);
  if (inPrimarySlot && options.slotIndex === 0) {
    if (rarity !== 9 && rarity !== 10 && talentOwned(TalentId.PetalSplit, 8))
      copy += 1;
    if ((rarity === 9 || rarity === 10) && talentOwned(TalentId.PetalSplit, 9))
      copy += 1;
  }
  return copy;
}

export function attachPetalInfoTooltip(element, petalType, rarity) {
  if (!element || !petalInfoTooltip || !petalType || !rarity) return;
  element.dataset.petalInfoType = String(petalType);
  element.dataset.petalInfoRarity = String(rarity);

  if (element.dataset.petalInfoBound === "1") return;
  element.dataset.petalInfoBound = "1";

  element.addEventListener("pointerenter", (event) =>
    showPetalInfoTooltipFromElement(element, event),
  );
  element.addEventListener("pointermove", movePetalInfoTooltip);
  element.addEventListener("pointerleave", hidePetalInfoTooltip);
  element.addEventListener("pointercancel", hidePetalInfoTooltip);
  element.addEventListener("blur", hidePetalInfoTooltip);
}

export function clearPetalInfoTooltip(element) {
  if (!element) return;
  if (state.petalInfoHoverElement === element) hidePetalInfoTooltip();
  delete element.dataset.petalInfoType;
  delete element.dataset.petalInfoRarity;
}

export function showPetalInfoTooltipFromElement(element, event) {
  const petalType = Number(element?.dataset?.petalInfoType || 0);
  const rarity = Number(element?.dataset?.petalInfoRarity || 0);
  if (!petalType || !rarity) return;
  state.petalInfoHoverElement = element;
  showPetalInfoTooltip(petalType, rarity, event);
}

export function showPetalInfoTooltip(petalType, rarity, event) {
  if (!petalInfoTooltip) return;
  const info = buildPetalInfo(petalType, rarity);
  if (!info || info.rows.length === 0) {
    hidePetalInfoTooltip();
    return;
  }

  renderPetalInfoTooltip(info);
  petalInfoTooltip.classList.remove("hidden");
  petalInfoTooltip.setAttribute("aria-hidden", "false");
  movePetalInfoTooltip(event);
}

export function hidePetalInfoTooltip() {
  if (!petalInfoTooltip) return;
  state.petalInfoHoverElement = null;
  petalInfoTooltip.classList.add("hidden");
  petalInfoTooltip.setAttribute("aria-hidden", "true");
}

export function syncPetalInfoTooltipHover() {
  const element = state.petalInfoHoverElement;
  if (
    !element ||
    !petalInfoTooltip ||
    petalInfoTooltip.classList.contains("hidden")
  )
    return;
  if (!element.isConnected || !element.matches(":hover"))
    hidePetalInfoTooltip();
}

export function movePetalInfoTooltip(event) {
  if (
    !petalInfoTooltip ||
    petalInfoTooltip.classList.contains("hidden") ||
    !event
  )
    return;
  syncPetalInfoTooltipHover();
  if (petalInfoTooltip.classList.contains("hidden")) return;
  const offset = 14;
  const pad = 8;
  const rect = petalInfoTooltip.getBoundingClientRect();
  let x = event.clientX + offset;
  let y = event.clientY + offset;
  if (x + rect.width + pad > window.innerWidth)
    x = event.clientX - rect.width - offset;
  if (y + rect.height + pad > window.innerHeight)
    y = event.clientY - rect.height - offset;
  x = clamp(x, pad, Math.max(pad, window.innerWidth - rect.width - pad));
  y = clamp(y, pad, Math.max(pad, window.innerHeight - rect.height - pad));
  petalInfoTooltip.style.left = `${x}px`;
  petalInfoTooltip.style.top = `${y}px`;
}

export function renderPetalInfoTooltip(info) {
  petalInfoTooltip.replaceChildren();

  // Same structure as the bestiary floating card: title, rarity line, then
  // rows (stats as plain numbers instead of loot cards).
  const title = document.createElement("div");
  title.className = "petal-info-title";
  title.textContent = petalTypeName(info.petalType);
  petalInfoTooltip.appendChild(title);

  const rarityLine = document.createElement("div");
  rarityLine.className = "petal-info-subtitle";
  rarityLine.style.color = rarityColor(info.rarity, 1);
  rarityLine.textContent = rarityName(info.rarity);
  petalInfoTooltip.appendChild(rarityLine);

  for (const item of info.rows) {
    const row = document.createElement("div");
    row.className = "petal-info-row";

    const label = document.createElement("span");
    label.className = "petal-info-label";
    label.textContent = item.label;
    row.appendChild(label);

    const value = document.createElement("span");
    value.className = "petal-info-value";
    value.textContent = item.value;
    row.appendChild(value);

    petalInfoTooltip.appendChild(row);
  }
}

const petalInfoStatKeys = Object.freeze({
  Petals: "petals",
  Damage: "damage",
  Durability: "durability",
  Armor: "armor",
  Reload: "reload",
  Preload: "preload",
  Vision: "vision",
  "Attack range": "attackRange",
  "Reload cut": "reloadCut",
  Undead: "undead",
  "Summon HP": "summonHp",
  "Summon DMG": "summonDmg",
  "Linked HP": "linkedHp",
  "Flower body": "flowerBody",
  Delay: "delay",
  Effect: "effect",
  Mass: "mass",
  Regen: "regen",
  "Defend regen": "defendRegen",
  "Petal hits/tick": "petalHitsPerTick",
  "Compression power": "compressionPower",
  "Petal HP healing/s": "petalHpHealingPerSecond",
  "Throw CD": "throwCooldown",
  "Web time": "webTime",
  "Web radius": "webRadius",
  "Flower HP": "flowerHp",
  "Healing taken": "healingTaken",
  "Flower radius": "flowerRadius",
  Attracts: "attracts",
  Burrow: "burrow",
  Heal: "heal",
  "Anti-heal": "antiHeal",
  Healing: "healing",
  Lifetime: "lifetime",
  Magnet: "magnet",
  Poison: "poison",
  "Poison time": "poisonTime",
  "Attract range": "attractRange",
  Rotation: "rotation",
  Revive: "revive",
  "Tri bonus": "triBonus",
  Copies: "copies",
  "Fire interval": "fireInterval",
  "Trap damage": "trapDamage",
  "Trap durability": "trapDurability",
  "Overheal to shield": "overhealToShield",
  "Projectile damage": "projectileDamage",
  "Growth delay": "growthDelay",
  "Growth time": "growthTime",
  "Max damage": "maxDamage",
  "Max durability": "maxDurability",
  "Max radius": "maxRadius",
  Hallucination: "hallucination",
  "Evolve hits": "evolveHits",
  Chain: "chain",
  Charges: "charges",
  Radius: "radius",
  "Absorb range": "absorbRange",
});

const petalInfoValueKeys = Object.freeze({
  Corrupted: "corrupted",
  Nullify: "nullify",
  "left petal": "leftPetal",
  "x0.8 per hit": "healingPerHit",
  "Cosmetic only": "cosmeticOnly",
});

export function localizePetalInfoLabel(label) {
  const key = petalInfoStatKeys[label];
  return key
    ? t("ui.petalInfo.stats." + key, {}, { defaultValue: label })
    : label;
}

export function localizePetalInfoValue(value) {
  const text = String(value);
  const key = petalInfoValueKeys[text];
  return key
    ? t("ui.petalInfo.values." + key, {}, { defaultValue: text })
    : text;
}

export function buildPetalInfo(petalType, rarity) {
  const rows = [];
  const level = rarityValueLevel(rarity);
  const scale = petalRarityScale(rarity);
  const addRow = (label, value) => {
    if (value === null || value === undefined || value === "") return;
    rows.push({
      label: localizePetalInfoLabel(label),
      value: localizePetalInfoValue(value),
    });
  };
  const addNumber = (label, value, suffix = "") => {
    if (!Number.isFinite(value) || Math.abs(value) <= 0.000001) return;
    addRow(label, `${formatPetalStat(value)}${suffix}`);
  };
  const addSeconds = (label, value) => addNumber(label, value, "s");
  const addPercent = (label, value, options = {}) => {
    if (!Number.isFinite(value) || Math.abs(value) <= 0.000001) return;
    const sign = options.sign && value > 0 ? "+" : "";
    addRow(label, `${sign}${formatPetalStat(value * 100)}%`);
  };
  const addCombat = (damage, health, reload, options = {}) => {
    const copy = Math.max(1, options.copy || 1);
    if (copy > 1) addRow("Petals", `x${copy}`);
    addNumber("Damage", damage / copy);
    addNumber("Durability", health / copy);
    addNumber("Armor", (options.armor || 0) / copy);
    addSeconds("Reload", reload);
    if (options.preload && Math.abs(options.preload - reload) > 0.0001)
      addSeconds("Preload", options.preload);
  };

  switch (petalType) {
    case petalAntennaeType:
      addPercent("Vision", antennaeVisionBonus(rarity), { sign: true });
      break;
    case petalThirdEyeType:
      addNumber("Attack range", thirdEyeReachBonus(rarity), "");
      break;
    case petalGoldenLeafType:
      addPercent("Reload cut", goldenLeafReloadReduction(rarity));
      break;
    case petalDustType:
      addPercent("Reload cut", dustReloadReduction(rarity));
      break;
    case petalBandageType:
      addSeconds("Undead", bandageUndeadDuration(rarity));
      break;
    case petalBrokenEggType:
      addPercent("Summon HP", brokenEggSummonHealthBonus(rarity), {
        sign: true,
      });
      addPercent("Summon DMG", brokenEggSummonDamageBonus(rarity), {
        sign: true,
      });
      break;
    case petalRelicType:
      addPercent("Linked HP", relicHealthBonus(rarity), { sign: true });
      break;
    case petalCorruptionType:
      addRow("Flower body", "Corrupted");
      break;
    case petalBloodSacrificeType:
      addSeconds("Delay", 10);
      break;
    case petalNullificationType:
      addRow("Effect", "Nullify");
      break;
    case petalDouliType:
      addRow("Effect", "Cosmetic only");
      break;
    case petalAirType:
      addRow("Mass", "16");
      break;
    case petalBasicType:
      addCombat(10 * scale, 10 * scale, 2.5);
      break;
    case petalLightType: {
      const copy = lightCopy(rarity);
      addCombat(13 * scale, 5 * scale, 0.8, { copy });
      break;
    }
    case petalLeafType:
      addCombat(16 * scale, 12 * scale, 1.8);
      addNumber("Regen", roseMedicineScale(rarity), "/s");
      break;
    case petalYuccaType:
      addCombat(16 * scale, 12 * scale, 1.8);
      addNumber("Defend regen", 3 * roseMedicineScale(rarity), "/s");
      break;
    case petalWhiteFungusType: {
      const hitBonus = Math.round(2 * rarityValueLevel(rarity));
      addCombat(8 * scale, 10 * scale, 2.5);
      addRow("Petal hits/tick", `+${hitBonus}`);
      break;
    }
    case petalBlackFungusType: {
      const compressionPower = Math.round(2 * rarityValueLevel(rarity));
      addCombat(8 * scale, 10 * scale, 2.5);
      addRow("Compression power", `+${compressionPower}`);
      break;
    }
    case petalBroccoliType: {
      const health = Math.round((20 * roseMedicineScale(rarity)) / 10) * 10;
      addCombat(2.5 * scale, health, 5);
      addPercent("Petal HP healing/s", 0.2);
      break;
    }
    case petalRockType:
      addCombat(25 * scale, 30 * scale, 3);
      break;
    case petalWebType:
      addCombat(5 * scale, 5 * scale, 3);
      addSeconds("Throw CD", 0.5);
      addSeconds("Web time", 10);
      addNumber("Web radius", webRadiusValue(rarity));
      break;
    case petalCactusType:
      addCombat(7 * scale, 15 * scale, 1);
      addNumber("Flower HP", 30 * scale);
      break;
    case petalPollenType: {
      const copy = pollenCopy(rarity);
      addCombat(40 * scale, 10 * scale, 2, { copy });
      addSeconds("Throw CD", 0.5);
      break;
    }
    case petalCornType:
      addCombat(8 * scale, 200 * scale, 7);
      break;
    case petalRiceType:
      addCombat(4 * scale, scale, 0.1);
      break;
    case petalBasilType:
      addNumber("Durability", 10 * scale);
      addSeconds("Reload", 2.5);
      addPercent("Healing taken", basilHealingBonus(rarity), { sign: true });
      break;
    case petalLightningType:
      addNumber("Durability", 20 * scale);
      addSeconds("Reload", 2.5);
      addNumber("Damage", 12 * scale);
      addNumber("Chain", 2 + level - 1);
      break;
    case petalBatteryType:
      addNumber("Damage", 36 * scale);
      addNumber("Chain", 2 + level - 1);
      addNumber("Charges", 3);
      addSeconds("Reload", 2.5);
      break;
    case petalBulbType:
      addNumber("Damage", 5 * scale);
      addNumber("Durability", 12 * scale);
      addSeconds("Preload", 1);
      addSeconds("Reload", 0.5);
      addNumber("Radius", 300 * level);
      break;
    case petalSoilType:
      addCombat(10 * scale, 10 * scale, 2.5);
      addNumber("Flower HP", 50 * scale);
      addNumber("Flower radius", 10);
      break;
    case petalHoneyType:
      addNumber("Durability", 50 * scale);
      addSeconds("Reload", 2);
      addSeconds("Throw CD", 0.5);
      addRow("Attracts", `<= ${rarityName(previousDisplayRarity(rarity))}`);
      break;
    case petalWaxType:
      addNumber("Durability", 1000 * scale);
      addSeconds("Reload", 30);
      break;
    case petalOrangeType:
      addCombat(8 * scale, 6.7 * scale, 3.5, { copy: 3 });
      break;
    case petalShovelType:
      addSeconds("Preload", shovelPreload(rarity));
      addSeconds("Burrow", 2);
      break;
    case petalRoseType:
      addCombat(5 * scale, 5 * scale, 3.5, { preload: 1.5 });
      addNumber("Heal", 7.5 * roseMedicineScale(rarity));
      break;
    case petalDahliaType:
      addCombat(1.7 * scale, 1.7 * scale, 1.5, { copy: 3 });
      addNumber("Heal", (1.2 * roseMedicineScale(rarity)) / 3);
      break;
    case petalDandelionType: {
      const copy = petalBaseCopy(petalType, rarity);
      addCombat(8 * scale, 8 * scale, 1, { copy });
      addSeconds("Anti-heal", 30);
      addRow("Healing", "x0.8 per hit");
      break;
    }
    case petalStingerType: {
      const copy = petalBaseCopy(petalType, rarity);
      addCombat(100 * scale, 1, 10, { copy });
      break;
    }
    case petalMissileType:
      addCombat(35 * scale, 2 * scale, 1.5);
      addSeconds("Lifetime", 5);
      break;
    case petalTrapperType:
      addCombat(2.5 * scale, 25 * scale, 4);
      addSeconds("Fire interval", 2);
      addNumber("Trap damage", 40 * scale);
      addNumber("Trap durability", 40 * scale);
      addSeconds("Lifetime", 5);
      break;
    case petalAmuletType:
      addCombat(0, 10 * scale, 2);
      addPercent("Overheal to shield", 0.05 * level);
      break;
    case petalPlankType:
      addCombat(18 * scale, 15 * scale, 2.5);
      addRow("Projectile damage", "x20");
      break;
    case petalTomatoType:
      addCombat(5 * scale, 10 * scale, 2.5);
      addSeconds("Growth delay", 1);
      addSeconds("Growth time", 3);
      addNumber("Max damage", 70 * scale);
      addNumber("Max durability", 70 * scale);
      addRow("Max radius", "x3.33");
      break;
    case petalHallucinogenicMushroomType:
      addCombat(4 * scale, 8 * scale, 1.5);
      addSeconds("Hallucination", 12);
      addNumber("Evolve hits", 3);
      break;
    case petalAntEggType:
      addRow("Petals", "x4");
      addNumber("Durability", 1);
      addSeconds("Reload", antEggReload(rarity));
      break;
    case petalBeetleEggType:
      addNumber("Durability", 1);
      addSeconds("Reload", beetleEggReload(rarity));
      break;
    case petalBubbleType:
      addNumber("Durability", 1);
      addSeconds("Reload", bubbleReload(rarity));
      break;
    case petalCarrotType:
      addCombat(18 * scale, 18 * scale, 1);
      break;
    case petalBoneType:
      addCombat(14 * scale, 10 * scale, 2.5, { armor: (20 / 3) * scale });
      break;
    case petalCoinType:
      addCombat(15 * scale, 10 * scale, 2.5);
      break;
    case petalCompassType:
      addCombat(scale, 40 * scale, 2.5);
      addNumber("Magnet", 1024);
      break;
    case petalCogwheelType:
      addCombat(19 * scale, 13 * scale, 2.5);
      break;
    case petalIrisType:
      addCombat(5 * scale, 5 * scale, 4);
      addNumber("Poison", 70);
      addSeconds("Poison time", 3);
      break;
    case petalLentilType:
      addCombat(13 * scale, 12 * scale, 2);
      addNumber("Attract range", 8 * level);
      break;
    case petalMagnetType:
      addCombat(5 * scale, 20 * scale, 2.5);
      addNumber("Absorb range", magnetAbsorbBonus(rarity));
      break;
    case petalMoonType:
      addCombat(3 * scale, 5000 * scale, 100);
      break;
    case petalPincerType:
      addCombat(5 * scale, 5 * scale, 2.5);
      addNumber("Poison", 15 * scale);
      addSeconds("Poison time", 0.75);
      break;
    case petalYinYangType:
      addCombat(10 * scale, 10 * scale, 2);
      break;
    case petalHeavyType:
      addCombat(10 * scale, 150 * scale, 10);
      break;
    case petalFasterType:
      addCombat(12 * scale, 5 * scale, 2.5);
      addNumber("Rotation", 0.3 + 0.2 * level, " rad/s");
      break;
    case petalYggdrasilType:
      addNumber("Durability", 10 * scale);
      addSeconds("Revive", yggdrasilChannelTime(rarity));
      break;
    case petalWingType:
      addCombat(20 * scale, 10 * scale, 3);
      break;
    case petalTriangleType:
      addCombat(6 * scale, 10 * scale, 2);
      addNumber("Tri bonus", triangleBonusDamage(rarity));
      break;
    case petalSawbladeType:
      addCombat(40 * scale, 25 * scale, 2.5);
      break;
    case petalFragmentType: {
      const value = fragmentValue(rarity);
      const copy = petalBaseCopy(petalType, rarity);
      addCombat(value, value, rarity === 10 ? 8 : 10, { copy });
      break;
    }
    case petalGlassType:
      addCombat(22 * scale, 10 * scale, 2.5);
      break;
    case petalMimicType:
      addNumber("Durability", 10);
      addSeconds("Reload", 2.5);
      addRow("Copies", "left petal");
      break;
    default:
      break;
  }

  return { petalType, rarity, rows };
}

export function rarityLevel(rarity) {
  const value = Math.max(1, Math.floor(Number(rarity) || 1));
  if (value === rarityExotic) return 1;
  if (value === 10) return 9;
  if (value >= 11) return 10;
  return clamp(value, 1, 9);
}

export function rarityValueLevel(rarity) {
  return rarityLevel(rarity);
}

export function raritySpecialLevel(rarity) {
  return rarityLevel(rarity);
}

export function raritySortRank(rarity) {
  const value = Math.floor(Number(rarity) || 0);
  return raritySortRanks.get(value) || 0;
}

export function petalRarityScale(rarity) {
  return 3 ** (rarityLevel(rarity) - 1);
}

export function lightCopy(rarity) {
  const level = rarityLevel(rarity);
  if (level <= 1) return 1;
  if (level <= 3) return 2;
  if (level <= 5) return 3;
  return 5;
}

export function pollenCopy(rarity) {
  return rarityLevel(rarity) <= 1 ? 1 : rarityLevel(rarity) <= 3 ? 2 : 3;
}

// Mirrors the server's MagnetAbsorbBonus: the pick-up absorb range grows
// linearly with rarity — Common grants nothing, Mythic reaches the full
// magnetism-talent range. Active only while the petal is alive.
export function magnetAbsorbBonus(rarity) {
  return (1024 * Math.max(0, rarityValueLevel(rarity) - 1)) / 5;
}

export function roseMedicineScale(rarity) {
  const sqrt3 = Math.sqrt(3);
  switch (Math.floor(Number(rarity) || 1)) {
    case 1:
      return 1;
    case 2:
      return 3;
    case 3:
      return 9;
    case 4:
      return 27;
    case 5:
      return 81;
    case 6:
      return 243;
    case 7:
      return 243 * sqrt3;
    case rarityExotic:
      return 1;
    case 8:
      return 243 * 3;
    case 9:
    case 10:
      return 243 * 3 * sqrt3;
    case 11:
      return 243 * 9 * sqrt3;
    default:
      return 1;
  }
}

export function antennaeVisionBonus(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 0.111, 0.176, 0.25, 0.333, 0.429, 1, 1.857, 4, 13.5, 13.5, 38],
  );
}

export function thirdEyeReachBonus(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 0, 0, 0, 0, 40, 90, 140, 190, 240, 240, 350],
  );
}

export function goldenLeafReloadReduction(rarity) {
  return 1 - Math.max(0.05, 1 - raritySpecialLevel(rarity) * 0.033);
}

export function dustReloadReduction(rarity) {
  return 1 - Math.max(0.05, 1 - raritySpecialLevel(rarity) * 0.03);
}

export function bandageUndeadDuration(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 1.5, 2.1, 2.9, 4.1, 5.8, 8.1, 11.3, 15.8, 22.1, 22.1, 31.3],
  );
}

export function brokenEggSummonHealthBonus(rarity) {
  const level = clamp(raritySpecialLevel(rarity), 1, 10);
  if (level <= 8) {
    const t = (level - 1) / 7;
    return 0.9 + (0.25 - 0.9) * t - 1;
  }
  const t = (level - 8) / 2;
  return 0.25 + (0.02 - 0.25) * t - 1;
}

export function brokenEggSummonDamageBonus(rarity) {
  const level = clamp(raritySpecialLevel(rarity), 1, 10);
  if (level <= 8) {
    const t = (level - 1) / 7;
    return 1.12 + (7.5 - 1.12) * t - 1;
  }
  const t = (level - 8) / 2;
  return 7.5 + (25.0 - 7.5) * t - 1;
}

export function basilHealingBonus(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.75, 1.0, 1.0, 2.0],
  );
}

export function relicHealthBonus(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.75, 1.0, 1.5, 1.5, 2.5],
  );
}

export function antEggReload(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 24, 8, 9, 10, 12, 16, 24, 60, 180, 180, 480],
  );
}

export function beetleEggReload(rarity) {
  return bySpecialRarity(rarity, [0, 12, 4, 4.5, 5, 6, 8, 12, 30, 90, 90, 240]);
}

export function bubbleReload(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 1, 0.8, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1, 0.05, 0.05, 0.025],
  );
}

export function shovelPreload(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 15, 13.1, 11.8, 10.1, 8.5, 6.9, 5.2, 3.6, 2, 2, 1],
  );
}

export function webRadiusValue(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 50, 60, 70, 80, 100, 150, 200, 250, 350, 350, 500],
  );
}

export function yggdrasilChannelTime(rarity) {
  if (Math.floor(Number(rarity) || 1) === rarityExotic) return 600;
  if (rarity >= 11) return 0.016;
  if (rarity >= 9) return 0.33;
  if (rarity === 8) return 1;
  const t = (rarityLevel(rarity) - 1) / 6;
  return 600 + (1 - 600) * clamp(t, 0, 1);
}

export function triangleBonusDamage(rarity) {
  return 3.75 * petalRarityScale(rarity);
}

export function fragmentValue(rarity) {
  switch (Math.floor(Number(rarity) || 1)) {
    case 7:
      return 364.5;
    case rarityExotic:
      return petalRarityScale(1);
    case 8:
      return 729;
    case 9:
      return 2187;
    case 10:
      return 328000;
    case 11:
      return 6561;
    default:
      return petalRarityScale(rarity);
  }
}

export function previousDisplayRarity(rarity) {
  const value = Math.floor(Number(rarity) || 1);
  if (value === rarityExotic) return 1;
  const index = rarityDisplayOrder.indexOf(value);
  if (index > 0) return rarityDisplayOrder[index - 1];
  return Math.max(1, value - 1);
}

export function byRarity(rarity, values) {
  return byRarityWeighted(rarity, values);
}

export function bySpecialRarity(rarity, values) {
  return byRarityWeighted(rarity, values);
}

export function byRarityWeighted(rarity, values) {
  if (Math.floor(Number(rarity) || 1) === rarityExotic) {
    return values[1] ?? 0;
  }
  const index = clamp(Math.floor(Number(rarity) || 1), 1, values.length - 1);
  return values[index] ?? values[1] ?? 0;
}

export function formatPetalStat(value) {
  const abs = Math.abs(value);
  if (abs >= 1000000) return `${formatCompact(value / 1000000)}m`;
  if (abs >= 10000) return `${formatCompact(value / 1000)}k`;
  if (abs >= 100) return String(Math.round(value));
  if (abs >= 10) return trimNumber(value, 1);
  if (abs >= 1) return trimNumber(value, 2);
  return trimNumber(value, 3);
}

export function trimNumber(value, digits) {
  return Number(value.toFixed(digits)).toString();
}

export function petalSingleRadiusFactor(petalType) {
  switch (petalType) {
    case petalDustType:
      return 0.24;
    case petalAntEggType:
      return 0.36;
    case petalDahliaType:
      return 0.3;
    case petalStingerType:
      return 0.38;
    default:
      return 0.42;
  }
}

export function petalCopyVisualScale(visualCopies, petalType) {
  if (visualCopies <= 2) return 0.86;
  if (petalType === petalAntEggType) return 0.82;
  if (petalType === petalDustType) return 0.9;
  if (petalType === petalDahliaType) return 0.78;
  if (visualCopies <= 4) return 0.76;
  if (visualCopies <= 6) return 0.66;
  return 0.6;
}

export function petalCopyChordFactor(visualCopies, petalType) {
  if (petalType === petalAntEggType) return 0.46;
  if (petalType === petalDustType) return 0.92;
  if (petalType === petalDahliaType) return 0.63;
  if (petalType === petalStingerType) return 0.66;
  if (visualCopies <= 2) return 0.82;
  if (visualCopies <= 4) return 0.72;
  return 0.68;
}

export function petalCopyMaxRadius(visualCopies, petalType, singleRadius) {
  if (petalType === petalDustType) return 6.4;
  if (petalType === petalAntEggType) return singleRadius * 0.82;
  if (visualCopies <= 2) return 8.5;
  if (visualCopies <= 4) return 8.8;
  return 10.5;
}

// Multi-copy ring distance comes from each petal's svg (`data-copy-distance`
// on the root element, in 110-unit frame coordinates; 0 = use the dynamic
// layout formula). Loaded once by ensurePetalCopyDistanceData(). Retune a
// petal by editing its .svg (or via tools/write_petal_copy_distance.mjs).
const petalCopyDistanceByType = new Map();

export function petalCopyDistanceOverride(petalType) {
  const value = petalCopyDistanceByType.get(petalType);
  return Number.isFinite(value) ? value : 0;
}

let petalCopyDistanceLoadStarted = false;

export async function ensurePetalCopyDistanceData() {
  if (petalCopyDistanceLoadStarted) return;
  petalCopyDistanceLoadStarted = true;
  try {
    for (const iconId of PETAL_ICON_IDS) {
      if (!iconId) continue;
      const url = `./assets/petals/${iconId}.svg`;
      const response = await fetch(url);
      if (!response.ok) continue;
      const text = await response.text();
      const match = text.match(/data-copy-distance\s*=\s*"?([\d.]+)/);
      if (!match) continue;
      const value = Number(match[1]);
      if (Number.isFinite(value) && value > 0)
        petalCopyDistanceByType.set(PETAL_ICON_IDS.indexOf(iconId), value);
    }
  } catch {
    // Silent fallback: dynamic radius formulas keep working.
  }
}

export function petalCopyLayout(copy, petalType = 0) {
  const visualCopies = Math.max(1, Math.min(8, Math.floor(copy || 1)));
  if (visualCopies === 1) return [{ x: 0, y: 0, angle: 0, scale: 1 }];

  const scale = petalCopyVisualScale(visualCopies, petalType);
  // A per-petal fixed ring distance comes only from the svg attribute now;
  // petals without one (the default) use the dynamic formula below.
  const fixedRadius = petalCopyDistanceOverride(petalType);
  let radius = fixedRadius;
  if (radius <= 0) {
    const singleRadius =
      50 * petalCardIconScale * scale * petalSingleRadiusFactor(petalType);
    const chord =
      singleRadius * 2 * petalCopyChordFactor(visualCopies, petalType);
    const ringRadius = chord / (2 * Math.sin(Math.PI / visualCopies));
    const copyGrowth = 1 + Math.max(0, visualCopies - 3) * 0.035;
    const minRadius = petalType === petalDustType ? 2.4 : 2.8;
    radius = clamp(
      ringRadius * copyGrowth,
      minRadius,
      petalCopyMaxRadius(visualCopies, petalType, singleRadius),
    );
  }
  const start = 0;
  const points = [];
  for (let i = 0; i < visualCopies; i += 1) {
    const angle = start + (Math.PI * 2 * i) / visualCopies;
    points.push({
      x: Math.cos(angle) * radius,
      y: Math.sin(angle) * radius,
      angle: angle + Math.PI * 0.5,
      scale,
    });
  }
  return points;
}

export function makePetalStack(
  petalType,
  rarity,
  count = 0,
  iconPetalType = petalType,
  options = {},
) {
  const stack = document.createElement("span");
  stack.className = "petal-stack";
  const mimicVisual =
    petalType === petalMimicType && iconPetalType !== petalType;
  const copy =
    options.copy ?? effectivePetalCardCopy(iconPetalType, rarity, options);
  const splitLabel = copy > 1;
  const liveVisual = options.liveVisual === true;
  const separateLabel = splitLabel || liveVisual;
  if (mimicVisual) stack.classList.add("mimic-visual");
  if (copy > 1) stack.classList.add("multi-copy");

  const base = document.createElement("img");
  base.className = "petal-bg";
  base.alt = "";
  base.draggable = false;
  base.src = petalBasePath(rarity);
  stack.appendChild(base);

  const layout = petalCopyLayout(copy, iconPetalType);
  for (let copyIndex = 0; copyIndex < layout.length; copyIndex += 1) {
    const part = layout[copyIndex];
    const icon = makePetalIcon(iconPetalType, rarity, {
      stripped: mimicVisual || separateLabel,
    });
    if (!icon) continue;
    if (liveVisual) {
      icon.classList.add("petal-live-visual");
      icon.dataset.petalCopyIndex = String(copyIndex);
      icon.dataset.petalVisualType = String(iconPetalType);
    }
    if (layout.length > 1) {
      icon.classList.add("petal-icon-copy");
      icon.style.setProperty("--copy-x", `${part.x * 0.54}px`);
      icon.style.setProperty("--copy-y", `${part.y * 0.54}px`);
      icon.style.setProperty("--copy-angle", `${part.angle}rad`);
      icon.style.setProperty("--copy-size", `${Math.round(part.scale * 100)}%`);
    }
    stack.appendChild(icon);
  }
  stack.appendChild(makePetalDisplayName(iconPetalType));
  if (count > 1) {
    const badge = document.createElement("span");
    badge.className = "petal-count";
    badge.textContent = `x${formatShortCount(count)}`;
    stack.appendChild(badge);
  }
  return stack;
}

// Unified card label: black-outlined white text below the petal art.
// The font is sized so the rendered text width never exceeds 105px (that is
// also the font-size cap); the label sits at the lower part of the card.
function makePetalDisplayName(petalType) {
  const span = document.createElement("span");
  span.className = "petal-card-name";
  span.textContent = petalDisplayName(petalType);
  // Synchronous font sizing (canvas measured): no post-layout flicker when
  // cards are rebuilt.
  span.style.fontSize = `${labelFontSizePx(span.textContent)}px`;
  return span;
}

let cardMeasureContext = null;
const labelFontSizeCache = new Map();
function labelFontSizePx(text) {
  try {
    const maxFont = getClientConfigValue(state.clientConfig, "petal_label_max_font_px") ?? 14;
    const maxWidth = getClientConfigValue(state.clientConfig, "petal_label_max_width_px") ?? 45;
    const cacheKey = `${maxFont}|${maxWidth}|${text}`;
    const cached = labelFontSizeCache.get(cacheKey);
    if (cached) return cached;
    if (!cardMeasureContext) {
      const canvas = new OffscreenCanvas(16, 16);
      cardMeasureContext = canvas.getContext("2d");
    }
    cardMeasureContext.font = "900 100px 'Florr UI', 'Sarasa SC', sans-serif";
    const widthAt100 = Math.max(1, cardMeasureContext.measureText(text).width);
    const widthCapped = maxWidth / (widthAt100 / 100);
    const font = Math.min(maxFont, Math.max(1, widthCapped));
    labelFontSizeCache.set(cacheKey, font);
    return font;
  } catch {
    return 12;
  }
}

export function makePetalLabelIcon(petalType, rarity = 0) {
  const src = petalIconPath(petalType, rarity);
  if (!src) return null;
  const image = document.createElement("img");
  image.className = "petal-card-label";
  image.alt = "";
  image.draggable = false;
  setPetalLabelIconSource(image, src);
  image.addEventListener("error", () => image.remove(), { once: true });
  return image;
}

export function setPetalLabelIconSource(image, src) {
  let entry = petalLabelIconUrls.get(src);
  if (entry && entry.url) {
    image.src = entry.url;
    return;
  }

  if (!entry) {
    entry = { url: "", waiters: [] };
    petalLabelIconUrls.set(src, entry);
    fetch(src)
      .then((response) => {
        if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
        return response.text();
      })
      .then((svgText) => {
        const blob = new Blob([extractPetalLabelSvg(svgText)], {
          type: "image/svg+xml",
        });
        entry.url = URL.createObjectURL(blob);
        for (const waiter of entry.waiters) waiter.src = entry.url;
        entry.waiters = [];
      })
      .catch(() => {
        entry.url = "";
        for (const waiter of entry.waiters) waiter.remove();
        entry.waiters = [];
      });
  }

  entry.waiters.push(image);
}

export function formatShortCount(value) {
  const count = Math.max(
    0,
    Math.min(1000000000, Math.floor(Number(value) || 0)),
  );
  if (count >= 1000000000) return "1b";
  if (count >= 1000000) return `${formatCompact(count / 1000000)}m`;
  if (count >= 1000) return `${formatCompact(count / 1000)}k`;
  return String(count);
}

export function formatCompact(value) {
  if (value >= 100) return String(Math.floor(value));
  if (value >= 10)
    return (Math.floor(value * 10) / 10).toFixed(1).replace(/\.0$/, "");
  return (Math.floor(value * 10) / 10).toFixed(1).replace(/\.0$/, "");
}

export function makePetalIcon(petalType, rarity = 0, options = {}) {
  const src = petalIconPath(petalType, rarity);
  if (!src) return null;
  const image = document.createElement("img");
  image.className = "petal-icon";
  image.alt = "";
  image.draggable = false;
  if (options.stripped) setStrippedPetalIconSource(image, src);
  else image.src = src;
  image.addEventListener("error", () => image.remove(), { once: true });
  return image;
}

export function setStrippedPetalIconSource(image, src) {
  let entry = strippedPetalIconUrls.get(src);
  if (entry && entry.url) {
    image.src = entry.url;
    return;
  }

  if (!entry) {
    entry = { url: "", waiters: [] };
    strippedPetalIconUrls.set(src, entry);
    fetch(src)
      .then((response) => {
        if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
        return response.text();
      })
      .then((svgText) => {
        const blob = new Blob([stripLivePetalLabel(svgText)], {
          type: "image/svg+xml",
        });
        entry.url = URL.createObjectURL(blob);
        for (const waiter of entry.waiters) waiter.src = entry.url;
        entry.waiters = [];
      })
      .catch(() => {
        entry.url = src;
        for (const waiter of entry.waiters) waiter.src = src;
        entry.waiters = [];
      });
  }

  entry.waiters.push(image);
}

export function makeMimicLabelIcon() {
  const src = petalIconPath(petalMimicType);
  if (!src) return null;
  const image = document.createElement("img");
  image.className = "petal-mimic-label";
  image.alt = "";
  image.draggable = false;
  setMimicLabelIconSource(image, src);
  image.addEventListener("error", () => image.remove(), { once: true });
  return image;
}

export function setMimicLabelIconSource(image, src) {
  let entry = mimicLabelIconUrls.get(src);
  if (entry && entry.url) {
    image.src = entry.url;
    return;
  }

  if (!entry) {
    entry = { url: "", waiters: [] };
    mimicLabelIconUrls.set(src, entry);
    fetch(src)
      .then((response) => {
        if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
        return response.text();
      })
      .then((svgText) => {
        const blob = new Blob([extractMimicLabelSvg(svgText)], {
          type: "image/svg+xml",
        });
        entry.url = URL.createObjectURL(blob);
        for (const waiter of entry.waiters) waiter.src = entry.url;
        entry.waiters = [];
      })
      .catch(() => {
        entry.url = "";
        for (const waiter of entry.waiters) waiter.remove();
        entry.waiters = [];
      });
  }

  entry.waiters.push(image);
}

export function rarityBaseIndex(rarity) {
  return clamp((rarity || 1) - 1, 0, 10);
}

export function petalBasePath(rarity) {
  return `./assets/petals/0_${rarityBaseIndex(rarity)}.svg`;
}

export function petalIconPath(petalType, rarity = 0, options = {}) {
  // The brush and eraser have no art of their own; cards use the basic look.
  if (petalType === petalBrushType || petalType === petalEraserType)
    petalType = petalBasicType;
  if (
    !options.live &&
    petalType === petalStingerType &&
    rarityLevel(rarity) >= rarityLevel(stingerSplitIconMinRarity)
  )
    return "./assets/petals/stinger_split.svg";
  if (
    petalType === petalCompassType &&
    rarityLevel(rarity) >= rarityLevel(compassUltraIconMinRarity)
  )
    return "./assets/petals/compass_ultra.svg";

  const iconId = PetalIconIds[petalType] || 0;
  if (!iconId) return "";
  return `./assets/petals/${iconId}.svg`;
}

export function normalizePetalName(name) {
  return String(name || "")
    .toLowerCase()
    .replace(/[^a-z0-9]/g, "");
}

export function petalTypeFromSnap(snap) {
  const fromEntity = petalTypeFromEntity(snap.entityType);
  if (fromEntity > 0 && fromEntity < PetalIconIds.length) return fromEntity;

  const fromName = PetalNameToType.get(normalizePetalName(snap.name));
  return fromName || fromEntity;
}

export function petalBaseImage(rarity) {
  return assetImage(petalBasePath(rarity));
}

export function assetImage(src) {
  if (!src) return null;
  let image = assetImages.get(src);
  if (!image) {
    image = new Image();
    image.decoding = "async";
    image.addEventListener("load", () => requestRedraw(), {
      once: true,
    });
    image.addEventListener(
      "error",
      () => {
        image.failed = true;
        requestRedraw();
      },
      { once: true },
    );
    image.src = src;
    assetImages.set(src, image);
  }
  return image;
}

export function imageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

// Kicks off loading every texture asset from the manifest; resolves when all
// have finished (failures resolve too so the game can never hang forever).
export function preloadAssets() {
  const pending = [];
  let failed = 0;
  for (const src of ASSET_MANIFEST) {
    const image = assetImage(src);
    if (!image || imageReady(image)) continue;
    pending.push(
      new Promise((resolve) => {
        image.addEventListener("load", resolve, { once: true });
        image.addEventListener(
          "error",
          () => {
            failed += 1;
            resolve();
          },
          { once: true },
        );
      }),
    );
  }
  return Promise.all(pending).then(() => {
    if (failed > 0)
      console.warn(`preloadAssets: ${failed} asset(s) failed to load`);
  });
}


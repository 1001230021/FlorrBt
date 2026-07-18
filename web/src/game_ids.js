import {
  NETWORK_DANDELION_MISSILE_ENTITY_TYPE,
  NETWORK_MISSILE_ENTITY_TYPE,
  NETWORK_POLLEN_ENTITY_TYPE,
  NETWORK_PORTAL_ENTITY_TYPE,
  NETWORK_SPIDER_WEB_ENTITY_TYPE,
} from "./protocol.js";

export const flagAttacking = 1 << 0;
export const flagDefending = 1 << 1;
export const flagDead = 1 << 2;
export const flagOwner = 1 << 3;
export const flagUndead = 1 << 4;
export const flagCorrupted = 1 << 5;
export const flagRelic = 1 << 6;
export const flagAntennae = 1 << 7;
export const flagSummoned = 1 << 8;
export const flagPoisoned = 1 << 9;
export const flagAttached = 1 << 10;
export const flagDigging = 1 << 11;
export const flagSkillWindupShift = 12;
export const flagSkillWindupMask = 0xf000;

export function skillWindupIdFromFlags(flags) {
  return ((flags || 0) & flagSkillWindupMask) >>> flagSkillWindupShift;
}

export const beetleType = 1;
export const normalLadybugType = 3;
export const soldierAntType = 7;
export const summonedBeetleType = 10;
export const summonedSoldierAntType = 11;
export const bandageBeetleType = 12;
export const beeType = 13;
export const hornetType = 14;
export const bumbleBeeType = 15;
export const rockType = 16;
export const babyAntType = 17;
export const workerAntType = 18;
export const queenAntType = 19;
export const antHoleType = 20;
export const spiderType = 21;
export const sandstormType = 22;
export const dummyType = 23;
export const dandelionType = 24;
export const dandelionMissileType = NETWORK_DANDELION_MISSILE_ENTITY_TYPE;
export const pollenProjectileType = NETWORK_POLLEN_ENTITY_TYPE;
export const spiderWebZoneType = NETWORK_SPIDER_WEB_ENTITY_TYPE;
export const hornetMissileType = NETWORK_MISSILE_ENTITY_TYPE;
export const portalType = NETWORK_PORTAL_ENTITY_TYPE;
export const playerFlowerType = 6;
export const bossRarity = 8;
export const maxBossBars = 3;

export const petalAirType = 1;
export const petalAntEggType = 2;
export const petalAntennaeType = 3;
export const petalBasicType = 4;
export const petalBeetleEggType = 5;
export const petalBoneType = 6;
export const petalBubbleType = 7;
export const petalCarrotType = 8;
export const petalCoinType = 9;
export const petalCompassType = 10;
export const petalCogwheelType = 11;
export const petalDiscType = 12;
export const petalDustType = 13;
export const petalGoldenLeafType = 14;
export const petalIrisType = 15;
export const petalLentilType = 16;
export const petalMoonType = 17;
export const petalNullificationType = 18;
export const petalPincerType = 19;
export const petalRelicType = 20;
export const petalRoseType = 21;
export const petalYinYangType = 22;
export const petalMissileType = 23;
export const petalBloodSacrificeType = 24;
export const petalCorruptionType = 25;
export const petalBandageType = 26;
export const petalHeavyType = 27;
export const petalFasterType = 28;
export const petalYggdrasilType = 29;
export const petalDahliaType = 30;
export const petalWingType = 31;
export const petalTriangleType = 32;
export const petalSawbladeType = 33;
export const petalFragmentType = 34;
export const petalMimicType = 35;
export const petalGlassType = 36;
export const petalStingerType = 37;
export const petalBrokenEggType = 38;
export const petalLightType = 39;
export const petalLeafType = 40;
export const petalRockType = 41;
export const petalWebType = 42;
export const petalCactusType = 43;
export const petalPollenType = 44;
export const petalCornType = 45;
export const petalRiceType = 46;
export const petalBasilType = 47;
export const petalSoilType = 48;
export const petalHoneyType = 49;
export const petalWaxType = 50;
export const petalThirdEyeType = 51;
export const petalDandelionType = 52;
export const petalOrangeType = 53;
export const petalShovelType = 54;

export const stingerSplitIconMinRarity = 6;
export const compassUltraIconMinRarity = 7;
export const flowerTextureVersion = "20260717a";

export const PetalIconIds = [
  0, 48, 51, 17, 1, 16, 58, 13, 57, 74, 71, 98, 72, 111, 97, 7, 113, 77,
  "nullification", 27, 53, 5, 19, 9, 108, 80, 103, 18, 12, 30, 38, 8, 106,
  109, 94, 93, "glass", 6, "broken_egg", 2, 22, 3, 20, 11, 14,
  25, 24, 49, 41, 21, 96, 43, 15, 50, 73,
];

export const worldPetalSizeScale = 6;
export const worldDropSizeScale = 3.25;
export const petalCardIconScale = 0.92;
export const mobSpriteEffectiveBox = 91.667;
export const mobSpriteViewBox = 110;
export const mobSpriteCoverScale = mobSpriteViewBox / mobSpriteEffectiveBox;
export const beetleSpriteForwardOffsetScale = 0.42;

export const nonStackPetalTypes = new Set([
  petalAirType,
  petalAntennaeType,
  petalMoonType,
  petalNullificationType,
  petalRelicType,
  petalBloodSacrificeType,
  petalCorruptionType,
  petalBandageType,
  petalBrokenEggType,
  petalWaxType,
  petalThirdEyeType,
  petalShovelType,
]);

export const rarityExotic = 12;
export const rarityDisplayOrder = [1, 2, 3, 4, 5, 6, 7, rarityExotic, 8, 9, 10, 11];
export const rarityShortNames = ["", "C", "Un", "R", "E", "L", "M", "U", "S", "Et", "Q", "P", "Ex"];
export const raritySortRanks = new Map(rarityDisplayOrder.map((rarity, index) => [rarity, index + 1]));

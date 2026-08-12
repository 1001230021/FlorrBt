import {
  NETWORK_BLOOD_SACRIFICE_ENTITY_TYPE,
  NETWORK_DANDELION_MISSILE_ENTITY_TYPE,
  NETWORK_MISSILE_ENTITY_TYPE,
  NETWORK_POLLEN_ENTITY_TYPE,
  NETWORK_PORTAL_ENTITY_TYPE,
  NETWORK_SPIDER_WEB_ENTITY_TYPE,
  NETWORK_TRAP_PROJECTILE_ENTITY_TYPE,
} from "./protocol.js";
import {
  RARITY_IDS,
  RARITY_NAMES,
  PETAL_IDS,
  PETAL_ICON_IDS as GENERATED_PETAL_ICON_IDS,
  MOB_IDS,
  STATE_IDS,
} from "./generated_ids.js";

export const flagAttacking = 1 << 0;
export const flagDefending = 1 << 1;
export const flagDead = 1 << 2;
export const flagOwner = 1 << 3;
export const flagAntennae = 1 << 7;
export const flagSummoned = 1 << 8;
export const flagAttached = 1 << 10;
export const flagCarryingLeafPiece = flagAttached;
export const flagSkillWindupShift = 12;
export const flagSkillWindupMask = 0xf000;

export const statePoison = STATE_IDS.poison;
export const stateBanSlot = STATE_IDS.ban_slot;
export const statePincerSpeedReduce = STATE_IDS.pincer_speed_reduce;
export const stateWebSpeedReduce = STATE_IDS.web_speed_reduce;
export const stateAntiHeal = STATE_IDS.anti_heal;
export const stateNullification = STATE_IDS.nullification;
export const stateUndead = STATE_IDS.undead;
export const stateCorruption = STATE_IDS.corruption;
export const stateNoRevive = STATE_IDS.no_revive;
export const stateInvincible = STATE_IDS.invincible;
export const stateDigging = STATE_IDS.digging;
export const statePsionicConnection = STATE_IDS.psionic_connection;
export const stateHallucinogenic = STATE_IDS.hallucinogenic;

export function entityState(snap, stateType) {
  if (!Array.isArray(snap?.states)) return null;
  return (
    snap.states.find((entry) => (entry?.type ?? entry) === stateType) || null
  );
}

export function entityHasState(snap, stateType) {
  return entityState(snap, stateType) !== null;
}

export function skillWindupIdFromFlags(flags) {
  return ((flags || 0) & flagSkillWindupMask) >>> flagSkillWindupShift;
}

export const beetleType = MOB_IDS.beetle;
export const normalLadybugType = MOB_IDS.normal_ladybug;
export const mechaFlowerType = MOB_IDS.mecha_flower;
export const normalFlowerType = MOB_IDS.normal_flower;
export const soldierAntType = MOB_IDS.soldier_ant;
export const soldierFireAntType = MOB_IDS.soldier_fire_ant;
export const soldierTermiteType = MOB_IDS.soldier_termite;
export const summonedBeetleType = MOB_IDS.summoned_beetle;
export const summonedSoldierAntType = MOB_IDS.summoned_soldier_ant;
export const bandageBeetleType = MOB_IDS.bandage_beetle;
export const beeType = MOB_IDS.bee;
export const hornetType = MOB_IDS.hornet;
export const bumbleBeeType = MOB_IDS.bumble_bee;
export const rockType = MOB_IDS.rock;
export const babyAntType = MOB_IDS.baby_ant;
export const workerAntType = MOB_IDS.worker_ant;
export const queenAntType = MOB_IDS.queen_ant;
export const antHoleType = MOB_IDS.ant_hole;
export const spiderType = MOB_IDS.spider;
export const sandstormType = MOB_IDS.sandstorm;
export const dummyType = MOB_IDS.dummy;
export const dandelionType = MOB_IDS.dandelion;
export const antEggMobType = MOB_IDS.ant_egg;
export const fireAntEggType = MOB_IDS.fire_ant_egg;
export const termiteEggType = MOB_IDS.termite_egg;
export const queenAntEggType = MOB_IDS.queen_ant_egg;
export const queenFireAntEggType = MOB_IDS.queen_fire_ant_egg;
export const babyFireAntType = MOB_IDS.baby_fire_ant;
export const workerFireAntType = MOB_IDS.worker_fire_ant;
export const fireQueenAntType = MOB_IDS.fire_queen_ant;
export const babyTermiteType = MOB_IDS.baby_termite;
export const workerTermiteType = MOB_IDS.worker_termite;
export const termiteOvermindType = MOB_IDS.termite_overmind;
export const leafPieceType = MOB_IDS.leaf_piece;
export const leafcutterSoldierType = MOB_IDS.leafcutter_soldier;
export const titanType = MOB_IDS.titan;
export const trapProjectileType = NETWORK_TRAP_PROJECTILE_ENTITY_TYPE;
export const bloodSacrificeEffectType = NETWORK_BLOOD_SACRIFICE_ENTITY_TYPE;
export const dandelionMissileType = NETWORK_DANDELION_MISSILE_ENTITY_TYPE;
export const pollenProjectileType = NETWORK_POLLEN_ENTITY_TYPE;
export const spiderWebZoneType = NETWORK_SPIDER_WEB_ENTITY_TYPE;
export const hornetMissileType = NETWORK_MISSILE_ENTITY_TYPE;
export const portalType = NETWORK_PORTAL_ENTITY_TYPE;
export const playerFlowerType = MOB_IDS.player_flower;
export const bossRarity = RARITY_IDS.super;
export const maxBossBars = 3;

export const petalAirType = PETAL_IDS.air;
export const petalAntEggType = PETAL_IDS.ant_egg;
export const petalAntennaeType = PETAL_IDS.antennae;
export const petalBasicType = PETAL_IDS.basic;
export const petalBeetleEggType = PETAL_IDS.beetle_egg;
export const petalBoneType = PETAL_IDS.bone;
export const petalBubbleType = PETAL_IDS.bubble;
export const petalCarrotType = PETAL_IDS.carrot;
export const petalCoinType = PETAL_IDS.coin;
export const petalCompassType = PETAL_IDS.compass;
export const petalCogwheelType = PETAL_IDS.cogwheel;
export const petalDiscType = PETAL_IDS.disc;
export const petalDustType = PETAL_IDS.dust;
export const petalGoldenLeafType = PETAL_IDS.golden_leaf;
export const petalIrisType = PETAL_IDS.iris;
export const petalLentilType = PETAL_IDS.lentil;
export const petalMoonType = PETAL_IDS.moon;
export const petalNullificationType = PETAL_IDS.nullification;
export const petalPincerType = PETAL_IDS.pincer;
export const petalRelicType = PETAL_IDS.relic;
export const petalRoseType = PETAL_IDS.rose;
export const petalYinYangType = PETAL_IDS.yin_yang;
export const petalMissileType = PETAL_IDS.missile;
export const petalBloodSacrificeType = PETAL_IDS.blood_sacrifice;
export const petalCorruptionType = PETAL_IDS.corruption;
export const petalBandageType = PETAL_IDS.bandage;
export const petalHeavyType = PETAL_IDS.heavy;
export const petalFasterType = PETAL_IDS.faster;
export const petalYggdrasilType = PETAL_IDS.yggdrasil;
export const petalDahliaType = PETAL_IDS.dahlia;
export const petalWingType = PETAL_IDS.wing;
export const petalTriangleType = PETAL_IDS.triangle;
export const petalSawbladeType = PETAL_IDS.sawblade;
export const petalFragmentType = PETAL_IDS.fragment;
export const petalMimicType = PETAL_IDS.mimic;
export const petalGlassType = PETAL_IDS.glass;
export const petalStingerType = PETAL_IDS.stinger;
export const petalBrokenEggType = PETAL_IDS.broken_egg;
export const petalLightType = PETAL_IDS.light;
export const petalLeafType = PETAL_IDS.leaf;
export const petalRockType = PETAL_IDS.rock;
export const petalWebType = PETAL_IDS.web;
export const petalCactusType = PETAL_IDS.cactus;
export const petalPollenType = PETAL_IDS.pollen;
export const petalCornType = PETAL_IDS.corn;
export const petalRiceType = PETAL_IDS.rice;
export const petalBasilType = PETAL_IDS.basil;
export const petalSoilType = PETAL_IDS.soil;
export const petalHoneyType = PETAL_IDS.honey;
export const petalWaxType = PETAL_IDS.wax;
export const petalThirdEyeType = PETAL_IDS.third_eye;
export const petalDandelionType = PETAL_IDS.dandelion;
export const petalOrangeType = PETAL_IDS.orange;
export const petalShovelType = PETAL_IDS.shovel;
export const petalYuccaType = PETAL_IDS.yucca;
export const petalWhiteFungusType = PETAL_IDS.white_fungus;
export const petalBlackFungusType = PETAL_IDS.black_fungus;
export const petalBroccoliType = PETAL_IDS.broccoli;
export const petalDouliType = PETAL_IDS.douli;
export const petalTrapperType = PETAL_IDS.trapper;
export const petalAmuletType = PETAL_IDS.amulet;
export const petalPlankType = PETAL_IDS.plank;
export const petalTomatoType = PETAL_IDS.tomato;
export const petalHallucinogenicMushroomType = PETAL_IDS.hallucinogenic_mushroom;

export const stingerSplitIconMinRarity = 6;
export const compassUltraIconMinRarity = 7;
export const flowerTextureVersion = "20260802a";

export const PetalIconIds = GENERATED_PETAL_ICON_IDS;

export const worldPetalSizeScale = 6;
export const worldDropSizeScale = 3.25;
export const hallucinogenicMushroomLiveViewBox = "30.5 13 52 52";
export const hallucinogenicMushroomLiveVisualScale = 1 / 3;
export const trapperLivePetalViewBox = "2.32 -14.95 110 110";
export const waxLivePetalViewBox = "29 22 52 52";
export const waxLivePetalVisualScale = 0.52;
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
  petalDouliType,
]);

export const rarityExotic = RARITY_IDS.exotic;
export const raritySuper = RARITY_IDS.super;
export const rarityEternal = RARITY_IDS.eternal;
export const rarityUnique = RARITY_IDS.unique;
export const rarityPrimordial = RARITY_IDS.primordial;
export const rarityDisplayOrder = [
  RARITY_IDS.common,
  RARITY_IDS.unusual,
  RARITY_IDS.rare,
  RARITY_IDS.epic,
  RARITY_IDS.legendary,
  RARITY_IDS.mythic,
  RARITY_IDS.ultra,
  rarityExotic,
  RARITY_IDS.super,
  rarityEternal,
  rarityUnique,
  rarityPrimordial,
];
export const rarityShortNames = [
  "",
  "C",
  "Un",
  "R",
  "E",
  "L",
  "M",
  "U",
  "S",
  "Et",
  "Q",
  "P",
  "Ex",
];
export const raritySortRanks = new Map(
  rarityDisplayOrder.map((rarity, index) => [rarity, index + 1]),
);

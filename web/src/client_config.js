const clientConfigEntries = new Map();
const clientConfigAliases = new Map();

registerClientConfig("dande_missile_scale", {
  prop: "dandeMissileScale",
  defaultValue: 4.5,
  min: 0.2,
  max: 6,
  aliases: ["dande_scale", "dandelion_missile_scale"],
});

registerClientConfig("dande_missile_angle", {
  prop: "dandeMissileAngle",
  defaultValue: -0.92,
  min: -Math.PI * 8,
  max: Math.PI * 8,
  aliases: [
    "dande_angle",
    "dande_missile_angle_offset",
    "dandelion_missile_angle",
  ],
});

registerClientConfig("dande_missile_offset", {
  prop: "dandeMissileOffset",
  defaultValue: 0,
  min: -4,
  max: 4,
  aliases: ["dande_offset", "dandelion_missile_offset"],
});

registerClientConfig("dande_missile_y_offset", {
  prop: "dandeMissileYOffset",
  defaultValue: 0,
  min: -4,
  max: 4,
  aliases: [
    "dande_y_offset",
    "dande_missile_tangent_offset",
    "dande_tangent_offset",
    "dandelion_missile_y_offset",
  ],
});

registerClientConfig("dande_missile_anchor_x", {
  prop: "dandeMissileAnchorX",
  defaultValue: 0,
  min: -4,
  max: 4,
  aliases: [
    "dande_anchor_x",
    "dande_missile_anchor_radial",
    "dandelion_missile_anchor_x",
  ],
});

registerClientConfig("dande_missile_anchor_y", {
  prop: "dandeMissileAnchorY",
  defaultValue: 0,
  min: -4,
  max: 4,
  aliases: [
    "dande_anchor_y",
    "dande_missile_anchor_tangent",
    "dandelion_missile_anchor_y",
  ],
});

registerClientConfig("dande_base_x", {
  prop: "dandeBaseX",
  defaultValue: 0,
  min: -4,
  max: 4,
  aliases: ["dande_base_offset_x", "dandelion_base_x"],
});

registerClientConfig("dande_base_y", {
  prop: "dandeBaseY",
  defaultValue: 0,
  min: -4,
  max: 4,
  aliases: ["dande_base_offset_y", "dandelion_base_y"],
});

registerClientConfig("dande_base_scale", {
  prop: "dandeBaseScale",
  defaultValue: 6.5,
  min: 0.2,
  max: 8,
  aliases: ["dandelion_base_scale"],
});

registerClientConfig("dande_base_angle", {
  prop: "dandeBaseAngle",
  defaultValue: 0.035,
  min: -Math.PI * 8,
  max: Math.PI * 8,
  aliases: ["dandelion_base_angle", "dande_base_rotation"],
});

registerClientConfig("hornet_sprite_scale", {
  prop: "hornetSpriteScale",
  defaultValue: 2.3,
  min: 0.2,
  max: 8,
  aliases: ["hornet_scale"],
});

registerClientConfig("dummy_sprite_scale", {
  prop: "dummySpriteScale",
  defaultValue: 1.5,
  min: 0.5,
  max: 4,
  aliases: ["dummy_scale"],
});

registerClientConfig("titan_sprite_scale", {
  prop: "titanSpriteScale",
  defaultValue: 2,
  min: 0.5,
  max: 4,
  aliases: ["titan_scale"],
});

registerClientConfig("hornet_body_offset_x", {
  prop: "hornetBodyOffsetX",
  defaultValue: -7,
  min: -32,
  max: 32,
  aliases: ["hornet_offset_x", "hornet_body_x"],
});

registerClientConfig("hornet_body_offset_y", {
  prop: "hornetBodyOffsetY",
  defaultValue: -7,
  min: -32,
  max: 32,
  aliases: ["hornet_offset_y", "hornet_body_y"],
});

registerClientConfig("colorful_map", {
  prop: "colorfulMap",
  defaultValue: 1,
  min: 0,
  max: 1,
});

const minimapMaterialColors = Object.freeze({
  anthole: "#9b6947",
  bridge: "#d7d9d6",
  bush: "#2d8539",
  castle: "#999999",
  cellar: "#8b6a45",
  cobblestone: "#666666",
  coral: "#cd7c92",
  desert: "#d7bd72",
  dirt: "#9b6947",
  dirt2: "#b48763",
  factory_floor: "#c6c6c6",
  factory_wall: "#999999",
  factory: "#8f9496",
  fire: "#8f3838",
  foliage: "#2f754c",
  grass: "#78b86b",
  grass2: "#68aa5d",
  grate: "#555555",
  hel: "#8f3838",
  hut: "#bf9847",
  ocean: "#4d98d4",
  organic_wall: "#6e1515",
  pvp: "#4c5b53",
  pyramid: "#c5b58a",
  pyramid_floor: "#c5b58a",
  pyramid_wall: "#998c6a",
  root: "#99532a",
  sand: "#d7bd72",
  scliff: "#d9c598",
  seaweed: "#72992c",
  sewage: "#777744",
  sewer: "#6f704d",
  shipwall: "#856e49",
  shipwreck: "#58421d",
  soil: "#9b6947",
  stone_bridge_rect: "#d7d9d6",
  termite: "#a88642",
  tumbleweed: "#a88642",
  vent: "#737373",
  vine: "#2e6f2d",
  water: "#4d98d4",
  wood: "#b1843b",
  worm: "#601d1d",
});

export const clientRuntimeConfig = Object.freeze({
  serverTickInterval: 0.016,
  expectedSnapshotInterval: 0.048,
  packetInterval: 1 / 30,
  deathFadeDuration: 0.25,
  deathScaleBoost: 0.72,
  hurtFlashDuration: 0.14,
  hurtFlashMinDelta: 0.002,
  hurtFlashFilterEntityLimit: 32,
  missingEntityViewEdgeGrace: 48,
  viewScreenFill: 0.46,
  viewScreenPadding: 36,
  mouseMoveDeadzonePx: 16,
  entityFrameMinScreenRadius: 8,
  entityPixelMinScreenRadius: 0.45,
  entityDetailMinScreenRadius: 2.4,
  petalDetailMinScreenRadius: 0.9,
  dropDetailMinScreenRadius: 1.15,
  bloodSacrificeDrawPhaseEnd: 0.5,
  bloodSacrificeInitialHeading: Math.PI * 0.5,
  bloodSacrificeInnerRadiusScale: 0.5,
  bloodSacrificeOuterAlpha: 0.25,
  bloodSacrificeShakeAmplitudePx: 9,
  bloodSacrificeParticleReferenceRadius: 2048,
  bloodSacrificeHeadParticleBurstTicks: 4,
  bloodSacrificeHeadParticleCount: 6,
  bloodSacrificeHeadParticleRadius: 10,
  bloodSacrificeSweepIntervalSeconds: 2.5,
  bloodSacrificeSweepDurationSeconds: 5,
  bloodSacrificeSweepEmissionSpacingPx: 2,
  bloodSacrificeSweepStepMin: 1,
  bloodSacrificeSweepStepMax: 16,
  bloodSacrificeSweepReferenceStep: 8,
  bloodSacrificeSweepParticleCount: 2,
  bloodSacrificeSweepParticleRadius: 6,
  bloodSacrificeVisionMaxAlpha: 0.55,
  petalParticleBurstTicks: 8,
  particleLifetimeSeconds: 0.4,
  particleMaxCount: 4096,
  particleSpeedMin: 28,
  particleSpeedMax: 64,
  particleVelocityDampingPerSecond: 7,
  particleSize: 5.5,
  directionalPetalAngleOffset: -Math.PI * 0.25,
  compassPetalAngleOffset: Math.PI * 0.5,
  renderCullPaddingPx: 120,
  renderLoadMediumEntityCount: 260,
  renderLoadHighEntityCount: 520,
  ownerRenderLerpRate: 44,
  entityRenderLerpRate: 24,
  ownerAngleLerpRate: 48,
  entityAngleLerpRate: 28,
  renderSnapBaseDistance: 220,
  ownerRenderSnapBaseDistance: 120,
  serverFixedDt: 0.016,
  ownerBaseMaxVelocity: 150 * 1.25,
  ownerBaseAcceleration: 300 * 1.25,
  ownerDiggingSpeedMultiplier: 0.5,
  ownerStopDampingPerTick: 0.9,
  ownerStopVelocityEpsilon: 0.00001,
  ownerSlowToMaxVelocityTime: 0.35,
  ownerPredictionTeleportDistance: 320,
  ownerPredictionHardCorrectionDistance: 144,
  ownerPredictionCorrectionDeadzone: 0.5,
  ownerPredictionSoftCorrectionRate: 4,
  ownerPredictionHardCorrectionRate: 18,
  ownerPredictionSpeedSampleMinDt: 0.005,
  ownerPredictionSnapshotStaleMin: 0.22,
  ownerPredictionTimingSampleCount: 7,
  ownerPredictionMinTickScale: 0.2,
  entitySnapshotVelocityBlend: 0.35,
  entityExtrapolateMaxSeconds: 0.05,
  entityExtrapolateMaxDistance: 32,
  petalRenderLerpRate: 18,
  petalAngleLerpRate: 16,
  petalExtrapolateMaxSeconds: 0.016,
  petalExtrapolateMaxDistance: 10,
  slotTransactionTimeoutMs: 3500,
  bossBarBandTopRatio: 1 / 32,
  bossBarBandBottomRatio: 1 / 3,
  bossBarWidthRatio: 0.31,
  bossBarMinWidthPx: 310,
  bossBarMaxWidthPx: 500,
  bossBarHorizontalMarginPx: 12,
  bossBarHeightPx: 60,
  bossBarTitleFontPx: 34,
  bossBarRarityFontPx: 27,
  bossBarLabelGapPx: 8,
  bossBarTitleOutlinePx: 7,
  bossBarRarityOutlinePx: 6,
  bossBarRadiusPx: 8,
  loginMapDefaultName: "garden.tmj",
  // Center of Garden's `new_players` checkpoint.
  loginMapDefaultX: 51204.928,
  loginMapDefaultY: 65008,
  // WorldUnits(256) * the Super Antennae horizon multiplier (4).
  loginMapDefaultHorizon: 2048,
  titanForgeRange: 2048,
  minimapMarginPx: 12,
  minimapLocalMaxPx: 164,
  minimapFullMaxPx: 272,
  minimapHudGapPx: 12,
  minimapTileSubdivisions: 2,
  minimapTextureSampleSizePx: 32,
  minimapTextureCoverageThreshold: 0.002,
  minimapCollisionSamplesPerAxis: 8,
  minimapCollisionCoverageThreshold: 0.08,
  minimapBackgroundColor: "#f4f5f2",
  minimapMaterialColors,
  minimapUnknownMaterialColor: "#777777",
  minimapMonochromeBackgroundColor: "#ffffff",
  minimapMonochromeWallColor: "#000000",
  checkpointZoneOutlineColor: "rgba(255, 216, 48, 0.96)",
  checkpointZoneOutlineWidthPx: 1.25,
  chatMaxHistory: 80,
  chatClosedMaxLines: 6,
  chatVisibleMs: 10000,
  chatFadeMs: 3000,
  chatClosedRenderIntervalMs: 180,
});

export const defaultClientConfig = Object.freeze(
  Object.fromEntries(
    Array.from(clientConfigEntries.values()).map((entry) => [
      entry.prop,
      entry.defaultValue,
    ]),
  ),
);

export function listClientConfigEntries() {
  return Array.from(clientConfigEntries.values());
}

export function getClientConfigEntry(name) {
  return findClientConfigEntry(name);
}

export function normalizeClientConfig(config) {
  const source = config && typeof config === "object" ? config : {};
  const normalized = {};
  for (const entry of clientConfigEntries.values()) {
    normalized[entry.prop] = clampNumber(
      source[entry.prop],
      entry.defaultValue,
      entry.min,
      entry.max,
    );
  }
  return normalized;
}

export function getClientConfigValue(config, propOrName) {
  const entry = findClientConfigEntry(propOrName);
  if (!entry) return undefined;
  const value = config?.[entry.prop];
  return Number.isFinite(value) ? value : entry.defaultValue;
}

export function setClientConfigValue(config, name, rawValue) {
  const entry = findClientConfigEntry(name);
  if (!entry) return { ok: false, error: "unknown", name };

  const parsed = parseClientConfigNumber(rawValue);
  if (!Number.isFinite(parsed))
    return { ok: false, error: "invalid", name: entry.name };

  const value = clampNumber(parsed, entry.defaultValue, entry.min, entry.max);
  return makeClientConfigResult(config, entry, value);
}

export function resetClientConfig(config, name) {
  if (!name)
    return { ok: true, name: "all", config: { ...defaultClientConfig } };

  const entry = findClientConfigEntry(name);
  if (!entry) return { ok: false, error: "unknown", name };
  return makeClientConfigResult(config, entry, entry.defaultValue);
}

export function parseClientConfigNumber(value) {
  const text = String(value ?? "")
    .trim()
    .toLowerCase();
  if (text === "pi") return Math.PI;
  if (text === "-pi") return -Math.PI;

  const piFraction = text.match(/^(-?)pi\/([0-9]+(?:\.[0-9]+)?)$/);
  if (piFraction) {
    const divisor = Number(piFraction[2]);
    return divisor > 0 ? ((piFraction[1] ? -1 : 1) * Math.PI) / divisor : NaN;
  }

  return Number(text);
}

export function formatClientConfigValue(value) {
  return Number.isFinite(value)
    ? Number(value)
        .toFixed(6)
        .replace(/\.?0+$/, "")
    : "nan";
}

function registerClientConfig(name, options) {
  const normalizedName = normalizeClientConfigKey(name);
  const entry = {
    name: normalizedName,
    prop: options.prop,
    defaultValue: options.defaultValue,
    min: options.min ?? -Infinity,
    max: options.max ?? Infinity,
  };
  clientConfigEntries.set(normalizedName, entry);
  for (const alias of options.aliases || []) {
    clientConfigAliases.set(normalizeClientConfigKey(alias), entry.name);
  }
}

function findClientConfigEntry(name) {
  const normalized = normalizeClientConfigKey(name);
  const entryName = clientConfigAliases.get(normalized) || normalized;
  return clientConfigEntries.get(entryName) || null;
}

function normalizeClientConfigKey(key) {
  return String(key || "")
    .trim()
    .toLowerCase()
    .replace(/[.\-]/g, "_");
}

function makeClientConfigResult(config, entry, value) {
  return {
    ok: true,
    name: entry.name,
    prop: entry.prop,
    value,
    config: {
      ...defaultClientConfig,
      ...normalizeClientConfig(config),
      [entry.prop]: value,
    },
  };
}

function clampNumber(value, fallback, min, max) {
  const number = Number(value);
  if (!Number.isFinite(number)) return fallback;
  return Math.max(min, Math.min(max, number));
}

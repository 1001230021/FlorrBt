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
  aliases: ["dande_angle", "dande_missile_angle_offset", "dandelion_missile_angle"],
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
  aliases: ["dande_y_offset", "dande_missile_tangent_offset", "dande_tangent_offset", "dandelion_missile_y_offset"],
});

registerClientConfig("dande_missile_anchor_x", {
  prop: "dandeMissileAnchorX",
  defaultValue: 0,
  min: -4,
  max: 4,
  aliases: ["dande_anchor_x", "dande_missile_anchor_radial", "dandelion_missile_anchor_x"],
});

registerClientConfig("dande_missile_anchor_y", {
  prop: "dandeMissileAnchorY",
  defaultValue: 0,
  min: -4,
  max: 4,
  aliases: ["dande_anchor_y", "dande_missile_anchor_tangent", "dandelion_missile_anchor_y"],
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

export const defaultClientConfig = Object.freeze(
  Object.fromEntries(Array.from(clientConfigEntries.values()).map((entry) => [entry.prop, entry.defaultValue]))
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
    normalized[entry.prop] = clampNumber(source[entry.prop], entry.defaultValue, entry.min, entry.max);
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
  if (!Number.isFinite(parsed)) return { ok: false, error: "invalid", name: entry.name };

  const value = clampNumber(parsed, entry.defaultValue, entry.min, entry.max);
  return makeClientConfigResult(config, entry, value);
}

export function resetClientConfig(config, name) {
  if (!name) return { ok: true, name: "all", config: { ...defaultClientConfig } };

  const entry = findClientConfigEntry(name);
  if (!entry) return { ok: false, error: "unknown", name };
  return makeClientConfigResult(config, entry, entry.defaultValue);
}

export function parseClientConfigNumber(value) {
  const text = String(value ?? "").trim().toLowerCase();
  if (text === "pi") return Math.PI;
  if (text === "-pi") return -Math.PI;

  const piFraction = text.match(/^(-?)pi\/([0-9]+(?:\.[0-9]+)?)$/);
  if (piFraction) {
    const divisor = Number(piFraction[2]);
    return divisor > 0 ? (piFraction[1] ? -1 : 1) * Math.PI / divisor : NaN;
  }

  return Number(text);
}

export function formatClientConfigValue(value) {
  return Number.isFinite(value) ? Number(value).toFixed(6).replace(/\.?0+$/, "") : "nan";
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
  return String(key || "").trim().toLowerCase().replace(/[.\-]/g, "_");
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

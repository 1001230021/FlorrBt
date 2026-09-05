#!/usr/bin/env node
"use strict";
// Generates web/src/generated_ids.js from the C++ enum definitions in
// src/Shared/. This keeps the JavaScript client's wire IDs aligned with the
// server's single source of truth: add/rename an enum entry in C++, then
// re-run `node tools/generate_shared_ids.js` and commit the regenerated file.
//
// Petals merge two sources: native EPetalType (petal_type.h) plus the
// controller-owned EModPetalType (mod_petal_type.h) — mod petals carry real
// client-visible wire ids, pinned forever. Mod MOBS (mod_mob_type.h) are
// deliberately NOT emitted: RegisterModMob forces a vanilla visual proxy, so
// mod mob ids never reach the wire and the client must not learn them.
//
// Usage: node tools/generate_shared_ids.js
// Output: web/src/generated_ids.js (checked in; the web client has no build step)

const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const sharedDir = path.join(root, "src", "Shared");
const outPath = path.join(root, "web", "src", "generated_ids.js");

function fail(message) {
  console.error(`[generate_shared_ids] ${message}`);
  process.exit(1);
}

function parseEnum(fileName, enumName) {
  const filePath = path.join(sharedDir, fileName);
  const text = fs.readFileSync(filePath, "utf8");
  const pattern = new RegExp(
    `enum\\s+class\\s+${enumName}\\s*:\\s*[A-Za-z_][\\w:]*\\s*\\{([^}]*)\\}`,
  );
  const match = pattern.exec(text);
  if (!match) fail(`enum ${enumName} not found in ${fileName}`);
  const entries = [];
  let nextValue = 0;
  for (const rawLine of match[1].split("\n")) {
    // Strip CR first: on CRLF files `.*$` stops before the \r and the
    // comment would survive into the entry parser.
    let line = rawLine.replace(/\r/g, "").replace(/\/\/.*$/, "").trim();
    if (!line) continue;
    line = line.replace(/,$/, "").trim();
    if (!line) continue;
    const equals = line.indexOf("=");
    let name;
    let value;
    if (equals >= 0) {
      name = line.slice(0, equals).trim();
      const raw = line.slice(equals + 1).trim().replace(/[uU]$/, "");
      const parsed = parseInt(raw, 10);
      if (!Number.isInteger(parsed)) fail(`cannot parse value for ${name} in ${fileName}: ${raw}`);
      value = parsed;
    } else {
      name = line;
      value = nextValue;
    }
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) fail(`invalid enum entry "${name}" in ${fileName}`);
    entries.push({ name, value });
    nextValue = value + 1;
  }
  if (!entries.length) fail(`enum ${enumName} in ${fileName} has no entries`);
  return entries;
}

// GoldenLeaf -> golden_leaf, BloodSacrifice -> blood_sacrifice, YinYang -> yin_yang
function toSnakeCase(name) {
  return name
    .replace(/([A-Z]+)([A-Z][a-z])/g, "$1_$2")
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .toLowerCase();
}

// Parses `inline constexpr std::array<std::string_view, N> arr = {...};`
// and returns the string entries in order.
function parseNamesArray(fileName, arrayName) {
  const filePath = path.join(sharedDir, fileName);
  const text = fs.readFileSync(filePath, "utf8");
  const pattern = new RegExp(
    `${arrayName}\\s*=\\s*\\{([^}]*)\\}`,
  );
  const match = pattern.exec(text);
  if (!match) fail(`array ${arrayName} not found in ${fileName}`);
  const names = [];
  for (const token of match[1].split(",")) {
    const trimmed = token.trim().replace(/^"|"$/g, "");
    if (trimmed) names.push(trimmed);
  }
  return names;
}

// Parses the mod manifest rows `{ static_cast<int>(EModPetalType::Brush), "Brush" }`
// so the C++ name table and the enum cannot drift apart.
function parseModManifest(fileName, enumName) {
  const filePath = path.join(sharedDir, fileName);
  const text = fs.readFileSync(filePath, "utf8");
  const pattern = new RegExp(
    `\\{\\s*static_cast<int>\\(${enumName}::(\\w+)\\)\\s*,\\s*"([^"]+)"\\s*\\}`,
    "g",
  );
  const rows = new Map();
  let match;
  while ((match = pattern.exec(text)) !== null) rows.set(match[1], match[2]);
  if (!rows.size) fail(`no manifest rows found for ${enumName} in ${fileName}`);
  return rows;
}

const rarity = parseEnum("rarity.h", "ERarity");
const petalNative = parseEnum("petal_type.h", "EPetalType");
const petalMod = parseEnum("mod_petal_type.h", "EModPetalType").filter((entry) => entry.name !== "None");
const mob = parseEnum("mob_type.h", "EMobType");
const state = parseEnum("state_type.h", "EStateType");

// ---- petals: merge natives + mod petals into one id space ----
const petal = [...petalNative, ...petalMod].sort((a, b) => a.value - b.value);
const petalByValue = new Map();
for (const entry of petal) {
  if (petalByValue.has(entry.value))
    fail(`petal id ${entry.value} claimed by both "${petalByValue.get(entry.value).name}" and "${entry.name}"`);
  petalByValue.set(entry.value, entry);
}

// Cross-check the C++ name arrays so a rename cannot silently desync the JS.
// petal_type_names is value-indexed; reserved mod slots must read "None"
// natively and be claimed by a mod petal instead.
const petalNames = parseNamesArray("petal_type.h", "petal_type_names");
for (const entry of petalNative) {
  if (entry.value < 0 || entry.value >= petalNames.length || petalNames[entry.value] !== entry.name)
    fail(`petal_type_names[${entry.value}] = "${petalNames[entry.value] || "(missing)"}" but EPetalType entry is "${entry.name}"`);
}
for (const entry of petalMod) {
  if (entry.value < petalNames.length && petalNames[entry.value] !== "None")
    fail(`mod petal "${entry.name}" (${entry.value}) collides with native name "${petalNames[entry.value]}"`);
}
const petalModManifest = parseModManifest("mod_petal_type.h", "EModPetalType");
for (const entry of petalMod) {
  const manifestName = petalModManifest.get(entry.name);
  if (!manifestName)
    fail(`mod petal "${entry.name}" has no row in mod_petal_type_names (the controller manifest)`);
  if (manifestName !== entry.name)
    fail(`mod petal manifest name "${manifestName}" does not match enum entry "${entry.name}"`);
}

// ---- mobs: natives only (mod mobs never reach the client) ----
const mobNames = parseNamesArray("mob_type.h", "mob_type_names");
if (mobNames.length !== mob.length)
  fail(`mob_type_names has ${mobNames.length} entries but EMobType has ${mob.length}`);
for (let i = 0; i < mob.length; i += 1) {
  const value = mob[i].value;
  if (value < 0 || value >= mobNames.length || mobNames[value] !== mob[i].name)
    fail(`mob_type_names[${value}] = "${mobNames[value] || "(missing)"}" but EMobType entry is "${mob[i].name}"`);
}

// Rarity names in rarity.h are a switch; the generator asserts the JS view of
// them so the hand-written rarity_names array (added to rarity.h) stays aligned.
const rarityNames = [
  "Null",
  "Common",
  "Unusual",
  "Rare",
  "Epic",
  "Legendary",
  "Mythic",
  "Ultra",
  "Super",
  "Eternal",
  "Unique",
  "Primordial",
  "Exotic",
];
if (rarityNames.length !== rarity.length)
  fail(`rarity names table has ${rarityNames.length} entries but ERarity has ${rarity.length}`);
for (let i = 0; i < rarity.length; i += 1)
  if (rarityNames[i] !== rarity[i].name)
    fail(`rarity names table[${i}] = "${rarityNames[i]}" but ERarity entry is "${rarity[i].name}"`);

function idMap(entries) {
  const map = {};
  for (const entry of entries) map[toSnakeCase(entry.name)] = entry.value;
  return map;
}

const lines = [];
lines.push("// AUTO-GENERATED by tools/generate_shared_ids.js - DO NOT EDIT.");
lines.push("// Sources of truth: src/Shared/rarity.h, petal_type.h, mod_petal_type.h,");
lines.push("// mob_type.h, state_type.h.");
lines.push("// Re-run `node tools/generate_shared_ids.js` after changing those enums.");
lines.push("");

function emitMap(exportName, map) {
  lines.push(`export const ${exportName} = {`);
  for (const key of Object.keys(map))
    lines.push(`  ${key}: ${map[key]},`);
  lines.push("};");
  lines.push("");
}

function emitIndexArray(exportName, values) {
  lines.push(`export const ${exportName} = [`);
  for (const value of values) lines.push(`  ${JSON.stringify(value)},`);
  lines.push("];");
  lines.push("");
}

emitMap("RARITY_IDS", idMap(rarity));
emitIndexArray("RARITY_NAMES", rarityNames);

emitMap("PETAL_IDS", idMap(petal));
// Dense value-indexed views; unclaimed holes (retired ids) emit placeholders.
const petalMaxValue = petal[petal.length - 1].value;
const petalIconIds = [];
const petalDenseNames = [];
for (let value = 0; value <= petalMaxValue; value += 1) {
  const entry = petalByValue.get(value);
  // Index 0 stays the numeric None sentinel used by the client for empty slots.
  petalIconIds.push(value === 0 || !entry ? 0 : toSnakeCase(entry.name));
  petalDenseNames.push(entry ? entry.name : "None");
}
emitIndexArray("PETAL_ICON_IDS", petalIconIds);
emitIndexArray("PETAL_NAMES", petalDenseNames);

emitMap("MOB_IDS", idMap(mob));
emitIndexArray("MOB_NAMES", mobNames);

emitMap("STATE_IDS", idMap(state));

fs.writeFileSync(outPath, lines.join("\n"), "utf8");
console.log(`[generate_shared_ids] wrote ${path.relative(root, outPath)}`);
console.log(
  `  rarity=${rarity.length} petal=${petal.length} (native=${petalNative.length} mod=${petalMod.length}) mob=${mob.length} state=${state.length} entries`,
);

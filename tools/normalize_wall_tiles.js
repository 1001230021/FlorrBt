#!/usr/bin/env node
"use strict";

// Normalizes terrain-style tile layers in a Tiled map so walls/terrain are
// continuous: every tile is re-picked from its own variant set based on the
// corner mask of its neighborhood (the same rule Tiled's terrain tool uses).
//
// The tileset has no Tiled "terrains" metadata, so groups are derived from
// tile image names: <prefix>_<variant>[_0] with variant in
// c/t/b/l/r/tl/tr/bl/br/tli/tri/bli/bri. Missing orientations are covered by
// H/V/D flips (the same transform order the client renderer applies).
//
// Only changed gid numbers are rewritten; the file formatting is preserved.
//
// Usage:
//   node tools/normalize_wall_tiles.js <map.tmj> [options]
// Options:
//   --layer  a,b     only process these tile layers (default: all)
//   --prefix a,b     only process these terrain prefixes (default: all)
//   --dry-run        print stats without writing
//   --no-backup      overwrite without creating <map>.bak

const fs = require("fs");
const path = require("path");

const FLIP_H = 0x80000000;
const FLIP_V = 0x40000000;
const FLIP_D = 0x20000000;
const GID_MASK = 0x1fffffff;

const VARIANT_BITS = Object.freeze({
  c: 15, // 1111
  t: 3, // TL|TR
  b: 12, // BL|BR
  l: 5, // TL|BL
  r: 10, // TR|BR
  tl: 1,
  tr: 2,
  bl: 4,
  br: 8,
  tli: 14, // all but TL
  tri: 13, // all but TR
  bli: 11, // all but BL
  bri: 7, // all but BR
});

const MASK_VARIANT = Object.freeze({
  15: "c",
  3: "t",
  12: "b",
  5: "l",
  10: "r",
  1: "tl",
  2: "tr",
  4: "bl",
  8: "br",
  14: "tli",
  13: "tri",
  11: "bli",
  7: "bri",
});

const VARIANT_NAME_RE = /^(.+)_(tli|tri|bli|bri|tl|tr|bl|br|c|t|b|l|r)$/;

function parseArgs(argv) {
  const args = { layers: null, prefixes: null, dryRun: false, backup: true };
  const rest = [];
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--dry-run") args.dryRun = true;
    else if (arg === "--no-backup") args.backup = false;
    else if (arg === "--layer")
      args.layers = new Set(String(argv[++i]).split(",").map((s) => s.trim()).filter(Boolean));
    else if (arg === "--prefix")
      args.prefixes = new Set(String(argv[++i]).split(",").map((s) => s.trim()).filter(Boolean));
    else rest.push(arg);
  }
  if (rest.length !== 1) {
    console.error(
      "Usage: node tools/normalize_wall_tiles.js <map.tmj> [--layer a,b] [--prefix a,b] [--dry-run] [--no-backup]",
    );
    process.exit(1);
  }
  args.mapPath = rest[0];
  return args;
}

function stripBom(text) {
  return text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
}

function readJson(file) {
  return JSON.parse(stripBom(fs.readFileSync(file, "utf8")));
}

// Mirrors the client renderer's transformTilePoint flip order (D, then H,
// then V) so bit matching produces exactly what the client draws.
function effectiveBits(bits, flipH, flipV, flipD) {
  let out = 0;
  for (let oy = 0; oy <= 1; oy += 1) {
    for (let ox = 0; ox <= 1; ox += 1) {
      let x = ox;
      let y = oy;
      if (flipV) y = 1 - y;
      if (flipH) x = 1 - x;
      if (flipD) {
        const t = x;
        x = y;
        y = t;
      }
      const src = x === 0 && y === 0 ? 0 : x === 1 && y === 0 ? 1 : x === 0 && y === 1 ? 2 : 3;
      if ((bits & (1 << src)) !== 0) out |= 1 << (ox + oy * 2);
    }
  }
  return out;
}

function buildGroups(tmj, mapDir, args) {
  // prefix -> variant -> [{ gid, bits }, ...] (later tilesets may duplicate
  // art under different gids; the map decides which one is real)
  const groups = new Map();
  const tileGroup = new Map(); // canonical gid -> prefix
  const tileBits = new Map(); // canonical gid -> corner bits of the variant
  const ensure = (prefix) => {
    if (!groups.has(prefix)) groups.set(prefix, new Map());
    return groups.get(prefix);
  };

  for (const tilesetRef of tmj.tilesets || []) {
    const firstgid = tilesetRef.firstgid || 1;
    let tiles = [];
    if (tilesetRef.tiles) {
      tiles = tilesetRef.tiles;
    } else if (tilesetRef.source) {
      const tsjPath = path.resolve(mapDir, tilesetRef.source);
      if (!fs.existsSync(tsjPath)) {
        console.warn(`tileset missing: ${tilesetRef.source}`);
        continue;
      }
      tiles = readJson(tsjPath).tiles || [];
    }
    for (const tile of tiles) {
      if (!tile.image) continue;
      const gid = firstgid + (tile.id || 0);
      let name = path.basename(String(tile.image), path.extname(String(tile.image)));
      if (name.endsWith("_0")) name = name.slice(0, -2);
      const match = VARIANT_NAME_RE.exec(name);
      if (!match) continue;
      const variant = match[2];
      const prefix = match[1];
      if (args.prefixes && !args.prefixes.has(prefix)) continue;
      const list = ensure(prefix).get(variant) || [];
      list.push({ gid, bits: VARIANT_BITS[variant] });
      ensure(prefix).set(variant, list);
      if (!tileGroup.has(gid)) tileGroup.set(gid, prefix);
      if (!tileBits.has(gid)) tileBits.set(gid, VARIANT_BITS[variant]);
    }
  }
  return { groups, tileGroup, tileBits };
}

// mask -> { gid, flags }; candidate gids that actually appear in the
// processed layers win, so duplicated art in other tilesets never shadows
// the gid the map really uses.
function buildResolve(groups, usedGids) {
  const resolve = new Map();
  for (const [prefix, variants] of groups) {
    const byMask = new Map();
    for (const [variant, candidates] of variants) {
      const entry = candidates.find((c) => usedGids.has(c.gid)) || candidates[0];
      if (!entry) continue;
      for (const flipH of [false, true])
        for (const flipV of [false, true])
          for (const flipD of [false, true]) {
            const mask = effectiveBits(entry.bits, flipH, flipV, flipD);
            if (!byMask.has(mask))
              byMask.set(mask, {
                gid: entry.gid,
                flags: (flipH ? FLIP_H : 0) | (flipV ? FLIP_V : 0) | (flipD ? FLIP_D : 0),
              });
          }
    }
    resolve.set(prefix, byMask);
  }
  return resolve;
}

function replaceTokens(inner, changes) {
  let result = "";
  let last = 0;
  let tokenIndex = 0;
  for (const m of inner.matchAll(/\d+/g)) {
    const next = changes.get(tokenIndex);
    result += inner.slice(last, m.index) + (next !== undefined ? String(next) : m[0]);
    last = m.index + m[0].length;
    tokenIndex += 1;
  }
  return result + inner.slice(last);
}

function main() {
  const args = parseArgs(process.argv);
  const mapPath = path.resolve(args.mapPath);
  if (!fs.existsSync(mapPath)) {
    console.error(`map not found: ${mapPath}`);
    process.exit(1);
  }
  const mapDir = path.dirname(mapPath);
  const tmj = readJson(mapPath);
  const { groups, tileGroup, tileBits } = buildGroups(tmj, mapDir, args);

  const processedLayers = tmj.layers.filter(
    (layer) =>
      layer.type === "tilelayer" &&
      Array.isArray(layer.data) &&
      (!args.layers || args.layers.has(layer.name)),
  );
  const usedGids = new Set();
  for (const layer of processedLayers)
    for (const raw of layer.data) if (raw) usedGids.add(raw & GID_MASK);
  const resolve = buildResolve(groups, usedGids);

  const stats = new Map(); // prefix -> { changed, kept, noMatch }
  const bump = (prefix, key) => {
    if (!stats.has(prefix)) stats.set(prefix, { changed: 0, kept: 0, noMatch: 0 });
    stats.get(prefix)[key] += 1;
  };

  const changedByLayer = new Map(); // layer index -> Map(tokenIndex -> nextGid)
  let totalChanged = 0;

  tmj.layers.forEach((layer, layerIndex) => {
    if (layer.type !== "tilelayer") return;
    if (args.layers && !args.layers.has(layer.name)) return;
    const data = layer.data;
    if (!Array.isArray(data)) return;
    const width = layer.width || 0;
    const height = layer.height || 0;
    if (!width || !height || data.length < width * height) return;

    const groupAt = (x, y) => {
      if (x < 0 || y < 0 || x >= width || y >= height) return null;
      return tileGroup.get(data[y * width + x] & GID_MASK) || null;
    };
    const isSameGroup = (x, y, prefix) => groupAt(x, y) === prefix;

    const changes = new Map();
    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        const raw = data[y * width + x] || 0;
        const prefix = tileGroup.get(raw & GID_MASK);
        if (!prefix) continue;

        const tl =
          isSameGroup(x - 1, y - 1, prefix) && isSameGroup(x - 1, y, prefix) && isSameGroup(x, y - 1, prefix);
        const tr =
          isSameGroup(x + 1, y - 1, prefix) && isSameGroup(x + 1, y, prefix) && isSameGroup(x, y - 1, prefix);
        const bl =
          isSameGroup(x - 1, y + 1, prefix) && isSameGroup(x - 1, y, prefix) && isSameGroup(x, y + 1, prefix);
        const br =
          isSameGroup(x + 1, y + 1, prefix) && isSameGroup(x + 1, y, prefix) && isSameGroup(x, y + 1, prefix);
        const mask = (tl ? 1 : 0) | (tr ? 2 : 0) | (bl ? 4 : 0) | (br ? 8 : 0);

        if (!MASK_VARIANT[mask]) {
          bump(prefix, "noMatch");
          continue;
        }
        // Tiles that already render the correct neighborhood mask are left
        // alone (even when the map uses a different equivalent gid/flip
        // combination), so the pass only fixes genuinely broken tiles.
        const currentMask = effectiveBits(
          tileBits.get(raw & GID_MASK) || 0,
          (raw & FLIP_H) !== 0,
          (raw & FLIP_V) !== 0,
          (raw & FLIP_D) !== 0,
        );
        if (currentMask === mask) {
          bump(prefix, "kept");
          continue;
        }
        const found = resolve.get(prefix).get(mask);
        if (!found) {
          bump(prefix, "noMatch");
          continue;
        }
        const next = (found.gid | found.flags) >>> 0;
        if (next === (raw >>> 0)) {
          bump(prefix, "kept");
          continue;
        }
        changes.set(y * width + x, next);
        bump(prefix, "changed");
        totalChanged += 1;
      }
    }
    if (changes.size) changedByLayer.set(layerIndex, changes);
  });

  console.log(`map: ${path.basename(mapPath)}`);
  for (const [prefix, s] of [...stats.entries()].sort()) {
    console.log(`  ${prefix}: changed=${s.changed} kept=${s.kept} noMatch=${s.noMatch}`);
  }
  console.log(`total changed tiles: ${totalChanged}`);

  if (args.dryRun) {
    console.log("dry-run: no file written");
    return;
  }

  if (!changedByLayer.size) {
    console.log("nothing to change");
    return;
  }

  // Rewrite only the changed numbers in the original text so the file
  // formatting stays untouched. Blocks are processed back to front so the
  // original match indices stay valid while the text is modified in place.
  const original = stripBom(fs.readFileSync(mapPath, "utf8"));
  const dataBlocks = [...original.matchAll(/"data":\s*\[([\s\S]*?)\]/g)];
  const blocksByLayer = new Map();
  let blockIndex = 0;
  tmj.layers.forEach((layer, layerIndex) => {
    if (layer.type !== "tilelayer" || !Array.isArray(layer.data)) return;
    if (blockIndex < dataBlocks.length) {
      blocksByLayer.set(layerIndex, { block: dataBlocks[blockIndex], expected: layer.width * layer.height });
    }
    blockIndex += 1;
  });

  let text = original;
  for (const [layerIndex, entry] of [...blocksByLayer.entries()].reverse()) {
    const changes = changedByLayer.get(layerIndex);
    if (!changes) continue;
    const tokenCount = (entry.block[1].match(/\d+/g) || []).length;
    if (tokenCount !== entry.expected) {
      console.error(
        `token count mismatch in layer block ${layerIndex}: got ${tokenCount}, expected ${entry.expected}`,
      );
      process.exit(1);
    }
    const replacement = replaceTokens(entry.block[1], changes);
    text =
      text.slice(0, entry.block.index) +
      `"data":[${replacement}]` +
      text.slice(entry.block.index + entry.block[0].length);
  }

  if (args.backup) {
    const backupPath = `${mapPath}.bak`;
    fs.copyFileSync(mapPath, backupPath);
    console.log(`backup: ${path.basename(backupPath)}`);
  }
  fs.writeFileSync(mapPath, text, { encoding: "utf8" });
  console.log(`written: ${path.basename(mapPath)}`);
}

main();

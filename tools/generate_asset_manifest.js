#!/usr/bin/env node
"use strict";
// Generates web/src/asset_manifest.js: every texture asset under web/assets
// (except audio), used by the post-startup background warmer.
// Run: node tools/generate_asset_manifest.js

const fs = require("fs");
const path = require("path");

const assetsDir = path.join(__dirname, "..", "web", "assets");
const outPath = path.join(__dirname, "..", "web", "src", "asset_manifest.js");
const imageExts = new Set([".svg", ".png", ".webp", ".gif", ".jpg", ".jpeg"]);

const entries = [];
function walk(dir) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.isDirectory()) {
      if (entry.name === "audio") continue;
      walk(path.join(dir, entry.name));
    } else if (imageExts.has(path.extname(entry.name).toLowerCase())) {
      const rel = path.relative(path.join(__dirname, "..", "web"), path.join(dir, entry.name));
      entries.push("./" + rel.split(path.sep).join("/"));
    }
  }
}
walk(assetsDir);
entries.sort();

const lines = [
  "// 由 tools/generate_asset_manifest.js 生成，请勿手动修改。",
  "// 包含 web/assets 内的贴图（不含音频），供后台按需预热。",
  "export const ASSET_MANIFEST = [",
  ...entries.map((e) => `  ${JSON.stringify(e)},`),
  "];",
  "",
];
fs.writeFileSync(outPath, lines.join("\n"), "utf8");
console.log(`wrote ${entries.length} assets to web/src/asset_manifest.js`);

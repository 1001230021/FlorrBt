// Writes a data-copy-distance attribute onto every petal SVG root element.
// The client reads this value to lay out multi-copy petal cards: it is the
// distance from the card center to each copy (the ring radius) in 110-unit
// frame coordinates. 0 = default (the client's dynamic layout formula).
// Edit any .svg to tune that petal's spacing, or rerun this tool.
//
// Usage: node tools/write_petal_copy_distance.mjs [--force]

import fs from "node:fs";
import path from "node:path";

const root = path.resolve(process.cwd(), "web/assets/petals");
const force = process.argv.includes("--force");

// Every petal uses the default dynamic layout; add entries here only to pin
// a specific ring distance (e.g. some_petal: 8.5).
const OVERRIDES = {};

let total = 0;
let written = 0;
let skipped = 0;
for (const file of fs.readdirSync(root).filter((f) => f.endsWith(".svg") && !f.endsWith(".bak"))) {
  total++;
  const full = path.join(root, file);
  let svg = fs.readFileSync(full, "utf8");
  if (/data-copy-distance=/.test(svg) && !force) {
    skipped++;
    continue;
  }
  const name = file.replace(/\.svg$/, "");
  const value = OVERRIDES[name] ?? 0;
  const quoted = value.toString();
  if (/data-copy-distance=/.test(svg)) {
    svg = svg.replace(/(data-copy-distance\s*=\s*")([^"]*)(")/, `$1${quoted}$3`);
  } else {
    svg = svg.replace(/<svg\b([^>]*)>/, (m, attrs) => `<svg${attrs} data-copy-distance="${quoted}">`);
  }
  fs.writeFileSync(full, svg, "utf8");
  written++;
  console.log(`set data-copy-distance="${quoted}" ${file}`);
}
console.log(`\n${written} written, ${skipped} skipped (already tagged), ${total} total.`);

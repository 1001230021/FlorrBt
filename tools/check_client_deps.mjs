#!/usr/bin/env node
// Client dependency ratchet. The web client is mid-refactor: game.js is a god
// module tangled in import cycles with a handful of UI/render modules. This
// check freezes the tangle at its current size so it can only shrink:
//   - no NEW import cycles (strongly connected components must stay within
//     the baseline groups),
//   - no NEW modules importing game.js.
// Run:            node tools/check_client_deps.mjs
// After shrinking the tangle on purpose: node tools/check_client_deps.mjs --update
import fs from "node:fs";
import path from "node:path";

const root = path.resolve(import.meta.dirname, "..");
const srcDir = path.join(root, "web", "src");
const baselinePath = path.join(root, "tools", "client_deps_baseline.json");
const update = process.argv.includes("--update");

// ---- import graph -----------------------------------------------------------
const files = fs
  .readdirSync(srcDir)
  .filter((name) => name.endsWith(".js"))
  .sort();
const edges = new Map(); // module -> Set(imported modules)
const namedImports = new Map(); // module -> [{ target, names }]
const exportedNames = new Map(); // module -> Set(exported binding names)
const importPattern = /(?:from\s+|import\s*\(\s*|^\s*import\s+)"\.\/([^"]+)"/gm;
const namedImportPattern = /import\s*\{([^}]*)\}\s*from\s*"\.\/([^"]+)"/g;
const namedExportPattern =
  /^export\s+(?:async\s+)?(?:function|const|let|var|class)\s+([A-Za-z_$][\w$]*)/gm;
const exportListPattern = /^export\s*\{([^}]*)\}/gm;

for (const file of files) {
  const text = fs.readFileSync(path.join(srcDir, file), "utf8");
  const imports = new Set();
  for (const match of text.matchAll(importPattern)) {
    const target = match[1];
    if (target.endsWith(".js")) imports.add(target);
  }
  edges.set(file, imports);

  const named = [];
  for (const match of text.matchAll(namedImportPattern)) {
    const names = match[1]
      .split(",")
      .map((part) => part.trim().split(/\s+as\s+/)[0].trim())
      .filter(Boolean);
    if (match[2].endsWith(".js")) named.push({ target: match[2], names });
  }
  namedImports.set(file, named);

  const exports = new Set();
  for (const match of text.matchAll(namedExportPattern)) exports.add(match[1]);
  for (const match of text.matchAll(exportListPattern)) {
    for (const part of match[1].split(",")) {
      const pieces = part.trim().split(/\s+as\s+/);
      const exported = (pieces[1] || pieces[0]).trim();
      if (exported) exports.add(exported);
    }
  }
  exportedNames.set(file, exports);
}

// ---- strongly connected components (Tarjan) ----------------------------------
let indexCounter = 0;
const indices = new Map();
const lowLinks = new Map();
const onStack = new Set();
const stack = [];
const tangles = []; // SCCs with more than one member

function strongConnect(node) {
  indices.set(node, indexCounter);
  lowLinks.set(node, indexCounter);
  indexCounter += 1;
  stack.push(node);
  onStack.add(node);

  for (const target of edges.get(node) || []) {
    if (!edges.has(target)) continue;
    if (!indices.has(target)) {
      strongConnect(target);
      lowLinks.set(node, Math.min(lowLinks.get(node), lowLinks.get(target)));
    } else if (onStack.has(target)) {
      lowLinks.set(node, Math.min(lowLinks.get(node), indices.get(target)));
    }
  }

  if (lowLinks.get(node) === indices.get(node)) {
    const component = [];
    let member;
    do {
      member = stack.pop();
      onStack.delete(member);
      component.push(member);
    } while (member !== node);
    if (component.length > 1) tangles.push(component.sort());
  }
}

for (const file of files) {
  if (!indices.has(file)) strongConnect(file);
}
tangles.sort((a, b) => a[0].localeCompare(b[0]));

const gameImporters = files
  .filter((file) => file !== "game.js" && (edges.get(file) || new Set()).has("game.js"))
  .sort();

const current = { tangles, gameImporters };

// ---- ratchet ------------------------------------------------------------------
if (update || !fs.existsSync(baselinePath)) {
  fs.writeFileSync(baselinePath, `${JSON.stringify(current, null, 2)}\n`);
  console.log(`[client-deps] baseline written: ${path.relative(root, baselinePath)}`);
}

const baseline = JSON.parse(fs.readFileSync(baselinePath, "utf8"));
const failures = [];

for (const tangle of current.tangles) {
  const covered = (baseline.tangles || []).some((allowed) =>
    tangle.every((member) => allowed.includes(member)),
  );
  if (!covered)
    failures.push(
      `new/grown import cycle: ${tangle.join(" <-> ")}\n  (baseline allows: ${
        (baseline.tangles || []).map((t) => t.join(",")).join(" | ") || "none"
      })`,
    );
}

for (const importer of current.gameImporters) {
  if (!(baseline.gameImporters || []).includes(importer))
    failures.push(`new module importing game.js: ${importer}`);
}

// Named imports must resolve: importing a name a module does not export is a
// load-time error the browser only reports at runtime.
for (const [file, imports] of namedImports) {
  for (const { target, names } of imports) {
    const targetExports = exportedNames.get(target);
    if (!targetExports) continue; // target outside web/src
    for (const name of names) {
      if (!targetExports.has(name))
        failures.push(`${file} imports "${name}" but ${target} does not export it`);
    }
  }
}

console.log(
  `[client-deps] ${files.length} modules, tangle groups: ${
    current.tangles.map((t) => `{${t.join(", ")}}`).join(" ") || "none"
  }`,
);
console.log(`[client-deps] game.js importers: ${current.gameImporters.join(", ") || "none"}`);

if (failures.length > 0) {
  console.error("\n[client-deps] FAIL — the dependency tangle grew:");
  for (const failure of failures) console.error(`  - ${failure}`);
  console.error(
    "\nBreak the cycle (inject callbacks or move shared code down a layer) instead of importing game.js.\n" +
      "If this shrinkage/regrouping is intentional, refresh with: node tools/check_client_deps.mjs --update",
  );
  process.exit(1);
}
console.log("[client-deps] OK — no new cycles, no new game.js importers");

#!/usr/bin/env node
import fs from "node:fs";
import path from "node:path";

const root = process.cwd();
const roots = [path.join(root, "src"), path.join(root, "web", "src")];
const extensions = new Set([".h", ".hpp", ".cpp", ".js"]);
const objectPrefixes = [
  "Flower",
  "Petal",
  "Inventory",
  "Craft",
  "Talent",
  "Mob",
  "Entity",
  "Player",
  "Owner",
  "State",
  "Map",
  "Panel",
  "Console",
  "Slot",
];
const behaviorPrefixes = [
  "Get", "Set", "Add", "Apply", "Update", "Refresh", "Rebuild", "Create",
  "Build", "Draw", "Render", "Handle", "Try", "Find", "Is", "Has", "Can",
  "Clear", "Reset", "Load", "Save", "Parse", "Normalize", "Format", "Execute",
  "Dispatch", "Process", "Resolve", "Record", "Note", "Schedule", "Open", "Close",
  "Show", "Hide", "Remove", "Destroy", "Spawn", "Capture", "Restore", "Register",
  "Unregister", "Queue", "Sync", "Move", "Equip", "Unequip", "Craft", "Obtain",
  "Take", "Give", "Use", "Consume", "Commit", "Prepare", "Send", "Receive", "Write",
  "Read", "Pack", "Unpack", "Serialize", "Deserialize", "Push", "Pop", "Tick", "On",
  "Count", "Calculate", "Share", "Join", "Report", "Broadcast", "Remember", "Collect",
];

// These are intentionally noun-shaped APIs, but they are not object-first
// behavior methods.  They are accessors, immutable talent/stat table factories,
// protocol field readers, or type adapters.  Keeping them in the audit output
// made the report look actionable while hiding the real candidates.
const intentionalNounNames = new Set([
  "EntityStats",
  "Slot",
  "FlowerHealthTalents",
  "PetalHealthTalents",
  "PetalReloadTalents",
  "PetalRotationTalents",
  "SlotNumTalents",
  "PetalSplitTalents",
  "PetalNetworkType",
  "OwnerSlots",
  "States",
]);

function filesUnder(directory) {
  const out = [];
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const full = path.join(directory, entry.name);
    if (entry.isDirectory()) out.push(...filesUnder(full));
    else if (extensions.has(path.extname(entry.name))) out.push(full);
  }
  return out;
}

function functionNames(text) {
  const names = [];
  const regex = /\b(?:function\s+|(?:[A-Za-z_][\w:<>*&\s]+\s+))([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?\{/g;
  for (const match of text.matchAll(regex)) names.push(match[1]);
  return names;
}

const findings = [];
for (const directory of roots) {
  if (!fs.existsSync(directory)) continue;
  for (const file of filesUnder(directory)) {
    const lines = fs.readFileSync(file, "utf8").split(/\r?\n/);
    const source = lines.join("\n");
    for (const name of functionNames(source)) {
      if (!objectPrefixes.some((prefix) => name.startsWith(prefix))) continue;
      if (intentionalNounNames.has(name)) continue;
      if (behaviorPrefixes.some((prefix) => name.startsWith(prefix))) continue;
      const line = lines.findIndex((value) => value.includes(name)) + 1;
      findings.push(`${path.relative(root, file)}:${line}: ${name}`);
    }
  }
}

console.log(`[function-names] ${findings.length} object-first candidates`);
for (const finding of findings.slice(0, 200)) console.log(finding);
if (findings.length > 200) console.log(`[function-names] ... ${findings.length - 200} more`);
if (process.argv.includes("--strict") && findings.length > 0) process.exitCode = 1;

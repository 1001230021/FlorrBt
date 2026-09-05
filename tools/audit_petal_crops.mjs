#!/usr/bin/env node
// Audits the in-world (live) petal sprite crops against the actual SVG art.
//
// The client renders live petals by stripping the baked-in name label and, for
// a few petals, cropping to a hardcoded viewBox / keeping only the first N
// paths (see livePetalIconImage in game.js). Art revamps silently break those
// assumptions, so this tool measures the real art bounds with getBBox() in a
// headless browser and reports:
//   - default-path petals whose label-stripped art is badly off-center in the
//     110x110 frame (the live sprite draws the full frame -> off-center art
//     renders skewed against the entity position),
//   - special-cased petals whose configured crop viewBox no longer matches
//     the retained content.
// Run: node tools/audit_petal_crops.mjs
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

const root = path.resolve(import.meta.dirname, "..");
const petalsDir = path.join(root, "web", "assets", "petals");
const petalSvgSource = fs
  .readFileSync(path.join(root, "web", "src", "petal_svg.js"), "utf8")
  .replace(/^export /gm, "");
const cdpPort = 9448;
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Special live-petal crops (wax/ant_egg/dandelion/trapper/mushroom) are now
// measured at runtime from the art bounds (measuredLiveViewBox in game.js),
// so they can no longer drift. This audit reports the default-path petals:
// art that is badly off-center in the 110x110 frame, empty after label
// stripping, or wildly out of frame indicates a broken art file.
const specialCrops = {};

const edgePaths = [
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
  "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
];
const edgePath = edgePaths.find((candidate) => fs.existsSync(candidate));
if (!edgePath) throw new Error("msedge not found");
const edge = spawn(edgePath, [
  `--remote-debugging-port=${cdpPort}`,
  `--user-data-dir=${path.join(root, "logs", "edge_audit_data")}`,
  "--headless=new", "--disable-gpu", "--no-first-run", "--mute-audio",
  "about:blank",
]);

let cdp = null;
let cdpId = 0;
const pending = new Map();
for (let i = 0; i < 50 && !cdp; i += 1) {
  await sleep(300);
  try {
    const targets = await (await fetch(`http://127.0.0.1:${cdpPort}/json`)).json();
    const page = targets.find((target) => target.type === "page");
    if (!page) continue;
    cdp = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => { cdp.onopen = resolve; cdp.onerror = reject; });
    cdp.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      if (msg.id && pending.has(msg.id)) { const { resolve, reject } = pending.get(msg.id); pending.delete(msg.id); msg.error ? reject(new Error(msg.error.message)) : resolve(msg.result); }
    };
  } catch { cdp = null; }
}
if (!cdp) throw new Error("CDP attach failed");
const send = (method, params = {}) => new Promise((resolve, reject) => {
  const id = ++cdpId; pending.set(id, { resolve, reject });
  cdp.send(JSON.stringify({ id, method, params }));
});
const evaluate = async (expression) => {
  const { result, exceptionDetails } = await send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
  if (exceptionDetails) throw new Error(`page eval failed: ${exceptionDetails.text} ${exceptionDetails.exception?.description || ""}`);
  return result.value;
};

// Install the client's real transforms + a bbox helper in the page.
await evaluate(`(() => { ${petalSvgSource}
  window.__stripLivePetalLabel = stripLivePetalLabel;
  window.__measure = (svgText) => {
    const host = document.createElement("div");
    host.style.position = "absolute";
    host.style.visibility = "hidden";
    host.innerHTML = svgText;
    const svg = host.querySelector("svg");
    if (!svg) return null;
    document.body.appendChild(host);
    try {
      const box = svg.getBBox();
      return { x: box.x, y: box.y, w: box.width, h: box.height };
    } finally {
      host.remove();
    }
  };
  window.__firstPaths = (svgText, count, viewBox) => {
    const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
    const paths = Array.from(doc.querySelectorAll("path")).slice(0, count);
    const body = paths.map((node) => new XMLSerializer().serializeToString(node)).join("");
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="' + viewBox + '">' + body + "</svg>";
  };
  return true;
})()`);

const files = fs
  .readdirSync(petalsDir)
  .filter((name) => name.endsWith(".svg"))
  .sort();
const rows = [];
for (const file of files) {
  const id = file.replace(/\.svg$/, "");
  const text = fs.readFileSync(path.join(petalsDir, file), "utf8");
  const special = specialCrops[id];
  const report = await evaluate(`(() => {
    const raw = ${JSON.stringify(text)};
    const special = ${JSON.stringify(special || null)};
    const stripped = window.__stripLivePetalLabel(raw);
    const art = window.__measure(stripped);
    let retained = null;
    if (special && special.mode === "firstPaths")
      retained = window.__measure(window.__firstPaths(raw, special.count, special.viewBox));
    return { art, retained };
  })()`);
  if (!report?.art) continue;

  const art = report.art;
  const artCx = art.x + art.w / 2;
  const artCy = art.y + art.h / 2;
  if (special) {
    const [vx, vy, vw, vh] = special.viewBox.split(" ").map(Number);
    const box = special.mode === "firstPaths" ? report.retained : art;
    if (!box) continue;
    const cx = box.x + box.w / 2;
    const cy = box.y + box.h / 2;
    const offset = Math.hypot(cx - (vx + vw / 2), cy - (vy + vh / 2));
    const overflow =
      Math.max(0, vx - box.x, vy - box.y, box.x + box.w - (vx + vw), box.y + box.h - (vy + vh));
    rows.push({
      id, kind: `special(${special.mode})`, offset, overflow,
      art: box, viewBox: special.viewBox,
    });
  } else {
    // Default path: full 110x110 frame, art should sit centered on (55,55).
    const offset = Math.hypot(artCx - 55, artCy - 55);
    rows.push({ id, kind: "default", offset, overflow: 0, art, viewBox: "0 0 110 110" });
  }
}

rows.sort((a, b) => b.offset - a.offset);
const fmt = (box) => `${box.x.toFixed(1)},${box.y.toFixed(1)} ${box.w.toFixed(1)}x${box.h.toFixed(1)}`;
console.log("id | kind | centerOffset | cropOverflow | contentBox | usedViewBox");
for (const row of rows) {
  const flag = row.offset > 3 || row.overflow > 0.5 ? "  <-- CHECK" : "";
  console.log(
    `${row.id} | ${row.kind} | ${row.offset.toFixed(2)} | ${row.overflow.toFixed(2)} | ${fmt(row.art)} | ${row.viewBox}${flag}`,
  );
}

edge.kill();
process.exit(0);

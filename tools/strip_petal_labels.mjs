// Strips the bottom petal label (the double-layer black/white Ubuntu text)
// from the florr petal SVG assets, then reframes every remaining path into
// its minimal bounding box (the box becomes the new viewBox, keeping the
// artwork pixel-identical but centered). Each file is backed up as
// <name>.svg.bak before being modified.
//
// Precise geometry comes from the browser's SVG getBBox() (headless Edge),
// so the code does not re-implement cubic/quadratic path math.
//
// Usage:
//   node tools/strip_petal_labels.mjs [--only <name>] [--dry-run]
//
// --only requires an exact file name (e.g. basic). Without --only the whole
// web/assets/petals directory is processed.

import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";

const root = path.resolve(process.cwd(), "web/assets/petals");
const args = process.argv.slice(2);
let only = "";
let dryRun = false;
for (let i = 0; i < args.length; ++i) {
  if (args[i] === "--only" && args[i + 1]) only = args[++i].toLowerCase();
  if (args[i] === "--dry-run") dryRun = true;
}

// ---- headless edge (CDP) -----------------------------------------------------
const EDGE = "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";
let cdp = null;
let nextId = 1;
let ws = null;
const pending = new Map();
let pageWs = null;
let pagePending = new Map();
let pageNextId = 1;

function pageCall(method, params) {
  return new Promise((resolve, reject) => {
    const id = pageNextId++;
    pagePending.set(id, { resolve, reject });
    pageWs.send(JSON.stringify({ id, method, params }));
  });
}

async function startCdp() {
  const port = 9524;
  const proc = spawn(EDGE, [
    "--headless=new", "--disable-gpu", `--remote-debugging-port=${port}`,
    "--user-data-dir=" + path.join(process.env.TEMP || "/tmp", "petal-strip-profile"),
    "--window-size=128,128", "about:blank",
  ], { stdio: "ignore", detached: true });
  let ok = false;
  let lastError = "";
  for (let attempt = 0; attempt < 60 && !ok; ++attempt) {
    await new Promise((r) => setTimeout(r, 300));
    try {
      const res = await fetch(`http://127.0.0.1:${port}/json/list`);
      const targets = await res.json();
      const page = targets.find((t) => t.type === "page");
      if (page) {
        const targetWs = page.webSocketDebuggerUrl;
        const target = new WebSocket(targetWs);
        target.addEventListener("message", (event) => {
          const msg = JSON.parse(event.data);
          if (pagePending.has(msg.id)) {
            const p = pagePending.get(msg.id);
            pagePending.delete(msg.id);
            msg.error ? p.reject(new Error(msg.error.message)) : p.resolve(msg.result);
          }
        });
        await new Promise((resolve, reject) => {
          target.addEventListener("open", resolve);
          target.addEventListener("error", reject);
        });
        ws = target;
        pageWs = target;
        await pageCall("Runtime.enable");
        ok = true;
      }
    } catch (e) {
      lastError = String(e && e.message || e);
    }
  }
  if (!ok) console.error("startCdp last error:", lastError);
  return ok;
}

async function endCdp() {
  try { await pageCall("Page.close"); } catch {}
  try { ws?.close(); } catch {}
  try { spawn("taskkill", ["/F", "/IM", "msedge.exe"], { stdio: "ignore", detached: true }); } catch {}
}

// Reuse one page per call, navigate to a data URL of the svg bytes.
async function svgPathBoxes(svgText) {
  const b64 = Buffer.from(svgText, "utf8").toString("base64");
  const url = `data:image/svg+xml;base64,${b64}`;
  await pageCall("Page.navigate", { url });
  await new Promise((r) => setTimeout(r, 160));
  const expr = `(async () => {
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const svg = document.querySelector("svg");
    if (!svg) return { err: "no svg" };
    const vb = svg.viewBox.baseVal;
    const size = Math.max(vb.width || 0, vb.height || 0);
    const paths = [...svg.querySelectorAll("path")].map(p => {
      const b = p.getBBox();
      return { d: p.getAttribute("d") || "", x: b.x, y: b.y, w: b.width, h: b.height };
    });
    return { size, paths };
  })()`;
  const res = await pageCall("Runtime.evaluate", { expression: expr, returnByValue: true, awaitPromise: true });
  const val = res.result?.value;
  if (!val || val.err || !Array.isArray(val.paths)) return null;
  return val;
}

function isLabel(box, size) {
  const minY = box.y;
  const h = box.h;
  const w = box.w;
  // The label band: sits in the lower half, is a short but wide glyph strip.
  return minY > size * 0.53 && h < size * 0.36 && w > size * 0.28;
}

async function processFile(file) {
  const svg = fs.readFileSync(file, "utf8");
  const vb = svg.match(/viewBox\s*=\s*"([^"]+)"/);
  if (!vb) { console.log(`SKIP (no viewBox): ${path.basename(file)}`); return { skipped: true }; }
  const bak = file + ".bak";

  // When the file was already processed once (a backup exists), re-run from
  // the backup so the latest behavior re-applies.
  const source = fs.existsSync(bak) ? fs.readFileSync(bak, "utf8") : svg;
  const reframeOnly = fs.existsSync(bak);

  const data = await svgPathBoxes(source);
  if (!data) { console.log(`SKIP (render failed): ${path.basename(file)}`); return { skipped: true }; }

  const keep = [];
  const drop = [];
  for (const p of data.paths) {
    const box = { y: p.y, h: p.h, w: p.w };
    if (isLabel(box, data.size)) drop.push(p);
    else keep.push(p);
  }
  if (!keep.length) {
    console.log(`SKIP (no artwork paths): ${path.basename(file)}`);
    return { skipped: true };
  }

  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const p of keep) {
    minX = Math.min(minX, p.x);
    minY = Math.min(minY, p.y);
    maxX = Math.max(maxX, p.x + p.w);
    maxY = Math.max(maxY, p.y + p.h);
  }
  const pad = 1.4; // half the 2.4 stroke + a hair, strokes stay inside
  minX -= pad;
  minY -= pad;
  maxX += pad;
  maxY += pad;

  // Keep the original 110x110 frame size; move the artwork so its bounding
  // box is centered in the frame (artwork size never changes).
  const centerX = (minX + maxX) * 0.5;
  const centerY = (minY + maxY) * 0.5;
  const cx = data.size * 0.5;
  const dx = Math.round((cx - centerX) * 10) / 10;
  const dy = Math.round((cx - centerY) * 10) / 10;

  // Guard: artwork living far outside the frame would be lost; restore + skip.
  if (Math.abs(dx) > 40 || Math.abs(dy) > 40) {
    if (fs.existsSync(bak)) fs.copyFileSync(bak, file);
    console.log(`SKIP (artwork spans far outside the frame): ${path.basename(file)} (translate ${dx} ${dy})`);
    return { skipped: true };
  }

  if (!dryRun) {
    if (!fs.existsSync(bak)) fs.copyFileSync(file, bak);
    const keepTags = source.match(/<path\b[^>]*\/>/g).filter((tagEvent) => {
      const d = (tagEvent.match(/ d="([^"]*)"/) || [])[1] || "";
      return keep.some((p) => p.d === d);
    }).join("");
    const outViewBox = `0 0 ${data.size} ${data.size}`;
    const outSvg =
      `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${outViewBox}">` +
      `<g transform="translate(${dx} ${dy})">${keepTags}</g></svg>`;
    fs.writeFileSync(file, outSvg, "utf8");
  }
  console.log(`${dryRun ? "[dry] " : ""}${path.basename(file)}: ${keep.length}/${data.paths.length} paths kept, ` +
    `bbox centered (translate ${dx} ${dy}), size kept`);
  return { skipped: false };
}

const ok = await startCdp();
if (!ok) { console.error("Cannot start headless Edge for getBBox(); aborting."); process.exit(1); }

const files = fs.readdirSync(root).filter((f) => f.endsWith(".svg") && !f.endsWith(".bak"));
let total = 0, done = 0;
for (const f of files) {
  if (only && f.toLowerCase() !== only + ".svg") continue;
  total++;
  const res = await processFile(path.join(root, f));
  if (res && !res.skipped) done++;
}
await endCdp();
console.log(`\n${done}/${total} files processed.`);

import { requestRedraw } from "./render_invalidate.js";

// A resource is ready only after fetch, SVG transformation AND image decode.
// Originals and derived bodies share source text, not a second HTTP request.
const sourceTexts = new Map();
const images = new Map();
const timeoutMs = 20000;

function sourceKey(src) {
  return new URL(src, globalThis.document?.baseURI || "http://assets.local/").href;
}

export function readSvgSource(src) {
  const key = sourceKey(src);
  if (!sourceTexts.has(key)) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    const job = fetch(src, { signal: controller.signal }).then(response => {
      if (!response.ok) throw new Error(`SVG ${src}: HTTP ${response.status}`);
      return response.text();
    }).finally(() => clearTimeout(timer));
    sourceTexts.set(key, job);
    // Permit an explicit subsequent preparation to retry a failed request.
    job.catch(() => { if (sourceTexts.get(key) === job) sourceTexts.delete(key); });
  }
  return sourceTexts.get(key);
}

// Resolve the first move point and final endpoint without mounting the SVG in
// the document. This accepts compact paths such as "M55 32.5q0-2.5-6-19",
// which are common in exported creature artwork.
export function parseSvgPathEndpoints(pathData, fallback = { x: 0, y: 0 }) {
  const tokens = String(pathData || "").match(/[-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?|[a-z]/gi) || [];
  const parameterCounts = { M: 2, L: 2, H: 1, V: 1, C: 6, S: 4, Q: 4, T: 2, A: 7 };
  let index = 0, command = "", x = 0, y = 0;
  let start = null;
  const isCommand = token => /^[a-z]$/i.test(token);

  while (index < tokens.length) {
    if (isCommand(tokens[index])) command = tokens[index++];
    if (!command) break;

    const upper = command.toUpperCase();
    if (upper === "Z") {
      if (start) ({ x, y } = start);
      command = "";
      continue;
    }

    const count = parameterCounts[upper];
    if (!count) {
      command = "";
      continue;
    }
    if (index + count > tokens.length || tokens.slice(index, index + count).some(isCommand)) {
      command = "";
      continue;
    }

    const values = tokens.slice(index, index + count).map(Number);
    index += count;
    const relative = command === command.toLowerCase();
    const point = (px, py) => ({ x: relative ? x + px : px, y: relative ? y + py : py });

    if (upper === "M" || upper === "L" || upper === "T") ({ x, y } = point(values[0], values[1]));
    else if (upper === "H") x = relative ? x + values[0] : values[0];
    else if (upper === "V") y = relative ? y + values[0] : values[0];
    else if (upper === "C") ({ x, y } = point(values[4], values[5]));
    else if (upper === "S" || upper === "Q") ({ x, y } = point(values[2], values[3]));
    else if (upper === "A") ({ x, y } = point(values[5], values[6]));

    if (!start) start = { x, y };
    // Additional coordinate pairs after a move are implicit line commands.
    if (upper === "M") command = relative ? "l" : "L";
  }

  const safeFallback = {
    x: Number.isFinite(fallback?.x) ? fallback.x : 0,
    y: Number.isFinite(fallback?.y) ? fallback.y : 0,
  };
  const end = Number.isFinite(x) && Number.isFinite(y) && start ? { x, y } : { ...safeFallback };
  return { start: start || { ...safeFallback }, end };
}

function createVisualImage(resolveSource) {
  const image = new Image();
  image.decoding = "async";
  image.visualState = "loading";
  image.visualReady = new Promise(resolve => {
    let settled = false, blobUrl = "";
    const finish = ok => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      image.onload = image.onerror = null;
      image.failed = !ok;
      image.visualState = ok ? "ready" : "failed";
      // DOM cards and the codex reuse this URL in additional <img> nodes.
      // Keep successful, finite manifest/variant URLs for the page lifetime.
      if (blobUrl && !ok) URL.revokeObjectURL(blobUrl);
      resolve(ok);
      requestRedraw();
    };
    const timer = setTimeout(() => finish(false), timeoutMs);
    image.onload = async () => {
      try { await image.decode?.(); } catch { /* Loaded SVGs may reject decode on older engines. */ }
      finish(image.naturalWidth > 0);
    };
    image.onerror = () => finish(false);
    Promise.resolve().then(resolveSource).then(source => {
      if (settled) return;
      if (typeof source === "string") image.src = source;
      else {
        blobUrl = URL.createObjectURL(source);
        image.src = blobUrl;
      }
    }).catch(() => finish(false));
  });
  return image;
}

export function prepareSvgImage(src, variant, transform = text => text, { retry = false } = {}) {
  if (!src) return null;
  const key = `${variant}:${sourceKey(src)}`;
  if (retry && images.get(key)?.failed) images.delete(key);
  if (!images.has(key)) images.set(key, createVisualImage(async () =>
    new Blob([await transform(await readSvgSource(src))], { type: "image/svg+xml" })));
  return images.get(key);
}

export function loadVisualImage(src, options = {}) {
  if (!src) return null;
  if (/\.svg(?:[?#]|$)/i.test(src)) return prepareSvgImage(src, "original", text => text, options);
  const key = `image:${sourceKey(src)}`;
  if (options.retry && images.get(key)?.failed) images.delete(key);
  if (!images.has(key)) images.set(key, createVisualImage(() => src));
  return images.get(key);
}

export function whenVisualReady(image) {
  if (!image || image.failed) return Promise.resolve(false);
  if (image.visualReady) return image.visualReady;
  if (image.complete && image.naturalWidth > 0) return Promise.resolve(true);
  return new Promise(resolve => {
    const finish = ok => {
      clearTimeout(timer);
      image.removeEventListener("load", onload);
      image.removeEventListener("error", onerror);
      resolve(ok);
    };
    const onload = () => finish(true), onerror = () => finish(false);
    const timer = setTimeout(onerror, timeoutMs);
    image.addEventListener("load", onload, { once: true });
    image.addEventListener("error", onerror, { once: true });
  });
}

export function assignVisualImage(target, resource) {
  target.visualResource = resource;
  const assign = ok => {
    if (target.visualResource !== resource) return;
    if (ok) target.src = resource.src;
    else target.remove?.();
  };
  if (resource?.visualState === "ready") assign(true);
  else whenVisualReady(resource).then(assign);
}

export function getPreparedVisualImages() {
  return [...images.values()].filter(image => image.visualState === "ready");
}

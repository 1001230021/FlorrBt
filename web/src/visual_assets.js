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

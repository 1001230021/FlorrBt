// Petal SVG text-label surgery. Petal icon SVGs carry their name baked in as
// vector label paths (an outline/fill pair) plus optional <text> nodes; the
// card pipeline strips or extracts those labels to compose icons freely.
// Pure string -> string transforms, safe to import from any layer.

export function stripLivePetalLabel(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  root.querySelectorAll("text").forEach((node) => node.remove());

  const paths = Array.from(root.querySelectorAll("path"));
  const remove = new Set();
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      remove.add(outline);
      remove.add(fill);
      i += 1;
    }
  }
  remove.forEach((node) => node.remove());

  return new XMLSerializer().serializeToString(root);
}

export function extractBandageOverlaySvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  root.querySelectorAll("text").forEach((node) => node.remove());
  root.querySelectorAll("path[fill-opacity]").forEach((node) => node.remove());

  const paths = Array.from(root.querySelectorAll("path"));
  const remove = new Set();
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      remove.add(outline);
      remove.add(fill);
      i += 1;
    }
  }
  remove.forEach((node) => node.remove());

  return new XMLSerializer().serializeToString(root);
}

export function extractPetalLabelSvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 110 110"></svg>`;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg")
    return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 110 110"></svg>`;

  const serializer = new XMLSerializer();
  const labelNodes = [];
  root.querySelectorAll("text").forEach((node) => labelNodes.push(node));

  const paths = Array.from(root.querySelectorAll("path"));
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      labelNodes.push(outline, fill);
      i += 1;
    }
  }

  const viewBox = root.getAttribute("viewBox") || "0 0 110 110";
  const body = labelNodes
    .map((node) => serializer.serializeToString(node))
    .join("");
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}">${body}</svg>`;
}

export function extractMimicLabelSvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  const paths = Array.from(root.querySelectorAll("path"));
  const labelPaths = [];
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      labelPaths.push(outline, fill);
      i += 1;
    }
  }

  const serializer = new XMLSerializer();
  const body = labelPaths
    .map((node) => serializer.serializeToString(node))
    .join("");
  if (!body) return svgText;
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="24 68 64 22">${body}</svg>`;
}

// Measures the rendered content bounds of an SVG markup string (getBBox needs
// the element attached, so it briefly joins the DOM hidden). Returns null when
// there is nothing measurable — callers fall back to the full frame.
export function measureSvgContentBox(svgMarkup) {
  if (typeof document === "undefined" || !document.body) return null;
  const host = document.createElement("div");
  host.style.position = "absolute";
  host.style.visibility = "hidden";
  host.style.pointerEvents = "none";
  host.innerHTML = svgMarkup;
  const svg = host.querySelector("svg");
  if (!svg) return null;
  document.body.appendChild(host);
  try {
    const box = svg.getBBox();
    if (!(box.width > 0) || !(box.height > 0)) return null;
    return { x: box.x, y: box.y, w: box.width, h: box.height };
  } catch {
    return null;
  } finally {
    host.remove();
  }
}

export function getCenteredSvgViewBox(box, paddingRatio = 0.04) {
  if (!box || ![box.x, box.y, box.w, box.h].every(Number.isFinite) || box.w <= 0 || box.h <= 0)
    return null;
  const padding = Number.isFinite(paddingRatio) ? Math.max(0, paddingRatio) : 0.04;
  const side = Math.max(box.w, box.h) * (1 + padding * 2);
  const cx = box.x + box.w / 2, cy = box.y + box.h / 2;
  return `${(cx - side / 2).toFixed(3)} ${(cy - side / 2).toFixed(3)} ${side.toFixed(3)} ${side.toFixed(3)}`;
}

// Only run on label-stripped equipment art, once when the cached image loads.
// Card padding and baked-in group translations must not become body offsets.
export function centerPetalArtSvg(svgMarkup) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return svgMarkup;
  const doc = new DOMParser().parseFromString(svgMarkup, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgMarkup;
  const viewBox = getCenteredSvgViewBox(measureSvgContentBox(svgMarkup));
  if (!viewBox) return svgMarkup;
  root.setAttribute("viewBox", viewBox);
  root.setAttribute("preserveAspectRatio", "xMidYMid meet");
  return new XMLSerializer().serializeToString(root);
}

function isLabelOutlinePath(path) {
  const stroke = normalizeColorAttr(path.getAttribute("stroke"));
  const fill = normalizeColorAttr(path.getAttribute("fill"));
  const strokeWidth = Number.parseFloat(
    path.getAttribute("stroke-width") || "0",
  );
  return (
    (stroke === "#000" || stroke === "#000000") &&
    (!fill || fill === "none") &&
    strokeWidth >= 1.5
  );
}

function isMatchingLabelFillPath(outline, fill) {
  if (!fill) return false;
  const color = normalizeColorAttr(fill.getAttribute("fill"));
  return (
    (color === "#fff" || color === "#ffffff") &&
    outline.getAttribute("d") === fill.getAttribute("d")
  );
}

function normalizeColorAttr(value) {
  return String(value || "")
    .trim()
    .toLowerCase();
}

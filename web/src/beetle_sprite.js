const BEETLE_VIEWBOX_SIZE = 110;
const BEETLE_EFFECTIVE_BOX = 91.667;
const BEETLE_BASE_FACE_ANGLE = -Math.PI * 0.75;
const BEETLE_SPRITE_SCALE = 2.4;
const BEETLE_MOTION_DEADZONE = 0.12;
const BEETLE_LIMB_FILL = "#333";
const BEETLE_LIMB_STROKE = "#333";
const BEETLE_LIMB_STROKE_WIDTH = 4.667;
const BEETLE_BASE_CACHE = new Map();
const SUMMONED_BODY_FILL = "#f4dc59";
const SUMMONED_BODY_DARK_FILL = "#d7bd3c";
const SUMMONED_BODY_STROKE = "#c8ad35";

const forelimbParts = [
  {
    pivot: { x: 53.25, y: 45.65 },
    direction: -1,
    path: "M57.357 43.215q-4.714-18.856-21.213-21.213 11.785 7.07 16.499 25.927z",
  },
  {
    pivot: { x: 45.65, y: 53.25 },
    direction: 1,
    path: "M43.215 57.357q-18.856-4.714-21.213-21.213 7.07 11.785 25.927 16.499z",
  },
];

let forelimbPaths = null;

export function drawBeetle(ctx, src, pos, radius, entityId, angle, motion, time, options = {}) {
  const summoned = Boolean(options.summoned);
  const spriteSize = Math.max(1, radius * 2 * (BEETLE_VIEWBOX_SIZE / BEETLE_EFFECTIVE_BOX) * BEETLE_SPRITE_SCALE);
  const spriteHalf = spriteSize * 0.5;
  const base = beetleBaseImage(src, summoned);
  const animation = beetleAnimation(entityId, motion || 0, time || 0);

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle)) ctx.rotate(angle - BEETLE_BASE_FACE_ANGLE);

  drawForelimbs(ctx, spriteSize, animation);
  if (isImageReady(base)) {
    ctx.drawImage(base, -spriteHalf, -spriteHalf, spriteSize, spriteSize);
  } else {
    drawFallback(ctx, radius * BEETLE_SPRITE_SCALE, summoned);
  }

  ctx.restore();
}

function beetleAnimation(entityId, motion, time) {
  const activeMotion = clamp01(motion) < BEETLE_MOTION_DEADZONE ? 0 : clamp01(motion);
  if (activeMotion <= 0) return { swing: 0 };
  const phase = time * (7.2 + activeMotion * 4.8) + entityId * 0.81;
  return {
    swing: Math.sin(phase) * activeMotion * 0.24,
  };
}

function drawForelimbs(ctx, spriteSize, animation) {
  const paths = getForelimbPaths();
  if (!paths) return;
  const scale = spriteSize / BEETLE_VIEWBOX_SIZE;

  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-BEETLE_VIEWBOX_SIZE * 0.5, -BEETLE_VIEWBOX_SIZE * 0.5);
  clipSvgBounds(ctx);
  ctx.fillStyle = BEETLE_LIMB_FILL;
  ctx.strokeStyle = BEETLE_LIMB_STROKE;
  ctx.lineWidth = BEETLE_LIMB_STROKE_WIDTH;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";

  for (const part of paths) {
    const amount = animation.swing * part.direction;
    drawPathAroundPivot(ctx, part.path, part.pivot, amount, () => {
      ctx.fill(part.path);
      ctx.stroke(part.path);
    });
  }
  ctx.restore();
}

function beetleBaseImage(src, summoned) {
  const cacheKey = `${src}|${summoned ? "summoned" : "normal"}`;
  let entry = BEETLE_BASE_CACHE.get(cacheKey);
  if (entry) return entry.image;

  const image = new Image();
  image.decoding = "async";
  entry = { image, url: "" };
  BEETLE_BASE_CACHE.set(cacheKey, entry);

  fetch(src)
    .then((response) => {
      if (!response.ok) throw new Error(`Beetle SVG failed: ${src}`);
      return response.text();
    })
    .then((svgText) => {
      let bodySvg = stripStaticForelimbs(svgText);
      if (summoned) bodySvg = recolorSummonedBodySvg(bodySvg);
      const blob = new Blob([bodySvg], { type: "image/svg+xml" });
      const url = URL.createObjectURL(blob);
      entry.url = url;
      image.src = url;
    })
    .catch(() => {
      image.failed = true;
    });

  return image;
}

function stripStaticForelimbs(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  const paths = Array.from(root.querySelectorAll("path"));
  paths.slice(0, 4).forEach((node) => node.remove());
  return new XMLSerializer().serializeToString(root);
}

function recolorSummonedBodySvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") {
    return svgText
      .replace(/#905db0/gi, SUMMONED_BODY_FILL)
      .replace(/#754b8f/gi, SUMMONED_BODY_DARK_FILL)
      .replace(/#454545/gi, SUMMONED_BODY_DARK_FILL)
      .replace(/#555\b/gi, SUMMONED_BODY_FILL);
  }

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  root.querySelectorAll("[fill]").forEach((node) => {
    const fill = (node.getAttribute("fill") || "").trim().toLowerCase();
    if (!fill || fill === "none") return;
    node.setAttribute("fill", summonedBodyFill(fill));
  });
  root.querySelectorAll("[stroke]").forEach((node) => {
    const stroke = (node.getAttribute("stroke") || "").trim().toLowerCase();
    if (!stroke || stroke === "none") return;
    node.setAttribute("stroke", SUMMONED_BODY_STROKE);
  });
  return new XMLSerializer().serializeToString(root);
}

function summonedBodyFill(fill) {
  if (fill === "#905db0" || fill === "#555") return SUMMONED_BODY_FILL;
  if (fill === "#754b8f" || fill === "#454545") return SUMMONED_BODY_DARK_FILL;
  return SUMMONED_BODY_FILL;
}

function clipSvgBounds(ctx) {
  if (typeof ctx.roundRect === "function") {
    ctx.beginPath();
    ctx.roundRect(9.167, 9.167, 91.667, 91.667, 4.167);
    ctx.clip();
    return;
  }

  ctx.beginPath();
  ctx.rect(9.167, 9.167, 91.667, 91.667);
  ctx.clip();
}

function drawPathAroundPivot(ctx, path, pivot, angle, draw) {
  ctx.save();
  ctx.translate(pivot.x, pivot.y);
  ctx.rotate(angle);
  ctx.translate(-pivot.x, -pivot.y);
  draw(path);
  ctx.restore();
}

function getForelimbPaths() {
  if (typeof Path2D === "undefined") return null;
  if (!forelimbPaths) {
    forelimbPaths = forelimbParts.map((part) => ({
      pivot: part.pivot,
      direction: part.direction,
      path: new Path2D(part.path),
    }));
  }
  return forelimbPaths;
}

function isImageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

function clamp01(value) {
  return Math.max(0, Math.min(1, value));
}

function drawFallback(ctx, radius, summoned = false) {
  ctx.fillStyle = summoned ? SUMMONED_BODY_FILL : "#905db0";
  ctx.beginPath();
  ctx.arc(radius * 0.42, radius * 0.42, radius * 0.95, 0, Math.PI * 2);
  ctx.arc(-radius * 0.12, -radius * 0.12, radius * 0.8, 0, Math.PI * 2);
  ctx.fill();
}

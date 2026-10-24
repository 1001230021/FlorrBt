import { drawSpriteImage } from "./sprite_raster_cache.js";
import { getLimbMotionAmount, getSpritePhase } from "./sprite_motion.js";
import { prepareSvgImage, whenVisualReady } from "./visual_assets.js";

const ASSET = "./assets/firefly_larva.svg";
const BASE_FACE_ANGLE = -Math.PI * 0.75;
const SEGMENT_RENDER_RADIUS = 22;
const BODY_CROP_SIDE = 66;
const LEGS_CROP_SIDE = 112;
const MOUTH_CROP_SIDE = 72;
const LEG_COUNT = 6;
const MANDIBLE_COUNT = 2;
const MANDIBLE_GRAB_CLOSE_ANGLE = 0.1;
const MANDIBLE_PARTS = Object.freeze([
  // Authored root positions relative to the head crop center. Each half is
  // rotated independently around the point where it enters the head shell.
  { pivot: { x: 1.7282888, y: -10.32056085 }, direction: -1 },
  { pivot: { x: -10.2343498, y: 2.18147485 }, direction: 1 },
]);
export const FireflyLarvaVisualRole = Object.freeze({
  Head: 0,
  Thorax: 1,
  Tail: 7,
});
const cache = {
  bodies: new Map(),
  legs: [],
  mouth: [],
};

export function drawFireflyLarva(
  ctx,
  pos,
  radius,
  entityId,
  angle,
  motion,
  time,
  visualVariant,
  grabbing = false,
) {
  if (!(radius > 0) || !Number.isFinite(radius)) return;
  const safeVariant = Math.max(
    FireflyLarvaVisualRole.Head,
    Math.min(FireflyLarvaVisualRole.Tail, Number(visualVariant) || 0),
  );
  const sourceSegment = sourceSegmentForVariant(safeVariant);
  const body = bodyImage(sourceSegment);
  const rotation = (Number.isFinite(angle) ? angle : 0) - BASE_FACE_ANGLE;
  const worldScale = radius / SEGMENT_RENDER_RADIUS;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  ctx.rotate(rotation);

  if (safeVariant === FireflyLarvaVisualRole.Tail) drawTailGlow(ctx, radius, entityId, time);
  if (safeVariant === FireflyLarvaVisualRole.Thorax)
    drawLegs(ctx, radius, entityId, motion, time, worldScale);
  if (safeVariant === FireflyLarvaVisualRole.Head)
    drawMandibles(ctx, radius, entityId, motion, time, grabbing, worldScale);

  if (isReady(body)) {
    const size = BODY_CROP_SIDE * worldScale;
    drawSpriteImage(ctx, body, -size * 0.5, -size * 0.5, size, size);
  } else {
    drawFallbackSegment(ctx, radius, safeVariant === FireflyLarvaVisualRole.Tail);
  }
  ctx.restore();
}

function sourceSegmentForVariant(variant) {
  if (variant === FireflyLarvaVisualRole.Head) return 1;
  if (variant === FireflyLarvaVisualRole.Thorax) return 2;
  if (variant === FireflyLarvaVisualRole.Tail) return 5;
  // Interior variants carry their real root-to-tail index. Reuse the two
  // authored middle shells without losing the depth order in the snapshot.
  return 3 + ((Math.max(2, Math.trunc(variant)) - 2) & 1);
}

export function prepareFireflyLarvaSprites() {
  const jobs = [];
  for (let segment = 1; segment <= 5; segment += 1)
    jobs.push(whenVisualReady(bodyImage(segment)));
  for (let limb = 0; limb < LEG_COUNT; limb += 1)
    jobs.push(whenVisualReady(legImage(limb)));
  for (let part = 0; part < MANDIBLE_COUNT; part += 1)
    jobs.push(whenVisualReady(mouthImage(part)));
  return Promise.all(jobs);
}

function bodyImage(segment) {
  if (!cache.bodies.has(segment)) {
    cache.bodies.set(
      segment,
      prepareSvgImage(ASSET, `larva-segment-${segment}`, (text) =>
        extractLayer(text, { kind: "body", segment, cropSide: BODY_CROP_SIDE }),
      ),
    );
  }
  return cache.bodies.get(segment);
}

function legImage(index) {
  if (!cache.legs[index]) {
    cache.legs[index] = prepareSvgImage(ASSET, `larva-leg-${index}`, (text) =>
      extractLayer(text, {
        kind: "leg",
        segment: 2,
        partIndex: index,
        cropSide: LEGS_CROP_SIDE,
      }),
    );
  }
  return cache.legs[index];
}

function mouthImage(index) {
  if (!cache.mouth[index]) {
    cache.mouth[index] = prepareSvgImage(ASSET, `larva-mandible-${index}`, (text) =>
      extractLayer(text, {
        kind: "mouth",
        segment: 1,
        partIndex: index,
        cropSide: MOUTH_CROP_SIDE,
      }),
    );
  }
  return cache.mouth[index];
}

function drawLegs(ctx, radius, entityId, motion, time, worldScale) {
  const amount = getLimbMotionAmount(motion);
  const phase = getSpritePhase(
    entityId || 0,
    "firefly-larva-legs",
    time || 0,
    4.4 + amount * 7.2,
    (entityId || 0) * 0.31,
  );
  const size = LEGS_CROP_SIDE * worldScale;
  for (let index = 0; index < LEG_COUNT; index += 1) {
    const image = legImage(index);
    if (!isReady(image)) continue;
    const side = index < LEG_COUNT / 2 ? -1 : 1;
    const pair = index % 3;
    const swing =
      Math.sin(phase + pair * Math.PI * 0.72 + side * 0.35) *
      amount *
      0.19 *
      side;
    ctx.save();
    ctx.rotate(swing);
    drawSpriteImage(ctx, image, -size * 0.5, -size * 0.5, size, size);
    ctx.restore();
  }
}

function drawMandibles(ctx, radius, entityId, motion, time, grabbing, worldScale) {
  const amount = getLimbMotionAmount(motion);
  const phase = getSpritePhase(
    entityId || 0,
    "firefly-larva-mouth",
    time || 0,
    5.2 + amount * 19.5,
    (entityId || 0) * 0.73,
  );
  // Match the Soldier Ant's mirrored root rotation. Grabbing freezes the
  // cycle at a slightly closed pose instead of deforming the artwork.
  const rotation = grabbing
    ? MANDIBLE_GRAB_CLOSE_ANGLE
    : Math.sin(phase) * amount * 0.22 * 0.35;
  const size = MOUTH_CROP_SIDE * worldScale;
  for (let index = 0; index < MANDIBLE_COUNT; index += 1) {
    const image = mouthImage(index);
    if (!isReady(image)) continue;
    const part = MANDIBLE_PARTS[index];
    const pivotX = part.pivot.x * worldScale;
    const pivotY = part.pivot.y * worldScale;
    ctx.save();
    ctx.translate(pivotX, pivotY);
    ctx.rotate(rotation * part.direction);
    ctx.translate(-pivotX, -pivotY);
    drawSpriteImage(ctx, image, -size * 0.5, -size * 0.5, size, size);
    ctx.restore();
  }
}

function drawTailGlow(ctx, radius, entityId, time) {
  const pulse = 1 + Math.sin((time || 0) * 4.4 + (entityId || 0) * 0.83) * 0.06;
  const glow = Math.max(1, radius * 1.45 * pulse);
  const gradient = ctx.createRadialGradient(0, 0, radius * 0.12, 0, 0, glow);
  gradient.addColorStop(0, "rgba(249, 236, 119, 0.58)");
  gradient.addColorStop(0.45, "rgba(202, 191, 96, 0.25)");
  gradient.addColorStop(1, "rgba(202, 191, 96, 0)");
  ctx.fillStyle = gradient;
  ctx.beginPath();
  ctx.arc(0, 0, glow, 0, Math.PI * 2);
  ctx.fill();
}

function drawFallbackSegment(ctx, radius, tail) {
  ctx.fillStyle = tail ? "#555" : "#454545";
  ctx.strokeStyle = "#292929";
  ctx.lineWidth = Math.max(1.2, radius * 0.13);
  ctx.beginPath();
  ctx.arc(0, 0, radius, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
}

function extractLayer(svgText, options) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return svgText;
  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  const art = root?.querySelector("#g24");
  const segment = root?.querySelector(`#segment-${options.segment}`);
  if (!root || !art || !segment) return svgText;

  const center = transformedSegmentCenter(segment, root);
  const keep = new Set();
  if (options.kind === "body") {
    keep.add(`segment-${options.segment}`);
    keep.add("dorsal-spines");
    if (options.segment === 5) keep.add("tail-glow");
  } else if (options.kind === "leg") {
    keep.add("legs");
  } else if (options.kind === "mouth") {
    keep.add("mandibles");
  }

  for (const child of Array.from(art.children)) {
    if (!keep.has(child.id)) child.remove();
  }

  const spines = art.querySelector("#dorsal-spines");
  if (spines) {
    Array.from(spines.children).forEach((node, index) => {
      if (index !== options.segment - 1) node.remove();
    });
  }
  const legs = art.querySelector("#legs");
  if (legs && options.kind === "leg") {
    Array.from(legs.children).forEach((node, index) => {
      if (index !== options.partIndex) node.remove();
    });
  }
  const mouthparts = root.querySelector("#needle-mouthparts");
  if (mouthparts && options.kind === "mouth") {
    Array.from(mouthparts.children).forEach((node, index) => {
      if (index !== options.partIndex) node.remove();
    });
  }

  const side = options.cropSide;
  root.setAttribute(
    "viewBox",
    `${center.x - side * 0.5} ${center.y - side * 0.5} ${side} ${side}`,
  );
  root.removeAttribute("width");
  root.removeAttribute("height");
  return new XMLSerializer().serializeToString(root);
}

function transformedSegmentCenter(segment, root) {
  const values = String(segment.getAttribute("data-center") || "55 55")
    .trim()
    .split(/[\s,]+/)
    .map(Number);
  let point = {
    x: Number.isFinite(values[0]) ? values[0] : 55,
    y: Number.isFinite(values[1]) ? values[1] : 55,
  };
  let matrix = identityMatrix();
  for (let node = segment; node && node !== root; node = node.parentElement) {
    matrix = multiplyMatrices(parseTransform(node.getAttribute("transform")), matrix);
  }
  point = applyMatrix(matrix, point);
  return point;
}

function identityMatrix() {
  return { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0 };
}

function multiplyMatrices(lhs, rhs) {
  return {
    a: lhs.a * rhs.a + lhs.c * rhs.b,
    b: lhs.b * rhs.a + lhs.d * rhs.b,
    c: lhs.a * rhs.c + lhs.c * rhs.d,
    d: lhs.b * rhs.c + lhs.d * rhs.d,
    e: lhs.a * rhs.e + lhs.c * rhs.f + lhs.e,
    f: lhs.b * rhs.e + lhs.d * rhs.f + lhs.f,
  };
}

function applyMatrix(matrix, point) {
  return {
    x: matrix.a * point.x + matrix.c * point.y + matrix.e,
    y: matrix.b * point.x + matrix.d * point.y + matrix.f,
  };
}

function parseTransform(value) {
  let result = identityMatrix();
  const text = String(value || "");
  const pattern = /(matrix|translate|scale|rotate)\s*\(([^)]*)\)/gi;
  let match;
  while ((match = pattern.exec(text))) {
    const values = match[2]
      .trim()
      .split(/[\s,]+/)
      .map(Number)
      .filter(Number.isFinite);
    let next = identityMatrix();
    if (match[1].toLowerCase() === "matrix" && values.length >= 6) {
      [next.a, next.b, next.c, next.d, next.e, next.f] = values;
    } else if (match[1].toLowerCase() === "translate") {
      next.e = values[0] || 0;
      next.f = values.length > 1 ? values[1] : 0;
    } else if (match[1].toLowerCase() === "scale") {
      next.a = values[0] ?? 1;
      next.d = values.length > 1 ? values[1] : next.a;
    } else if (match[1].toLowerCase() === "rotate") {
      const angle = ((values[0] || 0) * Math.PI) / 180;
      const cos = Math.cos(angle);
      const sin = Math.sin(angle);
      const rotate = { a: cos, b: sin, c: -sin, d: cos, e: 0, f: 0 };
      if (values.length >= 3) {
        const [cx, cy] = values.slice(1, 3);
        next = multiplyMatrices(
          { a: 1, b: 0, c: 0, d: 1, e: cx, f: cy },
          multiplyMatrices(rotate, { a: 1, b: 0, c: 0, d: 1, e: -cx, f: -cy }),
        );
      } else next = rotate;
    }
    result = multiplyMatrices(result, next);
  }
  return result;
}

function isReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

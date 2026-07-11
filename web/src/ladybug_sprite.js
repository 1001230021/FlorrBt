const LADYBUG_VIEWBOX_SIZE = 110;
const LADYBUG_BODY_RADIUS = 27.5;
const LADYBUG_BASE_FACE_ANGLE = -Math.PI * 0.75;
const LADYBUG_BODY_PATH_DATA = "M50.392 30.428q.603-.113 1.21-.196.609-.084 1.22-.137.612-.053 1.225-.077.613-.023 1.227-.016.613.006 1.226.043t1.223.104 1.216.163 1.207.223 1.194.282 1.179.34 1.16.398q.576.213 1.14.454.565.241 1.117.51.552.268 1.09.563.538.296 1.06.617.524.321 1.03.668.507.346.995.717.49.371.96.766.47.394.92.811t.88.856q.428.439.836.898.407.46.79.938.385.478.745.975t.696 1.011q.335.514.645 1.044t.593 1.074.54 1.102.484 1.127.43 1.15.372 1.17.314 1.185q.142.597.256 1.2.113.604.196 1.212.084.608.137 1.22.053.61.077 1.224.023.613.016 1.227-.006.613-.043 1.226t-.104 1.223-.163 1.216-.223 1.207-.282 1.194q-.156.593-.34 1.179-.185.585-.398 1.16-.213.576-.454 1.14-.241.565-.51 1.117-.268.552-.563 1.09-.296.538-.617 1.06-.321.524-.668 1.03-.346.507-.717.995-.371.49-.766.96-.394.47-.811.92t-.856.88q-.439.428-.898.836-.46.407-.938.79-.478.385-.975.745t-1.011.696q-.514.335-1.044.645t-1.074.593-1.102.54-1.127.484-1.15.43-1.17.372-1.185.314q-.597.142-1.2.256-.604.113-1.212.196-.608.084-1.22.137-.61.053-1.224.077-.613.023-1.227.016-.613-.006-1.226-.043t-1.223-.104-1.216-.163-1.207-.223-1.194-.282q-.593-.156-1.179-.34-.585-.185-1.16-.398-.576-.213-1.14-.454-.565-.241-1.117-.51-.552-.268-1.09-.563-.538-.296-1.06-.617-.524-.321-1.03-.668-.507-.346-.995-.717-.49-.371-.96-.766-.47-.394-.92-.811t-.88-.856q-.428-.439-.836-.898-.407-.46-.79-.938-.385-.478-.745-.975t-.696-1.011-.645-1.044-.593-1.074-.54-1.102-.484-1.127-.43-1.15-.372-1.17-.314-1.185q-.142-.597-.256-1.2-.213-1.14-.32-2.295Q30 56.16 30 55t.107-2.314.321-2.294q18.68-1.285 19.964-19.964";

const baseImage = makeImage(new URL("../assets/normal_ladybug_base.svg", import.meta.url).href);
const outlineImage = makeImage(new URL("../assets/normal_ladybug_outline.svg", import.meta.url).href);
const patternCache = new Map();
let bodyPath = null;

export function clearLadybugPattern(entityId) {
  patternCache.delete(entityId);
}

export function drawNormalLadybug(ctx, pos, radius, entityId, angle) {
  const spriteSize = Math.max(1, radius * (LADYBUG_VIEWBOX_SIZE / LADYBUG_BODY_RADIUS));
  const spriteHalf = spriteSize * 0.5;
  const rotation = (Number.isFinite(angle) ? angle : 0) - LADYBUG_BASE_FACE_ANGLE;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  ctx.rotate(rotation);

  if (!isImageReady(baseImage)) {
    drawFallback(ctx, radius);
    ctx.restore();
    return;
  }

  ctx.drawImage(baseImage, -spriteHalf, -spriteHalf, spriteSize, spriteSize);
  drawPattern(ctx, spriteSize, entityId);
  if (isImageReady(outlineImage)) {
    ctx.drawImage(outlineImage, -spriteHalf, -spriteHalf, spriteSize, spriteSize);
  }

  ctx.restore();
}

function makeImage(src) {
  const image = new Image();
  image.decoding = "async";
  image.src = src;
  return image;
}

function isImageReady(image) {
  return image.complete && image.naturalWidth > 0;
}

function getBodyPath() {
  if (!bodyPath && typeof Path2D !== "undefined") bodyPath = new Path2D(LADYBUG_BODY_PATH_DATA);
  return bodyPath;
}

function drawPattern(ctx, spriteSize, entityId) {
  const path = getBodyPath();
  if (!path) return;

  const scale = spriteSize / LADYBUG_VIEWBOX_SIZE;
  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-LADYBUG_VIEWBOX_SIZE * 0.5, -LADYBUG_VIEWBOX_SIZE * 0.5);
  ctx.clip(path);
  ctx.fillStyle = "#111";
  for (const spot of getPattern(entityId)) {
    ctx.beginPath();
    ctx.arc(spot.x, spot.y, spot.r, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

function getPattern(entityId) {
  let pattern = patternCache.get(entityId);
  if (pattern) return pattern;

  const rand = seededRandom(entityId * 2654435761 + 0x9e3779b9);
  const bodyRadius = 25.5;
  const targetArea = Math.PI * bodyRadius * bodyRadius * rand() * 0.75;
  let area = 0;
  pattern = [];

  for (let attempt = 0; attempt < 120 && area < targetArea; attempt += 1) {
    const spotRadius = 2.2 + rand() * 6.4;
    const angle = rand() * Math.PI * 2;
    const distance = Math.sqrt(rand()) * (bodyRadius - spotRadius - 0.5);
    const x = 55 + Math.cos(angle) * distance;
    const y = 55 + Math.sin(angle) * distance;
    if (x < 53 && y < 53 && Math.hypot(x - 43, y - 43) < 16 + spotRadius) continue;
    pattern.push({ x, y, r: spotRadius });
    area += Math.PI * spotRadius * spotRadius;
  }

  patternCache.set(entityId, pattern);
  return pattern;
}

function seededRandom(seed) {
  let value = seed >>> 0;
  return () => {
    value += 0x6d2b79f5;
    let t = value;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function drawFallback(ctx, radius) {
  ctx.fillStyle = "#eb4034";
  ctx.beginPath();
  ctx.arc(0, 0, radius, 0, Math.PI * 2);
  ctx.fill();
  ctx.lineWidth = Math.max(1.25, radius * 0.1);
  ctx.strokeStyle = "#be342a";
  ctx.stroke();
}

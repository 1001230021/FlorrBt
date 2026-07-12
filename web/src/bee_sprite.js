const BEE_VIEWBOX_SIZE = 110;
const BEE_EFFECTIVE_BOX = 91.667;
const BEE_BASE_FACE_ANGLE = -Math.PI * 0.75;
const HORNET_BASE_FACE_ANGLE = -Math.PI * 0.75;
const BEE_SPRITE_SCALE = 2.5;
const BEE_BASE_CACHE = new Map();
const BEE_ANTENNA_STROKE = "#333";
const BEE_ANTENNA_LINE_WIDTH = 2.5;
const HORNET_MISSILE_CENTER = { x: 86.5, y: 86.5 };
const HORNET_MISSILE_BOX = 32;

const antennaParts = [
  {
    pivot: { x: 37.322, y: 43.215 },
    dot: { x: 22.591, y: 40.269 },
    direction: -1,
    path: "M37.322 43.215q-5.892-5.893-14.731-2.946",
  },
  {
    pivot: { x: 43.215, y: 37.322 },
    dot: { x: 40.269, y: 22.591 },
    direction: 1,
    path: "M43.215 37.322q-5.893-5.892-2.946-14.731",
  },
];

const hornetAntennaParts = [
  {
    pivot: { x: 37.185, y: 47.025 },
    direction: -1,
    fill: true,
    path: "M37.185 47.025q.114.057.235.096.12.038.247.057.125.02.252.019.127 0 .253-.021.125-.02.246-.06.12-.04.234-.098.113-.058.216-.132t.193-.165q.056-.057.107-.118.05-.062.095-.128.044-.067.082-.137.037-.07.068-.144.03-.074.054-.15.023-.077.038-.155.016-.078.024-.158t.008-.16-.008-.158-.024-.158-.038-.155-.054-.15-.068-.144-.082-.137q-.045-.066-.095-.128t-.107-.118Q27.094 32.456 11.1 32.456q-.128 0-.253.02-.126.02-.247.059-.12.039-.234.096-.114.058-.217.132t-.193.164-.166.192-.133.215-.1.234q-.024.075-.042.153t-.028.157-.012.16q-.002.079.004.159.005.08.019.158.013.079.034.156t.05.151.064.146.078.14q.043.067.092.13t.104.12q.054.059.115.111.06.052.125.098t.135.086.142.072.149.058q11.38 3.794 26.598 11.402",
  },
  {
    pivot: { x: 47.025, y: 37.185 },
    direction: 1,
    fill: true,
    path: "M47.025 37.185q.057.114.096.235.038.12.057.247.02.125.019.252 0 .127-.021.253-.02.125-.06.246-.04.12-.098.234-.058.113-.132.216t-.165.193q-.057.056-.118.107-.062.05-.128.095-.067.044-.137.082-.07.037-.144.068-.074.03-.15.054-.077.023-.155.038-.078.016-.158.024t-.16.008-.158-.008-.158-.024-.155-.038-.15-.054-.144-.068-.137-.082q-.066-.045-.128-.095t-.118-.107Q32.456 27.094 32.456 11.1q0-.128.02-.253.02-.126.059-.247.039-.12.096-.234.058-.114.132-.217t.164-.193.192-.166.215-.133.234-.1q.075-.024.153-.042t.157-.028.16-.012q.079-.002.159.004.08.005.158.019.079.013.156.034t.151.05.146.064.14.078q.067.043.13.092t.12.104q.059.054.111.115.052.06.098.125t.086.135.072.142.058.149q3.794 11.38 11.402 26.598",
  },
];

const hornetMissileParts = [
  "M96.896 96.896 84.64 75.447l-9.193 9.193z",
  "M94.545 98.24 82.288 76.79l2.352-1.343 1.915 1.915-9.193 9.193-1.915-1.915 1.344-2.352L98.24 94.545l-1.344 2.351zm4.703-2.688q.087.153.154.316.066.163.112.333.045.17.068.345.022.174.022.35t-.022.35-.068.345-.112.333-.154.316q-.066.115-.143.224-.078.108-.165.209t-.184.192-.202.172-.218.153q-.112.07-.231.13t-.244.107-.252.082-.26.057-.264.032q-.132.01-.265.005-.134-.003-.266-.02t-.261-.047q-.13-.03-.256-.071-.126-.043-.248-.097-.121-.054-.237-.12L74.103 86.99q-.314-.18-.57-.436-.095-.094-.18-.197t-.158-.214-.136-.228-.114-.24-.09-.25q-.038-.128-.064-.258t-.04-.263q-.012-.132-.012-.265 0-.134.013-.266t.039-.263q.026-.13.064-.258.04-.127.09-.25t.114-.24q.062-.118.136-.228t.159-.214q.084-.102.178-.197l9.192-9.192q.257-.256.572-.436.115-.066.237-.12.121-.055.247-.097.127-.042.256-.072.13-.03.262-.047.132-.016.265-.02t.266.005.264.032q.13.022.26.057.128.035.252.083.124.047.243.107.12.059.232.13.113.07.218.152t.202.173.184.192q.087.1.164.208.077.109.143.224z",
];

let antennaPaths = null;
let hornetAntennaPaths = null;
let hornetMissilePaths = null;

export function drawBee(ctx, pos, radius, entityId, angle, motion, time) {
  drawInsect(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/bee.svg",
    baseFaceAngle: BEE_BASE_FACE_ANGLE,
    antennaParts: getAntennaPaths(),
    strip: "bee",
  });
}

export function drawHornet(ctx, pos, radius, entityId, angle, motion, time) {
  drawInsect(ctx, pos, radius, entityId, angle, motion, time, {
    src: "./assets/hornet.svg",
    baseFaceAngle: HORNET_BASE_FACE_ANGLE,
    antennaParts: getHornetAntennaPaths(),
    strip: "hornet",
  });
}

export function drawHornetMissile(ctx, pos, radius, angle) {
  const paths = getHornetMissilePaths();
  const scale = (radius * 2.45) / HORNET_MISSILE_BOX;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle)) ctx.rotate(paths.length ? angle - Math.PI * 0.25 : angle);
  if (!paths.length) {
    ctx.fillStyle = "#333";
    ctx.beginPath();
    ctx.moveTo(radius * 1.05, 0);
    ctx.lineTo(-radius * 0.45, radius * 0.62);
    ctx.lineTo(-radius * 0.18, 0);
    ctx.lineTo(-radius * 0.45, -radius * 0.62);
    ctx.closePath();
    ctx.fill();
    ctx.restore();
    return;
  }
  ctx.scale(scale, scale);
  ctx.translate(-HORNET_MISSILE_CENTER.x, -HORNET_MISSILE_CENTER.y);
  ctx.fillStyle = "#333";
  for (const path of paths) ctx.fill(path);
  ctx.restore();
}

function drawInsect(ctx, pos, radius, entityId, angle, motion, time, options) {
  const spriteSize = Math.max(1, radius * 2 * (BEE_VIEWBOX_SIZE / BEE_EFFECTIVE_BOX) * BEE_SPRITE_SCALE);
  const spriteHalf = spriteSize * 0.5;
  const base = insectBaseImage(options.src, options.strip);
  const antennaJitter = beeAntennaJitter(entityId, motion || 0, time || 0);

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle)) ctx.rotate(angle - options.baseFaceAngle);

  if (isImageReady(base)) {
    ctx.drawImage(base, -spriteHalf, -spriteHalf, spriteSize, spriteSize);
  } else {
    drawFallback(ctx, radius * BEE_SPRITE_SCALE);
  }
  drawAntennae(ctx, spriteSize, antennaJitter, options.antennaParts);

  ctx.restore();
}

function beeAntennaJitter(entityId, motion, time) {
  const period = 4.4 + (entityId % 7) * 0.28;
  const phase = (time + entityId * 0.31) % period;
  const duration = 0.52;
  if (phase > duration) return 0;

  const t = phase / duration;
  const envelope = 1 - t;
  return Math.sin(t * Math.PI * 2) * envelope * (0.09 + clamp01(motion) * 0.04);
}

function drawAntennae(ctx, spriteSize, jitter, paths) {
  if (!paths) return;
  const scale = spriteSize / BEE_VIEWBOX_SIZE;

  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-BEE_VIEWBOX_SIZE * 0.5, -BEE_VIEWBOX_SIZE * 0.5);
  clipSvgBounds(ctx);
  ctx.strokeStyle = BEE_ANTENNA_STROKE;
  ctx.fillStyle = BEE_ANTENNA_STROKE;
  ctx.lineWidth = BEE_ANTENNA_LINE_WIDTH;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";

  for (const part of paths) {
    const angle = jitter * part.direction;
    drawPathAroundPivot(ctx, part.path, part.pivot, angle, () => {
      if (part.fill) {
        ctx.fill(part.path);
      } else {
        ctx.stroke(part.path);
        ctx.beginPath();
        ctx.arc(part.dot.x, part.dot.y, 4.2, 0, Math.PI * 2);
        ctx.fill();
      }
    });
  }
  ctx.restore();
}

function insectBaseImage(src, strip) {
  const key = `${strip}:${src}`;
  let entry = BEE_BASE_CACHE.get(key);
  if (entry) return entry.image;

  const image = new Image();
  image.decoding = "async";
  entry = { image, url: "" };
  BEE_BASE_CACHE.set(key, entry);

  fetch(src)
    .then((response) => {
      if (!response.ok) throw new Error(`Bee SVG failed: ${src}`);
      return response.text();
    })
    .then((svgText) => {
      const blob = new Blob([stripAntennae(svgText, strip)], { type: "image/svg+xml" });
      const url = URL.createObjectURL(blob);
      entry.url = url;
      image.src = url;
    })
    .catch(() => {
      image.failed = true;
    });

  return image;
}

function stripAntennae(svgText, strip) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  const paths = Array.from(root.querySelectorAll("path"));
  if (strip === "hornet") paths.slice(-2).forEach((node) => node.remove());
  else paths.slice(5, 9).forEach((node) => node.remove());
  return new XMLSerializer().serializeToString(root);
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

function getAntennaPaths() {
  if (typeof Path2D === "undefined") return null;
  if (!antennaPaths) {
    antennaPaths = antennaParts.map((part) => ({
      pivot: part.pivot,
      dot: part.dot,
      direction: part.direction,
      path: new Path2D(part.path),
    }));
  }
  return antennaPaths;
}

function getHornetAntennaPaths() {
  if (typeof Path2D === "undefined") return null;
  if (!hornetAntennaPaths) {
    hornetAntennaPaths = hornetAntennaParts.map((part) => ({
      pivot: part.pivot,
      direction: part.direction,
      fill: true,
      path: new Path2D(part.path),
    }));
  }
  return hornetAntennaPaths;
}

function getHornetMissilePaths() {
  if (typeof Path2D === "undefined") return [];
  if (!hornetMissilePaths) {
    hornetMissilePaths = hornetMissileParts.map((path) => new Path2D(path));
  }
  return hornetMissilePaths;
}

function isImageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

function clamp01(value) {
  return Math.max(0, Math.min(1, value));
}

function drawFallback(ctx, radius) {
  ctx.fillStyle = "#ffe763";
  ctx.strokeStyle = "#d3bd46";
  ctx.lineWidth = Math.max(1.5, radius * 0.14);
  ctx.beginPath();
  ctx.ellipse(0, 0, radius * 0.92, radius * 0.64, Math.PI * 0.25, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();

  ctx.fillStyle = "#333";
  ctx.beginPath();
  ctx.moveTo(radius * 0.84, radius * 0.84);
  ctx.lineTo(radius * 0.28, radius * 0.66);
  ctx.lineTo(radius * 0.66, radius * 0.28);
  ctx.closePath();
  ctx.fill();
}

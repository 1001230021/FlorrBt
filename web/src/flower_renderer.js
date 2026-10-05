// One geometry for world flowers and HUD portraits. Coordinates follow the
// supplied Flower.cc (25-unit body), with video-measured neutral proportions.
// Body/face use native canvas paths; equipment uses cached SVG images, with
// no per-flower filters or SVG decoding.
export const flowerGeometry = Object.freeze({
  radius: 25, outline: 2.4, eyeX: 7, eyeY: -5, eyeRX: 3, eyeRY: 6,
  pupilRadius: 3, mouthX: 6, mouthY: 10, mouthWidth: 1.5,
  attackMouth: 7,
  attackBrow: 0.35,
});

// Measured from the native Titan reference: the face disc is 720px across
// inside a 900px gear, and each of the 20 teeth spans one 18-degree sector.
// The collision radius maps to the tooth tips; no artwork extends past it.
export const titanGeometry = Object.freeze({
  toothCount: 20,
  coreRadiusRatio: 0.8,
  toothRootRadiusRatio: 416 / 450,
  toothTipHalfSectorRatio: 0.125,
});

export const magicEyeOutline = Object.freeze({
  color: "#42e3f5",
  width: 3.2,
});

export function stepFlowerFace(previous, target, dt) {
  const dead = !!target.dead;
  const mouth = dead ? 3 : target.attacking ? flowerGeometry.attackMouth : target.defending ? 4 : 16;
  const brow = target.attacking && !dead ? flowerGeometry.attackBrow : 0;
  const x = Number.isFinite(target.lookX) ? Math.max(-1, Math.min(1, target.lookX)) : 0;
  const y = Number.isFinite(target.lookY) ? Math.max(-1, Math.min(1, target.lookY)) : -0.65;
  const face = previous || { mouth, brow, eyeX: x * 2.6, eyeY: y * 3.2, tilt: 0, dead };
  const follow = 1 - Math.exp(-18 * Math.max(0, Math.min(0.1, dt)));
  face.mouth += (mouth - face.mouth) * follow;
  face.brow += (brow - face.brow) * follow;
  face.eyeX += (x * 2.6 - face.eyeX) * follow;
  face.eyeY += (y * 3.2 - face.eyeY) * follow;
  // Corpse roll is an inertial presentation value, not an unconditional
  // upside-down face. Portraits can request the same small, settled tilt.
  const deadTilt = Number.isFinite(target.deadTilt) ? target.deadTilt : 0.25;
  face.tilt += ((dead ? deadTilt : 0) - face.tilt) * (1 - Math.exp(-9 * Math.max(0, Math.min(0.1, dt))));
  face.dead = dead;
  return face;
}

export function mixFlowerColor(a, b, fraction) {
  const k = Math.max(0, Math.min(1, fraction));
  const aa = Number.parseInt(a.slice(1), 16), bb = Number.parseInt(b.slice(1), 16);
  const channel = shift => Math.round(((aa >> shift) & 255) * (1 - k) + ((bb >> shift) & 255) * k);
  return `rgb(${channel(16)}, ${channel(8)}, ${channel(0)})`;
}

export function getFlowerPalette({ poisoned = false, undead = false, corrupted = false,
  antiHeal = false, psionic = false, portrait = false, hit = 0, dummy = false,
  titan = false } = {}) {
  let fill = titan ? "#666666" : dummy ? "#99918f" : portrait ? "#edD35f" : "#ffe763";
  let border = titan ? "#535353" : dummy ? "#7b7472" : portrait ? "#c8b24f" : "#cfbc50";
  // Relic's psionic connection is the dark-flower state. It must win over
  // the generic pale psionic tint and remain visible even when another
  // support modifier (for example anti-heal) is present.
  if (psionic) { fill = "#3e4748"; border = "#202829"; }
  else if (poisoned) { fill = "#ce76db"; border = "#a55faF"; }
  else if (undead) { fill = "#c5d476"; border = "#9eaa5e"; }
  else if (corrupted) { fill = "#c65165"; border = "#9e4051"; }
  // Dandelion/anti-heal is a pale body, not a shifted muddy hue.
  if (antiHeal && !poisoned && !psionic) {
    const pale = titan ? "#969696" : dummy ? "#c7c2c1" : "#fff1a1";
    fill = pale;
    border = titan ? "#747474" : dummy ? "#9f9b9a" : "#ccc181";
  }
  return { fill: mixFlowerColor(fill, "#ff7046", hit * 0.65), border: mixFlowerColor(border, "#ce533a", hit * 0.65) };
}

export function drawFlowerDisc(ctx, radius, palette, heavyRim = false) {
  const line = radius * (heavyRim ? 0.18 : flowerGeometry.outline / 25);
  ctx.fillStyle = palette.fill;
  ctx.strokeStyle = heavyRim ? "#222222" : palette.border;
  ctx.lineWidth = line;
  ctx.beginPath();
  ctx.arc(0, 0, Math.max(0, radius - line / 2), 0, Math.PI * 2);
  ctx.fill(); ctx.stroke();
}

export function getTitanCoreRadius(radius) {
  return Math.max(0, Number.isFinite(radius) ? radius : 0) * titanGeometry.coreRadiusRatio;
}

export function drawTitanBody(ctx, radius, palette, heavyRim = false) {
  if (!(radius > 0) || !Number.isFinite(radius)) return;

  const toothCount = titanGeometry.toothCount;
  const sector = Math.PI * 2 / toothCount;
  const rootRadius = radius * titanGeometry.toothRootRadiusRatio;
  const tipHalfAngle = sector * titanGeometry.toothTipHalfSectorRatio;

  // Fill the root ring and all teeth as one path so adjacent trapezoids cannot
  // expose antialiasing seams while the camera is zooming or moving quickly.
  ctx.fillStyle = palette.border;
  ctx.beginPath();
  ctx.arc(0, 0, rootRadius, 0, Math.PI * 2);
  for (let index = 0; index < toothCount; index += 1) {
    const center = -Math.PI * 0.5 + index * sector;
    const rootLeft = center - sector * 0.5;
    const rootRight = center + sector * 0.5;
    const tipLeft = center - tipHalfAngle;
    const tipRight = center + tipHalfAngle;
    ctx.moveTo(Math.cos(rootLeft) * rootRadius, Math.sin(rootLeft) * rootRadius);
    ctx.lineTo(Math.cos(tipLeft) * radius, Math.sin(tipLeft) * radius);
    ctx.lineTo(Math.cos(tipRight) * radius, Math.sin(tipRight) * radius);
    ctx.lineTo(Math.cos(rootRight) * rootRadius, Math.sin(rootRight) * rootRadius);
    ctx.closePath();
  }
  ctx.fill();

  const coreRadius = getTitanCoreRadius(radius);
  if (heavyRim) drawFlowerDisc(ctx, coreRadius, palette, true);
  else {
    ctx.fillStyle = palette.fill;
    ctx.beginPath();
    ctx.arc(0, 0, coreRadius, 0, Math.PI * 2);
    ctx.fill();
  }
}

export function drawFlowerFace(ctx, radius, face, palette, { dummy = false, magicEye = false } = {}) {
  if (!(radius > 0)) return;
  const g = flowerGeometry;
  ctx.save();
  ctx.scale(radius / 25, radius / 25);
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.fillStyle = "#222222";
  ctx.strokeStyle = "#222222";
  if (face.dead) {
    const traceDeadEyes = () => {
      ctx.beginPath();
      for (const x of [-g.eyeX, g.eyeX]) {
        ctx.moveTo(x - 4, g.eyeY - 4); ctx.lineTo(x + 4, g.eyeY + 4);
        ctx.moveTo(x + 4, g.eyeY - 4); ctx.lineTo(x - 4, g.eyeY + 4);
      }
    };
    if (magicEye) {
      ctx.strokeStyle = magicEyeOutline.color;
      ctx.lineWidth = 3 + magicEyeOutline.width;
      traceDeadEyes();
      ctx.stroke();
    }
    ctx.strokeStyle = "#222222";
    ctx.lineWidth = 3;
    traceDeadEyes();
    ctx.stroke();
  } else {
    ctx.save();
    ctx.beginPath();
    for (const x of [-g.eyeX, g.eyeX]) {
      ctx.moveTo(x + g.eyeRX, g.eyeY);
      ctx.ellipse(x, g.eyeY, g.eyeRX, g.eyeRY, 0, 0, Math.PI * 2);
    }
    if (magicEye) {
      ctx.strokeStyle = magicEyeOutline.color;
      ctx.lineWidth = magicEyeOutline.width;
      ctx.stroke();
    }
    ctx.fillStyle = "#222222";
    ctx.strokeStyle = "#222222";
    ctx.lineWidth = 1;
    ctx.fill(); ctx.stroke(); ctx.clip();
    ctx.fillStyle = "#ffffff";
    ctx.beginPath();
    for (const x of [-g.eyeX, g.eyeX]) {
      if (dummy) ctx.rect(x - 2, g.eyeY - 1.5, 4, 4);
      else {
        ctx.moveTo(x + face.eyeX + g.pupilRadius, g.eyeY + face.eyeY);
        ctx.arc(x + face.eyeX, g.eyeY + face.eyeY, g.pupilRadius, 0, Math.PI * 2);
      }
    }
    ctx.fill(); ctx.restore();
  }
  // Only trim the upper eye edge. The previous full-depth V reached the
  // pupil centres and left two tiny, angry white triangles. Derive the mask
  // from the eye bounds so changing eye size cannot misalign the expression.
  if (face.brow > 0.001 && !face.dead && !dummy) {
    ctx.fillStyle = palette.fill;
    const brow = Math.max(0, Math.min(flowerGeometry.attackBrow, face.brow));
    const eyeTop = g.eyeY - g.eyeRY - 0.5;
    const outerX = g.eyeX + g.eyeRX + 2;
    const top = eyeTop - 3 + brow * 8;
    ctx.beginPath();
    ctx.moveTo(-outerX, eyeTop - 6); ctx.lineTo(outerX, eyeTop - 6);
    ctx.lineTo(outerX, top); ctx.lineTo(0, top + 6); ctx.lineTo(-outerX, top);
    ctx.closePath(); ctx.fill();

    // The body-colored attack mask trims the oval outline too. Restore only
    // the newly exposed sloped edge, clipped to the two enlarged eye sockets,
    // so Magic Eye follows the expression instead of drawing a floating oval.
    if (magicEye) {
      ctx.save();
      ctx.beginPath();
      const expansion = magicEyeOutline.width * 0.5;
      for (const x of [-g.eyeX, g.eyeX]) {
        ctx.moveTo(x + g.eyeRX + expansion, g.eyeY);
        ctx.ellipse(x, g.eyeY, g.eyeRX + expansion, g.eyeRY + expansion, 0, 0, Math.PI * 2);
      }
      ctx.clip();
      ctx.strokeStyle = magicEyeOutline.color;
      ctx.lineWidth = magicEyeOutline.width;
      ctx.beginPath();
      ctx.moveTo(-outerX, top);
      ctx.lineTo(0, top + 6);
      ctx.lineTo(outerX, top);
      ctx.stroke();
      ctx.restore();
    }
  }
  ctx.strokeStyle = "#222222";
  ctx.lineWidth = g.mouthWidth;
  ctx.beginPath();
  ctx.moveTo(-g.mouthX, g.mouthY);
  ctx.quadraticCurveTo(0, dummy ? g.mouthY : face.mouth, g.mouthX, g.mouthY);
  ctx.stroke();
  ctx.restore();
}

// The attachment coordinates share the body's 25-unit frame. Art is fitted
// by its actual content bounds, never by the old 110-unit petal-card canvas.
export const flowerAttachmentLayout = Object.freeze({
  // Include the fitted SVG's 4% padding and let strip ends reach the outer
  // body rim. Clipping below keeps the enlarged art inside the silhouette.
  bandage: Object.freeze({ size: 2.3, y: 0, angle: 0, clipToBody: true }),
  // The reference's roots sit inside the forehead (about -0.74R), not on
  // top of the rim. Preserve the SVG's curved, tapered shape and aspect.
  antennae: Object.freeze({ size: 1.3, y: -1.25, angle: 0 }),
  thirdEye: Object.freeze({ size: 0.32, y: -14 / 25, angle: 0 }),
  douli: Object.freeze({ size: 2.65, y: -0.95, angle: 0 }),
});

function drawAttachmentImage(ctx, image, x, y, width, height) {
  ctx.drawImage(image, x, y, width, height);
}

function drawFlowerAttachment(ctx, radius, image, layout, drawImage) {
  if (!(radius > 0) || !Number.isFinite(radius) || !image?.complete ||
      !(image.naturalWidth > 0) || image.failed) return false;
  const size = radius * layout.size;
  if (layout.clipToBody) {
    ctx.save(); ctx.beginPath();
    ctx.arc(0, 0, radius, 0, Math.PI * 2); ctx.clip();
  }
  drawImage(ctx, image, -size / 2, radius * layout.y - size / 2, size, size);
  if (layout.clipToBody) ctx.restore();
  return true;
}

// Pass the world's sprite-raster draw callback to reuse its high-DPI cache;
// HUD/death canvases can draw the very same fitted SVG and geometry directly.
export function drawFlowerBandage(ctx, radius, image, drawImage = drawAttachmentImage) {
  return drawFlowerAttachment(ctx, radius, image, flowerAttachmentLayout.bandage, drawImage);
}

export function drawFlowerAntennae(ctx, radius, image, drawImage = drawAttachmentImage) {
  return drawFlowerAttachment(ctx, radius, image, flowerAttachmentLayout.antennae, drawImage);
}

export function getFlowerFrameLayout(radius, uiScale = 1, isOwner = false) {
  const s = Math.max(0.5, Math.min(1.5, uiScale || 1));
  const r = Math.max(0, radius);
  const nameSize = Math.max(8 * s, Math.min(12 * s, r * 0.6));
  const gap = 3 * s;
  const nameBaseline = r + gap + nameSize;
  const barY = isOwner ? r + 5 * s : nameBaseline + gap;
  const barHeight = Math.max(3 * s, Math.min(5 * s, r * 0.17));
  return {
    width: Math.max(36 * s, Math.min(80 * s, r * 3.2)),
    barY, barHeight, nameSize, nameBaseline,
    levelY: barY + barHeight + 2 * s,
    levelSize: nameSize * 0.82,
  };
}

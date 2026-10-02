import { drawFlowerDisc, drawFlowerFace, drawFlowerBandage, drawFlowerAntennae, getFlowerPalette, stepFlowerFace,
  flowerAttachmentLayout } from "./flower_renderer.js";
import { PETAL_IDS, STATE_IDS } from "./generated_ids.js";

const attachments = new Set([PETAL_IDS.antennae, PETAL_IDS.third_eye, PETAL_IDS.bandage, PETAL_IDS.douli]);
const contexts = new WeakMap();

export function buildDeathPortraitPetals(slots, getCopyCount = () => 1) {
  const result = [];
  const list = (slots || []).filter(slot => slot?.petalType > 0 && slot.rarity > 0);
  for (let i = 0; i < list.length; i++) {
    const slot = list[i];
    if (attachments.has(slot.petalType)) continue;
    const copies = Math.max(0, Math.min(8, slot.copies?.length || getCopyCount(slot.petalType, slot.rarity)));
    for (let copy = 0; copy < copies && result.length < 64; copy++) {
      const groupAngle = -Math.PI / 2 + i * Math.PI * 2 / Math.max(1, list.length);
      const copyAngle = copies > 1 ? copy / copies * Math.PI * 2 : 0;
      result.push({ petalType: slot.petalType, rarity: slot.rarity, angle: groupAngle,
        offsetX: copies > 1 ? Math.cos(copyAngle) * 8 : 0,
        offsetY: copies > 1 ? Math.sin(copyAngle) * 8 : 0,
        alpha: slot.copies?.[copy]?.state === 1 ? 0.45 : 1 });
    }
  }
  return result;
}

export function drawDeathPortrait(canvas, summary, now, { getImage, getCopyCount, getAttachmentImage } = {}) {
  if (!canvas || !summary) return;
  let cached = contexts.get(canvas);
  if (!cached) {
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    cached = { ctx, summary: null, petals: [] };
    contexts.set(canvas, cached);
  }
  const { ctx } = cached;
  const size = 176;
  const dpr = Math.max(1, Math.min(3, Number(globalThis.devicePixelRatio) || 1));
  if (canvas.width !== Math.round(size * dpr)) {
    canvas.width = canvas.height = Math.round(size * dpr);
  }
  if (cached.summary !== summary) {
    cached.summary = summary;
    cached.petals = buildDeathPortraitPetals(summary.slots, getCopyCount);
  }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, size, size);
  ctx.translate(size / 2, size / 2 + 6);
  const has = type => summary.slots.some(slot => slot.petalType === type);
  const state = type => (summary.snapshot.states || []).some(entry => entry.type === type);
  const palette = getFlowerPalette({ portrait: false, poisoned: state(STATE_IDS.poison) });
  const radius = 36;
  const face = stepFlowerFace(null, { dead: true, deadTilt: -0.12 }, 0);
  ctx.save();
  ctx.rotate(-0.12);
  drawFlowerDisc(ctx, radius, palette, has(PETAL_IDS.disc));
  if (has(PETAL_IDS.bandage)) drawFlowerBandage(ctx, radius, getAttachmentImage?.("bandage"));
  drawFlowerFace(ctx, radius, face, palette);
  if (has(PETAL_IDS.antennae)) drawFlowerAntennae(ctx, radius, getAttachmentImage?.("antennae"));
  if (has(PETAL_IDS.third_eye)) {
    const y = radius * flowerAttachmentLayout.thirdEye.y;
    ctx.strokeStyle = "#222"; ctx.lineWidth = 2; ctx.lineCap = "round";
    ctx.beginPath(); ctx.moveTo(-3, y - 3); ctx.lineTo(3, y + 3);
    ctx.moveTo(3, y - 3); ctx.lineTo(-3, y + 3); ctx.stroke();
  }
  if (has(PETAL_IDS.douli)) {
    const image = getAttachmentImage?.("douli");
    const layout = flowerAttachmentLayout.douli;
    const width = radius * layout.size;
    if (image?.complete && image.naturalWidth > 0)
      ctx.drawImage(image, -width / 2, radius * layout.y - width / 2, width, width);
  }
  ctx.restore();
  const reduced = !!globalThis.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches;
  const orbit = reduced ? 0 : Math.max(0, now - summary.startedAt) / 1000 * 0.18;
  for (const petal of cached.petals) {
    const image = getImage?.(petal.petalType, petal.rarity);
    if (!image?.complete || image.naturalWidth <= 0) continue;
    const x = Math.cos(petal.angle + orbit) * 53 + petal.offsetX;
    const y = Math.sin(petal.angle + orbit) * 53 + petal.offsetY;
    ctx.save(); ctx.globalAlpha = petal.alpha;
    ctx.drawImage(image, x - 25, y - 25, 50, 50); ctx.restore();
  }
}

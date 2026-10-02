import assert from "node:assert/strict";
import { drawFlowerDisc, drawFlowerFace, drawFlowerBandage, drawFlowerAntennae, getFlowerPalette, stepFlowerFace,
  flowerGeometry, flowerAttachmentLayout, getFlowerFrameLayout } from "../web/src/flower_renderer.js";
import { getCenteredSvgViewBox, centerPetalArtSvg } from "../web/src/petal_svg.js";
import { createCombatFeedback, formatCombatAmount, stepHealthTrail } from "../web/src/combat_feedback.js";
import { drawMovementGuide } from "../web/src/movement_guide.js";
import { parseServerMessage, ServerType } from "../web/src/protocol.js";

const ctxCalls = [];
const context = new Proxy({ globalAlpha: 1 }, {
  get(target, prop) { return prop in target ? target[prop] : (...args) => ctxCalls.push([prop, ...args]); },
  set(target, prop, value) { target[prop] = value; return true; },
});
const antennaeImage = Object.freeze({ complete: true, naturalWidth: 110, id: "antennae-svg" });
const bandageImage = Object.freeze({ complete: true, naturalWidth: 110, id: "bandage-svg" });
for (const radius of [4, 18, 32, 100, 256]) {
  const palette = getFlowerPalette();
  const face = stepFlowerFace(null, { lookX: 0, lookY: -1 }, 0);
  ctxCalls.length = 0;
  drawFlowerDisc(context, radius, palette);
  drawFlowerFace(context, radius, face, palette);
  drawFlowerAntennae(context, radius, antennaeImage);
  assert.ok(ctxCalls.flat().every(value => typeof value !== "number" || Number.isFinite(value)));
  assert.equal(ctxCalls.filter(call => call[0] === "clip").length, 1, "pupils must stay inside eye silhouettes");
  assert.ok(ctxCalls.some(call => call[0] === "ellipse" && call[1] === -flowerGeometry.eyeX));
  assert.equal(ctxCalls.filter(call => call[0] === "save").length, ctxCalls.filter(call => call[0] === "restore").length);
  assert.equal(Object.hasOwn(context, "filter"), false); // no per-flower filter effect
}
for (const radius of [4, 18, 25, 36, 100, 256]) {
  for (const [kind, draw, image] of [
    ["antennae", drawFlowerAntennae, antennaeImage],
    ["bandage", drawFlowerBandage, bandageImage],
  ]) {
    ctxCalls.length = 0;
    assert.equal(draw(context, radius, image), true);
    const size = radius * flowerAttachmentLayout[kind].size;
    const y = radius * flowerAttachmentLayout[kind].y - size / 2;
    assert.deepEqual(ctxCalls.filter(call => call[0] === "drawImage"),
      [["drawImage", image, -size / 2, y, size, size]], "equipment must use the SVG, not a procedural imitation");
    assert.ok(!ctxCalls.some(call => call[0] === "bezierCurveTo"));
    const clips = ctxCalls.filter(call => call[0] === "clip");
    assert.equal(clips.length, kind === "bandage" ? 1 : 0, "antennae must not be clipped to the head");
    if (kind === "bandage") {
      assert.ok(ctxCalls.some(call => call[0] === "arc" && call[3] === radius), "bandage must reach the outer rim");
      assert.ok(size / 1.08 > radius * 2, "visible bandage art must cover the body even after SVG padding");
    }
    assert.equal(ctxCalls.filter(call => call[0] === "save").length, ctxCalls.filter(call => call[0] === "restore").length);
    let cachedDraw;
    draw(context, radius, image, (ctx, ...args) => { assert.equal(ctx, context); cachedDraw = args; });
    assert.deepEqual(cachedDraw, [image, -size / 2, y, size, size], "world raster cache and portrait must share the same geometry");
  }
}
for (const draw of [drawFlowerAntennae, drawFlowerBandage]) {
  ctxCalls.length = 0;
  for (const image of [null, {}, { complete: false, naturalWidth: 110 },
    { complete: true, naturalWidth: 0 }, { ...antennaeImage, failed: true }])
    assert.equal(draw(context, 25, image), false);
  for (const radius of [0, -1, NaN, Infinity]) assert.equal(draw(context, radius, antennaeImage), false);
  assert.equal(ctxCalls.length, 0, "undecoded SVGs must not throw or flash a different fallback shape");
}
const faceAt = (fps, target) => {
  let value = stepFlowerFace(null, {}, 0);
  for (let frame = 0; frame < fps; frame++) value = stepFlowerFace(value, target, 1 / fps);
  return value;
};
for (const fps of [30, 60, 120]) {
  const face = faceAt(fps, { attacking: true, lookX: 1, lookY: 1 });
  assert.ok(Math.abs(face.mouth - flowerGeometry.attackMouth) < 1e-5);
  assert.ok(Math.abs(face.eyeX - 2.6) < 1e-5);
  assert.ok(Math.abs(face.brow - flowerGeometry.attackBrow) < 1e-5);
  assert.ok((flowerGeometry.mouthY - face.mouth) / 2 <= 2, "attack mouth should be a shallow frown");
  ctxCalls.length = 0;
  drawFlowerFace(context, 25, face, getFlowerPalette());
  const browTip = ctxCalls.find(call => call[0] === "lineTo" && call[1] === 0);
  assert.ok(browTip, "attack still has a brow, rather than silently losing the expression");
  const cutAtEye = browTip[2] - 6 * flowerGeometry.eyeX / (flowerGeometry.eyeX + flowerGeometry.eyeRX + 2);
  const eyeTop = flowerGeometry.eyeY - flowerGeometry.eyeRY - 0.5;
  assert.ok(cutAtEye > eyeTop && cutAtEye < eyeTop + 3,
    "attack must only trim the eye's upper edge, never reach the pupil centre");
  const dead = faceAt(fps, { dead: true });
  assert.ok(dead.dead && Math.abs(dead.tilt - 0.25) < 0.001);
}

for (const radius of [4, 8, 18, 32, 100, 256]) {
  for (const scale of [0.6, 1, 1.4]) {
    const remote = getFlowerFrameLayout(radius, scale);
    const self = getFlowerFrameLayout(radius, scale, true);
    assert.ok(remote.nameBaseline - remote.nameSize > radius, "name must clear the body");
    assert.ok(remote.barY > remote.nameBaseline, "bar must clear the name");
    assert.ok(remote.levelY > remote.barY + remote.barHeight, "level must clear the bar");
    assert.ok(self.barY < remote.barY && self.barY > radius, "local bar needs no empty name row");
    assert.ok(remote.width <= 80 * Math.max(0.5, Math.min(1.5, scale)));
  }
}
assert.equal(flowerAttachmentLayout.bandage.y, 0);
// The antenna SVG's art is about 47.14 x 40; fitting it without stretching
// places its tips above the head and its rounded roots inside the forehead.
const antennaeLayout = flowerAttachmentLayout.antennae;
const antennaeArtHeight = antennaeLayout.size / 1.08 * 40 / 47.14;
assert.ok(antennaeLayout.y + antennaeArtHeight / 2 > -0.78 && antennaeLayout.y + antennaeArtHeight / 2 < -0.70);
assert.ok(antennaeLayout.y - antennaeArtHeight / 2 > -1.8 && antennaeLayout.y - antennaeArtHeight / 2 < -1.65);
assert.equal(flowerAttachmentLayout.thirdEye.y, -14 / 25);
assert.equal(flowerAttachmentLayout.douli.angle, 0);
for (const box of [{ x: 12, y: 30, w: 80, h: 38 }, { x: 46, y: 38, w: 18, h: 32 }, { x: -45, y: 72, w: 40, h: 40 }]) {
  const [x, y, w, h] = getCenteredSvgViewBox(box).split(" ").map(Number);
  assert.equal(w, h);
  assert.ok(Math.abs(x + w / 2 - (box.x + box.w / 2)) < 0.002);
  assert.ok(Math.abs(y + h / 2 - (box.y + box.h / 2)) < 0.002);
  assert.ok(x < box.x && y < box.y && x + w > box.x + box.w && y + h > box.y + box.h);
}
assert.equal(getCenteredSvgViewBox({ x: 0, y: 0, w: 0, h: 12 }), null);
assert.equal(getCenteredSvgViewBox({ x: NaN, y: 0, w: 10, h: 12 }), null);
assert.equal(centerPetalArtSvg("<svg/>"), "<svg/>", "headless fallback keeps the original asset");
assert.notEqual(getFlowerPalette({ poisoned: true }).fill, getFlowerPalette().fill);
assert.notEqual(getFlowerPalette({ hit: 1 }).fill, getFlowerPalette().fill);
assert.notEqual(getFlowerPalette({ portrait: true }).fill, getFlowerPalette().fill);
const rgb = color => color.match(/rgb\((\d+), (\d+), (\d+)\)/).slice(1).map(Number);
const luminance = color => { const [r, g, b] = rgb(color); return r * 0.2126 + g * 0.7152 + b * 0.0722; };
assert.ok(luminance(getFlowerPalette({ psionic: true }).fill) < 100,
  "Relic psionic connection must render a dark flower");
assert.ok(luminance(getFlowerPalette({ psionic: true, antiHeal: true }).fill) < 100,
  "anti-heal must not wash out the Relic dark state");

let trail = stepHealthTrail(null, 1, 0);
trail = stepHealthTrail(trail, 0.4, 10);
assert.equal(trail.trail, 1);
trail = stepHealthTrail(trail, 0.4, 100);
assert.equal(trail.trail, 1);
for (let now = 200; now <= 1000; now += 20) trail = stepHealthTrail(trail, 0.4, now);
assert.ok(trail.trail >= 0.4 && trail.trail < 0.41);
trail = stepHealthTrail(trail, 0.9, 1010);
assert.equal(trail.trail, 0.9);

const labels = createCombatFeedback();
labels.add([{ targetId: 1, pos: { x: 5, y: 6 }, radius: 10, amount: 5, kind: 0 }], 0);
labels.add([{ targetId: 1, pos: { x: 5, y: 6 }, radius: 10, amount: 6, kind: 0 }], 50);
assert.equal(labels.count(), 1);
labels.draw(context, 100, pos => pos, text => assert.equal(text, "11"));
for (let i = 0; i < 200; i++) labels.add([{ targetId: i + 2, pos: { x: 0, y: 0 }, radius: 5, amount: 1, kind: 1 }], 200);
assert.equal(labels.count(), 96);
labels.draw(context, 900, pos => pos, () => {});
assert.equal(labels.count(), 0);
assert.equal(formatCombatAmount(NaN), "");
assert.equal(formatCombatAmount(42943), "42943");

const bytes = new Uint8Array(21), view = new DataView(bytes.buffer);
bytes[0] = ServerType.CombatFeedback; bytes[1] = 1;
view.setUint16(2, 42, true); view.setInt32(4, -128, true); view.setInt32(8, 320, true);
view.setUint16(12, 36, true); view.setFloat32(14, 12.5, true);
bytes[18] = 1; bytes[19] = 7; bytes[20] = 6;
const parsed = parseServerMessage(bytes);
assert.equal(parsed.hits[0].amount, 12.5); assert.equal(parsed.hits[0].kind, 1);
assert.deepEqual(parsed.hits[0].pos, { x: -2, y: 5 });
assert.equal(parsed.hits[0].sourceType, 7);
drawMovementGuide(context, { x: 0, y: 0 }, { x: 200, y: 60 }, 18);
console.log("PASS SVG antennae, full-rim bandage, shared attachment geometry, mild attack, nameplates, health trail and damage wire decoding");

// Pure visual-state regression checks: no browser, network or game process.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { petalCardVanishPose } from "../web/src/card_motion.js";
import { beginEntityAppearance, beginEntityExit, resumeEntityAppearance, stepEntityAppearance,
  getEntityAppearance, getDropCardTilt, stepEntityGait } from "../web/src/entity_motion.js";
import { getSpritePhase, getLimbMotionAmount, clearSpriteMotion, resetSpriteMotion } from "../web/src/sprite_motion.js";
import { getMobFrameLayout } from "../web/src/render_primitives.js";
import { clientRuntimeConfig } from "../web/src/client_config.js";

const near = (a, b, tolerance = 1e-6) => assert.ok(Math.abs(a - b) <= tolerance, `${a} != ${b}`);
const snapshot = Object.freeze({ entityId: 10, radius: 20, hpPercent: 1, pos: Object.freeze({ x: 5, y: 6 }) });
const entity = () => ({ snapshot, renderPos: { ...snapshot.pos }, dying: false, motionBlend: 0,
  snapshotAt: 1, snapshotVelocity: { x: 45, y: 0 } });

for (const fps of [30, 60, 120]) {
  const card = entity();
  beginEntityExit(card, 100, { card: true });
  near(getEntityAppearance(card).alpha, 1);
  near(getEntityAppearance(card).scale, 1);
  let previousScale = 1, previousRotation = 0, done = false;
  for (let frame = 0; frame < fps; frame++) {
    done = stepEntityAppearance(card, 1 / fps);
    const pose = getEntityAppearance(card);
    assert.ok(pose.scale <= previousScale && pose.rotation >= previousRotation);
    previousScale = pose.scale; previousRotation = pose.rotation;
    if (done) break;
  }
  assert.ok(done); near(previousScale, 0); near(previousRotation, Math.PI * 2);
  assert.equal(card.snapshot, snapshot);
  assert.deepEqual(card.renderPos, snapshot.pos);

  const mob = entity();
  beginEntityExit(mob, 1); // missing from snapshot is NOT a confirmed kill
  stepEntityAppearance(mob, 0.07);
  near(getEntityAppearance(mob).scale, 1);
  assert.ok(getEntityAppearance(mob).alpha < 1);
  const from = getEntityAppearance(mob);
  resumeEntityAppearance(mob);
  assert.deepEqual(getEntityAppearance(mob), from, "resume must not pop to opaque");
  for (let frame = 0; frame < fps; frame++) stepEntityAppearance(mob, 1 / fps);
  near(getEntityAppearance(mob).alpha, 1);
  assert.equal(mob.dying, false);
  assert.equal(mob.retirementTime, null);

  // Restore mob retirement growth even when the last visible snapshot had
  // HP left; retirement is still presentation, not a fabricated kill event.
  const retiringMob = entity();
  beginEntityExit(retiringMob, 1, { mob: true });
  near(retiringMob.visualMotion.duration, clientRuntimeConfig.deathFadeDuration);
  let retired = false;
  for (let frame = 0; frame < fps && !retired; frame++) retired = stepEntityAppearance(retiringMob, 1 / fps);
  assert.ok(retired);
  near(getEntityAppearance(retiringMob).scale, 1 + clientRuntimeConfig.deathScaleBoost);
  near(getEntityAppearance(retiringMob).alpha, 0);
  assert.equal(retiringMob.snapshot, snapshot);
}

const interrupted = entity();
beginEntityAppearance(interrupted, { card: true });
stepEntityAppearance(interrupted, 0.045);
const entering = getEntityAppearance(interrupted);
beginEntityExit(interrupted, 2, { card: true });
assert.deepEqual(getEntityAppearance(interrupted), entering);
stepEntityAppearance(interrupted, 0.11);
const exiting = getEntityAppearance(interrupted);
resumeEntityAppearance(interrupted);
assert.deepEqual(getEntityAppearance(interrupted), exiting);
stepEntityAppearance(interrupted, 0.12);
near(getEntityAppearance(interrupted).scale, 1);
near(getEntityAppearance(interrupted).rotation % (Math.PI * 2), 0);
const firstScene = entity();
beginEntityAppearance(firstScene, { immediate: true });
near(getEntityAppearance(firstScene).alpha, 1);
const killed = entity(); beginEntityExit(killed, 0, { defeated: true }); stepEntityAppearance(killed, 0.24);
near(getEntityAppearance(killed).scale, 1.12);
assert.equal(getDropCardTilt(10, 5), getDropCardTilt(10, 5));
assert.ok(Number.isFinite(petalCardVanishPose(NaN).scale));
assert.ok(petalCardVanishPose(0.1).alpha > 0.98, "the name/card should not instantly disappear");

// A long-running session changing speed must not change phase by elapsedTime
// times the speed delta. Duplicate render passes reuse the exact same pose.
resetSpriteMotion();
const start = getSpritePhase(8, "legs", 1000000, 5, 0.7);
const next = getSpritePhase(8, "legs", 1000000 + 1 / 60, 35, 0.7);
near(next, start + 20 / 60, 1e-5);
near(getSpritePhase(8, "legs", 1000000 + 1 / 60, 80, 0.7), next);
clearSpriteMotion(8);
near(getSpritePhase(8, "legs", 1000000 + 1, 20, 0.7), 0.7);
assert.equal(getLimbMotionAmount(0.119), 0);
for (const motion of [0.12, 0.18, 0.5, 1])
  assert.equal(getLimbMotionAmount(motion), motion, "legacy limb amplitude must not be damped a second time");

let reference;
for (const fps of [30, 60, 120]) {
  const mob = entity();
  for (let frame = 1; frame <= fps; frame++) {
    const now = frame / fps;
    mob.snapshotAt = now;
    stepEntityGait(mob, 1 / fps, now, 0.05);
  }
  assert.ok(mob.motionBlend > 0.99, "normal travel must restore the original full-strength gait");
  if (reference === undefined) reference = mob.motionBlend;
  else near(mob.motionBlend, reference);
  for (let frame = 1; frame <= fps; frame++) stepEntityGait(mob, 1 / fps, 1 + frame / fps, 0.05);
  assert.ok(mob.motionBlend < 0.003, "stale snapshots should stop the walk, not keep it running forever");
}

for (const radius of [5, 20, 100, 500, 2000]) {
  const mob = { ...entity(), snapshot: { ...snapshot, radius } };
  for (let frame = 1; frame <= 120; frame++) {
    mob.snapshotAt = frame / 120;
    stepEntityGait(mob, 1 / 120, frame / 120);
  }
  near(mob.motionBlend, reference);
  for (const uiScale of [0.6, 1, 1.4]) {
    const frame = getMobFrameLayout(radius, uiScale);
    assert.ok(frame.width >= radius * 2, "mob health bar cannot be narrower than its body");
    assert.ok(frame.barHeight <= 6 * uiScale);
    assert.ok(frame.textSize <= 12 * uiScale);
    assert.ok(frame.y > radius && frame.y - radius <= 12 * uiScale + 1e-6);
    assert.ok(frame.labelGap <= 4 * uiScale);
  }
}

// Style contracts that caused the reported regressions: press effects cannot
// replace a centring transform, and the codex must own the well's hue.
const css = readFileSync(new URL("../web/styles.css", import.meta.url), "utf8");
for (const selector of [".talent-node", ".talent-root"]) {
  const block = css.slice(css.indexOf(`${selector} {`)).split("}", 1)[0];
  assert.match(block, /translate:\s*-50% -50%/);
  assert.doesNotMatch(block, /transform:\s*translate/);
}
const codex = css.slice(css.indexOf(".game-panel.codex-panel {")).split("}", 1)[0];
for (const name of ["border", "background", "well"]) assert.match(codex, new RegExp(`--game-panel-${name}:`));
console.log("PASS full-width mob bars, restored gait/death strength at 30/60/120fps, preserved card lifecycle/phase continuity and UI anchors");

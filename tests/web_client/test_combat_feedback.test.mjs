import test from "node:test";
import assert from "node:assert/strict";

import { createCombatFeedback } from "../../web/src/combat_feedback.js";

function hit(amount, { targetId = 7, kind = 0, x = 10, y = 20 } = {}) {
  return {
    targetId,
    kind,
    amount,
    pos: { x, y },
    radius: 8,
    sourceType: 2,
    sourceRarity: 3,
  };
}

function draw(feedback, now) {
  const texts = [];
  const ctx = {
    globalAlpha: 1,
    save() {},
    restore() {},
  };
  feedback.draw(
    ctx,
    now,
    point => point,
    text => texts.push(text),
    true,
  );
  return texts;
}

test("damage numbers settle once per target and kind each second", () => {
  const feedback = createCombatFeedback();

  feedback.add([hit(3)], 0);
  feedback.add([hit(4)], 400);
  feedback.add([hit(5, { x: 30, y: 40 })], 999);

  assert.equal(feedback.pendingCount(), 1);
  assert.equal(feedback.count(), 0);
  assert.deepEqual(draw(feedback, 999), []);
  assert.deepEqual(draw(feedback, 1000), ["12"]);
  assert.equal(feedback.pendingCount(), 0);
  assert.equal(feedback.count(), 1);
});

test("different targets and feedback kinds use separate settlement buckets", () => {
  const feedback = createCombatFeedback();
  feedback.add([
    hit(2, { targetId: 1, kind: 0 }),
    hit(3, { targetId: 1, kind: 1 }),
    hit(4, { targetId: 2, kind: 0 }),
  ], 100);

  assert.equal(feedback.pendingCount(), 3);
  assert.deepEqual(draw(feedback, 1100).sort(), ["2", "3", "4"]);
  assert.equal(feedback.pendingCount(), 0);
});

test("disabling or clearing damage numbers removes pending settlements", () => {
  const feedback = createCombatFeedback();
  feedback.add([hit(8)], 0);
  feedback.draw({}, 500, point => point, () => {}, false);
  assert.equal(feedback.pendingCount(), 0);
  assert.equal(feedback.count(), 0);

  feedback.add([hit(9)], 600);
  feedback.clear();
  assert.equal(feedback.pendingCount(), 0);
  assert.equal(feedback.count(), 0);
});

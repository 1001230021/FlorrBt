// Offline checks for the deterministic screen-space rain field.
// No browser, server, socket or account I/O.
import assert from "node:assert/strict";
import { createRainField, getRainLean, rainConstants } from "../web/src/weather_rain.js";

function makeContext() {
  const calls = [];
  const values = {};
  const context = new Proxy(values, {
    get(target, key) {
      if (key in target) return target[key];
      return (...args) => calls.push([key, ...args]);
    },
    set(target, key, value) {
      target[key] = value;
      calls.push(["set", key, value]);
      return true;
    },
  });
  return { context, calls };
}

const finiteCalls = calls => calls.flat().every(value => typeof value !== "number" || Number.isFinite(value));
const segments = calls => calls.filter(call => call[0] === "moveTo").map((move, index) => {
  const line = calls.filter(call => call[0] === "lineTo")[index];
  return { x: move[1], y: move[2], dx: line[1] - move[1], dy: line[2] - move[2] };
});

assert.ok(rainConstants.defaultDropCount >= 600);
assert.ok(getRainLean(0, 0) > 0, "calm rain keeps a small consistent diagonal");
assert.ok(getRainLean(90, 18) > getRainLean(0, 0));
assert.ok(getRainLean(270, 18) < getRainLean(0, 0));
assert.ok(getRainLean(0, 999) <= 0.34);

const sparse = createRainField({ seed: 123, maxDrops: 180 });
const sparseDraw = makeContext();
sparse.step(1 / 60);
const sparseCount = sparse.draw(sparseDraw.context, 800, 600, 0.25, 0, 0);
assert.equal(sparseCount, Math.floor(180 * Math.pow(0.25, 0.72)));
assert.ok(segments(sparseDraw.calls).length > 0);
assert.ok(finiteCalls(sparseDraw.calls));

const full = createRainField({ seed: 123, maxDrops: 180 });
const fullDraw = makeContext();
full.step(1 / 60);
const fullCount = full.draw(fullDraw.context, 800, 600, 1, 0, 0);
assert.equal(fullCount, 180);
assert.ok(fullCount > sparseCount);
assert.ok(segments(fullDraw.calls).every(segment => {
  const ratio = segment.dx / segment.dy;
  return segment.dy > 0 && ratio > 0.1 && ratio < 0.18;
}), "all strokes share one fine diagonal instead of fanning by screen position");

const windDraw = makeContext();
const windy = createRainField({ seed: 123, maxDrops: 180 });
windy.step(1 / 60);
windy.draw(windDraw.context, 800, 600, 1, 270, 18);
assert.ok(segments(windDraw.calls).every(segment => segment.dx < 0), "wind changes the whole sheet together");

const oneStep = createRainField({ seed: 88, maxDrops: 96 });
const twoSteps = createRainField({ seed: 88, maxDrops: 96 });
oneStep.step(0.1); twoSteps.step(0.05); twoSteps.step(0.05);
const oneDraw = makeContext(); const twoDraw = makeContext();
oneStep.draw(oneDraw.context, 640, 480, 0.8, 0, 4);
twoSteps.draw(twoDraw.context, 640, 480, 0.8, 0, 4);
const oneSegments = segments(oneDraw.calls), twoSegments = segments(twoDraw.calls);
assert.equal(oneSegments.length, twoSegments.length);
for (let i = 0; i < Math.min(10, oneSegments.length); i += 1) {
  assert.ok(Math.abs(oneSegments[i].x - twoSegments[i].x) < 1e-9);
  assert.ok(Math.abs(oneSegments[i].y - twoSegments[i].y) < 1e-9);
}

full.reset();
const resetDraw = makeContext();
full.draw(resetDraw.context, 800, 600, 1, 0, 0);
assert.ok(segments(resetDraw.calls).length > 0);
assert.ok(finiteCalls(resetDraw.calls));
console.log("PASS deterministic uniform rain field, intensity density, stable diagonal, wind response, frame-rate-independent motion and reset");

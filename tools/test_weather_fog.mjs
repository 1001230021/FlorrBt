// Offline checks for block fog geometry and bounded logical rendering.
import assert from "node:assert/strict";
import { createBlockFogField, fogConstants } from "../web/src/weather_fog.js";

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

function makeCanvas() {
  const draw = makeContext();
  const canvas = {
    width: 0,
    height: 0,
    getContext: () => draw.context,
  };
  return { canvas, draw };
}

const main = makeContext();
const logical = makeCanvas();
const fog = createBlockFogField({ canvasFactory: () => logical.canvas });
const painted = fog.draw(main.context, 1920, 1080, 1, { x: 960, y: 540 });
assert.ok(painted > 0);
assert.deepEqual(fog.dimensions(), { columns: 44, rows: 25 });
assert.equal(logical.canvas.width, 44);
assert.equal(logical.canvas.height, 25);
assert.ok(main.calls.some(call => call[0] === "drawImage"));
assert.ok(logical.draw.calls.filter(call => call[0] === "fillRect").length === painted);

const near = makeCanvas();
const nearFog = createBlockFogField({ canvasFactory: () => near.canvas });
nearFog.draw(near.draw.context, 800, 600, 1, { x: 400, y: 300 });
const alphaValue = call => Number(call[2].match(/, ([0-9.]+)\)$/)?.[1] || 0);
const nearStyles = near.draw.calls.filter(call => call[0] === "set" && call[1] === "fillStyle");
assert.ok(nearStyles.length > 0);
assert.match(nearStyles[0][2], /rgba\(137, 149, 148,/);
const far = makeCanvas();
const farFog = createBlockFogField({ canvasFactory: () => far.canvas });
farFog.draw(far.draw.context, 800, 600, 1, { x: -500, y: -500 });
const farStyles = far.draw.calls.filter(call => call[0] === "set" && call[1] === "fillStyle");
assert.ok(Math.min(...nearStyles.map(alphaValue)) < Math.min(...farStyles.map(alphaValue)),
  "fog opacity changes with distance from the player");

const sparse = makeCanvas();
const sparseFog = createBlockFogField({ canvasFactory: () => sparse.canvas });
assert.equal(sparseFog.draw(sparse.draw.context, 1920, 1080, 0), 0);
assert.deepEqual(sparseFog.dimensions(), { columns: 0, rows: 0 });
assert.equal(fogConstants.maxColumns, 48);
assert.equal(fogConstants.maxRows, 32);
fog.reset();
console.log("PASS block fog uses bounded logical pixels, nearest-neighbour scaling, player-distance fade and zero-intensity bypass");

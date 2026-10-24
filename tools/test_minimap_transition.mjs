import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { sampleMinimapTransition } from "../web/src/minimap_transition.js";

// Exercise the real toggle/draw coordination, including rapid reversal and
// hit-testing. The raster/background/markers receive the same single pose.
const source = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
function method(name) {
  const start = source.indexOf(`function ${name}(`);
  assert.ok(start >= 0);
  const next = source.indexOf("\nfunction ", start + 1);
  return source.slice(start, next < 0 ? source.length : next);
}
const local = { x: 800, y: 18, size: 100 }, full = { x: 600, y: 18, size: 300 };
const bounds = { local: { left: 100, top: 200, width: 7000, height: 7000 },
  full: { left: 0, top: 0, width: 131072, height: 131072 } };
for (const fps of [30, 60, 120]) {
  let now = 0;
  const calls = [];
  const state = { minimapMode: "local", map: { tileWidth: 512, tileHeight: 512 } };
  const scope = { state, sampleMinimapTransition, performance: { now: () => now }, requestRedraw() {},
    minimapLayout: () => state.minimapMode === "full" ? full : local,
    minimapWorldBounds: () => bounds[state.minimapMode],
    drawMinimapLayer: (map, view) => calls.push(view) };
  const api = vm.runInNewContext(`let minimapTransition = null, minimapHitRect = null;
    const minimapTransitionDurationMs = 240;
    ${["drawMinimap", "currentMinimapView", "toggleMinimapMode", "isMinimapPoint"].map(method).join("\n")}
    ;({drawMinimap, toggleMinimapMode, isMinimapPoint})`, scope);
  api.drawMinimap(now); api.toggleMinimapMode(); api.drawMinimap(now);
  assert.equal(calls.at(-1).layout.size, 100);
  let lastSize = 100;
  for (let frame = 1; frame <= Math.ceil(fps * 0.12); frame++) {
    now = Math.min(120, frame * 1000 / fps); api.drawMinimap(now);
    const view = calls.at(-1);
    assert.ok(view.layout.size >= lastSize && view.layout.size < 300);
    assert.ok(Math.abs(view.layout.x + view.layout.size - 900) < 1e-8);
    assert.ok(view.bounds.width >= 7000 && view.bounds.width < 131072);
    assert.ok(api.isMinimapPoint({ x: view.layout.x + 1, y: 20 }));
    assert.ok(!api.isMinimapPoint({ x: view.layout.x - 1, y: 20 }));
    lastSize = view.layout.size;
  }
  const halfway = calls.at(-1);
  api.toggleMinimapMode(); api.drawMinimap(now);
  assert.deepEqual(calls.at(-1), halfway, "reverse from current pose without jumping to an endpoint");
  now += 240; api.drawMinimap(now);
  assert.equal(calls.at(-1).layout, local);
  api.toggleMinimapMode(); now += 1000; api.drawMinimap(now);
  assert.equal(calls.at(-1).layout, full, "tab stalls must settle exactly on the target");
  api.toggleMinimapMode(); state.map = { tileWidth: 512, tileHeight: 512 }; api.drawMinimap(now);
  assert.equal(calls.at(-1).layout, local, "new maps cannot inherit the previous scene transition");
  state.map = null; api.drawMinimap(now); assert.equal(api.isMinimapPoint({ x: 850, y: 20 }), false);
}
console.log("PASS minimap continuous size/view interpolation, shared hit area, 30/60/120fps, reversal and scene reset");

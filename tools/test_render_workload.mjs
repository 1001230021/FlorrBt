import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { RasterBudget } from "../web/src/raster_budget.js";
import { getRenderFrameTiming } from "../web/src/render_timing.js";
import { isDebugEntityInView } from "../web/src/dev_debug_model.js";
import * as ids from "../web/src/game_ids.js";
import { isDropEntity, isPetalEntity, petalTypeFromEntity } from "../web/src/protocol.js";

const code = file => readFileSync(new URL(`../web/src/${file}`, import.meta.url), "utf8");
const stripImports = source => source.replace(/^import[\s\S]*?from "[^"]+";\r?\n/gm, "").replace(/^export /gm, "");
const near = (a, b) => assert.ok(Math.abs(a - b) < 1e-8, `${a} != ${b}`);

// Per-frame sharing eliminates thousands of LRU relinks for identical parts.
const budget = new RasterBudget(4096), key = {};
budget.store(key, 100, () => {}); budget.beginFrame(1);
let relinks = 0;
const originalSet = budget.entries.set.bind(budget.entries);
budget.entries.set = (...args) => { relinks++; return originalSet(...args); };
for (let i = 0; i < 10000; i++) assert.ok(budget.touch(key));
assert.equal(relinks, 1);
budget.beginFrame(2); budget.touch(key); assert.equal(relinks, 2);

// Camera memoization also keys changes made by the scoped debug renderer.
const state = { canvasWidth: 1920, canvasHeight: 1080, viewRadius: 2000,
  renderViewZoomPercent: 100, viewZoomPercent: 100, deathViewFactor: 1,
  camera: { x: 20, y: 30 }, screenShake: { x: 0, y: 0 } };
let squareRoots = 0;
const math = Object.create(Math); math.hypot = (...args) => { squareRoots++; return Math.hypot(...args); };
const cameraSource = stripImports(code("camera.js").replace(/^export\s*\{[\s\S]*?from "[^"]+";\r?\n/gm, ""));
const camera = vm.runInNewContext(`${cameraSource};({worldScale,worldToScreen,screenToWorld})`, {
  state, ui: {}, Math: math, normalizeViewZoomPercent: value => Math.max(25, Math.min(100, value || 100)),
  clamp: (value, a, b) => Math.max(a, Math.min(b, value)),
});
const normalScale = camera.worldScale();
for (let i = 0; i < 10000; i++) camera.worldToScreen({ x: i, y: 50 });
assert.equal(squareRoots, 1, "one world-scale computation for unchanged camera inputs");
state.renderViewZoomPercent = 50;
near(camera.worldScale(), normalScale * 2);
state.canvasWidth = 800; state.canvasHeight = 600; state.viewRadius = 500; state.renderViewZoomPercent = 100;
near(camera.worldScale(), 1);
state.camera = { x: 400, y: 200 }; state.screenShake = { x: 5, y: -8 };
const screen = camera.worldToScreen({ x: 10, y: 15 });
const world = camera.screenToWorld(screen); near(world.x, 10); near(world.y, 15);
state.canvasWidth = 1920; state.canvasHeight = 1080; state.viewRadius = 2000;
near(camera.worldScale(), normalScale);

// Thousands of entities, including subpixel entities, reuse pass arrays but
// retain every visible body and do not leak removed records between frames.
const entities = new Map(Array.from({ length: 8000 }, (_, i) => [i + 1,
  { snapshot: { entityId: i + 1, entityType: ids.beeType, radius: 0.01, states: [] } }]));
const sceneState = { entities, ownerEntityId: -1 };
const scene = vm.runInNewContext(`${stripImports(code("scene_passes.js"))};({collectSceneRenderPasses,createSceneRenderPasses})`, {
  ...ids, state: sceneState, isDropEntity, isPetalEntity, petalTypeFromEntity,
});
const passes = scene.createSceneRenderPasses(), worldPass = passes.world;
scene.collectSceneRenderPasses({ scale: 0.001, isEntityInRenderView: () => true, passes });
assert.equal(passes.world.length, 8000);
sceneState.entities.delete(1);
scene.collectSceneRenderPasses({ scale: 0.001, isEntityInRenderView: () => true, passes });
assert.equal(passes.world, worldPass); assert.equal(passes.world.length, 7999);
assert.ok(!passes.world.some(entity => entity.snapshot.entityId === 1));
assert.equal(isDebugEntityInView({ radius: 0.001 }, { x: 0, y: 0 }, { x: 0, y: 0, scale: 0.001 }, { width: 800, height: 600 }), true);
assert.equal(isDebugEntityInView({ radius: 1000 }, { x: 4500, y: 0 }, { x: 0, y: 0, scale: 0.1 }, { width: 800, height: 600 }), true);
assert.equal(isDebugEntityInView({ radius: 10 }, { x: 500000, y: 0 }, { x: 0, y: 0, scale: 1 }, { width: 800, height: 600 }), false);

// A 120ms hitch used to execute every remote entity up to eight times. The
// owner still gets all physics substeps, remote analytic motion advances once.
let ownerCalls = 0, remoteCalls = 0, ownerElapsed = 0, remoteElapsed = 0;
const owner = { snapshot: { entityId: 0, entityType: ids.playerFlowerType, flags: 0 }, renderPos: { x: 0, y: 0 } };
const simState = { ...sceneState, entities: new Map([[0, owner], ...entities]), ownerEntityId: 0, camera: {} };
const simSource = code("sim_entities.js");
const simUpdate = simSource.slice(simSource.indexOf("export function updateRenderPositions("), simSource.indexOf("function lockTrapperRenderPositionsToMounts("));
const update = vm.runInNewContext(`${simUpdate.replace(/^export /, "")};updateRenderPositions`, {
  ...ids, state: simState, performance: { now: () => 1000 }, flowerFaceTypes: new Set(),
  stepDeathViewFactor: value => value, stepOwnerPrediction(_, dt) { ownerCalls++; ownerElapsed += dt; },
  stepEntityAppearance: () => false, stepEntityGait() {}, hurtFlashDuration: 0.1, serverFixedDt: 0.05,
  stepEntityRenderToSnapshot(_, dt, isOwner, now) { assert.equal(now, 1); remoteCalls++; remoteElapsed += dt; },
  lockTrapperRenderPositionsToMounts() {},
});
const timing = getRenderFrameTiming(0.12);
update(timing.dt, timing.steps);
assert.equal(ownerCalls, timing.steps); near(ownerElapsed, 0.12);
assert.equal(remoteCalls, 7999); near(remoteElapsed / remoteCalls, 0.12);

// Exponential remote interpolation has the same endpoint as repeated small
// steps at a fixed snapshot/time; combining steps does not drop elapsed time.
const remoteSource = simSource.slice(simSource.indexOf("function stepEntityRenderToSnapshot("), simSource.indexOf("function ownerCollidesWithWalls("));
const remoteStep = vm.runInNewContext(`${remoteSource};stepEntityRenderToSnapshot`, {
  ...ids, isPetalEntity, petalTypeFromEntity,
  smoothFactor: (rate, dt) => -Math.expm1(-rate * dt),
  lerpAngle: (a, b, k) => a + (b - a) * k,
  renderSnapBaseDistance: 1000, ownerRenderSnapBaseDistance: 1000,
  petalExtrapolateMaxSeconds: 0.08, entityExtrapolateMaxSeconds: 0.08,
  petalExtrapolateMaxDistance: 80, entityExtrapolateMaxDistance: 80,
  petalRenderLerpRate: 20, entityRenderLerpRate: 20, ownerRenderLerpRate: 20,
  petalAngleLerpRate: 20, entityAngleLerpRate: 20, ownerAngleLerpRate: 20, serverFixedDt: 0.05,
});
const seed = { snapshot: { pos: { x: 100, y: 50 }, angle: 0.9, radius: 20, entityType: ids.beeType },
  renderPos: { x: 0, y: 0 }, renderAngle: 0, snapshotAt: 1, snapshotVelocity: { x: 4, y: 2 } };
const combined = structuredClone(seed), subdivided = structuredClone(seed);
remoteStep(combined, 0.12, false, 1.05);
for (let i = 0; i < 8; i++) remoteStep(subdivided, 0.015, false, 1.05);
near(combined.renderPos.x, subdivided.renderPos.x); near(combined.renderPos.y, subdivided.renderPos.y);
near(combined.renderAngle, subdivided.renderAngle);
console.log("PASS 10k sprite touches -> 1 LRU update, 10k transforms -> 1 scale calculation, retained 8k-entity passes, no tiny-entity LOD, debug culling and single-pass remote hitch recovery");

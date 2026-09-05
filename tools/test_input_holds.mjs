// Regression tests for the input hold set in web/src/bindings.js.
//
// The bug these guard against: movement used to be a signed accumulator, so a
// single swallowed keyup ("down" without its "up") latched the flower into
// walking forever. Every scenario below ends with an assertion that the
// aggregate input is neutral again.
import assert from "node:assert/strict";

const store = new Map();
globalThis.localStorage = {
  getItem: (key) => (store.has(key) ? store.get(key) : null),
  setItem: (key, value) => store.set(key, String(value)),
  removeItem: (key) => store.delete(key),
};

const bindings = await import("../web/src/bindings.js");
const {
  defaultBindings,
  executeBinding,
  initBindingContext,
  inputKeyCandidates,
  loadBindings,
  normalizeInputKey,
  deviceForInputKey,
  pruneStaleInputHolds,
  releaseInputHolds,
  resetBindings,
  listBindings,
  parseBindArgs,
  bindKey,
} = bindings;

const ctx = {
  move: { x: 0, y: 0 },
  attacking: false,
  defending: false,
  setMoveVector(vector) {
    this.move = { x: vector.x, y: vector.y };
  },
  setAttacking(active) {
    this.attacking = active;
  },
  setDefending(active) {
    this.defending = active;
  },
  selectSlot() {},
  selectLoadoutPreset() {
    this.loadoutPicked = true;
  },
};
initBindingContext(ctx);

const table = loadBindings();
let clock = 1000;
const down = (key, options = {}) =>
  executeBinding(table, deviceForInputKey(key), key, "down", {
    now: (clock += 20),
    ...options,
  });
const up = (key, options = {}) =>
  executeBinding(table, deviceForInputKey(key), key, "up", {
    now: (clock += 20),
    ...options,
  });
const neutral = () =>
  ctx.move.x === 0 && ctx.move.y === 0 && !ctx.attacking && !ctx.defending;

let failures = 0;
function check(name, fn) {
  releaseInputHolds();
  try {
    fn();
    assert.ok(neutral(), `${name}: input did not return to neutral`);
    console.log(`PASS  ${name}`);
  } catch (error) {
    failures += 1;
    console.log(`FAIL  ${name}: ${error.message}`);
  }
}

check("press and release moves then stops", () => {
  down("d");
  assert.deepEqual(ctx.move, { x: 1, y: 0 });
  up("d");
  assert.deepEqual(ctx.move, { x: 0, y: 0 });
});

check("auto-repeat does not stack", () => {
  down("a");
  for (let i = 0; i < 30; i += 1) down("a", { repeat: true });
  assert.deepEqual(ctx.move, { x: -1, y: 0 }, "repeat must not accumulate");
  up("a");
  assert.deepEqual(ctx.move, { x: 0, y: 0 });
});

check("duplicated down without up is idempotent", () => {
  down("w");
  down("w");
  down("w");
  assert.deepEqual(ctx.move, { x: 0, y: -1 });
  up("w");
});

check("release that was never pressed is a no-op", () => {
  up("s");
  assert.deepEqual(ctx.move, { x: 0, y: 0 });
  up("s");
  down("s");
  assert.deepEqual(ctx.move, { x: 0, y: 1 });
  up("s");
});

check("keyup swallowed while typing is healed by release-all", () => {
  down("d");
  assert.deepEqual(ctx.move, { x: 1, y: 0 });
  // Focus moves into a text field: the keyup never reaches the game.
  releaseInputHolds({ device: "keyboard" });
  assert.deepEqual(ctx.move, { x: 0, y: 0 }, "typing must drop keyboard holds");
  // The eventual keyup must not push the vector negative.
  up("d");
  assert.deepEqual(ctx.move, { x: 0, y: 0 }, "late keyup must not reverse");
});

check("blur mid-hold then late keyup does not reverse movement", () => {
  down("a");
  releaseInputHolds();
  up("a");
  assert.deepEqual(ctx.move, { x: 0, y: 0 });
});

check("stale repeating hold is pruned when keyup is lost", () => {
  down("d");
  down("d", { repeat: true });
  assert.deepEqual(ctx.move, { x: 1, y: 0 });
  // Repeats stop arriving because the release was dropped on the floor.
  assert.equal(pruneStaleInputHolds(clock + 400), false, "must not prune early");
  assert.deepEqual(ctx.move, { x: 1, y: 0 });
  assert.equal(pruneStaleInputHolds(clock + 3000), true, "must prune when silent");
  assert.deepEqual(ctx.move, { x: 0, y: 0 });
});

check("a hold that never repeated is never pruned", () => {
  down("d");
  assert.equal(pruneStaleInputHolds(clock + 60000), false);
  assert.deepEqual(ctx.move, { x: 1, y: 0 });
  up("d");
});

check("opposite keys cancel and both release cleanly", () => {
  down("a");
  down("d");
  assert.deepEqual(ctx.move, { x: 0, y: 0 });
  up("a");
  assert.deepEqual(ctx.move, { x: 1, y: 0 });
  up("d");
});

check("arrow keys drive movement through bindings", () => {
  down("arrowleft");
  assert.deepEqual(ctx.move, { x: -1, y: 0 });
  up("arrowleft");
});

check("attack stacks across mouse and keyboard", () => {
  executeBinding(table, "mouse", "mouse1", "down", { now: (clock += 20) });
  down("space");
  assert.equal(ctx.attacking, true);
  // Releasing one source must not clear the other.
  down("space", { repeat: true });
  executeBinding(table, "mouse", "mouse1", "up", { now: (clock += 20) });
  assert.equal(ctx.attacking, true, "keyboard attack must survive mouseup");
  up("space");
  assert.equal(ctx.attacking, false);
});

check("commands are skipped while typing but releases still run", () => {
  down("d", { typing: true });
  assert.deepEqual(ctx.move, { x: 0, y: 0 }, "typing must not move");
  up("d");
  down("d");
  assert.deepEqual(ctx.move, { x: 1, y: 0 });
  up("d", { typing: true });
  assert.deepEqual(ctx.move, { x: 0, y: 0 }, "release must run while typing");
});

check("loadout modifier reroutes select_slot", () => {
  ctx.loadoutPicked = false;
  down("3");
  assert.equal(ctx.loadoutPicked, false);
  down("l");
  down("4");
  assert.equal(ctx.loadoutPicked, true, "L + number must pick a preset");
  up("l");
  up("4");
  up("3");
});

check("move_toward angle contributes a unit vector", () => {
  bindKey(table, "keyboard", "k", ["move_toward 180"]);
  down("k");
  assert.ok(Math.abs(ctx.move.x + 1) < 1e-9 && Math.abs(ctx.move.y) < 1e-9);
  up("k");
});

// ---------------------------------------------------------- naming + parsing

function expect(name, actual, wanted) {
  try {
    assert.deepEqual(actual, wanted);
    console.log(`PASS  ${name}`);
  } catch (error) {
    failures += 1;
    console.log(`FAIL  ${name}: ${error.message}`);
  }
}

expect("esc alias", normalizeInputKey("ESC"), "escape");
expect("ctrl alias", normalizeInputKey("Ctrl"), "control");
expect("space from text", normalizeInputKey(" "), "space");
expect("m1 alias", normalizeInputKey("M1"), "mouse1");
expect("arrow alias", normalizeInputKey("LeftArrow"), "arrowleft");
expect("device inference (mouse)", deviceForInputKey("wheelup"), "mouse");
expect("device inference (keyboard)", deviceForInputKey("f5"), "keyboard");
expect(
  "sided modifier candidates",
  inputKeyCandidates({ code: "ShiftLeft", key: "Shift" }),
  ["lshift", "shift"],
);
expect("numpad from code", inputKeyCandidates({ code: "Numpad7", key: "7" }), [
  "numpad7",
]);
expect("space from code", inputKeyCandidates({ code: "Space", key: " " }), [
  "space",
]);
expect("bind args with chain", parseBindArgs(["A", "move_toward", "90", "+", "attack"]), {
  key: "a",
  commands: ["move_toward 90", "attack"],
});
expect("bind args key only", parseBindArgs(["Esc"]), {
  key: "escape",
  commands: [],
});

// A side-specific bind must win over the generic one.
bindKey(table, "keyboard", "rshift", ["attack"]);
releaseInputHolds();
executeBinding(table, "keyboard", ["rshift", "shift"], "down", { now: (clock += 20) });
expect("sided bind wins", [ctx.attacking, ctx.defending], [true, false]);
executeBinding(table, "keyboard", ["rshift", "shift"], "up", { now: (clock += 20) });
executeBinding(table, "keyboard", ["lshift", "shift"], "down", { now: (clock += 20) });
expect("generic fallback", [ctx.attacking, ctx.defending], [false, true]);
executeBinding(table, "keyboard", ["lshift", "shift"], "up", { now: (clock += 20) });

// reset must drop overrides and restore the shipped table.
resetBindings(table);
expect("reset drops overrides", table.keyboard.k, undefined);
expect("reset restores defaults", table.keyboard.a, defaultBindings.keyboard.a);
expect(
  "reset clears storage",
  store.has("florrbt.web.bindings"),
  false,
);
expect(
  "listBindings reports escape",
  listBindings(table, "keyboard").some((entry) => entry.key === "escape"),
  true,
);

console.log(failures === 0 ? "\nALL PASS" : `\n${failures} FAILED`);
process.exit(failures === 0 ? 0 : 1);

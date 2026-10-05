// Exercise the production input flusher and wire encoder without a browser.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { packInputFrame, clamp } from "../web/src/protocol.js";
import { quantizeMoveIntent } from "../web/src/movement_input.js";

const game = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
const source = game.slice(game.indexOf("function flushInput("), game.indexOf("export function isOwnerDead("));
const state = { connected: true, authenticated: true, devDebugOpen: false,
  attacking: false, defending: false, holdAttack: false, holdDefense: false,
  sendTimer: 0, inputSequence: 0,
  lastInput: { x: 0, y: 0, attacking: false, defending: false, digging: false } };
const touch = { attack: false, defend: false };
const packets = [];
const flush = vm.runInNewContext(`${source};flushInput`, {
  state, ui: { mobileControls: { isAttacking: () => touch.attack, isDefending: () => touch.defend } },
  performance: { now: () => 0 }, pruneStaleInputHolds() {}, releaseInputHolds() {}, isTyping: () => false,
  isOwnerDead: () => false, readMoveInput: () => ({ x: 0, y: 0 }), quantizeMoveIntent,
  clamp, packInputFrame, packetInterval: .05, estimateInputTargetTick: () => 1,
  sendBytes(bytes) { packets.push(bytes); return true; },
});
for (const input of ["keyboard", "toggle", "touch"]) {
  const set = (attack, defend) => {
    state.attacking = input === "keyboard" && attack;
    state.defending = input === "keyboard" && defend;
    state.holdAttack = input === "toggle" && attack;
    state.holdDefense = input === "toggle" && defend;
    touch.attack = input === "touch" && attack;
    touch.defend = input === "touch" && defend;
  };
  for (const [attack, defend] of [[true, true], [true, false], [false, true], [false, false]]) {
    set(attack, defend); flush(.05);
    assert.equal(state.lastInput.attacking, attack, input);
    assert.equal(state.lastInput.defending, defend, input);
    assert.deepEqual(Array.from(packets.at(-1)), Array.from(packInputFrame(
      state.inputSequence - 1, 1, 0, 0, attack, defend, false)), "both holds reach the wire");
  }
}
state.attacking = state.defending = true;
flush(.05);
const before = packets.length;
for (let frame = 0; frame < 120; frame++) flush(1 / 120);
assert.ok(packets.length - before <= 21, "holding both does not create a packet/repeat storm");
state.devDebugOpen = true; flush(.05);
assert.equal(state.lastInput.attacking, false); assert.equal(state.lastInput.defending, false);

const controller = readFileSync(new URL("../src/Server/Game/controllers/player_controller.cpp", import.meta.url), "utf8");
assert.match(controller, /SetAttacking\(\*op.is_attacking\)/);
assert.match(controller, /SetDefending\(\*op.is_defending\)/);
assert.doesNotMatch(controller, /IsAttacking\(\).*SetDefending\(false\)/);
const petals = readFileSync(new URL("../src/Server/Game/entities/petals/petals_behavior.h", import.meta.url), "utf8");
const bubble = petals.slice(petals.indexOf("class CBubbleBehavior"), petals.indexOf("inline float SignedAngleBetween"));
assert.match(bubble, /flower->m_defending && owner->m_lifetime >= BubbleReload\(rarity\)/);
assert.match(bubble, /if \(moon\) moon->m_vel \+=/);
const pheromone = petals.slice(petals.indexOf("class CEouPheromoneBehavior"), petals.indexOf("class CWaxBehavior"));
assert.match(pheromone, /const bool defend = !flower->m_attacking && flower->m_defending/,
  "EOU still selects one throw action instead of running both");
console.log("PASS Bubble attack+defense wire flags, keyboard/toggle/touch release, debug isolation and bounded packet rate");

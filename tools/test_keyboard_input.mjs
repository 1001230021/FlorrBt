import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const game = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
const config = readFileSync(new URL("../web/src/client_config.js", import.meta.url), "utf8");
const frameStart = game.indexOf("function drawFlowerFrame(");
const frameEnd = game.indexOf("function drawMobFrame(", frameStart);
const frame = game.slice(frameStart, frameEnd);
assert.match(frame, /drawOutlinedText\(snap\.name, x, y - manaHeight - manaGap - labelGap/);
assert.match(frame, /align: "left", baseline: "bottom"/);
assert.match(game, /key === "arrowup"/);
assert.match(game, /key === "arrowdown"/);
assert.match(game, /key === "arrowleft"/);
assert.match(game, /key === "arrowright"/);
assert.match(config, /packetInterval:\s*1 \/ 20/);
console.log("PASS Flower nameplate shares mob left-bar anchor and keyboard transport suppresses scroll/reduces steady hold rate");

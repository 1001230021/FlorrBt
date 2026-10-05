// Offline only: fake sockets/DOM, no server/database/browser is started.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { DebugFrameDecoder, packDebugMessage, DEBUG_SELECTION_LIMIT } from "../web/src/dev_debug_protocol.js";
import * as model from "../web/src/dev_debug_model.js";
import { stepFlowerFace } from "../web/src/flower_renderer.js";
import { createBurrowParticles } from "../web/src/burrow_particles.js";
import { flagAttacking, flagDefending, flagDead, playerFlowerType, normalFlowerType, mechaFlowerType, dummyType, titanType } from "../web/src/game_ids.js";
import { parseServerMessage, ServerType } from "../web/src/protocol.js";

const frame = value => {
  const bytes = new TextEncoder().encode(JSON.stringify(value)), packet = new Uint8Array(bytes.length + 6);
  packet[0] = 0xd0; packet[1] = 1; new DataView(packet.buffer).setUint32(2, bytes.length, true); packet.set(bytes, 6);
  return packet;
};
const sample = frame({ kind: "logs", lines: ["世界 · test"] });
for (let split = 0; split <= sample.length; split++) {
  const decoder = new DebugFrameDecoder();
  const result = [...decoder.push(sample.subarray(0, split)), ...decoder.push(sample.subarray(split))];
  assert.deepEqual(result, [{ kind: "logs", lines: ["世界 · test"] }]);
}
const decoder = new DebugFrameDecoder();
assert.equal(decoder.push(new Uint8Array([...sample, ...sample])).length, 2);
for (const bytes of [Uint8Array.from([0xfb]), Uint8Array.from([0xd0, 2]), Uint8Array.from([0xd0, 1, 0xff, 0xff, 0xff, 0xff])])
  assert.throws(() => new DebugFrameDecoder().push(bytes));
assert.throws(() => packDebugMessage({ op: "console", line: "x".repeat(32768) }));
assert.throws(() => new DebugFrameDecoder().push(Uint8Array.from([0xd0, 1, 0, 0, 0, 0])));
const ticket = new Uint8Array(65); ticket[0] = ServerType.DebugTicket; ticket.fill(97, 1);
assert.equal(parseServerMessage(ticket).ticket, "a".repeat(64));
assert.equal(parseServerMessage(ticket.subarray(0, 64)).type, 255);

const camera = { x: 100, y: 80, scale: 0.5 }, size = { width: 800, height: 500 }, point = { x: 231, y: 178 };
const worldPoint = model.pointToDebugWorld(point, camera, size);
assert.deepEqual(model.pointToDebugScreen(worldPoint, camera, size), point);
const zoom = model.zoomDebugCamera(camera, point, size, 2);
assert.deepEqual(model.pointToDebugWorld(point, zoom, size), worldPoint, "wheel zoom must anchor under cursor");
const entities = [
  { entityId: 1, generation: "10", pos: { x: 10, y: 10 }, radius: 100, family: 1 },
  { entityId: 2, generation: "11", pos: { x: 10, y: 10 }, radius: 5, family: 1 },
  { entityId: 3, generation: "12", pos: { x: 50, y: 50 }, radius: 4, family: 2 },
];
assert.equal(model.hitTestDebugEntities(entities, { x: 10, y: 10 }).entityId, 2);
assert.equal(model.boxSelectDebugEntities(entities, { x: 20, y: 20 }, { x: 0, y: 0 }).length, 2);
assert.equal(model.boxSelectDebugEntities(Array.from({ length: 200 }, (_, i) => ({ ...entities[0], entityId: i })),
  { x: 0, y: 0 }, { x: 20, y: 20 }).length, DEBUG_SELECTION_LIMIT);
let selected = model.toggleDebugSelection(new Map(), entities[0]);
selected = model.toggleDebugSelection(selected, entities[1], true); assert.equal(selected.size, 2);
selected = model.toggleDebugSelection(selected, entities[0], true); assert.equal(selected.size, 1);
assert.notEqual(model.getDebugEntityKey(entities[0]), model.getDebugEntityKey({ ...entities[0], generation: "99" }));
assert.notEqual(model.getDebugWorldKey(2, "1"), model.getDebugWorldKey(2, "2"));
assert.deepEqual(model.sortDebugPlayers([
  { id: 1, name: "Aaron", world: 2, worldGeneration: "1" }, { id: 2, name: "Zoe", world: 3, worldGeneration: "2" },
  { id: 3, name: "amy", world: 3, worldGeneration: "2" },
], 3, "2").map(p => p.id), [3, 2, 1]);
const gameState = { camera, entities: new Map([[42, {}]]), dpr: 2 };
const original = { ...gameState };
assert.throws(() => model.withDebugRenderState(gameState, { camera: zoom, entities: new Map(), temporary: true }, () => { throw Error("draw failed"); }));
assert.deepEqual(gameState, original, "failed rendering must restore ordinary game state");

// Exercise the actual workspace controller with an isolated fake DOM. This
// catches callback/camera/selection regressions without connecting anywhere.
let clock = 1000, active = null;
function element(tag = "div") {
  const events = new Map(), classes = new Set(), attributes = new Map(); let selectedValue = "";
  const el = {
    tagName: tag.toUpperCase(), nodeType: tag === "fragment" ? 11 : 1, children: [], parentElement: null,
    className: "", textContent: "", dataset: {}, style: {}, hidden: false, disabled: false,
    clientHeight: 200, scrollHeight: 200, scrollTop: 0, width: 1, height: 1,
    get value() { return this.tagName === "SELECT" ? selectedValue || this.options[0]?.value || "" : selectedValue; },
    set value(v) { selectedValue = String(v); },
    get options() { return this.children.filter(child => child.tagName === "OPTION"); },
    get selectedOptions() { return this.options.filter(option => option.value === this.value); },
    get isConnected() { return this === document.body || !!this.parentElement?.isConnected; },
    get firstElementChild() { return this.children[0]; },
    append(...nodes) {
      for (const node of nodes) {
        if (node.nodeType === 11) { this.append(...[...node.children]); continue; }
        node.remove?.(); this.children.push(node); node.parentElement = this;
      }
    },
    replaceChildren(...nodes) { for (const child of [...this.children]) child.remove?.(); this.children = []; this.append(...nodes); },
    remove() { if (!this.parentElement) return; const list = this.parentElement.children; list.splice(list.indexOf(this), 1); this.parentElement = null; },
    setAttribute(key, value) { attributes.set(key, value); },
    addEventListener(name, fn) { if (!events.has(name)) events.set(name, []); events.get(name).push(fn); },
    fire(name, event = {}) { for (const fn of events.get(name) || []) fn({ target: this, preventDefault() {}, stopPropagation() {}, ...event }); },
    contains(other) { return this === other || this.children.some(child => child.contains?.(other)); },
    querySelectorAll(selector) { return walk(this).filter(node => String(node.className || "").split(/\s+/).includes(selector.slice(1))); },
    focus() { active = this; }, setPointerCapture() {}, releasePointerCapture() {},
    getBoundingClientRect() { return { left: 0, top: 0, right: 800, bottom: 500, width: 800, height: 500 }; },
    getContext() { return new Proxy({}, { get: (_, name) => name === "measureText" ? () => ({ width: 40 }) : () => {} }); },
    classList: { toggle(name, on) { if (on) classes.add(name); else classes.delete(name); }, contains(name) { return classes.has(name); } },
  };
  return el;
}
const document = { createElement: element, createTextNode: text => ({ nodeType: 3, textContent: text }),
  createDocumentFragment: () => element("fragment"), body: null };
document.body = element("body");
const background = element("button"); background.inert = false; document.body.append(background);
const walk = root => [root, ...(root.children || []).flatMap(walk)];
const find = name => walk(document.body).find(node => String(node.className || "").split(/\s+/).includes(name));
const sockets = [];
class FakeSocket {
  static OPEN = 1;
  constructor(url) { this.url = url; this.readyState = 0; this.bufferedAmount = 0; this.sent = []; sockets.push(this); }
  send(data) { this.sent.push(new DebugFrameDecoder().push(data)[0]); }
  close() { this.readyState = 3; this.onclose?.(); }
  message(data) { this.onmessage?.({ data: frame(data).buffer }); }
}
let rendered, opened = 0, closed = 0;
const loadedMaps = [];
let failMapLoad = false, debugMapState;
const timers = new Map(); let timer = 0;
const scope = {
  ...model, DebugFrameDecoder, packDebugMessage, DEBUG_SELECTION_LIMIT, stepFlowerFace, createBurrowParticles, flagAttacking, flagDefending, flagDead,
  playerFlowerType, normalFlowerType, mechaFlowerType, dummyType, titanType,
  document, navigator: {}, window: { innerWidth: 1400, innerHeight: 900, devicePixelRatio: 1, prompt: () => "3", confirm: () => true },
  WebSocket: FakeSocket, performance: { now: () => clock },
  setTimeout: fn => { timers.set(++timer, fn); return timer; }, clearTimeout: id => timers.delete(id),
  ResizeObserver: class { observe() {} disconnect() {} },
  createMapRenderer: ({ state }) => {
    debugMapState = state;
    return { loadMap: async path => { loadedMaps.push(path); state.map = failMapLoad ? null : { path }; return state.map; },
      drawGrid() {}, applyTerrainDelta() {}, applyWallRemove() {} };
  },
  createDebugTypePicker: () => ({ open: async () => null, close() {}, isOpen: () => false }),
};
const source = readFileSync(new URL("../web/src/dev_debug_ui.js", import.meta.url), "utf8")
  .replace(/^import[\s\S]*?from "[^"]+";\r?\n/gm, "").replace(/^export /gm, "");
const create = vm.runInNewContext(`${source}\n;createDevDebugUi`, scope);
const ui = create({ renderEntities: (_, scene) => { rendered = scene; }, onOpen: () => opened++, onClose: () => closed++ });
ui.open("ws://offline.test/ws", "a".repeat(64));
assert.equal(opened, 1); assert.ok(ui.isOpen());
assert.equal(background.inert, true, "workbench must block native focus/clicks in the game behind it");
const socket = sockets[0]; socket.readyState = 1; socket.onopen();
assert.deepEqual(socket.sent[0], { op: "hello", ticket: "a".repeat(64) });
socket.message({ kind: "hello", operator: "admin", world: 1, worldGeneration: "5", center: { x: 100, y: 80 } });
socket.message({ kind: "catalog", types: [], rarities: [{ id: 1, name: "Common" }], commands: [] });
const status = { kind: "status", worlds: [{ id: 1, generation: "5", map: "garden", width: 1000, height: 1000 }], players: [], tps: 20, tickMs: 1 };
socket.message(status);
assert.equal(loadedMaps.at(-1), "garden", "legacy servers can send only the display name");
status.worlds = [{ ...status.worlds[0], mapPath: "data/maps/garden.tmj" }];
socket.message(status);
assert.equal(loadedMaps.at(-1), "data/maps/garden.tmj", "explicit map path wins over the display name");
assert.match(find("dev-world-select").options[0].textContent, /^#1 garden ·/);
const loadedMapCount = loadedMaps.length;
socket.message(status);
assert.equal(loadedMaps.length, loadedMapCount, "repeated status must not reload a loaded map");
failMapLoad = true;
status.worlds = [{ ...status.worlds[0], mapPath: "data/maps/custom/garden.tmj" }];
socket.message(status);
assert.equal(loadedMaps.at(-1), "data/maps/custom/garden.tmj", "same display name may refer to a different file");
assert.equal(debugMapState.map, null);
failMapLoad = false;
find("dev-header").children.find(node => node.textContent === "刷新").fire("click");
assert.deepEqual(loadedMaps.slice(-2), ["data/maps/custom/garden.tmj", "data/maps/custom/garden.tmj"],
  "refresh must retry failed map loading even when the world metadata is unchanged");
const refresh = socket.sent.findLast(message => message.op === "refresh");
socket.message({ kind: "result", request: refresh.request, ok: true, message: "World view refreshed" });
await Promise.resolve();
socket.message({ kind: "snapshot", world: 1, worldGeneration: "5", entities: [
  { ...entities[0], pos: { x: 100, y: 80 }, radius: 15, entityType: playerFlowerType, visible: true, flags: flagDead },
  { ...entities[1], pos: { x: 150, y: 80 }, radius: 15, entityType: 7, visible: true, flags: 0 },
] });
clock += 120; ui.render(clock);
assert.equal(rendered.entities.size, 2);
assert.ok([...rendered.entities.keys()].every(id => id < -1000000), "debug sprite identities cannot collide with player scene caches");
assert.equal([...rendered.entities.values()][0].flowerFace.dead, true);
assert.equal([...rendered.entities.values()][1].flowerFace, undefined, "non-flowers must not simulate faces");
assert.equal(debugMapState.dpr, 1, "map and entity caches must use the same observer DPR");
const canvas = find("dev-canvas");
canvas.fire("pointerdown", { button: 0, pointerId: 1, clientX: 400, clientY: 250 });
canvas.fire("pointerup", { button: 0, pointerId: 1, clientX: 400, clientY: 250 });
assert.match(find("dev-view-footer").children[0].textContent, /已选 1/);
assert.ok(find("dev-menu"), "left-click must expose entity operations immediately");
assert.ok(socket.sent.some(message => message.op === "inspect" && message.id === 1 && message.generation === "10"));
const inspection = socket.sent.findLast(message => message.op === "inspect");
socket.message({ kind: "result", request: inspection.request, ok: true, message: "Entity", data: {
  entityId: 1, generation: "10", pos: { x: 100, y: 80 }, stats: { damage: 5 }, states: [],
} });
await Promise.resolve();
const details = find("dev-inspector"); details.scrollTop = 70;
details.querySelectorAll(".dev-detail-section").find(section => section.firstElementChild.textContent === "states").open = true;
clock += 1100; ui.render(clock);
const repeatedInspection = socket.sent.findLast(message => message.op === "inspect");
socket.message({ kind: "result", request: repeatedInspection.request, ok: true, message: "Entity", data: {
  entityId: 1, generation: "10", pos: { x: 100, y: 80 }, stats: { damage: 6 }, states: [],
} });
await Promise.resolve();
assert.equal(details.scrollTop, 70);
assert.equal(details.querySelectorAll(".dev-detail-section").find(section => section.firstElementChild.textContent === "states").open, true);
find("dev-debug").fire("keydown", { key: "Shift" });
assert.equal(find("dev-menu"), undefined, "shift selection must not be obstructed by the previous menu");
canvas.fire("pointerdown", { button: 0, pointerId: 1, clientX: 432.5, clientY: 250, shiftKey: true });
canvas.fire("pointerup", { button: 0, pointerId: 1, clientX: 432.5, clientY: 250, shiftKey: true });
assert.match(find("dev-view-footer").children[0].textContent, /已选 2/);
canvas.fire("contextmenu", { clientX: 400, clientY: 250 });
assert.ok(find("dev-menu").children.some(node => node.textContent.includes("设置阵营")));
find("dev-menu").children.find(node => node.textContent.includes("设置阵营")).fire("click");
const action = socket.sent.findLast(message => message.op === "entity_action");
assert.equal(action.action, "team"); assert.equal(action.team, 3); assert.equal(action.targets.length, 2);
assert.equal(action.worldGeneration, "5");
socket.message({ kind: "result", request: action.request, ok: true, message: "offline action accepted" });
canvas.fire("contextmenu", { clientX: 650, clientY: 440 });
assert.ok(find("dev-menu").children.some(node => node.textContent.includes("生成生物")));
const oldWorld = socket.sent.findLast(message => message.op === "view");
canvas.fire("wheel", { clientX: 500, clientY: 250, deltaY: -120 }); clock += 120; ui.render(clock);
assert.ok(socket.sent.findLast(message => message.op === "view").radius < oldWorld.radius);
const renderMap = rendered.entities;
socket.message({ kind: "snapshot", world: 1, worldGeneration: "5", entities: [
  { ...entities[0], pos: { x: 100, y: 80 }, radius: 0.01, entityType: 7, visible: true, flags: 0 },
  { ...entities[1], pos: { x: 1000000, y: 80 }, radius: 15, entityType: 7, visible: true, flags: 0 },
] });
clock += 120; ui.render(clock);
assert.equal(rendered.entities, renderMap, "observer render collections are reused");
assert.equal(rendered.entities.size, 1, "offscreen records are not submitted to the renderer");
assert.equal([...rendered.entities.values()][0].raw.radius, 0.01, "tiny visible entities still render");
assert.equal(find("dev-viewport").dataset.count, "2", "culling never deletes authoritative records");
ui.close(); await Promise.resolve();
assert.equal(closed, 1); assert.equal(ui.isOpen(), false); assert.equal(socket.readyState, 3); assert.equal(timers.size, 0);
assert.equal(background.inert, false, "closing restores the original game's focus state");

const mapSource = readFileSync(new URL("../web/src/map_renderer.js", import.meta.url), "utf8");
assert.match(mapSource, /state = appState/);
assert.match(mapSource, /loadMapDecorations\(tmj, path, token, state\)/);
const gameSource = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
assert.match(gameSource, /const rawMove = state\.devDebugOpen \? \{ x: 0, y: 0 \}/);
assert.match(gameSource, /const effectiveAttacking = !state\.devDebugOpen && attacking/);
assert.match(gameSource, /const effectiveDefending = !state\.devDebugOpen/);
const debugServerSource = readFileSync(new URL("../src/Server/DevTools/dev_debug_service.cpp", import.meta.url), "utf8");
assert.equal([...debugServerSource.matchAll(/Set\((?:entry|out), "mapPath", Str\(world->GetMapPath\(\)\)\)/g)].length, 3,
  "world status, player details and entity snapshots must include the actual map path");
assert.match(debugServerSource, /else if \(op == "refresh"\)[\s\S]*?Reply\(session, request, true, "World view refreshed"\)/);
console.log("PASS debug framing/tickets, map paths and reload retry, camera, selection/groups, world identities, restored render state, fake-socket workspace and input isolation");

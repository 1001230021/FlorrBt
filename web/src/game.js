import {
  ChatFlag,
  NETWORK_PETAL_TYPE_OFFSET,
  PetalNames,
  ServerType,
  appendBytes,
  clamp,
  isDropEntity,
  isPetalEntity,
  mobTypeName,
  packAuth,
  packChat,
  packChores,
  packCraft,
  packEquip,
  packInput,
  packSecondarySlot,
  packUnequip,
  parseServerMessage,
  petalTypeFromEntity,
  petalTypeName,
  popFrame,
  rarityColor,
  rarityName,
} from "./protocol.js";
import { clearLadybugPattern, drawNormalLadybug } from "./ladybug_sprite.js";
import { drawBeetle } from "./beetle_sprite.js";
import { drawSoldierAnt } from "./soldier_ant_sprite.js";
import { drawBee, drawBumbleBee, drawHornet, drawHornetMissile, drawPollen } from "./bee_sprite.js";

const canvas = document.getElementById("game");
const ctx = canvas.getContext("2d");
const authPanel = document.getElementById("authPanel");
const wsUrlInput = document.getElementById("wsUrl");
const accountInput = document.getElementById("account");
const passwordInput = document.getElementById("password");
const registerModeInput = document.getElementById("registerMode");
const connectBtn = document.getElementById("connectBtn");
const authStatus = document.getElementById("authStatus");
const deathOverlay = document.getElementById("deathOverlay");
const reviveBtn = document.getElementById("reviveBtn");
const chatLog = document.getElementById("chatLog");
const chatInput = document.getElementById("chatInput");
const consolePanel = document.getElementById("consolePanel");
const consoleLog = document.getElementById("consoleLog");
const consoleInput = document.getElementById("consoleInput");
const backpackPanel = document.getElementById("backpackPanel");
const backpackCloseBtn = document.getElementById("backpackCloseBtn");
const craftPanel = document.getElementById("craftPanel");
const craftCloseBtn = document.getElementById("craftCloseBtn");
const craftStage = document.getElementById("craftStage");
const craftResultSlot = document.getElementById("craftResultSlot");
const craftList = document.getElementById("craftList");
const craftSelection = document.getElementById("craftSelection");
const craftChance = document.getElementById("craftChance");
const craftOnceBtn = document.getElementById("craftOnceBtn");
const primarySlots = document.getElementById("primarySlots");
const secondarySlots = document.getElementById("secondarySlots");
const inventoryList = document.getElementById("inventoryList");

const state = {
  ws: null,
  connected: false,
  authenticated: false,
  receiveBuffer: new Uint8Array(),
  entities: new Map(),
  playerId: 0,
  ownerEntityId: 0,
  ownerLevel: 1,
  ownerExp: 0,
  ownerExpRequired: 10,
  snapshotId: 0,
  viewRadius: 0,
  map: null,
  mapName: "",
  mapDisplayName: "",
  mapLoadToken: 0,
  ownerSlots: Array.from({ length: 5 }, () => emptySlot()),
  secondarySlots: Array.from({ length: 5 }, () => emptySlot()),
  inventory: [],
  chats: [],
  selectedSlot: 0,
  camera: { x: 0, y: 0 },
  keys: new Set(),
  mouse: { x: 0, y: 0, seen: false, inUi: false },
  keyboardControl: false,
  attacking: false,
  defending: false,
  lastInput: { x: 99, y: 99, attacking: false, defending: false },
  sendTimer: 1,
  lastFrameTime: performance.now(),
  fps: 0,
  consoleOpen: false,
  debugGrid: false,
  backpackOpen: false,
  craftOpen: false,
  craftSlots: Array.from({ length: 5 }, () => null),
  craftSource: null,
  craftPhase: "idle",
  craftResult: null,
  craftBurstUntil: 0,
  craftSpinStarted: 0,
  drag: null,
  suppressClickUntil: 0,
  canvasWidth: 0,
  canvasHeight: 0,
  dpr: 1,
};

const settingsKey = "florrbt.web.settings";
const defaultMapName = "training_grounds.tmj";
const packetInterval = 1 / 30;
const predictionSnapDistance = 260;
const ownerPredictionCorrectionRate = 12;
const ownerCameraFollowRate = 24;
const deathFadeDuration = 0.1;
const viewScreenFill = 0.46;
const viewScreenPadding = 36;
const mouseMoveDeadzonePx = 16;
const entityFrameMinScreenRadius = 8;
const flagAttacking = 1 << 0;
const flagDefending = 1 << 1;
const flagDead = 1 << 2;
const flagOwner = 1 << 3;
const flagUndead = 1 << 4;
const flagCorrupted = 1 << 5;
const flagRelic = 1 << 6;
const flagAntennae = 1 << 7;
const beetleType = 1;
const normalLadybugType = 3;
const soldierAntType = 7;
const summonedBeetleType = 10;
const summonedSoldierAntType = 11;
const bandageBeetleType = 12;
const beeType = 13;
const hornetType = 14;
const bumbleBeeType = 15;
const playerFlowerType = 6;
const bossRarity = 8;
const maxBossBars = 3;
const petalAntEggType = 2;
const petalAntennaeType = 3;
const petalCompassType = 10;
const petalDustType = 13;
const petalMoonType = 17;
const petalNullificationType = 18;
const petalRelicType = 20;
const petalBandageType = 26;
const petalDahliaType = 30;
const petalMimicType = 35;
const petalStingerType = 37;
const stingerSplitIconMinRarity = 6;
const flowerTextureVersion = "20260713a";
const PetalIconIds = [
  0, 48, 51, 17, 1, 16, 58, 13, 57, 74, 71, 98, 72, 111, 97, 7, 113, 77,
  "nullification", 27, 53, 5, 19, 9, 108, 80, 103, 18, 12, 30, 38, 8, 106,
  109, 94, 93, "glass", 6, "broken_egg",
];
const worldPetalSizeScale = 6;
const worldDropSizeScale = 3.25;
const petalCardIconScale = 0.92;
const mobSpriteEffectiveBox = 91.667;
const mobSpriteViewBox = 110;
const mobSpriteCoverScale = mobSpriteViewBox / mobSpriteEffectiveBox;
const beetleSpriteForwardOffsetScale = 0.42;
const PetalNameToType = new Map(PetalNames.map((name, index) => [normalizePetalName(name), index]));
const assetImages = new Map();
const livePetalImages = new Map();
const bandageOverlayImages = new Map();
const strippedPetalIconUrls = new Map();
const mimicLabelIconUrls = new Map();

function emptySlot() {
  return { petalType: 0, rarity: 0 };
}

function slotHasItem(slot) {
  return slot && slot.petalType > 0 && slot.rarity > 0;
}

function loadSettings() {
  let saved = {};
  try {
    saved = JSON.parse(localStorage.getItem(settingsKey) || "{}");
  } catch {
    saved = {};
  }
  wsUrlInput.value = saved.wsUrl || `ws://${location.host || "127.0.0.1:8080"}/ws`;
  accountInput.value = saved.account || "";
  passwordInput.value = saved.password || "";
  state.keyboardControl = saved.keyboardControl === true;
}

function saveSettings() {
  localStorage.setItem(settingsKey, JSON.stringify({
    wsUrl: wsUrlInput.value.trim(),
    account: accountInput.value,
    password: passwordInput.value,
    keyboardControl: state.keyboardControl,
  }));
}

function setStatus(text, good = false) {
  authStatus.textContent = text;
}

function addConsoleLine(text, kind = "") {
  const line = document.createElement("div");
  line.className = `console-line${kind ? ` ${kind}` : ""}`;
  line.textContent = text;
  consoleLog.appendChild(line);
  while (consoleLog.children.length > 90) consoleLog.removeChild(consoleLog.firstChild);
  consoleLog.scrollTop = consoleLog.scrollHeight;
}

function toggleConsole(force) {
  state.consoleOpen = typeof force === "boolean" ? force : !state.consoleOpen;
  state.debugGrid = state.consoleOpen;
  consolePanel.classList.toggle("open", state.consoleOpen);
  if (state.consoleOpen) {
    closeChat();
    consoleInput.focus();
    consoleInput.setSelectionRange(consoleInput.value.length, consoleInput.value.length);
  } else {
    consoleInput.blur();
    canvas.focus();
  }
}

function toggleBackpack(force) {
  state.backpackOpen = typeof force === "boolean" ? force : !state.backpackOpen;
  if (state.backpackOpen) toggleCraft(false);
  backpackPanel.classList.toggle("open", state.backpackOpen);
}

function toggleCraft(force) {
  state.craftOpen = typeof force === "boolean" ? force : !state.craftOpen;
  if (state.craftOpen) {
    toggleBackpack(false);
    renderCraftPanel();
  } else {
    clearCraftDisplay();
  }
  craftPanel.classList.toggle("open", state.craftOpen);
}

function submitConsole() {
  const line = consoleInput.value.trim();
  consoleInput.value = "";
  if (!line) return;
  addConsoleLine(`> ${line}`, "command");
  executeClientCommand(line.startsWith("/") ? line.slice(1) : line);
}

function connectAndAuth() {
  const wsUrl = wsUrlInput.value.trim();
  if (!wsUrl) {
    setStatus("Socket is empty");
    return;
  }

  saveSettings();
  closeSocket(false);
  setStatus("Connecting...");

  const ws = new WebSocket(wsUrl);
  state.ws = ws;
  ws.binaryType = "arraybuffer";

  ws.addEventListener("open", () => {
    state.connected = true;
    setStatus("Connected", true);
    addConsoleLine("Connected to web bridge");
    sendAuth();
  });

  ws.addEventListener("message", (event) => {
    if (typeof event.data === "string") {
      setStatus(event.data);
      addConsoleLine(event.data, "error");
      return;
    }
    receiveBytes(new Uint8Array(event.data));
  });

  ws.addEventListener("close", () => {
    state.connected = false;
    state.authenticated = false;
    authPanel.classList.remove("hidden");
    setStatus("Disconnected");
    addConsoleLine("Disconnected", "error");
  });

  ws.addEventListener("error", () => {
    setStatus("Socket error");
    addConsoleLine("Socket error", "error");
  });
}

function closeSocket(sendDisconnect = true) {
  if (state.ws && state.ws.readyState === WebSocket.OPEN && sendDisconnect) {
    sendBytes(packChores(false, false, false, true));
  }
  if (state.ws) state.ws.close();
  state.ws = null;
  state.connected = false;
  state.authenticated = false;
}

function sendBytes(bytes) {
  if (!bytes || !state.ws || state.ws.readyState !== WebSocket.OPEN) return false;
  state.ws.send(bytes);
  return true;
}

function sendAuth() {
  const packet = packAuth(accountInput.value.trim(), passwordInput.value, registerModeInput.checked);
  if (!packet) {
    setStatus("Account is empty");
    return;
  }
  sendBytes(packet);
  setStatus("Auth sent", true);
  addConsoleLine("Auth sent");
}

function receiveBytes(bytes) {
  state.receiveBuffer = appendBytes(state.receiveBuffer, bytes);
  for (;;) {
    const frame = popFrame(state.receiveBuffer);
    if (!frame) break;
    state.receiveBuffer = frame.rest;
    if (frame.payload.length === 0) continue;
    handleServerMessage(parseServerMessage(frame.payload));
  }
}

function handleServerMessage(msg) {
  if (!msg || msg.type === 0xff) return;

  if (msg.type === ServerType.Welcome) {
    state.playerId = msg.playerId;
    state.ownerEntityId = msg.ownerEntityId;
    if (msg.mapName) loadMap(msg.mapName);
    setStatus(`Player ${msg.playerId}`, true);
    return;
  }

  if (msg.type === ServerType.AuthResult) {
    state.authenticated = !!msg.success;
    setStatus(msg.message || (msg.success ? "Authenticated" : "Auth failed"), msg.success);
    if (msg.success) authPanel.classList.add("hidden");
    addConsoleLine(msg.message || (msg.success ? "Authenticated" : "Auth failed"), msg.success ? "" : "error");
    return;
  }

  if (msg.type === ServerType.Snapshot) {
    applySnapshot(msg);
    return;
  }

  if (msg.type === ServerType.OwnerState) {
    state.ownerLevel = Math.max(1, msg.level || 1);
    state.ownerExp = Math.max(0, msg.exp || 0);
    state.ownerExpRequired = Math.max(1, msg.expRequired || 10);
    const primaryCount = Math.max(1, (msg.ownerSlots || []).length || state.ownerSlots.length || 5);
    state.ownerSlots = normalizeSlots(msg.ownerSlots, primaryCount);
    state.secondarySlots = normalizeSlots(msg.secondarySlots, primaryCount);
    state.selectedSlot = clamp(state.selectedSlot, 0, state.ownerSlots.length - 1);
    renderInventoryPanel();
    renderCraftPanel();
    return;
  }

  if (msg.type === ServerType.Inventory) {
    state.inventory = msg.inventory || [];
    renderInventoryPanel();
    renderCraftPanel();
    return;
  }

  if (msg.type === ServerType.CraftResult) {
    handleCraftResult(msg);
    return;
  }

  if (msg.type === ServerType.Chat) {
    state.chats.push(msg.chat);
    if (state.chats.length > 8) state.chats.splice(0, state.chats.length - 8);
    renderChat();
    addConsoleLine(`${msg.chat.playerName || msg.chat.playerId}: ${msg.chat.message || ""}`);
  }
}

function normalizeSlots(slots, size) {
  const out = Array.from({ length: Math.max(0, size) }, () => emptySlot());
  (slots || []).forEach((slot, index) => {
    if (index < out.length) out[index] = { petalType: slot.petalType || 0, rarity: slot.rarity || 0 };
  });
  return out;
}

function normalizeMapPath(path) {
  const normalized = String(path || "").replace(/\\/g, "/").replace(/^\/+/, "");
  if (!normalized) return "";
  if (normalized.startsWith("data/")) return `/${normalized}`;
  if (normalized.startsWith("maps/") || normalized.startsWith("tiles/")) return `/data/${normalized}`;
  return `/data/maps/${normalized}`;
}

function joinAssetPath(basePath, relativePath) {
  const relative = String(relativePath || "").replace(/\\/g, "/");
  if (!relative) return "";
  if (relative.startsWith("/") || /^[a-z]+:\/\//i.test(relative)) return relative;
  const base = String(basePath || "").replace(/\\/g, "/");
  const parts = base.split("/");
  parts.pop();
  for (const part of relative.split("/")) {
    if (!part || part === ".") continue;
    if (part === "..") parts.pop();
    else parts.push(part);
  }
  return parts.join("/");
}

async function loadMap(mapName) {
  const path = normalizeMapPath(mapName);
  if (!path || (path === state.mapName && state.map)) return;

  const token = ++state.mapLoadToken;
  state.mapName = path;
  state.mapDisplayName = String(mapName || path);
  addConsoleLine(`Loading map: ${path}`);
  try {
    const response = await fetch(path, { cache: "no-cache" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const tmj = await response.json();
    const tileImages = new Map();
    for (const tilesetRef of tmj.tilesets || []) {
      const tilesetPath = joinAssetPath(path, tilesetRef.source || "");
      if (!tilesetPath) continue;
      const tsjResponse = await fetch(tilesetPath, { cache: "no-cache" });
      if (!tsjResponse.ok) throw new Error(`tileset HTTP ${tsjResponse.status}`);
      const tileset = await tsjResponse.json();
      for (const tile of tileset.tiles || []) {
        if (!tile.image) continue;
        const gid = (tilesetRef.firstgid || 1) + (tile.id || 0);
        const image = new Image();
        image.decoding = "async";
        image.onload = () => {
          if (token === state.mapLoadToken) drawScene();
        };
        image.onerror = () => {
          if (token === state.mapLoadToken) addConsoleLine(`Tile image failed: ${image.src}`, "error");
        };
        image.src = joinAssetPath(tilesetPath, tile.image);
        tileImages.set(gid, image);
      }
    }

    const layers = [];
    for (const layer of tmj.layers || []) {
      if (layer.type !== "tilelayer") continue;
      layers.push({
        name: layer.name || "",
        width: layer.width || tmj.width || 0,
        height: layer.height || tmj.height || 0,
        tiles: await decodeTileLayer(layer),
      });
    }

    if (token !== state.mapLoadToken) return;
    state.map = {
      path,
      width: tmj.width || 0,
      height: tmj.height || 0,
      tileWidth: tmj.tilewidth || 512,
      tileHeight: tmj.tileheight || 512,
      layers,
      tileImages,
    };
    addConsoleLine(`Map loaded: ${path} (${layers.length} layers, ${tileImages.size} tiles)`);
  } catch (error) {
    if (token === state.mapLoadToken) {
      state.map = null;
      addConsoleLine(`Map load failed: ${path} (${error.message})`, "error");
    }
  }
}

async function decodeTileLayer(layer) {
  if (Array.isArray(layer.data)) return layer.data.map((value) => Number(value) || 0);
  if (typeof layer.data !== "string") return [];

  const compressed = Uint8Array.from(atob(layer.data), (char) => char.charCodeAt(0));
  let bytes = compressed;
  if (layer.compression === "gzip") {
    if (typeof DecompressionStream === "undefined") throw new Error("gzip map layers need DecompressionStream");
    const stream = new Blob([compressed]).stream().pipeThrough(new DecompressionStream("gzip"));
    bytes = new Uint8Array(await new Response(stream).arrayBuffer());
  } else if (layer.compression) {
    throw new Error(`unsupported map compression: ${layer.compression}`);
  }

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const out = [];
  for (let offset = 0; offset + 4 <= bytes.byteLength; offset += 4)
    out.push(view.getUint32(offset, true));
  return out;
}

function applySnapshot(msg) {
  state.snapshotId = msg.snapshotId;
  state.ownerEntityId = msg.ownerEntityId || state.ownerEntityId;
  const nextViewRadius = msg.viewRadius || state.viewRadius;
  if (nextViewRadius > 0) {
    state.viewRadius = nextViewRadius;
  }

  const live = new Set();
  for (const snap of msg.entities || []) {
    live.add(snap.entityId);
    const existing = state.entities.get(snap.entityId);
    if (existing) {
      const wasDead = (existing.snapshot.flags & flagDead) !== 0;
      const isDead = (snap.flags & flagDead) !== 0;
      existing.snapshotMove = Math.hypot(snap.pos.x - existing.snapshot.pos.x, snap.pos.y - existing.snapshot.pos.y);
      existing.snapshot = snap;
      existing.dying = false;
      existing.deathAge = 0;
      if (isDead && !wasDead) existing.deathAngle = Math.random() * Math.PI * 2;
      if (!isDead) existing.deathAngle = null;
    } else {
      const isDead = (snap.flags & flagDead) !== 0;
      state.entities.set(snap.entityId, {
        snapshot: snap,
        renderPos: { x: snap.pos.x, y: snap.pos.y },
        renderAngle: snap.angle || 0,
        snapshotMove: 0,
        motionBlend: 0,
        dying: false,
        deathAge: 0,
        deathAngle: isDead ? Math.random() * Math.PI * 2 : null,
      });
    }
  }
  for (const id of state.entities.keys()) {
    if (live.has(id)) continue;
    const entity = state.entities.get(id);
    if (!entity) continue;
    if (entity.dying) continue;
    if (!shouldFadeMissingEntity(entity)) {
      state.entities.delete(id);
      clearLadybugPattern(id);
      continue;
    }
    entity.dying = true;
    entity.deathAge = 0;
  }
}

function shouldFadeMissingEntity(entity) {
  const owner = state.entities.get(state.ownerEntityId);
  if (!owner || !state.viewRadius) return false;
  const dx = entity.renderPos.x - owner.renderPos.x;
  const dy = entity.renderPos.y - owner.renderPos.y;
  const visibleEdge = state.viewRadius + Math.max(0, entity.snapshot.radius || 0);
  return isNullVisualEntity(entity) && dx * dx + dy * dy <= visibleEdge * visibleEdge;
}

function isNullVisualEntity(entity) {
  if (!entity || !entity.snapshot) return false;
  const isOwner = (entity.snapshot.flags & flagOwner) !== 0 || entity.snapshot.entityId === state.ownerEntityId;
  return isOwner && ownerHasPetal(petalNullificationType);
}

function readKeyboardMoveInput() {
  let x = 0;
  let y = 0;
  if (state.keys.has("a") || state.keys.has("arrowleft")) x -= 1;
  if (state.keys.has("d") || state.keys.has("arrowright")) x += 1;
  if (state.keys.has("w") || state.keys.has("arrowup")) y -= 1;
  if (state.keys.has("s") || state.keys.has("arrowdown")) y += 1;

  const length = Math.hypot(x, y);
  if (length > 1) {
    x /= length;
    y /= length;
  }
  return { x, y };
}

function readMouseMoveInput() {
  if (!state.mouse.seen || state.mouse.inUi) return { x: 0, y: 0 };

  const owner = state.entities.get(state.ownerEntityId);
  const center = owner ? worldToScreen(owner.renderPos) : { x: state.canvasWidth * 0.5, y: state.canvasHeight * 0.5 };
  let x = state.mouse.x - center.x;
  let y = state.mouse.y - center.y;
  const length = Math.hypot(x, y);
  if (length <= mouseMoveDeadzonePx) return { x: 0, y: 0 };

  const fullSpeedDistance = Math.max(80, Math.min(state.canvasWidth, state.canvasHeight) * 0.18);
  const scale = Math.min(1, length / fullSpeedDistance) / length;
  return { x: x * scale, y: y * scale };
}

function readMoveInput() {
  return state.keyboardControl ? readKeyboardMoveInput() : readMouseMoveInput();
}

function updateMousePointer(event) {
  state.mouse.x = event.clientX;
  state.mouse.y = event.clientY;
  state.mouse.seen = true;
  state.mouse.inUi = !!isUiTarget(event.target);
}

function flushInput(dt) {
  if (!state.connected || !state.authenticated || isTyping() || isOwnerDead()) return;

  state.sendTimer += dt;
  const move = readMoveInput();
  const packetMoveX = move.y;
  const packetMoveY = move.x;
  const px = Math.round(clamp(packetMoveX, -1, 1) * 127);
  const py = Math.round(clamp(packetMoveY, -1, 1) * 127);
  const moveChanged = px !== state.lastInput.x || py !== state.lastInput.y;
  if (moveChanged || state.sendTimer >= packetInterval) {
    sendBytes(packInput(packetMoveX, packetMoveY));
    state.lastInput.x = px;
    state.lastInput.y = py;
    state.sendTimer = 0;
  }

  if (state.attacking !== state.lastInput.attacking || state.defending !== state.lastInput.defending) {
    sendBytes(packChores(state.attacking, state.defending));
    state.lastInput.attacking = state.attacking;
    state.lastInput.defending = state.defending;
  }
}

function isOwnerDead() {
  const owner = state.authenticated ? state.entities.get(state.ownerEntityId) : null;
  return !!owner && ((owner.snapshot.flags & flagDead) !== 0);
}

function updateDeathOverlay() {
  deathOverlay.classList.toggle("hidden", !isOwnerDead());
}

function requestRevive() {
  if (!state.connected || !state.authenticated || !isOwnerDead()) return;
  state.attacking = false;
  state.defending = false;
  state.keys.clear();
  sendBytes(packChores(false, false, true));
}

function isTyping() {
  const active = document.activeElement;
  return active && (active.tagName === "INPUT" || active.tagName === "TEXTAREA");
}

function openChat(prefix = "") {
  if (!state.authenticated) return;
  chatInput.value = prefix;
  chatInput.classList.remove("hidden");
  chatInput.focus();
  chatInput.setSelectionRange(chatInput.value.length, chatInput.value.length);
}

function closeChat() {
  chatInput.value = "";
  chatInput.classList.add("hidden");
  canvas.focus();
}

function submitChat() {
  const text = chatInput.value.trim();
  closeChat();
  if (!text) return;
  executeClientCommand(text.startsWith("/") ? text.slice(1) : `say local ${text}`);
}

function executeClientCommand(line) {
  const args = splitCommand(line.trim());
  if (args.length === 0) return;

  const name = args.shift().replace(/^\//, "").toLowerCase();
  if (name === "say") {
    if (args.length < 2) return;
    const flagText = String(args.shift()).toLowerCase();
    const message = args.join(" ").trim();
    if (!message) return;
    if (flagText === "global" || flagText === "g") sendChat(ChatFlag.Global, message);
    else if (flagText === "local" || flagText === "l") sendChat(ChatFlag.Local, message);
    else if (flagText === "whisper" || flagText === "w") sendChat(ChatFlag.Whisper, `/whisper ${message}`);
    return;
  }
  if (name === "g") {
    const message = args.join(" ").trim();
    if (message) sendChat(ChatFlag.Global, message);
    return;
  }
  if (name === "l") {
    const message = args.join(" ").trim();
    if (message) sendChat(ChatFlag.Local, message);
    return;
  }
  if (name === "whisper" || name === "w") {
    const message = args.join(" ").trim();
    if (message) sendChat(ChatFlag.Whisper, `/whisper ${message}`);
    return;
  }
  if (name === "map") {
    const mapName = args.join(" ").trim() || defaultMapName;
    loadMap(mapName);
    return;
  }
  if (name === "grid") {
    state.debugGrid = args.length ? args[0] !== "0" && args[0].toLowerCase() !== "off" : !state.debugGrid;
    addConsoleLine(`Debug grid ${state.debugGrid ? "on" : "off"}`);
    return;
  }
  if (name === "keyboard_control") {
    if (args.length) {
      const value = String(args[0]).toLowerCase();
      state.keyboardControl = !(value === "0" || value === "off" || value === "false");
      saveSettings();
    }
    addConsoleLine(`Keyboard control ${state.keyboardControl ? "on" : "off"}`);
    return;
  }

  sendChat(ChatFlag.Local, `/${line}`);
}

function sendChat(flag, message) {
  const packet = packChat(flag, message);
  if (packet) sendBytes(packet);
}

function splitCommand(text) {
  const out = [];
  let current = "";
  let quote = "";
  for (const ch of text) {
    if ((ch === "\"" || ch === "'") && !quote) {
      quote = ch;
      continue;
    }
    if (ch === quote) {
      quote = "";
      continue;
    }
    if (/\s/.test(ch) && !quote) {
      if (current) out.push(current);
      current = "";
      continue;
    }
    current += ch;
  }
  if (current) out.push(current);
  return out;
}

function renderChat() {
  chatLog.replaceChildren();
  for (const chat of state.chats) {
    const line = document.createElement("div");
    line.className = "chat-line";
    const name = document.createElement("span");
    name.className = "chat-name";
    name.textContent = chat.flag === ChatFlag.Server ? "Server" : `${chat.playerName || chat.playerId}: `;
    line.appendChild(name);

    const body = document.createElement("span");
    const styled = parseRarityMessage(chat.message || "");
    body.textContent = styled.text;
    if (styled.color) body.style.color = styled.color;
    line.appendChild(body);
    chatLog.appendChild(line);
  }
}

function parseRarityMessage(message) {
  const match = message.match(/^<([^>]+)>\((.*)\)$/s);
  if (!match) return { text: message, color: "" };
  const rarity = matchRarity(match[1]);
  if (rarity <= 0) return { text: message, color: "" };
  return { text: match[2], color: rarityColor(rarity, 1) };
}

function matchRarity(text) {
  const key = String(text).toLowerCase();
  const aliases = {
    c: 1, common: 1,
    u: 2, unusual: 2,
    r: 3, rare: 3,
    e: 4, epic: 4,
    l: 5, legendary: 5, led: 5,
    m: 6, mythic: 6, my: 6,
    ultra: 7,
    s: 8, super: 8,
    et: 9, eternal: 9,
    q: 10, unique: 10,
    p: 11, primordial: 11,
  };
  return aliases[key] || 0;
}

function copySlot(slot) {
  return slotHasItem(slot) ? { petalType: slot.petalType, rarity: slot.rarity } : emptySlot();
}

function shouldSuppressClick() {
  return performance.now() < state.suppressClickUntil;
}

function renderInventoryPanel() {
  primarySlots.replaceChildren();
  secondarySlots.replaceChildren();
  inventoryList.replaceChildren();
  primarySlots.style.setProperty("--slot-count", state.ownerSlots.length);
  secondarySlots.style.setProperty("--slot-count", state.ownerSlots.length);

  state.ownerSlots.forEach((slot, index) => {
    primarySlots.appendChild(makeSlotButton(slot, index, "primary"));
  });
  normalizeSlots(state.secondarySlots, state.ownerSlots.length).forEach((slot, index) => {
    secondarySlots.appendChild(makeSlotButton(slot, index, "secondary"));
  });

  renderInventoryGroups(inventoryList, state.inventory, "inventory");
}

function renderCraftPanel() {
  craftList.replaceChildren();
  renderCraftStage();
  renderCraftMatrix();
}

function selectCraftPetal(petalType, rarity) {
  if (state.craftPhase === "spinning") return;
  if (!canCraftRarity(rarity)) return;
  if (state.craftPhase === "success" || state.craftPhase === "returned") clearCraftDisplay();
  const sameSource = state.craftSource &&
    state.craftSource.petalType === petalType &&
    state.craftSource.rarity === rarity;
  const stagedSame = sameSource ? getCraftSlotCount(petalType, rarity) : 0;
  const available = Math.max(0, getInventoryCount(petalType, rarity) - stagedSame);
  if (available <= 0) return;
  if (available < 5 && stagedSame + available < 5) return;

  const addCount = Math.min(5, available);
  const total = stagedSame + addCount;

  state.craftPhase = "staged";
  state.craftSource = { petalType, rarity };
  state.craftResult = null;
  state.craftSlots = distributeCraftCount(petalType, rarity, total);
  renderCraftPanel();
}

function renderInventoryGroups(container, items, mode) {
  const groups = groupInventoryItems(items);
  container.classList.toggle("hidden", groups.length === 0);

  for (const group of groups) {
    const section = document.createElement("section");
    section.className = "inventory-group";

    const header = document.createElement("div");
    header.className = "inventory-group-title";
    header.style.color = rarityColor(group.rarity, 1);
    header.textContent = rarityName(group.rarity);
    section.appendChild(header);

    const grid = document.createElement("div");
    grid.className = "inventory-grid";
    for (const item of group.items) {
      const button = document.createElement("button");
      const lit = mode !== "craft" || canLightCraftItem(item.petalType, item.rarity);
      button.className = `inventory-item${mode === "craft" ? " craft-inventory-item" : ""}${lit ? "" : " locked"}`;
      button.type = "button";
      button.title = `${petalTypeName(item.petalType)} ${rarityName(item.rarity)} x${formatShortCount(item.count)}`;
      button.appendChild(makePetalStack(item.petalType, item.rarity, item.count));

      if (mode === "inventory") {
        button.addEventListener("pointerdown", (event) => beginDrag(event, button, "inventory", -1, item));
        button.addEventListener("click", () => {
          if (shouldSuppressClick()) return;
          equipFromInventory(item);
        });
      } else {
        button.addEventListener("click", (event) => {
          event.preventDefault();
          event.stopPropagation();
          if (lit) selectCraftPetal(item.petalType, item.rarity);
        });
      }
      grid.appendChild(button);
    }
    section.appendChild(grid);
    container.appendChild(section);
  }
}

function renderCraftMatrix() {
  const rows = buildCraftRows();
  craftList.classList.toggle("hidden", rows.length === 0);
  if (rows.length === 0) return;

  const matrix = document.createElement("div");
  matrix.className = "craft-matrix";

  const corner = document.createElement("div");
  corner.className = "craft-matrix-corner";
  corner.textContent = "type ↓  rarity →";
  matrix.appendChild(corner);

  for (let rarity = 1; rarity <= 11; rarity += 1) {
    const header = document.createElement("div");
    header.className = "craft-matrix-rarity";
    header.style.color = rarityColor(rarity, 1);
    header.textContent = rarityName(rarity).slice(0, 2);
    header.title = rarityName(rarity);
    matrix.appendChild(header);
  }

  for (const row of rows) {
    const label = document.createElement("div");
    label.className = "craft-matrix-type";
    label.textContent = petalTypeName(row.petalType);
    matrix.appendChild(label);

    for (let rarity = 1; rarity <= 11; rarity += 1) {
      const baseCount = row.counts.get(rarity) || 0;
      const heldCount = getCraftHeldCount(row.petalType, rarity);
      const count = Math.max(0, baseCount - heldCount);
      const displayCount = count;
      const lit = canLightCraftItem(row.petalType, rarity);
      const selected = state.craftSource &&
        state.craftSource.petalType === row.petalType &&
        state.craftSource.rarity === rarity;

      const cell = document.createElement(baseCount > 0 ? "button" : "div");
      cell.className = `craft-matrix-cell${baseCount > 0 ? "" : " empty"}${lit ? " lit" : " locked"}${selected ? " selected" : ""}`;
      if (baseCount > 0) {
        cell.type = "button";
        cell.title = `${petalTypeName(row.petalType)} ${rarityName(rarity)} x${formatShortCount(displayCount)}`;
        cell.appendChild(makePetalStack(row.petalType, rarity, displayCount));
        cell.addEventListener("pointerdown", (event) => {
          event.preventDefault();
          event.stopPropagation();
          if (lit) selectCraftPetal(row.petalType, rarity);
        });
      }
      matrix.appendChild(cell);
    }
  }

  craftList.appendChild(matrix);
}

function buildCraftRows() {
  const rows = new Map();
  for (const item of state.inventory) {
    if (!slotHasItem(item) || item.count <= 0) continue;
    if (!rows.has(item.petalType)) rows.set(item.petalType, new Map());
    rows.get(item.petalType).set(item.rarity, item.count);
  }
  return Array.from(rows, ([petalType, counts]) => ({ petalType, counts }))
    .sort((a, b) => petalTypeName(a.petalType).localeCompare(petalTypeName(b.petalType), "en") || a.petalType - b.petalType);
}

function groupInventoryItems(items) {
  const groups = new Map();
  for (const item of items) {
    if (!slotHasItem(item) || item.count <= 0) continue;
    if (!groups.has(item.rarity)) groups.set(item.rarity, []);
    groups.get(item.rarity).push({ ...item });
  }
  return Array.from(groups, ([rarity, groupItems]) => ({
    rarity,
    items: groupItems.sort((a, b) => {
      const byName = petalTypeName(a.petalType).localeCompare(petalTypeName(b.petalType), "en");
      return byName || a.petalType - b.petalType;
    }),
  })).sort((a, b) => b.rarity - a.rarity);
}

function getCraftInventoryItems() {
  return state.inventory
    .map((item) => ({
      ...item,
      count: Math.max(0, (item.count || 0) - getCraftHeldCount(item.petalType, item.rarity)),
    }))
    .filter((item) => item.count > 0);
}

function getInventoryCount(petalType, rarity) {
  const item = state.inventory.find((entry) => entry.petalType === petalType && entry.rarity === rarity);
  return item ? item.count || 0 : 0;
}

function getCraftSlotCount(petalType, rarity) {
  return state.craftSlots.reduce((sum, slot) => (
    slotHasItem(slot) && slot.petalType === petalType && slot.rarity === rarity
      ? sum + Math.max(1, slot.count || 1)
      : sum
  ), 0);
}

function getActiveCraftStagedCount(petalType, rarity) {
  if (state.craftPhase !== "staged" && state.craftPhase !== "spinning") return 0;
  return getCraftSlotCount(petalType, rarity);
}

function getCraftHeldCount(petalType, rarity) {
  if (state.craftPhase === "idle") return 0;
  if (state.craftPhase === "success") {
    return slotHasItem(state.craftResult) &&
      state.craftResult.petalType === petalType &&
      state.craftResult.rarity === rarity
      ? Math.max(1, state.craftResult.count || 1)
      : 0;
  }
  return getCraftSlotCount(petalType, rarity);
}

function canLightCraftItem(petalType, rarity) {
  if (state.craftPhase === "spinning") return false;
  if (!canCraftRarity(rarity)) return false;
  const visible = Math.max(0, getInventoryCount(petalType, rarity) - getCraftHeldCount(petalType, rarity));
  return visible > 0 && visible + getCraftSlotCount(petalType, rarity) >= 5;
}

function canCraftRarity(rarity) {
  return rarity >= 1 && rarity <= 9;
}

function renderCraftStage() {
  craftStage.classList.toggle("spinning", state.craftPhase === "spinning");
  craftStage.classList.toggle("success", state.craftPhase === "success");
  craftStage.classList.toggle("burst", state.craftPhase === "returned" && performance.now() < state.craftBurstUntil);

  const showRing = state.craftPhase !== "success";
  craftStage.querySelectorAll("[data-craft-slot]").forEach((button) => {
    const index = Number(button.dataset.craftSlot || 0);
    const slot = state.craftSlots[index];
    button.replaceChildren();
    button.classList.toggle("hidden", !showRing);
    button.classList.toggle("filled", slotHasItem(slot));
    button.disabled = state.craftPhase === "spinning";
    button.title = slotHasItem(slot)
      ? `${petalTypeName(slot.petalType)} ${rarityName(slot.rarity)} x${formatShortCount(slot.count || 1)}`
      : "Craft slot";
    if (slotHasItem(slot)) button.appendChild(makePetalStack(slot.petalType, slot.rarity, slot.count || 1));
    button.onclick = (event) => {
      event.preventDefault();
      clearCraftDisplay();
      renderCraftPanel();
    };
  });

  craftResultSlot.replaceChildren();
  craftResultSlot.classList.toggle("hidden", state.craftPhase !== "success" || !slotHasItem(state.craftResult));
  if (state.craftPhase === "success" && slotHasItem(state.craftResult)) {
    craftResultSlot.title = `${petalTypeName(state.craftResult.petalType)} ${rarityName(state.craftResult.rarity)} x${formatShortCount(state.craftResult.count || 1)}`;
    craftResultSlot.appendChild(makePetalStack(state.craftResult.petalType, state.craftResult.rarity, state.craftResult.count || 1));
  }
  craftResultSlot.onclick = (event) => {
    event.preventDefault();
    clearCraftDisplay();
    renderCraftPanel();
  };

  const stagedCount = state.craftSource ? getCraftSlotCount(state.craftSource.petalType, state.craftSource.rarity) : 0;
  craftOnceBtn.disabled = !(state.authenticated && state.craftPhase === "staged" && stagedCount >= 5);
  craftChance.textContent = state.craftSource ? `${formatCraftChance(state.craftSource.rarity)} success chance` : "";
  if (state.craftPhase === "spinning") {
    craftSelection.textContent = "Crafting...";
  } else if (state.craftPhase === "success") {
    craftSelection.textContent = "Craft success";
  } else if (state.craftPhase === "returned") {
    craftSelection.textContent = "Craft failed";
  } else if (state.craftSource) {
    craftSelection.textContent = `${petalTypeName(state.craftSource.petalType)} ${rarityName(state.craftSource.rarity)} x${stagedCount}`;
  } else {
    craftSelection.textContent = "Combine 5 of the same petal to craft an upgrade";
  }
}

function formatCraftChance(rarity) {
  if (rarity === 8) return "0.33%";
  if (rarity === 9) return "0.1%";
  if (rarity >= 1 && rarity < 8) {
    const chance = 64 / (2 ** (rarity - 1));
    return `${chance.toFixed(chance >= 1 ? 0 : 2)}%`;
  }
  return "0%";
}

function submitCraft() {
  if (!state.authenticated || state.craftPhase !== "staged" || !state.craftSource) return;
  const { petalType, rarity } = state.craftSource;
  if (!canCraftRarity(rarity)) return;
  const count = getCraftSlotCount(petalType, rarity);
  if (count < 5 || count > getInventoryCount(petalType, rarity)) return;
  state.craftPhase = "spinning";
  state.craftSpinStarted = performance.now();
  renderCraftPanel();
  sendBytes(packCraft(petalType, rarity, count));
}

function handleCraftResult(msg) {
  const finish = () => {
    const items = compactCraftItems(msg.items || []);
    if (msg.success) {
      state.craftPhase = "success";
      state.craftSlots = Array.from({ length: 5 }, () => null);
      state.craftResult = pickCraftSuccessItem(items, msg) || items[0] || null;
    } else {
      state.craftPhase = "returned";
      state.craftResult = null;
      state.craftSlots = distributeCraftItems(items);
      state.craftBurstUntil = performance.now() + 420;
    }
    state.craftSource = null;
    renderCraftPanel();
  };

  const elapsed = performance.now() - (state.craftSpinStarted || 0);
  window.setTimeout(finish, Math.max(0, 700 - elapsed));
}

function compactCraftItems(items) {
  const compacted = [];
  for (const item of items) {
    if (!slotHasItem(item) || item.count <= 0) continue;
    const existing = compacted.find((entry) => entry.petalType === item.petalType && entry.rarity === item.rarity);
    if (existing) existing.count = clamp(existing.count + item.count, 0, 1000000000);
    else compacted.push({ petalType: item.petalType, rarity: item.rarity, count: item.count });
  }
  return compacted.sort((a, b) => b.rarity - a.rarity || petalTypeName(a.petalType).localeCompare(petalTypeName(b.petalType), "en"));
}

function pickCraftSuccessItem(items, msg) {
  return items.find((item) => item.petalType === msg.petalType && item.rarity !== msg.rarity) || null;
}

function distributeCraftItems(items) {
  if (items.length === 1) {
    const item = items[0];
    return distributeCraftCount(item.petalType, item.rarity, item.count || 0);
  }

  const slots = Array.from({ length: 5 }, () => null);
  let slotIndex = 0;
  for (const item of items) {
    let remaining = Math.max(0, item.count || 0);
    while (remaining > 0 && slotIndex < slots.length) {
      const count = slotIndex === slots.length - 1 ? remaining : 1;
      slots[slotIndex] = { petalType: item.petalType, rarity: item.rarity, count };
      remaining -= count;
      slotIndex += 1;
    }
  }
  return slots;
}

function distributeCraftCount(petalType, rarity, count) {
  const slots = Array.from({ length: 5 }, () => null);
  const total = Math.max(0, Math.min(1000000000, Math.floor(count || 0)));
  const base = Math.floor(total / slots.length);
  const extra = total % slots.length;
  for (let i = 0; i < slots.length; i += 1) {
    const slotCount = base + (i < extra ? 1 : 0);
    if (slotCount > 0) slots[i] = { petalType, rarity, count: slotCount };
  }
  return slots;
}

function clearCraftDisplay() {
  if (state.craftPhase === "spinning") return;
  state.craftSlots = Array.from({ length: 5 }, () => null);
  state.craftSource = null;
  state.craftResult = null;
  state.craftPhase = "idle";
  state.craftBurstUntil = 0;
}

function makeSlotButton(slot, index, kind) {
  const button = document.createElement("button");
  button.className = `slot${slotHasItem(slot) ? "" : " empty"}${kind === "primary" && index === state.selectedSlot ? " selected" : ""}`;
  button.type = "button";
  button.dataset.dropKind = kind;
  button.dataset.dropIndex = String(index);

  if (slotHasItem(slot)) {
    button.title = `${petalTypeName(slot.petalType)} ${rarityName(slot.rarity)}`;
    button.appendChild(makePetalStack(slot.petalType, slot.rarity, 0, slotVisualPetalType(slot, index, kind)));
    button.addEventListener("pointerdown", (event) => beginDrag(event, button, kind, index, slot));
  }

  if (kind === "primary") {
    button.addEventListener("click", () => {
      if (shouldSuppressClick()) return;
      state.selectedSlot = index;
      renderInventoryPanel();
    });
    button.addEventListener("contextmenu", (event) => {
      event.preventDefault();
      unequipSlot(index);
    });
  } else {
    button.addEventListener("click", () => {
      if (shouldSuppressClick()) return;
      quickSwapSlot(index);
    });
    button.addEventListener("contextmenu", (event) => {
      event.preventDefault();
      clearSecondarySlot(index);
    });
  }
  return button;
}

function slotVisualPetalType(slot, index, kind) {
  if (!slotHasItem(slot) || slot.petalType !== petalMimicType) return slot ? slot.petalType : 0;
  if (kind === "secondary") return slot.petalType;

  const slots = state.ownerSlots;
  if (!slots.length) return slot.petalType;

  const targetIndex = index <= 0 ? slots.length - 1 : index - 1;
  const target = slots[targetIndex];
  if (slotHasItem(target) && target.petalType !== petalMimicType) return target.petalType;
  return slot.petalType;
}

function makePetalStack(petalType, rarity, count = 0, iconPetalType = petalType) {
  const stack = document.createElement("span");
  stack.className = "petal-stack";
  const mimicVisual = petalType === petalMimicType && iconPetalType !== petalType;
  if (mimicVisual) stack.classList.add("mimic-visual");

  const base = document.createElement("img");
  base.className = "petal-bg";
  base.alt = "";
  base.draggable = false;
  base.src = petalBasePath(rarity);
  stack.appendChild(base);

  const icon = makePetalIcon(iconPetalType, rarity, { stripped: mimicVisual });
  if (icon) stack.appendChild(icon);
  if (mimicVisual) {
    const label = makeMimicLabelIcon();
    if (label) stack.appendChild(label);
  }
  if (count > 1) {
    const badge = document.createElement("span");
    badge.className = "petal-count";
    badge.textContent = `x${formatShortCount(count)}`;
    stack.appendChild(badge);
  }
  return stack;
}

function formatShortCount(value) {
  const count = Math.max(0, Math.min(1000000000, Math.floor(Number(value) || 0)));
  if (count >= 1000000000) return "1b";
  if (count >= 1000000) return `${formatCompact(count / 1000000)}m`;
  if (count >= 1000) return `${formatCompact(count / 1000)}k`;
  return String(count);
}

function formatCompact(value) {
  if (value >= 100) return String(Math.floor(value));
  if (value >= 10) return (Math.floor(value * 10) / 10).toFixed(1).replace(/\.0$/, "");
  return (Math.floor(value * 10) / 10).toFixed(1).replace(/\.0$/, "");
}

function makePetalIcon(petalType, rarity = 0, options = {}) {
  const src = petalIconPath(petalType, rarity);
  if (!src) return null;
  const image = document.createElement("img");
  image.className = "petal-icon";
  image.alt = "";
  image.draggable = false;
  if (options.stripped) setStrippedPetalIconSource(image, src);
  else image.src = src;
  image.addEventListener("error", () => image.remove(), { once: true });
  return image;
}

function setStrippedPetalIconSource(image, src) {
  let entry = strippedPetalIconUrls.get(src);
  if (entry && entry.url) {
    image.src = entry.url;
    return;
  }

  if (!entry) {
    entry = { url: "", waiters: [] };
    strippedPetalIconUrls.set(src, entry);
    fetch(src)
      .then((response) => {
        if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
        return response.text();
      })
      .then((svgText) => {
        const blob = new Blob([stripLivePetalLabel(svgText)], { type: "image/svg+xml" });
        entry.url = URL.createObjectURL(blob);
        for (const waiter of entry.waiters) waiter.src = entry.url;
        entry.waiters = [];
      })
      .catch(() => {
        entry.url = src;
        for (const waiter of entry.waiters) waiter.src = src;
        entry.waiters = [];
      });
  }

  entry.waiters.push(image);
}

function makeMimicLabelIcon() {
  const src = petalIconPath(petalMimicType);
  if (!src) return null;
  const image = document.createElement("img");
  image.className = "petal-mimic-label";
  image.alt = "";
  image.draggable = false;
  setMimicLabelIconSource(image, src);
  image.addEventListener("error", () => image.remove(), { once: true });
  return image;
}

function setMimicLabelIconSource(image, src) {
  let entry = mimicLabelIconUrls.get(src);
  if (entry && entry.url) {
    image.src = entry.url;
    return;
  }

  if (!entry) {
    entry = { url: "", waiters: [] };
    mimicLabelIconUrls.set(src, entry);
    fetch(src)
      .then((response) => {
        if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
        return response.text();
      })
      .then((svgText) => {
        const blob = new Blob([extractMimicLabelSvg(svgText)], { type: "image/svg+xml" });
        entry.url = URL.createObjectURL(blob);
        for (const waiter of entry.waiters) waiter.src = entry.url;
        entry.waiters = [];
      })
      .catch(() => {
        entry.url = "";
        for (const waiter of entry.waiters) waiter.remove();
        entry.waiters = [];
      });
  }

  entry.waiters.push(image);
}

function rarityBaseIndex(rarity) {
  return clamp((rarity || 1) - 1, 0, 10);
}

function petalBasePath(rarity) {
  return `./assets/petals/0_${rarityBaseIndex(rarity)}.svg`;
}

function petalIconPath(petalType, rarity = 0, options = {}) {
  if (!options.live && petalType === petalStingerType && rarity >= stingerSplitIconMinRarity)
    return "./assets/petals/6_5.svg";

  const iconId = PetalIconIds[petalType] || 0;
  if (!iconId) return "";
  return `./assets/petals/${iconId}.svg`;
}

function normalizePetalName(name) {
  return String(name || "").toLowerCase().replace(/[^a-z0-9]/g, "");
}

function petalTypeFromSnap(snap) {
  const fromEntity = petalTypeFromEntity(snap.entityType);
  if (fromEntity > 0 && fromEntity < PetalIconIds.length) return fromEntity;

  const fromName = PetalNameToType.get(normalizePetalName(snap.name));
  return fromName || fromEntity;
}

function assetImage(src) {
  if (!src) return null;
  let image = assetImages.get(src);
  if (!image) {
    image = new Image();
    image.decoding = "async";
    image.addEventListener("load", () => requestAnimationFrame(drawScene), { once: true });
    image.addEventListener("error", () => {
      image.failed = true;
      requestAnimationFrame(drawScene);
    }, { once: true });
    image.src = src;
    assetImages.set(src, image);
  }
  return image;
}

function imageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

function ownerHasPetal(petalType) {
  return state.ownerSlots.some((slot) => slotHasItem(slot) && slot.petalType === petalType);
}

function playerSnapHasPetal(snap, petalType) {
  return (snap.primarySlots || []).some((slot) => slotHasItem(slot) && slot.petalType === petalType);
}

function equipFromInventory(item) {
  const index = clamp(state.selectedSlot, 0, state.ownerSlots.length - 1);
  equipFromInventoryAt(index, item);
}

function equipFromInventoryAt(index, item) {
  if (!state.authenticated || !slotHasItem(item)) return;
  if (index < 0 || index >= state.ownerSlots.length) return;
  const oldSlot = copySlot(state.ownerSlots[index]);
  sendBytes(packEquip(index, item.petalType, item.rarity));
  state.ownerSlots[index] = copySlot(item);
  adjustInventory(item.petalType, item.rarity, -1);
  if (slotHasItem(oldSlot)) adjustInventory(oldSlot.petalType, oldSlot.rarity, 1);
  renderInventoryPanel();
}

function unequipSlot(index) {
  if (!state.authenticated || !state.ownerSlots[index] || !slotHasItem(state.ownerSlots[index])) return;
  const oldSlot = copySlot(state.ownerSlots[index]);
  sendBytes(packUnequip(index));
  state.ownerSlots[index] = emptySlot();
  adjustInventory(oldSlot.petalType, oldSlot.rarity, 1);
  renderInventoryPanel();
}

function setSecondaryFromInventory(index, item) {
  if (!state.authenticated || !slotHasItem(item)) return;
  if (index < 0 || index >= state.ownerSlots.length) return;
  const oldSlot = copySlot(state.secondarySlots[index]);
  sendBytes(packSecondarySlot(index, item.petalType, item.rarity));
  state.secondarySlots[index] = copySlot(item);
  adjustInventory(item.petalType, item.rarity, -1);
  if (slotHasItem(oldSlot)) adjustInventory(oldSlot.petalType, oldSlot.rarity, 1);
  renderInventoryPanel();
}

function clearSecondarySlot(index) {
  if (!state.authenticated || !state.secondarySlots[index] || !slotHasItem(state.secondarySlots[index])) return;
  const oldSlot = copySlot(state.secondarySlots[index]);
  sendBytes(packSecondarySlot(index, 0, 0));
  state.secondarySlots[index] = emptySlot();
  adjustInventory(oldSlot.petalType, oldSlot.rarity, 1);
  renderInventoryPanel();
}

function quickSwapSlot(index) {
  if (index < 0 || index >= state.ownerSlots.length) return;
  if (!state.authenticated || !state.secondarySlots[index] || !slotHasItem(state.secondarySlots[index])) return;

  const target = state.secondarySlots[index];
  const oldPrimary = state.ownerSlots[index] && slotHasItem(state.ownerSlots[index]) ? state.ownerSlots[index] : null;
  sendBytes(packSecondarySlot(index, 0, 0));
  if (oldPrimary) sendBytes(packUnequip(index));
  sendBytes(packEquip(index, target.petalType, target.rarity));
  if (oldPrimary) sendBytes(packSecondarySlot(index, oldPrimary.petalType, oldPrimary.rarity));
  state.ownerSlots[index] = { ...target };
  state.secondarySlots[index] = oldPrimary ? { ...oldPrimary } : emptySlot();
  renderInventoryPanel();
}

function adjustInventory(petalType, rarity, delta) {
  if (!petalType || !rarity || delta === 0) return;
  const existing = state.inventory.find((item) => item.petalType === petalType && item.rarity === rarity);
  if (existing) {
    existing.count = clamp(Math.max(0, (existing.count || 0) + delta), 0, 1000000000);
    if (existing.count <= 0) state.inventory = state.inventory.filter((item) => item !== existing);
    return;
  }
  if (delta > 0) state.inventory.push({ petalType, rarity, count: clamp(delta, 0, 1000000000) });
}

function movePrimaryToPrimary(fromIndex, toIndex) {
  if (fromIndex === toIndex) return;
  if (!state.authenticated || !slotHasItem(state.ownerSlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= state.ownerSlots.length) return;

  const source = copySlot(state.ownerSlots[fromIndex]);
  const target = copySlot(state.ownerSlots[toIndex]);
  sendBytes(packUnequip(fromIndex));
  if (slotHasItem(target)) sendBytes(packUnequip(toIndex));
  sendBytes(packEquip(toIndex, source.petalType, source.rarity));
  if (slotHasItem(target)) sendBytes(packEquip(fromIndex, target.petalType, target.rarity));

  state.ownerSlots[toIndex] = source;
  state.ownerSlots[fromIndex] = target;
  state.selectedSlot = toIndex;
  renderInventoryPanel();
}

function movePrimaryToSecondary(fromIndex, toIndex) {
  if (!state.authenticated || !slotHasItem(state.ownerSlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= state.ownerSlots.length) return;

  const source = copySlot(state.ownerSlots[fromIndex]);
  const oldSecondary = copySlot(state.secondarySlots[toIndex]);
  sendBytes(packUnequip(fromIndex));
  sendBytes(packSecondarySlot(toIndex, source.petalType, source.rarity));
  if (slotHasItem(oldSecondary)) sendBytes(packEquip(fromIndex, oldSecondary.petalType, oldSecondary.rarity));

  state.ownerSlots[fromIndex] = oldSecondary;
  state.secondarySlots[toIndex] = source;
  renderInventoryPanel();
}

function moveSecondaryToPrimary(fromIndex, toIndex) {
  if (!state.authenticated || !slotHasItem(state.secondarySlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= state.ownerSlots.length) return;

  const source = copySlot(state.secondarySlots[fromIndex]);
  const oldPrimary = copySlot(state.ownerSlots[toIndex]);
  sendBytes(packSecondarySlot(fromIndex, 0, 0));
  if (slotHasItem(oldPrimary)) sendBytes(packUnequip(toIndex));
  sendBytes(packEquip(toIndex, source.petalType, source.rarity));
  if (slotHasItem(oldPrimary)) sendBytes(packSecondarySlot(fromIndex, oldPrimary.petalType, oldPrimary.rarity));

  state.ownerSlots[toIndex] = source;
  state.secondarySlots[fromIndex] = oldPrimary;
  state.selectedSlot = toIndex;
  renderInventoryPanel();
}

function moveSecondaryToSecondary(fromIndex, toIndex) {
  if (fromIndex === toIndex) return;
  if (!state.authenticated || !slotHasItem(state.secondarySlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= state.ownerSlots.length) return;

  const source = copySlot(state.secondarySlots[fromIndex]);
  const target = copySlot(state.secondarySlots[toIndex]);
  sendBytes(packSecondarySlot(fromIndex, 0, 0));
  sendBytes(packSecondarySlot(toIndex, source.petalType, source.rarity));
  if (slotHasItem(target)) sendBytes(packSecondarySlot(fromIndex, target.petalType, target.rarity));

  state.secondarySlots[fromIndex] = target;
  state.secondarySlots[toIndex] = source;
  renderInventoryPanel();
}

function beginDrag(event, element, source, index, item) {
  if (event.button !== 0 || !slotHasItem(item)) return;
  if (!state.authenticated && source !== "inventory") return;
  state.drag = {
    source,
    index,
    item: copySlot(item),
    element,
    active: false,
    startX: event.clientX,
    startY: event.clientY,
    x: event.clientX,
    y: event.clientY,
    targetElement: null,
    ghost: null,
  };
}

function updateDrag(event) {
  const drag = state.drag;
  if (!drag) return;

  drag.x = event.clientX;
  drag.y = event.clientY;
  const moved = Math.hypot(drag.x - drag.startX, drag.y - drag.startY);
  if (!drag.active && moved >= 5) {
    drag.active = true;
    drag.element.classList.add("dragging");
    drag.ghost = makeDragGhost(drag.item);
    document.body.appendChild(drag.ghost);
  }

  if (!drag.active) return;
  event.preventDefault();
  positionDragGhost(drag);
  highlightDropTarget(dropTargetFromPoint(drag.x, drag.y));
}

function finishDrag() {
  const drag = state.drag;
  if (!drag) return;

  if (drag.active) {
    state.suppressClickUntil = performance.now() + 240;
    performDrop(dropTargetFromPoint(drag.x, drag.y), drag);
  }

  clearDrag();
}

function clearDrag() {
  const drag = state.drag;
  if (!drag) return;
  drag.element.classList.remove("dragging");
  if (drag.ghost) drag.ghost.remove();
  highlightDropTarget(null);
  state.drag = null;
}

function makeDragGhost(item) {
  const ghost = document.createElement("div");
  ghost.className = "drag-ghost";
  ghost.style.borderColor = rarityColor(item.rarity, 0.82);
  ghost.innerHTML = `<strong>${petalTypeName(item.petalType)}</strong><span>${rarityName(item.rarity)}</span>`;
  return ghost;
}

function positionDragGhost(drag) {
  if (!drag.ghost) return;
  drag.ghost.style.transform = `translate(${drag.x + 12}px, ${drag.y + 12}px)`;
}

function dropTargetFromPoint(x, y) {
  const element = document.elementFromPoint(x, y);
  const target = element && element.closest ? element.closest("[data-drop-kind]") : null;
  if (!target) return null;
  const kind = target.dataset.dropKind;
  if (kind !== "primary" && kind !== "secondary" && kind !== "backpack") return null;
  return {
    element: target,
    kind,
    index: Number(target.dataset.dropIndex ?? -1),
  };
}

function highlightDropTarget(target) {
  const drag = state.drag;
  const oldTarget = drag && drag.targetElement;
  if (oldTarget && (!target || target.element !== oldTarget)) oldTarget.classList.remove("drag-target");
  if (drag) drag.targetElement = target ? target.element : null;
  if (target) target.element.classList.add("drag-target");
}

function performDrop(target, drag) {
  if (!target) return;

  if (target.kind === "backpack") {
    if (drag.source === "primary") unequipSlot(drag.index);
    else if (drag.source === "secondary") clearSecondarySlot(drag.index);
    return;
  }

  if (drag.source === "inventory") {
    if (target.kind === "primary") equipFromInventoryAt(target.index, drag.item);
    else if (target.kind === "secondary") setSecondaryFromInventory(target.index, drag.item);
    return;
  }

  if (drag.source === "primary") {
    if (target.kind === "primary") movePrimaryToPrimary(drag.index, target.index);
    else if (target.kind === "secondary") movePrimaryToSecondary(drag.index, target.index);
    return;
  }

  if (drag.source === "secondary") {
    if (target.kind === "primary") moveSecondaryToPrimary(drag.index, target.index);
    else if (target.kind === "secondary") moveSecondaryToSecondary(drag.index, target.index);
  }
}

function resizeCanvas() {
  state.dpr = Math.max(1, Math.min(2, window.devicePixelRatio || 1));
  state.canvasWidth = window.innerWidth;
  state.canvasHeight = window.innerHeight;
  canvas.width = Math.round(state.canvasWidth * state.dpr);
  canvas.height = Math.round(state.canvasHeight * state.dpr);
  canvas.style.width = `${state.canvasWidth}px`;
  canvas.style.height = `${state.canvasHeight}px`;
  ctx.setTransform(state.dpr, 0, 0, state.dpr, 0, 0);
}

function updateRenderPositions(dt) {
  const owner = state.entities.get(state.ownerEntityId);
  if (owner && !owner.dying) {
    updateEntityMotion(owner, dt);
    const distance = Math.hypot(owner.snapshot.pos.x - owner.renderPos.x, owner.snapshot.pos.y - owner.renderPos.y);
    const blend = distance > predictionSnapDistance ? 1 : smoothFactor(ownerPredictionCorrectionRate, dt);
    owner.renderPos.x += (owner.snapshot.pos.x - owner.renderPos.x) * blend;
    owner.renderPos.y += (owner.snapshot.pos.y - owner.renderPos.y) * blend;
    owner.renderAngle = smoothAngle(owner.renderAngle, owner.snapshot.angle, 14, dt);
    state.camera.x += (owner.renderPos.x - state.camera.x) * smoothFactor(ownerCameraFollowRate, dt);
    state.camera.y += (owner.renderPos.y - state.camera.y) * smoothFactor(ownerCameraFollowRate, dt);
  }

  const dead = [];
  for (const [id, entity] of state.entities) {
    if (entity.dying) {
      entity.deathAge = (entity.deathAge || 0) + dt;
      if (entity.deathAge >= deathFadeDuration) dead.push(id);
      continue;
    }
    updateEntityMotion(entity, dt);
    if (id === state.ownerEntityId) continue;
    entity.renderPos.x += (entity.snapshot.pos.x - entity.renderPos.x) * smoothFactor(16, dt);
    entity.renderPos.y += (entity.snapshot.pos.y - entity.renderPos.y) * smoothFactor(16, dt);
    entity.renderAngle = smoothAngle(entity.renderAngle, entity.snapshot.angle,
                                     entity.snapshot.entityType === hornetType ? 40 : 10, dt);
  }

  for (const id of dead) {
    state.entities.delete(id);
    clearLadybugPattern(id);
  }
}

function updateEntityMotion(entity, dt) {
  const rawMove = entity.snapshotMove || 0;
  const target = rawMove < 0.16 ? 0 : clamp((rawMove - 0.16) / 1.25, 0, 1);
  const current = entity.motionBlend || 0;
  const rate = target > current ? 18 : 14;
  entity.motionBlend = current + (target - current) * smoothFactor(rate, dt);
  if (entity.motionBlend < 0.04) entity.motionBlend = 0;
}

function smoothFactor(rate, dt) {
  return 1 - Math.exp(-rate * dt);
}

function smoothAngle(current, target, rate, dt) {
  if (!Number.isFinite(current)) return Number.isFinite(target) ? target : 0;
  if (!Number.isFinite(target)) return current;
  const delta = Math.atan2(Math.sin(target - current), Math.cos(target - current));
  return current + delta * smoothFactor(rate, dt);
}

function screenViewRadius() {
  const shortSide = Math.max(1, Math.min(state.canvasWidth, state.canvasHeight));
  return Math.max(96, shortSide * viewScreenFill - viewScreenPadding);
}

function scaleForViewRadius(viewRadius) {
  if (!viewRadius || viewRadius <= 0) return 1;
  return screenViewRadius() / viewRadius;
}

function worldScale() {
  return scaleForViewRadius(state.viewRadius);
}

function worldLengthToScreen(value) {
  return value * worldScale();
}

function worldToScreen(pos) {
  const scale = worldScale();
  return {
    x: (pos.x - state.camera.x) * scale + state.canvasWidth * 0.5,
    y: (pos.y - state.camera.y) * scale + state.canvasHeight * 0.5,
  };
}

function canonicalTileGid(raw) {
  return (raw >>> 0) & 0x1fffffff;
}

function transformTilePoint(x, y, width, height, flipH, flipV, flipD) {
  if (flipD) {
    const oldX = x;
    x = y;
    y = oldX;
  }
  if (flipH) x = width - x;
  if (flipV) y = height - y;
  return { x, y };
}

function drawTileImage(image, pos, width, height, raw) {
  const flipH = (raw & 0x80000000) !== 0;
  const flipV = (raw & 0x40000000) !== 0;
  const flipD = (raw & 0x20000000) !== 0;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (flipH || flipV || flipD) {
    const origin = transformTilePoint(0, 0, width, height, flipH, flipV, flipD);
    const xAxis = transformTilePoint(width, 0, width, height, flipH, flipV, flipD);
    const yAxis = transformTilePoint(0, height, width, height, flipH, flipV, flipD);
    ctx.transform(
      (xAxis.x - origin.x) / width,
      (xAxis.y - origin.y) / width,
      (yAxis.x - origin.x) / height,
      (yAxis.y - origin.y) / height,
      origin.x,
      origin.y,
    );
  }
  const bleed = 0.75;
  ctx.drawImage(image, -bleed, -bleed, width + bleed * 2, height + bleed * 2);
  ctx.restore();
}

function drawMapTiles() {
  const map = state.map;
  if (!map || !map.layers || !map.tileImages) return;

  const scale = worldScale();
  const tileW = map.tileWidth;
  const tileH = map.tileHeight;
  if (tileW <= 0 || tileH <= 0) return;

  const worldLeft = state.camera.x - state.canvasWidth * 0.5 / scale - tileW;
  const worldTop = state.camera.y - state.canvasHeight * 0.5 / scale - tileH;
  const worldRight = state.camera.x + state.canvasWidth * 0.5 / scale + tileW;
  const worldBottom = state.camera.y + state.canvasHeight * 0.5 / scale + tileH;

  for (const layer of map.layers) {
    if (!layer.width || !layer.height || !layer.tiles || layer.tiles.length <= 0) continue;

    const minX = Math.floor(worldLeft / tileW);
    const minY = Math.floor(worldTop / tileH);
    const maxX = Math.ceil(worldRight / tileW);
    const maxY = Math.ceil(worldBottom / tileH);
    const maxTileX = Math.max(0, layer.width - 1);
    const maxTileY = Math.max(0, layer.height - 1);

    for (let y = minY; y <= maxY; y += 1) {
      const sampleY = clamp(y, 0, maxTileY);
      for (let x = minX; x <= maxX; x += 1) {
        const sampleX = clamp(x, 0, maxTileX);
        const raw = layer.tiles[sampleY * layer.width + sampleX] >>> 0;
        const gid = canonicalTileGid(raw);
        if (!gid) continue;
        const image = map.tileImages.get(gid);
        if (!image || !image.complete || image.naturalWidth <= 0) continue;

        const pos = worldToScreen({ x: x * tileW, y: y * tileH });
        const width = tileW * scale;
        const height = tileH * scale;
        drawTileImage(image, pos, width, height, raw);
      }
    }
  }
}

function drawGrid() {
  ctx.fillStyle = "#edf3f7";
  ctx.fillRect(0, 0, state.canvasWidth, state.canvasHeight);
  drawMapTiles();

  if (!state.map) {
    ctx.save();
    ctx.font = "13px Segoe UI, Microsoft YaHei, sans-serif";
    ctx.textAlign = "left";
    ctx.textBaseline = "top";
    ctx.fillStyle = "rgba(57, 70, 88, 0.72)";
    ctx.fillText(`Loading map: ${state.mapDisplayName || defaultMapName}`, 14, 72);
    ctx.restore();
  }

  if (!state.debugGrid) return;

  const scale = worldScale();
  drawTileDebugGrid(scale);

  let spacingWorld = 64;
  while (spacingWorld * scale < 42) spacingWorld *= 2;
  while (spacingWorld * scale > 130 && spacingWorld > 8) spacingWorld *= 0.5;
  const spacing = spacingWorld * scale;
  const startX = ((-state.camera.x * scale + state.canvasWidth * 0.5) % spacing + spacing) % spacing;
  const startY = ((-state.camera.y * scale + state.canvasHeight * 0.5) % spacing + spacing) % spacing;
  ctx.lineWidth = 1;
  ctx.strokeStyle = "rgba(184, 198, 214, 0.55)";
  ctx.beginPath();
  for (let x = startX; x < state.canvasWidth; x += spacing) {
    ctx.moveTo(x, 0);
    ctx.lineTo(x, state.canvasHeight);
  }
  for (let y = startY; y < state.canvasHeight; y += spacing) {
    ctx.moveTo(0, y);
    ctx.lineTo(state.canvasWidth, y);
  }
  ctx.stroke();

  const origin = worldToScreen({ x: 0, y: 0 });
  ctx.strokeStyle = "rgba(118, 136, 158, 0.42)";
  ctx.beginPath();
  ctx.moveTo(origin.x, 0);
  ctx.lineTo(origin.x, state.canvasHeight);
  ctx.moveTo(0, origin.y);
  ctx.lineTo(state.canvasWidth, origin.y);
  ctx.stroke();
}

function drawTileDebugGrid(scale) {
  const map = state.map;
  if (!map || !map.layers) return;

  const tileW = map.tileWidth;
  const tileH = map.tileHeight;
  if (tileW <= 0 || tileH <= 0) return;

  const width = (map.width || map.layers[0]?.width || 0) * tileW;
  const height = (map.height || map.layers[0]?.height || 0) * tileH;
  if (width <= 0 || height <= 0) return;

  const worldLeft = state.camera.x - state.canvasWidth * 0.5 / scale;
  const worldTop = state.camera.y - state.canvasHeight * 0.5 / scale;
  const worldRight = state.camera.x + state.canvasWidth * 0.5 / scale;
  const worldBottom = state.camera.y + state.canvasHeight * 0.5 / scale;
  const minX = Math.max(0, Math.floor(worldLeft / tileW));
  const minY = Math.max(0, Math.floor(worldTop / tileH));
  const maxX = Math.min(Math.ceil(width / tileW), Math.ceil(worldRight / tileW));
  const maxY = Math.min(Math.ceil(height / tileH), Math.ceil(worldBottom / tileH));

  ctx.save();
  ctx.lineWidth = 1;
  ctx.strokeStyle = "rgba(98, 116, 138, 0.28)";
  ctx.beginPath();
  for (let x = minX; x <= maxX; x += 1) {
    const sx = worldToScreen({ x: x * tileW, y: 0 }).x;
    ctx.moveTo(sx, 0);
    ctx.lineTo(sx, state.canvasHeight);
  }
  for (let y = minY; y <= maxY; y += 1) {
    const sy = worldToScreen({ x: 0, y: y * tileH }).y;
    ctx.moveTo(0, sy);
    ctx.lineTo(state.canvasWidth, sy);
  }
  ctx.stroke();
  ctx.restore();
}

function drawEntity(entity) {
  const snap = entity.snapshot;
  const pos = worldToScreen(entity.renderPos);
  const radius = Math.max(3, worldLengthToScreen(snap.radius));
  const isOwner = (snap.flags & flagOwner) !== 0 || snap.entityId === state.ownerEntityId;
  const isPetal = isPetalEntity(snap.entityType);
  const isDrop = isDropEntity(snap.entityType);
  const dead = (snap.flags & flagDead) !== 0;
  const deathProgress = entity.dying ? clamp((entity.deathAge || 0) / deathFadeDuration, 0, 1) : 0;
  const deathScale = entity.dying ? 1 + deathProgress * 0.45 : 1;
  const deathAlpha = entity.dying ? 1 - deathProgress : 1;

  ctx.save();
  ctx.globalAlpha = deathAlpha;

  if (!isPetal && (snap.entityType === beetleType || snap.entityType === summonedBeetleType)) {
    const summoned = snap.entityType === summonedBeetleType;
    drawBeetle(ctx, "./assets/beetle.svg", pos, radius * deathScale, snap.entityId,
               entity.renderAngle ?? snap.angle, entity.motionBlend || 0, performance.now() / 1000,
               { summoned, forwardOffset: radius * deathScale * beetleSpriteForwardOffsetScale });
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === normalLadybugType) {
    drawNormalLadybug(ctx, pos, radius * deathScale, snap.entityId, entity.renderAngle ?? snap.angle);
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && (snap.entityType === soldierAntType || snap.entityType === summonedSoldierAntType)) {
    const summoned = snap.entityType === summonedSoldierAntType;
    drawSoldierAnt(ctx, pos, radius * deathScale, snap.entityId, entity.renderAngle ?? snap.angle,
                   entity.motionBlend || 0, performance.now() / 1000, { summoned });
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === bandageBeetleType) {
    drawBeetle(ctx, "./assets/bandage_beetle.svg", pos, radius * deathScale, snap.entityId,
               entity.renderAngle ?? snap.angle, entity.motionBlend || 0, performance.now() / 1000,
               { forwardOffset: radius * deathScale * beetleSpriteForwardOffsetScale });
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === beeType) {
    drawBee(ctx, pos, radius * deathScale, snap.entityId, entity.renderAngle ?? snap.angle,
            entity.motionBlend || 0, performance.now() / 1000);
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === bumbleBeeType) {
    drawBumbleBee(ctx, pos, radius * deathScale, snap.entityId, entity.renderAngle ?? snap.angle,
                  entity.motionBlend || 0, performance.now() / 1000);
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === hornetType) {
    drawHornet(ctx, pos, radius * deathScale, snap.entityId, entity.renderAngle ?? snap.angle,
               entity.motionBlend || 0, performance.now() / 1000);
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && !isDrop && snap.name === "Pollen") {
    drawPollen(ctx, pos, radius * deathScale, snap.entityId, performance.now() / 1000);
    ctx.restore();
    return;
  }

  if (!isPetal && !isDrop && snap.name === "Missile") {
    drawHornetMissile(ctx, pos, radius * deathScale, entity.renderAngle ?? snap.angle);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === playerFlowerType) {
    const flowerAngle = dead ? (entity.deathAngle ?? entity.renderAngle ?? snap.angle) : (entity.renderAngle ?? snap.angle);
    drawPlayerFlower(snap, pos, radius * deathScale, flowerAngle, isOwner);
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (isDrop) {
    drawDropPetalCard(petalTypeFromSnap(snap), snap.rarity, pos, radius * deathScale);
    ctx.restore();
    return;
  }

  if (isPetal) {
    const petalType = petalTypeFromSnap(snap);
    drawLivePetal(petalType, snap.rarity, pos, radius * deathScale, entity.renderAngle ?? snap.angle, snap.entityId);
    if (petalType === petalCompassType) drawCompassNeedle(pos, radius * deathScale, entity.renderAngle ?? snap.angle);
    ctx.restore();
    return;
  }

  ctx.beginPath();
  ctx.arc(pos.x, pos.y, radius * deathScale, 0, Math.PI * 2);

  if (isOwner) ctx.fillStyle = "#ffe0e7";
  else if (isPetal || isDrop) ctx.fillStyle = rarityColor(snap.rarity, 0.82);
  else ctx.fillStyle = rarityColor(snap.rarity, 0.72);
  ctx.fill();

  ctx.lineWidth = (isOwner ? 3 : 1.5) * deathScale;
  if (isOwner && (snap.flags & flagDefending)) ctx.strokeStyle = "#467acd";
  else if (isOwner && (snap.flags & flagAttacking)) ctx.strokeStyle = "#be5e7a";
  else ctx.strokeStyle = (isPetal || isDrop) ? "#4f6680" : "rgba(48, 62, 82, 0.52)";
  ctx.stroke();

  if (isPetal && petalTypeFromSnap(snap) === petalCompassType)
    drawCompassNeedle(pos, radius * deathScale, entity.renderAngle ?? snap.angle);
  if (!isPetal && !isDrop && !entity.dying) drawMobFrame(snap, pos, radius);
  ctx.restore();
}

function drawPlayerFlower(snap, pos, radius, angle, isOwner) {
  const flags = snap.flags || 0;
  const hasAntennae = (flags & flagAntennae) !== 0 || playerSnapHasPetal(snap, petalAntennaeType) ||
    (isOwner && ownerHasPetal(petalAntennaeType));
  const hasBandage = playerSnapHasPetal(snap, petalBandageType) || (isOwner && ownerHasPetal(petalBandageType));
  const hasRelic = (flags & flagRelic) !== 0 || playerSnapHasPetal(snap, petalRelicType) ||
    (isOwner && ownerHasPetal(petalRelicType));
  const nullified = playerSnapHasPetal(snap, petalNullificationType) || (isOwner && ownerHasPetal(petalNullificationType));
  const corrupted = (flags & flagCorrupted) !== 0;
  const undead = (flags & flagUndead) !== 0;
  const dead = (flags & flagDead) !== 0;
  const suppressPetalOverlays = corrupted;

  if (hasAntennae && !suppressPetalOverlays) drawAntennaeUnderlay(pos, radius, angle);

  let texture = "normal";
  if (corrupted) texture = "gambler";
  else if (undead) texture = "undead";

  const image = assetImage(playerFlowerTexturePath(texture));
  const size = Math.max(18, radius * 2.36);
  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (dead && Number.isFinite(angle)) ctx.rotate(angle);
  if (nullified && !suppressPetalOverlays) ctx.globalAlpha *= 0.58;
  if (imageReady(image)) {
    ctx.drawImage(image, -size * 0.5, -size * 0.5, size, size);
  } else {
    drawPlayerFlowerFallback(radius, texture);
  }

  if (hasRelic && !corrupted && !undead) {
    ctx.globalCompositeOperation = "source-atop";
    ctx.fillStyle = "rgba(20, 24, 32, 0.38)";
    ctx.fillRect(-size * 0.5, -size * 0.5, size, size);
  }
  ctx.globalCompositeOperation = "source-over";
  if (hasBandage && !dead && !suppressPetalOverlays) drawBandagePetalLayer(radius);
  const attacking = (flags & flagAttacking) !== 0;
  const defending = !attacking && (flags & flagDefending) !== 0;
  drawPlayerFlowerFace(radius, angle, texture, dead ? "dead" : (attacking ? "attack" : (defending ? "defend" : "normal")));
  ctx.restore();
}

function drawMobSvg(src, pos, radius, angle) {
  const image = assetImage(src);
  const size = Math.max(1, radius * 2 * mobSpriteCoverScale);

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle)) ctx.rotate(angle);

  if (imageReady(image)) {
    ctx.drawImage(image, -size * 0.5, -size * 0.5, size, size);
  } else {
    ctx.fillStyle = "#7f678f";
    ctx.beginPath();
    ctx.arc(0, 0, radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = Math.max(1.5, radius * 0.12);
    ctx.strokeStyle = "rgba(35, 44, 54, 0.45)";
    ctx.stroke();
  }

  ctx.restore();
}

function playerFlowerTexturePath(texture = "normal") {
  const textureFile = texture === "undead" ? "player_flower_undead" :
                      texture === "gambler" ? "player_flower_gambler" :
                      "player_flower";
  return `./assets/${textureFile}.svg?v=${flowerTextureVersion}`;
}

function drawBandagePetalLayer(radius) {
  const icon = bandageOverlayImage();
  const size = Math.max(18, radius * 4.48);
  const offsetY = radius * 0.22;
  ctx.save();
  ctx.beginPath();
  ctx.arc(0, 0, radius * 1.08, 0, Math.PI * 2);
  ctx.clip();
  if (imageReady(icon)) {
    ctx.drawImage(icon, -size * 0.5, -size * 0.5 + offsetY, size, size);
  } else {
    drawBandagePetalLayerFallback(radius);
  }
  ctx.restore();
}

function drawBandagePetalLayerFallback(radius) {
  ctx.save();
  ctx.rotate(-0.26);
  ctx.fillStyle = "#e8dba6";
  ctx.strokeStyle = "#a89d73";
  ctx.lineJoin = "round";
  ctx.lineWidth = Math.max(1.1, radius * 0.07);
  const strips = [
    { y: -radius * 0.48, w: radius * 1.62 },
    { y: -radius * 0.08, w: radius * 1.94 },
    { y: radius * 0.34, w: radius * 1.18 },
  ];
  for (const strip of strips) {
    ctx.beginPath();
    ctx.rect(-strip.w * 0.5, strip.y - radius * 0.12, strip.w, radius * 0.24);
    ctx.fill();
    ctx.stroke();
    ctx.strokeStyle = "rgba(251, 241, 191, 0.82)";
    ctx.lineWidth = Math.max(1, radius * 0.055);
    ctx.beginPath();
    ctx.moveTo(-strip.w * 0.42, strip.y - radius * 0.04);
    ctx.lineTo(strip.w * 0.42, strip.y - radius * 0.04);
    ctx.stroke();
    ctx.strokeStyle = "#a89d73";
    ctx.lineWidth = Math.max(1.1, radius * 0.07);
  }
  ctx.restore();
}

function drawAntennaeUnderlay(pos, radius, angle) {
  const icon = assetImage(petalIconPath(petalAntennaeType));
  const size = Math.max(18, radius * 2.65);
  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle)) ctx.rotate(angle + Math.PI * 0.5);
  ctx.globalAlpha *= 0.9;
  if (imageReady(icon)) {
    const sourceWidth = icon.naturalWidth || 110;
    const sourceHeight = icon.naturalHeight || 110;
    const cropHeight = sourceHeight * 0.64;
    ctx.drawImage(icon, 0, 0, sourceWidth, cropHeight,
                  -size * 0.5, -size * 0.58, size, size * 0.66);
  } else {
    ctx.strokeStyle = "#111";
    ctx.lineWidth = Math.max(2, radius * 0.14);
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(-radius * 0.22, -radius * 0.08);
    ctx.quadraticCurveTo(-radius * 0.66, -radius * 0.9, -radius * 1.08, -radius * 1.24);
    ctx.moveTo(radius * 0.22, -radius * 0.08);
    ctx.quadraticCurveTo(radius * 0.66, -radius * 0.9, radius * 1.08, -radius * 1.24);
    ctx.stroke();
  }
  ctx.restore();
}

function drawPlayerFlowerFallback(radius, texture) {
  const centerFill = texture === "undead" ? "#a7c66f" :
                     texture === "gambler" ? "#c64136" :
                     "#f2cc42";
  const stroke = texture === "undead" ? "#73934c" :
                 texture === "gambler" ? "#782821" :
                 "#d9b638";
  ctx.fillStyle = centerFill;
  ctx.beginPath();
  ctx.arc(0, 0, radius * 0.88, 0, Math.PI * 2);
  ctx.fill();
  ctx.lineWidth = Math.max(2.2, radius * 0.18);
  ctx.strokeStyle = stroke;
  ctx.stroke();
}

function drawPlayerFlowerFace(radius, angle, texture, expression = "normal") {
  const look = Number.isFinite(angle) ? { x: Math.cos(angle), y: Math.sin(angle) } : { x: 0, y: 0 };
  const eyeFill = texture === "undead" ? "#27351f" : "#333127";
  const smileFill = texture === "undead" ? "#35472a" : "#5a4a26";

  ctx.save();
  ctx.translate(0, radius * 0.06);
  ctx.scale(0.86, 0.86);
  if (expression === "dead") drawPlayerFlowerDeadEyes(radius, eyeFill);
  else if (expression === "attack") drawPlayerFlowerAngryEyes(radius, look, eyeFill);
  else drawPlayerFlowerNormalEyes(radius, look, eyeFill);

  if (expression === "dead")
    drawPlayerFlowerDeadMouth(radius, smileFill);
  else if (expression === "attack" || expression === "defend")
    drawPlayerFlowerAngryMouth(radius, smileFill);
  else
    drawPlayerFlowerSmile(radius, texture, smileFill);
  ctx.restore();
}

function drawPlayerFlowerNormalEyes(radius, look, eyeFill) {
  const eyeOffset = radius * 0.055;
  const leftEye = { x: -radius * 0.23, y: -radius * 0.31 };
  const rightEye = { x: radius * 0.23, y: -radius * 0.31 };

  ctx.fillStyle = eyeFill;
  ctx.beginPath();
  ctx.ellipse(leftEye.x, leftEye.y, radius * 0.105, radius * 0.27, 0, 0, Math.PI * 2);
  ctx.ellipse(rightEye.x, rightEye.y, radius * 0.105, radius * 0.27, 0, 0, Math.PI * 2);
  ctx.fill();

  ctx.fillStyle = "rgba(255, 255, 222, 0.92)";
  ctx.beginPath();
  ctx.ellipse(leftEye.x - radius * 0.035 + look.x * eyeOffset,
              leftEye.y - radius * 0.09 + look.y * eyeOffset, radius * 0.032, radius * 0.065, 0, 0, Math.PI * 2);
  ctx.ellipse(rightEye.x - radius * 0.035 + look.x * eyeOffset,
              rightEye.y - radius * 0.09 + look.y * eyeOffset, radius * 0.032, radius * 0.065, 0, 0, Math.PI * 2);
  ctx.fill();
}

function drawPlayerFlowerAngryEyes(radius, look, eyeFill) {
  ctx.save();
  ctx.beginPath();
  ctx.moveTo(-radius * 0.48, -radius * 0.62);
  ctx.lineTo(0, -radius * 0.29);
  ctx.lineTo(radius * 0.48, -radius * 0.62);
  ctx.lineTo(radius * 0.48, radius * 0.16);
  ctx.lineTo(-radius * 0.48, radius * 0.16);
  ctx.closePath();
  ctx.clip();

  drawPlayerFlowerNormalEyes(radius, look, eyeFill);
  ctx.restore();
}

function drawPlayerFlowerDeadEyes(radius, eyeFill) {
  const centers = [
    { x: -radius * 0.23, y: -radius * 0.31 },
    { x: radius * 0.23, y: -radius * 0.31 },
  ];
  ctx.strokeStyle = eyeFill;
  ctx.lineWidth = Math.max(2, radius * 0.115);
  ctx.lineCap = "round";
  for (const eye of centers) {
    const dx = radius * 0.12;
    const dy = radius * 0.16;
    ctx.beginPath();
    ctx.moveTo(eye.x - dx, eye.y - dy);
    ctx.lineTo(eye.x + dx, eye.y + dy);
    ctx.moveTo(eye.x + dx, eye.y - dy);
    ctx.lineTo(eye.x - dx, eye.y + dy);
    ctx.stroke();
  }
}

function drawPlayerFlowerSmile(radius, texture, smileFill) {
  ctx.strokeStyle = smileFill;
  ctx.lineWidth = Math.max(1.5, radius * 0.075);
  ctx.lineCap = "round";
  ctx.beginPath();
  if (texture === "undead") {
    ctx.arc(0, radius * 0.37, radius * 0.2, Math.PI * 1.16, Math.PI * 1.84);
  } else {
    ctx.arc(0, radius * 0.02, radius * 0.29, 0.22 * Math.PI, 0.78 * Math.PI);
  }
  ctx.stroke();
}

function drawPlayerFlowerAngryMouth(radius, smileFill) {
  ctx.strokeStyle = smileFill;
  ctx.lineWidth = Math.max(1.5, radius * 0.075);
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.beginPath();
  ctx.moveTo(-radius * 0.22, radius * 0.27);
  ctx.quadraticCurveTo(0, radius * 0.16, radius * 0.22, radius * 0.27);
  ctx.stroke();
}

function drawPlayerFlowerDeadMouth(radius, smileFill) {
  ctx.strokeStyle = smileFill;
  ctx.lineWidth = Math.max(1.5, radius * 0.075);
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.beginPath();
  ctx.moveTo(-radius * 0.24, radius * 0.34);
  ctx.quadraticCurveTo(0, radius * 0.17, radius * 0.24, radius * 0.34);
  ctx.stroke();
}

function drawLivePetal(petalType, rarity, pos, radius, angle, entityId = 0) {
  const size = Math.max(1, radius * worldPetalSizeScale);

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (petalType !== petalMoonType && Number.isFinite(angle)) ctx.rotate(angle);

  if (petalType === petalDustType) {
    drawDustParticlePetal(size, rarity);
    ctx.restore();
    return;
  }

  if (petalType === petalMoonType) {
    drawProceduralMoonPetal(size, rarity, entityId);
    ctx.restore();
    return;
  }

  let icon = null;
  if (petalType === petalAntEggType) {
    icon = livePetalIconImage(petalType, rarity, extractSingleAntEggLiveSvg, "single-ant-egg");
  } else if (petalType === petalDahliaType) {
    icon = livePetalIconImage(petalType, rarity, extractSingleDahliaLiveSvg, "single-dahlia");
  } else {
    icon = livePetalIconImage(petalType, rarity);
  }

  if (imageReady(icon)) {
    drawCenteredImage(icon, size, 1);
  } else {
    drawPetalSilhouette(size, rarity);
  }

  ctx.restore();
}

function drawProceduralMoonPetal(size, rarity, entityId) {
  const radius = size * 0.16;
  const borderWidth = 5.2;
  const innerRadius = Math.max(1, radius - borderWidth);
  const seed = moonPatternSeed(entityId, rarity);
  const rand = seededRandom(seed);

  ctx.fillStyle = moonTone("border");
  ctx.beginPath();
  ctx.arc(0, 0, radius, 0, Math.PI * 2);
  ctx.fill();

  ctx.save();
  ctx.beginPath();
  ctx.arc(0, 0, innerRadius, 0, Math.PI * 2);
  ctx.clip();
  ctx.fillStyle = moonTone("base");
  ctx.fillRect(-innerRadius, -innerRadius, innerRadius * 2, innerRadius * 2);

  const craterCount = 5 + Math.floor(rand() * 5);
  for (let i = 0; i < craterCount; i += 1) {
    const angle = rand() * Math.PI * 2;
    const dist = Math.sqrt(rand()) * innerRadius * 0.9;
    const x = Math.cos(angle) * dist;
    const y = Math.sin(angle) * dist;
    const craterRadius = innerRadius * (0.08 + rand() * 0.13);
    const strokeWidth = Math.max(1.8, borderWidth * 0.45);

    ctx.lineWidth = strokeWidth;
    ctx.strokeStyle = moonTone("craterBorder");
    ctx.fillStyle = moonTone(rand() > 0.36 ? "crater" : "craterLight");
    ctx.beginPath();
    ctx.arc(x, y, craterRadius, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
  }

  ctx.restore();

  ctx.lineWidth = Math.max(1.5, borderWidth * 0.34);
  ctx.strokeStyle = moonTone("outerEdge");
  ctx.beginPath();
  ctx.arc(0, 0, Math.max(0, radius - ctx.lineWidth * 0.5), 0, Math.PI * 2);
  ctx.stroke();
}

function moonPatternSeed(entityId, rarity) {
  let seed = ((entityId || 0) * 1103515245 + (rarity || 0) * 2654435761) >>> 0;
  seed ^= seed >>> 16;
  seed = Math.imul(seed, 2246822507) >>> 0;
  seed ^= seed >>> 13;
  seed = Math.imul(seed, 3266489909) >>> 0;
  return seed >>> 0;
}

function seededRandom(seed) {
  let stateValue = seed || 0x6d2b79f5;
  return () => {
    stateValue = (stateValue + 0x6d2b79f5) >>> 0;
    let t = stateValue;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function moonTone(part) {
  switch (part) {
    case "border":
      return "#6b6b6b";
    case "outerEdge":
      return "#676767";
    case "craterBorder":
      return "#838383";
    case "craterLight":
      return "#a0a0a0";
    case "crater":
      return "#8a8a8a";
    case "base":
    default:
      return "#939393";
  }
}

function drawDustParticlePetal(size, rarity) {
  const drawSize = size * 0.32;
  const dustPoints = [
    [79.359, 47.857],
    [74.375, 56.514],
    [64.135, 55.46],
    [59.222, 47.857],
    [64.472, 39.004],
    [73.848, 39.732],
  ];
  const sourceCenterX = 69.291;
  const sourceCenterY = 47.737;
  const scale = drawSize / 24;

  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-sourceCenterX, -sourceCenterY);
  ctx.fillStyle = "rgba(102, 102, 102, 0.72)";
  ctx.strokeStyle = "rgba(0, 0, 0, 0.24)";
  ctx.lineWidth = 1.35;
  ctx.beginPath();
  for (let i = 0; i < dustPoints.length; i += 1) {
    const [x, y] = dustPoints[i];
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
  ctx.restore();
}

function drawDropPetalCard(petalType, rarity, pos, radius) {
  const size = Math.max(1, radius * worldDropSizeScale);
  const base = petalBaseImage(rarity);
  const icon = petalIconImage(petalType, rarity);

  ctx.save();
  ctx.translate(pos.x, pos.y);
  drawPetalCardShadow(size);

  if (imageReady(base)) {
    drawCenteredImage(base, size, 1);
  } else {
    drawPetalCardFallback(size, rarity);
  }

  if (imageReady(icon)) {
    drawCenteredImage(icon, size, petalCardIconScale);
  } else {
    drawPetalSilhouette(size * petalCardIconScale, rarity);
  }

  ctx.restore();
}

function petalIconImage(petalType, rarity = 0) {
  return assetImage(petalIconPath(petalType, rarity));
}

function livePetalIconImage(petalType, rarity = 0, transformSvg = stripLivePetalLabel, cacheSuffix = "live") {
  const src = petalIconPath(petalType, rarity, { live: true });
  if (!src) return null;

  const cacheKey = `${cacheSuffix}:${src}`;
  let entry = livePetalImages.get(cacheKey);
  if (entry) return entry.image;

  const image = new Image();
  image.decoding = "async";
  entry = { image, url: "" };
  livePetalImages.set(cacheKey, entry);

  fetch(src)
    .then((response) => {
      if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
      return response.text();
    })
    .then((svgText) => {
      const blob = new Blob([transformSvg(svgText)], { type: "image/svg+xml" });
      const url = URL.createObjectURL(blob);
      entry.url = url;
      image.addEventListener("load", () => requestAnimationFrame(drawScene), { once: true });
      image.addEventListener("error", () => {
        image.failed = true;
        if (entry.url) URL.revokeObjectURL(entry.url);
        requestAnimationFrame(drawScene);
      }, { once: true });
      image.src = url;
    })
    .catch(() => {
      image.failed = true;
      requestAnimationFrame(drawScene);
    });

  return image;
}

function extractSingleAntEggLiveSvg(svgText) {
  return extractFirstLivePetalPathsSvg(svgText, 2, "18 10 74 74");
}

function extractSingleDahliaLiveSvg(svgText) {
  return extractFirstLivePetalPathsSvg(svgText, 2, "54 33 31 31");
}

function extractFirstLivePetalPathsSvg(svgText, pathCount, viewBox) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return stripLivePetalLabel(svgText);

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return stripLivePetalLabel(svgText);

  const paths = Array.from(root.querySelectorAll("path")).slice(0, pathCount);
  if (!paths.length) return stripLivePetalLabel(svgText);

  const serializer = new XMLSerializer();
  const body = paths.map((node) => serializer.serializeToString(node)).join("");
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}">${body}</svg>`;
}

function bandageOverlayImage() {
  const src = petalIconPath(petalBandageType);
  if (!src) return null;

  let entry = bandageOverlayImages.get(src);
  if (entry) return entry.image;

  const image = new Image();
  image.decoding = "async";
  entry = { image, url: "" };
  bandageOverlayImages.set(src, entry);

  fetch(src)
    .then((response) => {
      if (!response.ok) throw new Error(`Bandage SVG failed: ${src}`);
      return response.text();
    })
    .then((svgText) => {
      const blob = new Blob([extractBandageOverlaySvg(svgText)], { type: "image/svg+xml" });
      const url = URL.createObjectURL(blob);
      entry.url = url;
      image.addEventListener("load", () => requestAnimationFrame(drawScene), { once: true });
      image.addEventListener("error", () => {
        image.failed = true;
        if (entry.url) URL.revokeObjectURL(entry.url);
        requestAnimationFrame(drawScene);
      }, { once: true });
      image.src = url;
    })
    .catch(() => {
      image.failed = true;
      requestAnimationFrame(drawScene);
    });

  return image;
}

function stripLivePetalLabel(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  root.querySelectorAll("text").forEach((node) => node.remove());

  const paths = Array.from(root.querySelectorAll("path"));
  const remove = new Set();
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      remove.add(outline);
      remove.add(fill);
      i += 1;
    }
  }
  remove.forEach((node) => node.remove());

  return new XMLSerializer().serializeToString(root);
}

function extractBandageOverlaySvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  root.querySelectorAll("text").forEach((node) => node.remove());
  root.querySelectorAll("path[fill-opacity]").forEach((node) => node.remove());

  const paths = Array.from(root.querySelectorAll("path"));
  const remove = new Set();
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      remove.add(outline);
      remove.add(fill);
      i += 1;
    }
  }
  remove.forEach((node) => node.remove());

  return new XMLSerializer().serializeToString(root);
}

function extractMimicLabelSvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  const paths = Array.from(root.querySelectorAll("path"));
  const labelPaths = [];
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      labelPaths.push(outline, fill);
      i += 1;
    }
  }

  const serializer = new XMLSerializer();
  const body = labelPaths.map((node) => serializer.serializeToString(node)).join("");
  if (!body) return svgText;
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="24 68 64 22">${body}</svg>`;
}

function isLabelOutlinePath(path) {
  const stroke = normalizeColorAttr(path.getAttribute("stroke"));
  const fill = normalizeColorAttr(path.getAttribute("fill"));
  const strokeWidth = Number.parseFloat(path.getAttribute("stroke-width") || "0");
  return (stroke === "#000" || stroke === "#000000") && (!fill || fill === "none") && strokeWidth >= 1.5;
}

function isMatchingLabelFillPath(outline, fill) {
  if (!fill) return false;
  const color = normalizeColorAttr(fill.getAttribute("fill"));
  return (color === "#fff" || color === "#ffffff") && outline.getAttribute("d") === fill.getAttribute("d");
}

function normalizeColorAttr(value) {
  return String(value || "").trim().toLowerCase();
}

function petalBaseImage(rarity) {
  return assetImage(petalBasePath(rarity));
}

function drawCenteredImage(image, size, scale) {
  const drawSize = size * scale;
  ctx.drawImage(image, -drawSize * 0.5, -drawSize * 0.5, drawSize, drawSize);
}

function drawPetalCardShadow(size) {
  const half = size * 0.5;
  ctx.save();
  ctx.globalAlpha *= 0.18;
  ctx.fillStyle = "#142033";
  ctx.fillRect(-half + size * 0.05, -half + size * 0.07, size, size);
  ctx.restore();
}

function drawPetalCardFallback(size, rarity) {
  const half = size * 0.5;
  ctx.fillStyle = rarityColor(rarity || 1, 0.95);
  ctx.fillRect(-half, -half, size, size);
  ctx.lineWidth = Math.max(2, size * 0.08);
  ctx.strokeStyle = "rgba(45, 55, 68, 0.42)";
  ctx.strokeRect(-half, -half, size, size);
}

function drawPetalSilhouette(size, rarity) {
  const scale = size / 110;
  ctx.save();
  ctx.scale(scale, scale);
  ctx.fillStyle = rarityColor(rarity || 1, 0.82);
  ctx.strokeStyle = "rgba(35, 44, 54, 0.52)";
  ctx.lineWidth = 4.5;
  ctx.beginPath();
  ctx.ellipse(-12, -2, 15, 30, -0.64, 0, Math.PI * 2);
  ctx.ellipse(12, -2, 15, 30, 0.64, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
  ctx.restore();
}

function drawCompassNeedle(pos, radius, angle) {
  const length = radius * 1.6;
  const dx = Math.cos(angle);
  const dy = Math.sin(angle);
  ctx.lineCap = "round";
  ctx.lineWidth = Math.max(2, radius * 0.18);
  ctx.strokeStyle = "#243241";
  ctx.beginPath();
  ctx.moveTo(pos.x - dx * radius * 0.2, pos.y - dy * radius * 0.2);
  ctx.lineTo(pos.x + dx * length, pos.y + dy * length);
  ctx.stroke();

  ctx.fillStyle = "#e94c4c";
  ctx.beginPath();
  ctx.moveTo(pos.x + dx * (length + 4), pos.y + dy * (length + 4));
  ctx.lineTo(pos.x + Math.cos(angle + 2.55) * radius * 0.45, pos.y + Math.sin(angle + 2.55) * radius * 0.45);
  ctx.lineTo(pos.x + Math.cos(angle - 2.55) * radius * 0.45, pos.y + Math.sin(angle - 2.55) * radius * 0.45);
  ctx.closePath();
  ctx.fill();
}

function isMobSnap(snap) {
  return snap && !isPetalEntity(snap.entityType) && !isDropEntity(snap.entityType) &&
         snap.entityType !== playerFlowerType;
}

function hasHealthFrame(snap) {
  return snap && !isPetalEntity(snap.entityType) && !isDropEntity(snap.entityType);
}

function mobDisplayName(snap) {
  return snap.name || mobTypeName(snap.entityType);
}

function rarityOrLevelLabel(snap) {
  if (snap.entityType === playerFlowerType) return `Lvl ${snap.rarity || 1}`;
  return rarityName(snap.rarity);
}

function roundedRectPath(x, y, width, height, radius = height * 0.5) {
  const r = Math.max(0, Math.min(radius, width * 0.5, height * 0.5));
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + width - r, y);
  ctx.arcTo(x + width, y, x + width, y + r, r);
  ctx.lineTo(x + width, y + height - r);
  ctx.arcTo(x + width, y + height, x + width - r, y + height, r);
  ctx.lineTo(x + r, y + height);
  ctx.arcTo(x, y + height, x, y + height - r, r);
  ctx.lineTo(x, y + r);
  ctx.arcTo(x, y, x + r, y, r);
  ctx.closePath();
}

function drawCapsuleBar(x, y, width, height, progress, fill, border = 4, track = "#09090b") {
  const safeWidth = Math.max(1, width);
  const safeHeight = Math.max(1, height);
  const inset = Math.min(border, safeWidth * 0.35, safeHeight * 0.35);
  const innerX = x + inset;
  const innerY = y + inset;
  const innerW = Math.max(0, safeWidth - inset * 2);
  const innerH = Math.max(0, safeHeight - inset * 2);
  const amount = clamp(progress || 0, 0, 1);

  ctx.fillStyle = "#050506";
  roundedRectPath(x, y, safeWidth, safeHeight);
  ctx.fill();

  if (innerW <= 0 || innerH <= 0) return;

  ctx.save();
  roundedRectPath(innerX, innerY, innerW, innerH);
  ctx.clip();
  ctx.fillStyle = track;
  ctx.fillRect(innerX, innerY, innerW, innerH);
  if (amount > 0) {
    ctx.fillStyle = fill;
    ctx.fillRect(innerX, innerY, innerW * amount, innerH);
  }
  ctx.restore();
}

function drawSegmentedCapsuleBar(x, y, width, height, members, fill, border = 5) {
  const safeMembers = members.length > 0 ? members : [{ hp: 0 }];
  const inset = Math.min(border, width * 0.35, height * 0.35);
  const innerX = x + inset;
  const innerY = y + inset;
  const innerW = Math.max(0, width - inset * 2);
  const innerH = Math.max(0, height - inset * 2);
  const segment = safeMembers.length > 0 ? innerW / safeMembers.length : innerW;

  ctx.fillStyle = "#050506";
  roundedRectPath(x, y, width, height);
  ctx.fill();

  if (innerW <= 0 || innerH <= 0) return;

  ctx.save();
  roundedRectPath(innerX, innerY, innerW, innerH);
  ctx.clip();
  ctx.fillStyle = "#111115";
  ctx.fillRect(innerX, innerY, innerW, innerH);
  safeMembers
    .sort((a, b) => b.hp - a.hp || a.distance - b.distance)
    .forEach((member, index) => {
      ctx.fillStyle = fill;
      ctx.globalAlpha = 0.98;
      ctx.fillRect(innerX + index * segment, innerY, segment * clamp(member.hp, 0, 1), innerH);
    });
  ctx.globalAlpha = 1;
  ctx.restore();
}

function drawOutlinedText(text, x, y, {
  font = "700 14px Segoe UI, Microsoft YaHei, sans-serif",
  fill = "#fff",
  stroke = "#050506",
  lineWidth = 4,
  align = "center",
  baseline = "middle",
} = {}) {
  ctx.font = font;
  ctx.textAlign = align;
  ctx.textBaseline = baseline;
  ctx.lineJoin = "round";
  ctx.lineWidth = lineWidth;
  ctx.strokeStyle = stroke;
  ctx.fillStyle = fill;
  ctx.strokeText(text, x, y);
  ctx.fillText(text, x, y);
}

function drawMobFrame(snap, pos, radius) {
  if (radius < entityFrameMinScreenRadius) return;

  if (!hasHealthFrame(snap)) {
    drawEntityLabel(snap, pos, radius);
    return;
  }

  const width = Math.max(10, radius * 2.08);
  const barHeight = Math.max(3, radius * 0.14);
  const y = pos.y + radius + Math.max(4, radius * 0.2);
  const x = pos.x - width * 0.5;
  const hp = clamp(snap.hpPercent, 0, 1);
  const labelColor = snap.entityType === playerFlowerType ? "#19e6d2" : rarityColor(snap.rarity, 1);
  const textSize = Math.max(4, radius * 0.2);
  const strokeColor = "#050506";

  drawOutlinedText(mobDisplayName(snap), x, y - Math.max(1, radius * 0.05), {
    font: `800 ${textSize}px Segoe UI, Microsoft YaHei, sans-serif`,
    fill: "#f7f7f7",
    stroke: strokeColor,
    lineWidth: Math.max(1.1, textSize * 0.42),
    align: "left",
    baseline: "bottom",
  });

  drawCapsuleBar(x, y, width, barHeight, hp, "#68d443", Math.max(1, barHeight * 0.28));

  drawOutlinedText(rarityOrLevelLabel(snap), x + width, y + barHeight + Math.max(1, radius * 0.05), {
    font: `800 ${textSize}px Segoe UI, Microsoft YaHei, sans-serif`,
    fill: labelColor,
    stroke: strokeColor,
    lineWidth: Math.max(1.1, textSize * 0.42),
    align: "right",
    baseline: "top",
  });
}

function drawEntityLabel(snap, pos, radius) {
  if (!snap.name || radius < entityFrameMinScreenRadius) return;
  ctx.font = "12px Segoe UI, Microsoft YaHei, sans-serif";
  ctx.textAlign = "center";
  ctx.textBaseline = "bottom";
  ctx.fillStyle = "rgba(39, 50, 65, 0.86)";
  ctx.fillText(snap.name, pos.x, pos.y - radius - 10);
}

function bossBarCandidates(entities) {
  const owner = state.entities.get(state.ownerEntityId);
  const origin = owner ? owner.renderPos : state.camera;
  const groups = new Map();

  for (const entity of entities) {
    const snap = entity.snapshot;
    if (!snap || entity.dying || !isMobSnap(snap) || snap.rarity < bossRarity) continue;
    const key = `${snap.entityType}:${snap.rarity}`;
    let group = groups.get(key);
    if (!group) {
      group = {
        key,
        entityType: snap.entityType,
        rarity: snap.rarity,
        name: mobDisplayName(snap),
        members: [],
        nearestDistance: Number.POSITIVE_INFINITY,
      };
      groups.set(key, group);
    }

    const distance = Math.hypot(entity.renderPos.x - origin.x, entity.renderPos.y - origin.y);
    group.nearestDistance = Math.min(group.nearestDistance, distance);
    group.members.push({ hp: clamp(snap.hpPercent, 0, 1), distance });
  }

  return [...groups.values()]
    .sort((a, b) => (b.rarity - a.rarity) || (a.nearestDistance - b.nearestDistance))
    .slice(0, maxBossBars);
}

function drawBossBars(entities) {
  const groups = bossBarCandidates(entities);
  if (groups.length === 0) return;

  const width = Math.min(500, Math.max(310, state.canvasWidth * 0.31));
  const height = 60;
  const gap = 92;
  const startY = 104;
  const x = (state.canvasWidth - width) * 0.5;

  groups.forEach((group, index) => {
    const y = startY + index * (height + gap);
    const rarityFill = rarityColor(group.rarity, 1);
    const count = Math.max(1, group.members.length);
    const title = count > 1 ? `${group.name} x${count}` : group.name;

    ctx.save();
    drawOutlinedText(title, x + width * 0.5, y - 8, {
      font: "900 34px Segoe UI, Microsoft YaHei, sans-serif",
      fill: "#f7f7f7",
      lineWidth: 7,
      baseline: "bottom",
    });
    drawSegmentedCapsuleBar(x, y, width, height, group.members, "#69d33e", 8);
    drawOutlinedText(rarityName(group.rarity), x + width * 0.5, y + height + 8, {
      font: "900 27px Segoe UI, Microsoft YaHei, sans-serif",
      fill: rarityFill,
      lineWidth: 6,
      baseline: "top",
    });
    ctx.restore();
  });
}

function drawSelfHud() {
  const owner = state.authenticated ? state.entities.get(state.ownerEntityId) : null;
  if (!owner) return;

  const health = owner ? clamp(owner.snapshot.hpPercent || 0, 0, 1) : 0;
  const expProgress = clamp(state.ownerExp / Math.max(1, state.ownerExpRequired), 0, 1);
  const name = owner.snapshot?.name || "Player";
  const level = Math.max(1, state.ownerLevel || owner?.snapshot?.rarity || 1);
  const fadeProgress = owner.dying ? clamp((owner.deathAge || 0) / deathFadeDuration, 0, 1) : 0;
  const fadeAlpha = owner.dying ? 1 - fadeProgress : 1;
  const fadeScale = owner.dying ? 1 + fadeProgress * 0.45 : 1;
  const ownerDead = (owner.snapshot.flags & flagDead) !== 0;
  const scale = Math.min(1, Math.max(0.7, state.canvasWidth / 320));
  const flowerPos = { x: 55 * scale, y: 60 * scale };
  const flowerRadius = 30 * scale;
  const healthX = 94 * scale;
  const healthY = 43 * scale;
  const healthW = 220 * scale;
  const healthH = 36 * scale;
  const healthBackX = 57 * scale;
  const healthBackW = healthX + healthW - healthBackX;
  const expX = 112 * scale;
  const expY = 85 * scale;
  const expW = 184 * scale;
  const expH = 20 * scale;

  ctx.save();
  ctx.globalAlpha *= fadeAlpha;
  drawCapsuleBar(healthBackX, healthY, healthBackW, healthH, 0, "#69d348", 6 * scale);
  drawCapsuleBar(healthX, healthY, healthW, healthH, health, "#69d348", 6 * scale);
  drawCapsuleBar(expX, expY, expW, expH, expProgress, "#e8e95a", 4 * scale);
  drawPlayerFlower(owner.snapshot, flowerPos, flowerRadius * fadeScale,
                   ownerDead ? (owner.deathAngle ?? owner.renderAngle ?? owner.snapshot.angle) :
                               (owner.renderAngle ?? owner.snapshot.angle),
                   true);
  drawOutlinedText(name, healthX + healthW * 0.5, healthY + healthH * 0.5, {
    font: `900 ${24 * scale}px Segoe UI, Microsoft YaHei, sans-serif`,
    fill: "#f7f7f7",
    lineWidth: 5 * scale,
  });

  drawOutlinedText(`Lvl ${level}`, expX + expW * 0.5, expY + expH * 0.5, {
    font: `900 ${12.5 * scale}px Segoe UI, Microsoft YaHei, sans-serif`,
    fill: "#f7f7f7",
    lineWidth: 3.4 * scale,
  });
  ctx.restore();
}

function drawScene() {
  drawGrid();
  const entities = [...state.entities.values()];
  for (const entity of entities) {
    if (!isPetalEntity(entity.snapshot.entityType) && entity.snapshot.entityId !== state.ownerEntityId)
      drawEntity(entity);
  }
  for (const entity of entities) {
    if (isPetalEntity(entity.snapshot.entityType)) drawEntity(entity);
  }
  const owner = state.entities.get(state.ownerEntityId);
  if (owner) drawEntity(owner);
  drawBossBars(entities);
  drawSelfHud();
  updateDeathOverlay();
}

function tick(now) {
  const dt = Math.min(0.05, Math.max(0, (now - state.lastFrameTime) / 1000));
  state.lastFrameTime = now;
  if (dt > 0) {
    const instantFps = 1 / dt;
    state.fps = state.fps <= 0 ? instantFps : state.fps + (instantFps - state.fps) * 0.08;
  }
  flushInput(dt);
  updateRenderPositions(dt);
  drawScene();
  requestAnimationFrame(tick);
}

function isUiTarget(target) {
  return target && target.closest && target.closest(".ui");
}

function setupEvents() {
  window.addEventListener("resize", resizeCanvas);
  connectBtn.addEventListener("click", connectAndAuth);
  reviveBtn.addEventListener("click", requestRevive);
  backpackCloseBtn.addEventListener("click", () => toggleBackpack(false));
  craftCloseBtn.addEventListener("click", () => toggleCraft(false));
  craftOnceBtn.addEventListener("click", () => submitCraft());
  for (const input of [wsUrlInput, accountInput, passwordInput]) {
    input.addEventListener("keydown", (event) => {
      if (event.key === "Enter") connectAndAuth();
    });
  }

  chatInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      submitChat();
    } else if (event.key === "Escape") {
      event.preventDefault();
      closeChat();
    }
  });

  consoleInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      submitConsole();
    } else if (event.key === "Escape") {
      event.preventDefault();
      toggleConsole(false);
    }
  });

  window.addEventListener("keydown", (event) => {
    if (event.key === "F1") {
      event.preventDefault();
      toggleConsole();
      return;
    }
    if (isTyping()) return;
    const key = event.key.toLowerCase();
    if (key === "z") {
      event.preventDefault();
      toggleBackpack();
      return;
    }
    if (key === "c") {
      event.preventDefault();
      toggleCraft();
      return;
    }
    if (key === "enter") {
      event.preventDefault();
      openChat("");
      return;
    }
    if (key === "/") {
      event.preventDefault();
      openChat("/");
      return;
    }
    if (key === "escape") {
      closeChat();
      return;
    }
    const slotIndex = slotIndexFromKey(key);
    if (slotIndex !== null && slotIndex < state.ownerSlots.length) {
      state.selectedSlot = slotIndex;
      quickSwapSlot(slotIndex);
      renderInventoryPanel();
      return;
    }
    state.keys.add(key);
  });

  window.addEventListener("keyup", (event) => {
    state.keys.delete(event.key.toLowerCase());
  });

  window.addEventListener("pointermove", (event) => {
    updateMousePointer(event);
    updateDrag(event);
  }, { passive: false });
  window.addEventListener("pointerup", finishDrag);
  window.addEventListener("pointercancel", clearDrag);

  window.addEventListener("blur", () => {
    state.keys.clear();
    state.attacking = false;
    state.defending = false;
    sendBytes(packInput(0, 0));
    sendBytes(packChores(false, false));
  });

  window.addEventListener("mousedown", (event) => {
    updateMousePointer(event);
    if (isUiTarget(event.target)) return;
    if (event.button === 0) state.attacking = true;
    if (event.button === 2) state.defending = true;
  });

  window.addEventListener("mouseup", (event) => {
    if (event.button === 0) state.attacking = false;
    if (event.button === 2) state.defending = false;
  });

  window.addEventListener("contextmenu", (event) => {
    if (!isUiTarget(event.target)) event.preventDefault();
  });

  window.addEventListener("beforeunload", () => closeSocket(true));
}

function slotIndexFromKey(key) {
  if (/^[1-9]$/.test(key)) return Number(key) - 1;
  if (key === "0") return 9;
  return null;
}

loadSettings();
setupEvents();
resizeCanvas();
renderInventoryPanel();
addConsoleLine("Web client ready");
loadMap(defaultMapName);
requestAnimationFrame(tick);

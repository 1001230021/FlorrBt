import {
  ChatFlag,
  NETWORK_PETAL_TYPE_OFFSET,
  ServerType,
  appendBytes,
  clamp,
  isPetalEntity,
  mobTypeName,
  packAuth,
  packChat,
  packChores,
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
import { drawSoldierAnt } from "./soldier_ant_sprite.js";

const canvas = document.getElementById("game");
const ctx = canvas.getContext("2d");
const authPanel = document.getElementById("authPanel");
const wsUrlInput = document.getElementById("wsUrl");
const accountInput = document.getElementById("account");
const passwordInput = document.getElementById("password");
const registerModeInput = document.getElementById("registerMode");
const connectBtn = document.getElementById("connectBtn");
const authStatus = document.getElementById("authStatus");
const statusLine = document.getElementById("statusLine");
const coordsLine = document.getElementById("coordsLine");
const fpsLine = document.getElementById("fpsLine");
const chatLog = document.getElementById("chatLog");
const chatInput = document.getElementById("chatInput");
const consolePanel = document.getElementById("consolePanel");
const consoleLog = document.getElementById("consoleLog");
const consoleInput = document.getElementById("consoleInput");
const backpackPanel = document.getElementById("backpackPanel");
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
  attacking: false,
  defending: false,
  lastInput: { x: 99, y: 99, attacking: false, defending: false },
  sendTimer: 1,
  lastFrameTime: performance.now(),
  fps: 0,
  consoleOpen: false,
  debugGrid: false,
  backpackOpen: false,
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
const deathFadeDuration = 0.1;
const viewScreenFill = 0.46;
const viewScreenPadding = 36;
const flagAttacking = 1 << 0;
const flagDefending = 1 << 1;
const flagDead = 1 << 2;
const flagOwner = 1 << 3;
const normalLadybugType = 3;
const soldierAntType = 7;
const summonedSoldierAntType = 11;
const playerFlowerType = 6;
const bossRarity = 8;
const maxBossBars = 3;
const PetalIconIds = [
  0, 48, 51, 17, 1, 16, 58, 13, 57, 74, 71, 98, 72, 111, 97, 7, 113, 77,
  "nullification", 27, 53, 5, 19, 9, 108, 80, 103, 18, 12, 30,
];
const assetImages = new Map();

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
}

function saveSettings() {
  localStorage.setItem(settingsKey, JSON.stringify({
    wsUrl: wsUrlInput.value.trim(),
    account: accountInput.value,
    password: passwordInput.value,
  }));
}

function setStatus(text, good = false) {
  statusLine.textContent = text;
  statusLine.style.color = good ? "#308f5f" : "#a84e45";
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
  backpackPanel.classList.toggle("open", state.backpackOpen);
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
    const primaryCount = Math.max(1, (msg.ownerSlots || []).length || state.ownerSlots.length || 5);
    state.ownerSlots = normalizeSlots(msg.ownerSlots, primaryCount);
    state.secondarySlots = normalizeSlots(msg.secondarySlots, primaryCount);
    state.selectedSlot = clamp(state.selectedSlot, 0, state.ownerSlots.length - 1);
    renderInventoryPanel();
    return;
  }

  if (msg.type === ServerType.Inventory) {
    state.inventory = msg.inventory || [];
    renderInventoryPanel();
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
  state.viewRadius = msg.viewRadius || state.viewRadius;

  const live = new Set();
  for (const snap of msg.entities || []) {
    live.add(snap.entityId);
    const existing = state.entities.get(snap.entityId);
    if (existing) {
      existing.snapshotMove = Math.hypot(snap.pos.x - existing.snapshot.pos.x, snap.pos.y - existing.snapshot.pos.y);
      existing.snapshot = snap;
      existing.dying = false;
      existing.deathAge = 0;
    } else {
      state.entities.set(snap.entityId, {
        snapshot: snap,
        renderPos: { x: snap.pos.x, y: snap.pos.y },
        renderAngle: snap.angle || 0,
        snapshotMove: 0,
        motionBlend: 0,
        dying: false,
        deathAge: 0,
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
  if (!owner || !state.viewRadius) return true;
  const dx = entity.renderPos.x - owner.renderPos.x;
  const dy = entity.renderPos.y - owner.renderPos.y;
  const visibleEdge = state.viewRadius + Math.max(0, entity.snapshot.radius || 0);
  return dx * dx + dy * dy <= visibleEdge * visibleEdge;
}

function readMoveInput() {
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

function flushInput(dt) {
  if (!state.connected || !state.authenticated || isTyping()) return;

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
  inventoryList.classList.toggle("hidden", state.inventory.length === 0);

  state.ownerSlots.forEach((slot, index) => {
    primarySlots.appendChild(makeSlotButton(slot, index, "primary"));
  });
  normalizeSlots(state.secondarySlots, state.ownerSlots.length).forEach((slot, index) => {
    secondarySlots.appendChild(makeSlotButton(slot, index, "secondary"));
  });

  for (const item of state.inventory) {
    if (!slotHasItem(item) || item.count <= 0) continue;
    const button = document.createElement("button");
    button.className = "inventory-item";
    button.type = "button";
    button.title = `${petalTypeName(item.petalType)} ${rarityName(item.rarity)} x${item.count}`;
    button.appendChild(makePetalStack(item.petalType, item.rarity));
    button.addEventListener("pointerdown", (event) => beginDrag(event, button, "inventory", -1, item));
    button.addEventListener("click", () => {
      if (shouldSuppressClick()) return;
      equipFromInventory(item);
    });
    inventoryList.appendChild(button);
  }
}

function makeSlotButton(slot, index, kind) {
  const button = document.createElement("button");
  button.className = `slot${slotHasItem(slot) ? "" : " empty"}${kind === "primary" && index === state.selectedSlot ? " selected" : ""}`;
  button.type = "button";
  button.dataset.dropKind = kind;
  button.dataset.dropIndex = String(index);
  button.innerHTML = `<span class="index">${index + 1}</span>`;

  if (slotHasItem(slot)) {
    button.title = `${petalTypeName(slot.petalType)} ${rarityName(slot.rarity)}`;
    button.appendChild(makePetalStack(slot.petalType, slot.rarity));
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

function makePetalStack(petalType, rarity) {
  const stack = document.createElement("span");
  stack.className = "petal-stack";

  const base = document.createElement("img");
  base.className = "petal-bg";
  base.alt = "";
  base.draggable = false;
  base.src = petalBasePath(rarity);
  stack.appendChild(base);

  const icon = makePetalIcon(petalType);
  if (icon) stack.appendChild(icon);
  return stack;
}

function makePetalIcon(petalType) {
  const src = petalIconPath(petalType);
  if (!src) return null;
  const image = document.createElement("img");
  image.className = "petal-icon";
  image.alt = "";
  image.draggable = false;
  image.src = src;
  image.addEventListener("error", () => image.remove(), { once: true });
  return image;
}

function rarityBaseIndex(rarity) {
  return clamp((rarity || 1) - 1, 0, 10);
}

function petalBasePath(rarity) {
  return `./assets/petals/0_${rarityBaseIndex(rarity)}.svg`;
}

function petalIconPath(petalType) {
  const iconId = PetalIconIds[petalType] || 0;
  if (!iconId) return "";
  return `./assets/petals/${iconId}.svg`;
}

function assetImage(src) {
  if (!src) return null;
  let image = assetImages.get(src);
  if (!image) {
    image = new Image();
    image.decoding = "async";
    image.src = src;
    assetImages.set(src, image);
  }
  return image;
}

function imageReady(image) {
  return image && image.complete && image.naturalWidth > 0;
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
    existing.count = Math.max(0, (existing.count || 0) + delta);
    if (existing.count <= 0) state.inventory = state.inventory.filter((item) => item !== existing);
    return;
  }
  if (delta > 0) state.inventory.push({ petalType, rarity, count: delta });
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

  state.ownerSlots[fromIndex] = emptySlot();
  state.secondarySlots[toIndex] = source;
  if (slotHasItem(oldSecondary)) adjustInventory(oldSecondary.petalType, oldSecondary.rarity, 1);
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

  state.secondarySlots[fromIndex] = emptySlot();
  state.secondarySlots[toIndex] = source;
  if (slotHasItem(target)) adjustInventory(target.petalType, target.rarity, 1);
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
    const blend = distance > predictionSnapDistance ? 1 : smoothFactor(18, dt);
    owner.renderPos.x += (owner.snapshot.pos.x - owner.renderPos.x) * blend;
    owner.renderPos.y += (owner.snapshot.pos.y - owner.renderPos.y) * blend;
    owner.renderAngle = smoothAngle(owner.renderAngle, owner.snapshot.angle, 14, dt);
    state.camera.x += (owner.renderPos.x - state.camera.x) * smoothFactor(12, dt);
    state.camera.y += (owner.renderPos.y - state.camera.y) * smoothFactor(12, dt);
    coordsLine.textContent = `${owner.snapshot.pos.x.toFixed(1)}, ${owner.snapshot.pos.y.toFixed(1)}`;
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
    entity.renderAngle = smoothAngle(entity.renderAngle, entity.snapshot.angle, 10, dt);
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

function worldScale() {
  if (!state.viewRadius || state.viewRadius <= 0) return 1;
  const shortSide = Math.max(1, Math.min(state.canvasWidth, state.canvasHeight));
  const targetRadius = Math.max(96, shortSide * viewScreenFill - viewScreenPadding);
  return targetRadius / state.viewRadius;
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
  ctx.drawImage(image, 0, 0, width, height);
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
    const minX = Math.max(0, Math.floor(worldLeft / tileW));
    const minY = Math.max(0, Math.floor(worldTop / tileH));
    const maxX = Math.min(layer.width - 1, Math.ceil(worldRight / tileW));
    const maxY = Math.min(layer.height - 1, Math.ceil(worldBottom / tileH));
    for (let y = minY; y <= maxY; y += 1) {
      for (let x = minX; x <= maxX; x += 1) {
        const raw = layer.tiles[y * layer.width + x] >>> 0;
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

function drawEntity(entity) {
  const snap = entity.snapshot;
  const pos = worldToScreen(entity.renderPos);
  const radius = Math.max(3, worldLengthToScreen(snap.radius));
  const isOwner = (snap.flags & flagOwner) !== 0 || snap.entityId === state.ownerEntityId;
  const isPetal = isPetalEntity(snap.entityType);
  const dead = (snap.flags & flagDead) !== 0;
  const deathProgress = entity.dying ? clamp((entity.deathAge || 0) / deathFadeDuration, 0, 1) : 0;
  const deathScale = entity.dying ? 1 + deathProgress * 0.45 : 1;
  const deathAlpha = entity.dying ? 1 - deathProgress : 1;

  ctx.save();
  ctx.globalAlpha = (dead ? 0.45 : 1) * deathAlpha;

  if (!isPetal && snap.entityType === normalLadybugType) {
    drawNormalLadybug(ctx, pos, radius * deathScale, snap.entityId, entity.renderAngle ?? snap.angle);
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && (snap.entityType === soldierAntType || snap.entityType === summonedSoldierAntType)) {
    drawSoldierAnt(ctx, pos, radius * deathScale, snap.entityId, entity.renderAngle ?? snap.angle,
                   entity.motionBlend || 0, performance.now() / 1000);
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (isPetal) {
    if (drawPetalComposite(petalTypeFromEntity(snap.entityType), snap.rarity, pos, radius * deathScale,
                           entity.renderAngle ?? snap.angle)) {
      if (petalTypeFromEntity(snap.entityType) === 10)
        drawCompassNeedle(pos, radius * deathScale, entity.renderAngle ?? snap.angle);
      ctx.restore();
      return;
    }
  }

  ctx.beginPath();
  ctx.arc(pos.x, pos.y, radius * deathScale, 0, Math.PI * 2);

  if (isOwner) ctx.fillStyle = "#ffe0e7";
  else if (isPetal) ctx.fillStyle = rarityColor(snap.rarity, 0.82);
  else ctx.fillStyle = rarityColor(snap.rarity, 0.72);
  ctx.fill();

  ctx.lineWidth = (isOwner ? 3 : 1.5) * deathScale;
  if (isOwner && (snap.flags & flagDefending)) ctx.strokeStyle = "#467acd";
  else if (isOwner && (snap.flags & flagAttacking)) ctx.strokeStyle = "#be5e7a";
  else ctx.strokeStyle = isPetal ? "#4f6680" : "rgba(48, 62, 82, 0.52)";
  ctx.stroke();

  if (isPetal && petalTypeFromEntity(snap.entityType) === 10)
    drawCompassNeedle(pos, radius * deathScale, entity.renderAngle ?? snap.angle);
  if (!isPetal && !entity.dying) drawMobFrame(snap, pos, radius);
  ctx.restore();
}

function drawPetalComposite(petalType, rarity, pos, radius, angle) {
  const base = assetImage(petalBasePath(rarity));
  const icon = assetImage(petalIconPath(petalType));
  if (!imageReady(base)) return false;

  const size = Math.max(12, radius * 2.25);
  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle)) ctx.rotate(angle);
  ctx.drawImage(base, -size * 0.5, -size * 0.5, size, size);

  if (imageReady(icon)) {
    const sourceWidth = icon.naturalWidth || 110;
    const sourceHeight = icon.naturalHeight || 110;
    const iconSize = size * 0.9;
    ctx.drawImage(icon, 0, 0, sourceWidth, sourceHeight, -iconSize * 0.5, -iconSize * 0.5, iconSize, iconSize);
  }
  ctx.restore();
  return true;
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
  return snap && !isPetalEntity(snap.entityType) && snap.entityType !== playerFlowerType;
}

function hasHealthFrame(snap) {
  return snap && !isPetalEntity(snap.entityType);
}

function mobDisplayName(snap) {
  return snap.name || mobTypeName(snap.entityType);
}

function rarityOrLevelLabel(snap) {
  if (snap.entityType === playerFlowerType) return `Lvl ${snap.rarity || 1}`;
  return rarityName(snap.rarity);
}

function drawMobFrame(snap, pos, radius) {
  if (!hasHealthFrame(snap)) {
    drawEntityLabel(snap, pos, radius);
    return;
  }

  const width = Math.max(42, radius * 2.25);
  const barHeight = Math.max(5, Math.min(8, radius * 0.22));
  const y = pos.y + radius + 8;
  const x = pos.x - width * 0.5;
  const hp = clamp(snap.hpPercent, 0, 1);
  const color = rarityColor(snap.rarity, 1);
  const textSize = Math.max(4, Math.round(11 / 3));
  const strokeColor = "rgba(5, 8, 14, 0.78)";

  ctx.font = `700 ${textSize}px Segoe UI, Microsoft YaHei, sans-serif`;
  ctx.lineWidth = 1.6;
  ctx.textBaseline = "bottom";
  ctx.textAlign = "left";
  ctx.strokeStyle = strokeColor;
  ctx.fillStyle = "rgba(235, 242, 250, 0.92)";
  ctx.strokeText(mobDisplayName(snap), x, y - 2);
  ctx.fillText(mobDisplayName(snap), x, y - 2);

  ctx.textBaseline = "top";
  ctx.textAlign = "right";
  ctx.strokeStyle = strokeColor;
  ctx.fillStyle = color;
  ctx.strokeText(rarityOrLevelLabel(snap), x + width, y + barHeight + 2);
  ctx.fillText(rarityOrLevelLabel(snap), x + width, y + barHeight + 2);

  ctx.fillStyle = "rgba(18, 24, 34, 0.28)";
  ctx.fillRect(x, y, width, barHeight);
  ctx.fillStyle = color;
  ctx.fillRect(x, y, width * hp, barHeight);
  ctx.strokeStyle = "rgba(20, 28, 40, 0.55)";
  ctx.lineWidth = 1;
  ctx.strokeRect(x + 0.5, y + 0.5, width - 1, barHeight - 1);
}

function drawEntityLabel(snap, pos, radius) {
  if (!snap.name) return;
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

  const width = Math.min(620, Math.max(300, state.canvasWidth * 0.48));
  const height = 18;
  const gap = 20;
  const startY = 18;
  const x = (state.canvasWidth - width) * 0.5;

  groups.forEach((group, index) => {
    const y = startY + index * (height + gap);
    const color = rarityColor(group.rarity, 1);
    const count = Math.max(1, group.members.length);
    const segment = width / count;

    ctx.save();
    ctx.font = "14px Segoe UI, Microsoft YaHei, sans-serif";
    ctx.textBaseline = "bottom";
    ctx.textAlign = "left";
    ctx.fillStyle = "rgba(18, 24, 34, 0.92)";
    ctx.fillText(`${group.name} ${rarityName(group.rarity)}${count > 1 ? ` x${count}` : ""}`, x, y - 4);

    ctx.fillStyle = "rgba(12, 17, 24, 0.36)";
    ctx.fillRect(x, y, width, height);

    group.members
      .sort((a, b) => b.hp - a.hp || a.distance - b.distance)
      .forEach((member, memberIndex) => {
        const sx = x + memberIndex * segment;
        ctx.fillStyle = color;
        ctx.globalAlpha = 0.96;
        ctx.fillRect(sx, y, segment * member.hp, height);
        ctx.globalAlpha = 1;
        if (memberIndex > 0) {
          ctx.strokeStyle = "rgba(255, 255, 255, 0.38)";
          ctx.beginPath();
          ctx.moveTo(sx + 0.5, y + 2);
          ctx.lineTo(sx + 0.5, y + height - 2);
          ctx.stroke();
        }
      });

    ctx.strokeStyle = "rgba(12, 17, 24, 0.72)";
    ctx.lineWidth = 2;
    ctx.strokeRect(x, y, width, height);
    ctx.restore();
  });
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

  const visible = entities.length;
  const mapLabel = state.map ? `  Map ${state.mapDisplayName || state.mapName}` : "  Map loading";
  const label = state.authenticated ? `Snapshot ${state.snapshotId}  Entities ${visible}${mapLabel}` :
                state.connected ? `Connected${mapLabel}` : `Disconnected${mapLabel}`;
  statusLine.textContent = label;
  statusLine.style.color = state.connected ? "#308f5f" : "#a84e45";
}

function tick(now) {
  const dt = Math.min(0.05, Math.max(0, (now - state.lastFrameTime) / 1000));
  state.lastFrameTime = now;
  if (dt > 0) {
    const instantFps = 1 / dt;
    state.fps = state.fps <= 0 ? instantFps : state.fps + (instantFps - state.fps) * 0.08;
    fpsLine.textContent = `${Math.round(state.fps)} fps`;
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

  window.addEventListener("pointermove", updateDrag, { passive: false });
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

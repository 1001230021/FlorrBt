// Client input binding system.
//
// Every gameplay action the client can perform is a *builtin command*; keys
// are only ever a way to run one. Bindings map an input key (keyboard keys
// like "a" / "space" / "f1", mouse buttons like "mouse1" / "wheelup") to a
// chain of those commands:
//
//   bind a left                     (device inferred from the key)
//   bind keyboard a "move_toward 90" + attack
//   bind space attack
//   unbind a
//   unbindall keyboard
//
// Bindings persist in localStorage and are loaded on every page load.
//
// Hold semantics: pressing a key runs its chain with phase "down", releasing
// with phase "up". Continuous commands (move/attack/defend/modifiers) do NOT
// accumulate deltas -- each one registers a *hold source* keyed by
// device+key+slot, and the aggregate movement/attack state is recomputed from
// the live set of held sources. That makes every transition idempotent: a
// duplicated "down" changes nothing, an "up" for a source that was never held
// is a no-op, and a keyup that never arrives (focus stolen, window switch,
// dropped event) can be healed by releasing the source from anywhere. The old
// signed-accumulator model turned a single dropped keyup into movement that
// stuck forever.

const BINDINGS_KEY = "florrbt.web.bindings";

// Keys bound by default; users may override or remove them.
export const defaultBindings = Object.freeze({
  keyboard: {
    a: ["left"],
    d: ["right"],
    w: ["up"],
    s: ["down"],
    arrowleft: ["left"],
    arrowright: ["right"],
    arrowup: ["up"],
    arrowdown: ["down"],
    space: ["attack"],
    shift: ["defend"],
    z: ["inventory"],
    x: ["talent"],
    c: ["craft"],
    m: ["minimap"],
    enter: ["chat"],
    "/": ["command"],
    r: ["swap"],
    o: ["save"],
    l: ["loadout"],
    alt: ["alt"],
    escape: ["close"],
    f1: ["console"],
    "1": ["select_slot 0"],
    "2": ["select_slot 1"],
    "3": ["select_slot 2"],
    "4": ["select_slot 3"],
    "5": ["select_slot 4"],
    "6": ["select_slot 5"],
    "7": ["select_slot 6"],
    "8": ["select_slot 7"],
    "9": ["select_slot 8"],
    "0": ["select_slot 9"],
  },
  mouse: {
    mouse1: ["attack"],
    mouse2: ["defend"],
    wheelup: ["zoom_out"],
    wheeldown: ["zoom_in"],
  },
});

// --------------------------------------------------------------- key naming

// Canonical key names are lowercase. Aliases exist so `bind esc close` and
// `bind escape close` mean the same thing.
const keyAliases = Object.freeze({
  " ": "space",
  spacebar: "space",
  esc: "escape",
  return: "enter",
  del: "delete",
  ins: "insert",
  pgup: "pageup",
  pgdn: "pagedown",
  pagedn: "pagedown",
  ctrl: "control",
  ctl: "control",
  win: "meta",
  cmd: "meta",
  command: "meta",
  super: "meta",
  os: "meta",
  leftarrow: "arrowleft",
  rightarrow: "arrowright",
  uparrow: "arrowup",
  downarrow: "arrowdown",
  left_arrow: "arrowleft",
  right_arrow: "arrowright",
  up_arrow: "arrowup",
  down_arrow: "arrowdown",
  m1: "mouse1",
  m2: "mouse2",
  m3: "mouse3",
  m4: "mouse4",
  m5: "mouse5",
  mouseleft: "mouse1",
  mouseright: "mouse2",
  mousemiddle: "mouse3",
  mouseback: "mouse4",
  mouseforward: "mouse5",
  leftclick: "mouse1",
  rightclick: "mouse2",
  middleclick: "mouse3",
  mwheelup: "wheelup",
  mwheeldown: "wheeldown",
  scrollup: "wheelup",
  scrolldown: "wheeldown",
});

const mouseKeyNames = Object.freeze([
  "mouse1",
  "mouse2",
  "mouse3",
  "mouse4",
  "mouse5",
  "wheelup",
  "wheeldown",
]);

const namedKeyboardKeys = Object.freeze([
  "escape",
  "tab",
  "capslock",
  "shift",
  "lshift",
  "rshift",
  "control",
  "lcontrol",
  "rcontrol",
  "alt",
  "lalt",
  "ralt",
  "meta",
  "space",
  "enter",
  "backspace",
  "delete",
  "insert",
  "home",
  "end",
  "pageup",
  "pagedown",
  "arrowup",
  "arrowdown",
  "arrowleft",
  "arrowright",
  "numpadadd",
  "numpadsubtract",
  "numpadmultiply",
  "numpaddivide",
  "numpaddecimal",
  "numpadenter",
]);

const punctuationKeys = Object.freeze([
  "-",
  "=",
  "[",
  "]",
  "\\",
  ";",
  "'",
  ",",
  ".",
  "/",
  "`",
]);

export function normalizeInputKey(key) {
  const raw = String(key ?? "");
  const lower = raw.length === 1 ? raw.toLowerCase() : raw.trim().toLowerCase();
  return keyAliases[lower] || lower;
}

// "mouse" for mouse buttons and the wheel, "keyboard" for everything else.
export function deviceForInputKey(key) {
  const normalized = normalizeInputKey(key);
  return mouseKeyNames.includes(normalized) ? "mouse" : "keyboard";
}

// Every key name the console can offer for completion.
export function inputKeyNames(device = "") {
  const normalized = String(device || "").toLowerCase();
  if (normalized === "mouse") return [...mouseKeyNames];

  const keys = [];
  for (let code = 97; code <= 122; code += 1) keys.push(String.fromCharCode(code));
  for (let digit = 0; digit <= 9; digit += 1) keys.push(String(digit));
  for (let fn = 1; fn <= 12; fn += 1) keys.push(`f${fn}`);
  for (let digit = 0; digit <= 9; digit += 1) keys.push(`numpad${digit}`);
  keys.push(...namedKeyboardKeys, ...punctuationKeys);
  if (!normalized) keys.push(...mouseKeyNames);
  return keys;
}

// Names a keyboard event can match, most specific first ("lshift" before
// "shift"), so side-specific binds win when they exist.
export function inputKeyCandidates(event) {
  const code = String(event?.code || "");
  if (code === "Space") return ["space"];
  if (code.startsWith("Numpad") && code.length > 6)
    return [`numpad${code.slice(6).toLowerCase()}`];

  const sided = {
    ShiftLeft: ["lshift", "shift"],
    ShiftRight: ["rshift", "shift"],
    ControlLeft: ["lcontrol", "control"],
    ControlRight: ["rcontrol", "control"],
    AltLeft: ["lalt", "alt"],
    AltRight: ["ralt", "alt"],
  }[code];
  if (sided) return sided;

  return [normalizeInputKey(event?.key)];
}

// The single name an event is reported as (debug/UI), ignoring side variants.
export function inputKeyFromEvent(event) {
  const candidates = inputKeyCandidates(event);
  return candidates[candidates.length - 1] || "";
}

export function mouseKeyFromButton(button) {
  return (
    { 0: "mouse1", 1: "mouse3", 2: "mouse2", 3: "mouse4", 4: "mouse5" }[button] ||
    null
  );
}

// ---------------------------------------------------------------- commands

// `hold` marks a continuous command: it contributes to the aggregate input
// state for as long as its key is down instead of firing once.
//   hold: "move"          -> vector(args) contributes to the move vector
//   hold: "attack"/"defend" -> sets the matching flag while held
//   hold: "modifier"      -> exposes `modifier` to other commands while held
// Everything else is one-shot and runs `run(ctx, args)` on press.
const builtinCommands = {
  move_toward: {
    params: "<angle> [strength 0..1]",
    args: "number",
    hold: "move",
    help: "Move toward an angle (0=right, 90=down); optional strength 0..1 sets speed (default 1).",
    vector(args) {
      const angle = Number.parseFloat(args[0]);
      const strength = args[1] === undefined ? 1 : Number(args[1]);
      if (!Number.isFinite(angle) || !Number.isFinite(strength) || strength < 0 || strength > 1) return null;
      const radians = (angle * Math.PI) / 180;
      return { x: Math.cos(radians) * strength, y: Math.sin(radians) * strength };
    },
  },
  left: {
    params: "",
    args: "",
    hold: "move",
    help: "Move left (default: A / Left arrow).",
    vector: () => ({ x: -1, y: 0 }),
  },
  right: {
    params: "",
    args: "",
    hold: "move",
    help: "Move right (default: D / Right arrow).",
    vector: () => ({ x: 1, y: 0 }),
  },
  up: {
    params: "",
    args: "",
    hold: "move",
    help: "Move up (default: W / Up arrow).",
    vector: () => ({ x: 0, y: -1 }),
  },
  down: {
    params: "",
    args: "",
    hold: "move",
    help: "Move down (default: S / Down arrow).",
    vector: () => ({ x: 0, y: 1 }),
  },
  attack: {
    params: "",
    args: "",
    hold: "attack",
    help: "Attack while held (default: mouse1 / Space).",
  },
  defend: {
    params: "",
    args: "",
    hold: "defend",
    help: "Defend while held (default: mouse2 / Shift).",
  },
  zoom_in: {
    params: "",
    args: "",
    help: "Expand the view by one percentage step (default: wheel down).",
    run: (ctx) => ctx.zoomIn(),
  },
  zoom_out: {
    params: "",
    args: "",
    help: "Shrink the view by one percentage step (default: wheel up).",
    run: (ctx) => ctx.zoomOut(),
  },
  loadout: {
    params: "",
    args: "",
    hold: "modifier",
    modifier: "loadout",
    help: "While held, select_slot switches loadout presets (default: L).",
  },
  inventory: {
    params: "",
    args: "",
    help: "Toggle the inventory (default: Z).",
    run: (ctx) => ctx.toggleBackpack(),
  },
  craft: {
    params: "",
    args: "",
    help: "Toggle the crafting panel (default: C).",
    run: (ctx) => ctx.toggleCraft(),
  },
  talent: {
    params: "",
    args: "",
    help: "Toggle the talent tree (default: X).",
    run: (ctx) => ctx.toggleTalent(),
  },
  codex: {
    params: "",
    args: "",
    help: "Toggle the bestiary.",
    run: (ctx) => ctx.toggleCodex(),
  },
  settings: {
    params: "",
    args: "",
    help: "Toggle the settings panel.",
    run: (ctx) => ctx.toggleSettings(),
  },
  console: {
    params: "",
    args: "",
    whileTyping: true,
    help: "Toggle the console (default: F1).",
    run: (ctx) => ctx.toggleConsole(),
  },
  close: {
    params: "",
    args: "",
    help: "Close the top-most open panel, or open settings (default: Escape).",
    run: (ctx) => ctx.closeTopPanel(),
  },
  fullscreen: {
    params: "",
    args: "",
    help: "Toggle fullscreen.",
    run: (ctx) => ctx.toggleFullscreen(),
  },
  minimap: {
    params: "",
    args: "",
    help: "Cycle the minimap mode (default: M).",
    run: (ctx) => ctx.toggleMinimap(),
  },
  chat: {
    params: "",
    args: "",
    help: "Open chat (default: Enter).",
    run: (ctx) => ctx.openChat(""),
  },
  command: {
    params: "",
    args: "",
    help: "Open the console with a command prefix (default: /).",
    run: (ctx) => ctx.openChat("/"),
  },
  swap: {
    params: "",
    args: "",
    help: "Swap the primary and secondary slots (default: R).",
    run: (ctx) => ctx.quickSwapAllSlots(),
  },
  save: {
    params: "",
    args: "",
    help: "Save the current loadout preset (default: O).",
    run: (ctx) => ctx.saveActiveLoadoutPreset(),
  },
  alt: {
    params: "",
    args: "",
    help: "Toggle the alt features flag (default: Alt).",
    run: (ctx) => ctx.toggleAlt(),
  },
  select_slot: {
    params: "<index>",
    args: "number",
    help: "Select a loadout slot (default: the number keys).",
    run(ctx, args) {
      const index = Number.parseInt(args[0], 10);
      if (!Number.isInteger(index)) return false;
      if (isInputModifierActive("loadout")) ctx.selectLoadoutPreset(index);
      else ctx.selectSlot(index);
      return true;
    },
  },
  select_loadout: {
    params: "<index>",
    args: "number",
    help: "Switch to a loadout preset directly.",
    run(ctx, args) {
      const index = Number.parseInt(args[0], 10);
      if (!Number.isInteger(index)) return false;
      ctx.selectLoadoutPreset(index);
      return true;
    },
  },
};

export function builtinCommandNames() {
  return Object.keys(builtinCommands);
}

export function findBuiltinCommand(name) {
  return builtinCommands[String(name || "").toLowerCase()] || null;
}

export function builtinCommandHelp() {
  return Object.entries(builtinCommands).map(([name, command]) => ({
    name,
    params: command.params || "",
    hold: !!command.hold,
    help: command.help || "",
  }));
}

// ------------------------------------------------------------- persistence

// Merges stored bindings over the defaults (stored entries win).
export function loadBindings() {
  let stored = {};
  try {
    stored = JSON.parse(localStorage.getItem(BINDINGS_KEY) || "{}") || {};
  } catch {
    stored = {};
  }

  const keyboard = { ...defaultBindings.keyboard };
  const mouse = { ...defaultBindings.mouse };
  for (const [key, cmds] of Object.entries(stored.keyboard || {})) {
    if (Array.isArray(cmds)) keyboard[normalizeInputKey(key)] = cmds.map(String);
  }
  for (const [key, cmds] of Object.entries(stored.mouse || {})) {
    if (Array.isArray(cmds)) mouse[normalizeInputKey(key)] = cmds.map(String);
  }
  return { keyboard, mouse };
}

export function saveBindings(bindings) {
  try {
    localStorage.setItem(BINDINGS_KEY, JSON.stringify(bindings));
  } catch {
    // Storage may be unavailable; bindings stay in memory for the session.
  }
}

// Drops every stored override and restores the shipped defaults in place.
export function resetBindings(bindings) {
  try {
    localStorage.removeItem(BINDINGS_KEY);
  } catch {
    // Nothing stored is fine; the in-memory tables below are what matter.
  }
  if (!bindings) return loadBindings();
  for (const device of ["keyboard", "mouse"]) {
    const table = bindings[device];
    if (!table) continue;
    for (const key of Object.keys(table)) delete table[key];
    Object.assign(table, defaultBindings[device]);
  }
  return bindings;
}

export function commandsFor(bindings, device, key) {
  // Touch controls use a fixed, action-labelled table without impersonating
  // mouse/keyboard keys or sharing their hold-source identities.
  const table = device === "touch" ? bindings.touch : device === "mouse" ? bindings.mouse : bindings.keyboard;
  return table?.[normalizeInputKey(key)] || [];
}

export function listBindings(bindings, device = "") {
  const wanted = String(device || "").toLowerCase();
  const out = [];
  for (const name of ["keyboard", "mouse"]) {
    if (wanted && wanted !== name) continue;
    const table = bindings?.[name] || {};
    for (const key of Object.keys(table).sort()) {
      const commands = table[key];
      if (commands?.length) out.push({ device: name, key, commands });
    }
  }
  return out;
}

export function bindKey(bindings, device, key, commands) {
  const table = device === "mouse" ? bindings.mouse : bindings.keyboard;
  table[normalizeInputKey(key)] = (commands || []).map(String).filter(Boolean);
  saveBindings(bindings);
  return commands.length > 0;
}

export function unbindKey(bindings, device, key) {
  const table = device === "mouse" ? bindings.mouse : bindings.keyboard;
  delete table[normalizeInputKey(key)];
  saveBindings(bindings);
}

export function unbindAll(bindings, device) {
  const table = device === "mouse" ? bindings.mouse : bindings.keyboard;
  for (const key of Object.keys(table)) delete table[key];
  saveBindings(bindings);
}

// Parses the remainder of a `bind` command into { key, commands }.
// Examples (args after the optional device token):
//   ["a", "left"]                       -> { a, ["left"] }
//   ["a", "move_toward", "90"]          -> { a, ["move_toward 90"] }
//   ["a", "cmd1", "+", "cmd2"]          -> { a, ["cmd1", "cmd2"] }
//   ["a", "move_toward", "90", "+", "attack"] -> { a, ["move_toward 90", "attack"] }
export function parseBindArgs(args) {
  const tokens = Array.isArray(args) ? args.filter((token) => token !== "") : [];
  const key = normalizeInputKey(tokens[0] || "");
  if (!key || key === "+") return null;

  const groups = [[]];
  for (let i = 1; i < tokens.length; i += 1) {
    const token = tokens[i];
    if (token === "+") {
      groups.push([]);
      continue;
    }
    groups[groups.length - 1].push(token);
  }
  const commands = groups.map((group) => group.join(" ").trim()).filter(Boolean);

  return { key, commands };
}

// ------------------------------------------------------------- hold sources

// sourceId -> { device, key, kind, modifier, vector, lastSeenAt, repeated }
const activeHolds = new Map();
const activeModifiers = new Set();

// The game-side surface the builtin commands drive. Injected once at startup
// so any module can release holds (settings, loading screen, respawn) without
// threading a context through -- and so this module never imports game.js.
let boundContext = null;

export function initBindingContext(context) {
  boundContext = context;
}

function holdSourceId(device, key, slot) {
  return `${device}:${key}#${slot}`;
}

export function isInputModifierActive(name) {
  return activeModifiers.has(String(name || ""));
}

export function heldInputSources() {
  return [...activeHolds.values()].map(({ device, key, kind }) => ({
    device,
    key,
    kind,
  }));
}

// Recomputes the aggregate input state from the live hold set and pushes it
// to the game context. Always absolute -- never a delta.
function applyHoldState() {
  let x = 0;
  let y = 0;
  let attacking = false;
  let defending = false;
  activeModifiers.clear();

  for (const hold of activeHolds.values()) {
    if (hold.kind === "move" && hold.vector) {
      x += hold.vector.x;
      y += hold.vector.y;
    } else if (hold.kind === "attack") {
      attacking = true;
    } else if (hold.kind === "defend") {
      defending = true;
    } else if (hold.kind === "modifier" && hold.modifier) {
      activeModifiers.add(hold.modifier);
    }
  }

  if (!boundContext) return;
  boundContext.setMoveVector({ x, y });
  boundContext.setAttacking(attacking);
  boundContext.setDefending(defending);
}

// Releases hold sources and re-applies the aggregate. Called on focus loss,
// tab hide, chat/panel focus steal, respawn -- anywhere a keyup might never
// arrive. Safe to call at any time: releasing what is not held is a no-op.
export function releaseInputHolds({ device = "", key = "" } = {}) {
  const wantedKey = key ? normalizeInputKey(key) : "";
  let changed = false;
  for (const [id, hold] of activeHolds) {
    if (device && hold.device !== device) continue;
    if (wantedKey && hold.key !== wantedKey) continue;
    activeHolds.delete(id);
    changed = true;
  }
  // With no filter this doubles as a full reset, so always re-assert the
  // neutral aggregate even when nothing was held.
  if (changed || (!device && !wantedKey)) applyHoldState();
  return changed;
}

// Auto-repeat safety net: a physically held key repeats every few tens of ms,
// so once repeats have been observed for a source, silence means the release
// was never delivered (window switch, RDP hiccup, swallowed event). Sources
// that have not repeated yet are left alone -- repeat may simply be disabled.
export function pruneStaleInputHolds(nowMs, timeoutMs = 1500) {
  const now = Number.isFinite(nowMs) ? nowMs : performance.now();
  let changed = false;
  for (const [id, hold] of activeHolds) {
    if (hold.device !== "keyboard" || !hold.repeated) continue;
    if (now - hold.lastSeenAt <= timeoutMs) continue;
    activeHolds.delete(id);
    changed = true;
  }
  if (changed) applyHoldState();
  return changed;
}

// Runs every command bound to a key for the given phase ("down" | "up").
//
// `key` may be a list of candidate names (e.g. ["lshift", "shift"]); the first
// one with a binding wins. Returns the number of commands that took effect.
export function executeBinding(
  bindings,
  device,
  key,
  phase,
  { repeat = false, typing = false, now = 0 } = {},
) {
  const ctx = boundContext;
  if (!ctx) return 0;
  const candidates = Array.isArray(key) ? key : [key];
  let resolvedKey = normalizeInputKey(candidates[candidates.length - 1] || "");
  let commands = [];
  for (const candidate of candidates) {
    const normalized = normalizeInputKey(candidate);
    const bound = commandsFor(bindings, device, normalized);
    if (bound.length > 0) {
      resolvedKey = normalized;
      commands = bound;
      break;
    }
  }
  if (commands.length === 0) return 0;

  const timestamp = Number.isFinite(now) && now > 0 ? now : performance.now();
  let executed = 0;
  let holdsChanged = false;

  for (let slot = 0; slot < commands.length; slot += 1) {
    const parts = String(commands[slot]).trim().split(/\s+/);
    const name = (parts.shift() || "").toLowerCase();
    const command = builtinCommands[name];
    if (!command) continue;
    // While a text field owns the keyboard only opt-in commands may run, so
    // typing never fires gameplay actions -- but releases still go through.
    if (typing && phase === "down" && !command.whileTyping) continue;

    const id = holdSourceId(device, resolvedKey, slot);
    if (command.hold) {
      if (phase === "down") {
        const existing = activeHolds.get(id);
        if (existing) {
          // A repeat (or a duplicated press) only refreshes liveness.
          existing.lastSeenAt = timestamp;
          if (repeat) existing.repeated = true;
          executed += 1;
          continue;
        }
        const vector = command.vector ? command.vector(parts) : null;
        if (command.hold === "move" && !vector) continue;
        activeHolds.set(id, {
          device,
          key: resolvedKey,
          kind: command.hold,
          modifier: command.modifier || "",
          vector,
          lastSeenAt: timestamp,
          repeated: !!repeat,
        });
        holdsChanged = true;
        executed += 1;
      } else if (activeHolds.delete(id)) {
        holdsChanged = true;
        executed += 1;
      } else {
        // Releasing something that was never held is a no-op, but the key is
        // still "handled" so the caller does not fall through to legacy paths.
        executed += 1;
      }
      continue;
    }

    if (phase !== "down" || repeat) {
      executed += 1;
      continue;
    }
    try {
      if (command.run(ctx, parts) !== false) executed += 1;
    } catch {
      // A misbehaving command must not break the input pipeline.
    }
  }

  if (holdsChanged) applyHoldState();
  return executed;
}

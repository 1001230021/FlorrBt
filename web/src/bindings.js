// 按键映射到内置命令链，绑定保存在本地存储。
// 连续动作以设备、按键及命令槽组成独立按住来源，统一计算移动和攻防状态。
// 重复按下幂等，释放不存在的来源无副作用；失焦可统一撤销，避免漏收松键后卡住。

const BINDINGS_KEY = "florrbt.web.bindings";

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


// 键名统一小写，别名规范化后共用绑定。
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

export function deviceForInputKey(key) {
  const normalized = normalizeInputKey(key);
  return mouseKeyNames.includes(normalized) ? "mouse" : "keyboard";
}

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

// 优先匹配左右区分键，再匹配通用键。
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


// hold 命令在按住期间贡献移动、攻防或修饰键状态；其余命令只在按下时执行。
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
    // 存储不可用时，本次会话仍保留内存绑定。
  }
}

export function resetBindings(bindings) {
  try {
    localStorage.removeItem(BINDINGS_KEY);
  } catch {
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
  // 触摸控件使用独立动作表和按住来源，不伪装成鼠标/键盘。
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

// 将 bind 参数解析为按键和命令链，支持可选设备前缀及 + 分隔。
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


const activeHolds = new Map();
const activeModifiers = new Set();

// 启动时注入游戏操作接口，避免反向导入 game.js；各界面可统一撤销按住来源。
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

// 从当前按住来源计算绝对输入状态，不累计增量。
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

// 失焦、隐藏、打开面板或重生时撤销来源；重复释放无副作用。
export function releaseInputHolds({ device = "", key = "" } = {}) {
  const wantedKey = key ? normalizeInputKey(key) : "";
  let changed = false;
  for (const [id, hold] of activeHolds) {
    if (device && hold.device !== device) continue;
    if (wantedKey && hold.key !== wantedKey) continue;
    activeHolds.delete(id);
    changed = true;
  }
  // 无过滤器时始终提交中立输入，兼作完整重置。
  if (changed || (!device && !wantedKey)) applyHoldState();
  return changed;
}

// 已观察到连发的按键若长期静默，按漏收松键处理；未连发来源保留，以兼容关闭连发。
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

// 按最具体的键名选首个绑定，执行对应按下/释放命令并返回生效数。
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
    // 文本框占用键盘时仅运行明确允许的命令，但始终处理释放。
    if (typing && phase === "down" && !command.whileTyping) continue;

    const id = holdSourceId(device, resolvedKey, slot);
    if (command.hold) {
      if (phase === "down") {
        const existing = activeHolds.get(id);
        if (existing) {
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
        // 不存在的来源也算已处理，避免继续落入旧输入路径。
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
      // 单个命令失败不能打断其他输入处理。
    }
  }

  if (holdsChanged) applyHoldState();
  return executed;
}

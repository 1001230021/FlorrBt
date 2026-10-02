import { state } from "./app_context.js";
import { appDisplayName } from "./version.js";
import { ChatFlag } from "./protocol.js";
import {
  builtinCommandHelp,
  builtinCommandNames,
  deviceForInputKey,
  findBuiltinCommand,
  listBindings,
  normalizeInputKey,
  parseBindArgs,
  releaseInputHolds,
  resetBindings,
  bindKey as bindInputKey,
  commandsFor as commandsForInputKey,
  unbindKey as unbindInputKey,
  unbindAll as unbindAllInputs,
} from "./bindings.js";
import { resetClientPreferences } from "./client_settings.js";
import {
  clientRuntimeConfig,
  formatClientConfigValue,
  getClientConfigEntry,
  getClientConfigValue,
  listClientConfigEntries,
  normalizeViewZoomPercent,
  resetClientConfig,
  setClientConfigValue,
} from "./client_config.js";
import { freezeEvents } from "./freeze_watchdog.js";
import {
  changeLanguage,
  getAvailableLocales,
  getCurrentLocale,
  getLocale,
  t,
} from "./i18n.js";
import { setRenderScaleMode } from "./render_scale.js";

// Client-side console commands (typed into the in-game console or chat with a
// leading slash; unknown names fall through to the server as chat commands).
//
// game.js injects its command surface here instead of this module importing
// game.js: everything the handlers need from the god module / the ui layer
// arrives through initClientCommands, so this module never joins the
// game.js import cycle (enforced by tools/check_client_deps.mjs).
let context = null;

export function initClientCommands(commandContext) {
  context = commandContext;
}

const {
  loginMapDefaultName: defaultMapName,
  loginMapDefaultX,
  loginMapDefaultY,
  loginMapDefaultHorizon,
} = clientRuntimeConfig;

export function executeClientCommand(line) {
  if (!context) return;
  const args = splitCommand(line.trim());
  if (args.length === 0) return;

  const name = args.shift().replace(/^\//, "").toLowerCase();
  if (name === "dev_debug" || name === "dev") {
    context.requestDevDebug?.();
    return;
  }
  if (name === "clear" || name === "clear_console") {
    context.clearConsole?.();
    return;
  }
  if (name === "version" || name === "ver") {
    context.addConsoleLine(appDisplayName);
    return;
  }
  if (name === "bench" || name === "benchmark") {
    context.startRenderBenchmark({
      entities: Number(args[0]) || undefined,
      frames: Number(args[1]) || undefined,
    });
    return;
  }
  if (name === "freeze_log" || name === "freezelog") {
    const recorded = freezeEvents();
    if (recorded.length === 0) {
      context.addConsoleLine("[freeze] no stalls recorded this session");
      return;
    }
    for (const event of recorded.slice(-12)) {
      const { kind, at, durationMs, detail, ...sample } = event;
      context.addConsoleLine(
        `[freeze] ${at.slice(11, 19)} ${kind} ${durationMs}ms` +
          (detail ? ` ${detail}` : "") +
          ` | ` +
          Object.entries(sample)
            .map(([key, value]) => `${key}=${value}`)
            .join(" "),
      );
    }
    return;
  }
  if (name === "render_scale" || name === "renderscale") {
    if (args.length === 0) {
      context.addConsoleLine(
        t("commands.renderScale.current", {
          mode: String(state.renderScaleMode),
          dpr: state.dpr.toFixed(2),
        }),
      );
      return;
    }
    setRenderScaleMode(args[0]);
    context.addConsoleLine(
      t("commands.renderScale.changed", {
        mode: String(state.renderScaleMode),
        dpr: state.dpr.toFixed(2),
      }),
    );
    return;
  }
  if (name === "view_zoom" || name === "viewzoom" || name === "zoom") {
    if (args.length === 0) {
      context.addConsoleLine(
        t("commands.viewZoom.current", {
          percent: normalizeViewZoomPercent(state.viewZoomPercent),
        }),
      );
      return;
    }
    const rawValue = String(args[0] || "")
      .trim()
      .replace(/%$/, "");
    const parsed = Number(rawValue);
    if (!Number.isFinite(parsed)) {
      context.addConsoleLine(
        t("commands.viewZoom.invalid", { value: args[0] }),
        "error",
      );
      return;
    }
    context.setViewZoomPercent(parsed);
    context.addConsoleLine(
      t("commands.viewZoom.changed", {
        percent: normalizeViewZoomPercent(state.viewZoomPercent),
      }),
    );
    return;
  }
  if (
    name === "change_language" ||
    name === "change-language" ||
    name === "language" ||
    name === "lang"
  ) {
    void executeLanguageCommand(args);
    return;
  }
  if (name === "say") {
    if (args.length < 2) return;
    const flagText = String(args.shift()).toLowerCase();
    const message = args.join(" ").trim();
    if (!message) return;
    if (flagText === "global" || flagText === "g")
      context.sendChat(ChatFlag.Global, message);
    else if (flagText === "local" || flagText === "l")
      context.sendChat(ChatFlag.Local, message);
    else if (flagText === "squad" || flagText === "sq")
      context.sendChat(ChatFlag.Local, `/sq ${message}`);
    else if (flagText === "whisper" || flagText === "w")
      context.sendChat(ChatFlag.Whisper, `/whisper ${message}`);
    return;
  }
  if (name === "g") {
    const message = args.join(" ").trim();
    if (message) context.sendChat(ChatFlag.Global, message);
    return;
  }
  if (name === "l") {
    const message = args.join(" ").trim();
    if (message) context.sendChat(ChatFlag.Local, message);
    return;
  }
  if (name === "whisper" || name === "w") {
    const message = args.join(" ").trim();
    if (message) context.sendChat(ChatFlag.Whisper, `/whisper ${message}`);
    return;
  }
  if (name === "map") {
    const mapName = args.join(" ").trim() || defaultMapName;
    context.loadMap(mapName);
    return;
  }
  if (name === "login_map" || name === "loginmap") {
    void executeLoginMapCommand(args);
    return;
  }
  if (
    name === "login_map_x" ||
    name === "login_map_y" ||
    name === "login_map_horizon"
  ) {
    executeLoginMapViewCommand(name, args);
    return;
  }
  if (name === "hitbox" || name === "hitboxes") {
    state.debugHitbox = args.length
      ? args[0] !== "0" && args[0].toLowerCase() !== "off"
      : !state.debugHitbox;
    context.addConsoleLine(
      t("commands.toggle.hitbox", {
        value: t(
          state.debugHitbox ? "commands.toggle.on" : "commands.toggle.off",
        ),
      }),
    );
    return;
  }
  if (name === "colorful_map") {
    executeClientSetCommand(["colorful_map", ...args]);
    return;
  }
  if (name === "set") {
    executeClientSetCommand(args);
    return;
  }
  if (name === "slider") {
    executeClientSliderCommand(args);
    return;
  }
  if (name === "keyboard_control") {
    if (args.length) {
      const value = String(args[0]).toLowerCase();
      const enabled = !(
        value === "0" ||
        value === "off" ||
        value === "false"
      );
      context.setMovementMode(enabled ? "keyboard" : "mouse");
    }
    context.addConsoleLine(
      t("commands.toggle.keyboard", {
        value: t(
          state.movementMode === "keyboard"
            ? "commands.toggle.on"
            : "commands.toggle.off",
        ),
      }),
    );
    return;
  }
  if (name === "mobile" || name === "mobile_mode" || name === "touch") {
    if (args.length) {
      const value = String(args[0]).toLowerCase();
      if (value === "toggle") {
        context.setMobileControlMode(
          state.mobileControlMode === "on" ? "off" : "on",
        );
      } else {
        context.setMobileControlMode(value);
      }
    } else {
      context.addConsoleLine(
        t("commands.toggle.mobile", { value: state.mobileControlMode }),
      );
    }
    return;
  }
  if (name === "bind") {
    executeBindCommand(args);
    return;
  }
  if (name === "unbind") {
    executeUnbindCommand(args);
    return;
  }
  if (name === "unbindall") {
    executeUnbindAllCommand(args);
    return;
  }
  if (name === "reset") {
    executeResetCommand(args);
    return;
  }

  context.sendChat(ChatFlag.Local, `/${line}`);
}

// Pulls an explicit "keyboard"/"mouse" token off the front of the argument
// list, if there is one. Without it the device is inferred from the key name
// (mouse1/wheelup -> mouse, everything else -> keyboard).
function takeDeviceToken(args) {
  const first = String(args[0] || "").toLowerCase();
  if (first === "keyboard" || first === "kb") {
    args.shift();
    return "keyboard";
  }
  if (first === "mouse" || first === "m") {
    args.shift();
    return "mouse";
  }
  return "";
}

function printBindingList(device) {
  const entries = listBindings(context.bindings, device);
  if (entries.length === 0) {
    context.addConsoleLine(t("commands.bind.empty"));
    return;
  }
  for (const entry of entries) {
    context.addConsoleLine(
      t("commands.bind.listItem", {
        device: entry.device,
        key: entry.key,
        commands: entry.commands.join(" + "),
      }),
    );
  }
}

function printBuiltinCommandList() {
  for (const command of builtinCommandHelp()) {
    const label = command.params
      ? `${command.name} ${command.params}`
      : command.name;
    context.addConsoleLine(
      `${command.hold ? "[hold] " : ""}${label} - ${command.help}`,
    );
  }
}

function executeBindCommand(args) {
  const device = takeDeviceToken(args);

  const head = String(args[0] || "").toLowerCase();
  if (head === "commands" || head === "cmds") {
    printBuiltinCommandList();
    return;
  }
  if (!args.length || head === "list") {
    printBindingList(device);
    context.addConsoleLine(t("commands.bind.usage"));
    return;
  }

  const parsed = parseBindArgs(args);
  if (!parsed) {
    context.addConsoleLine(t("commands.bind.usage"), "error");
    return;
  }

  // `bind <key>` with no command reports what that key currently runs.
  if (parsed.commands.length === 0) {
    const target = device || deviceForInputKey(parsed.key);
    const bound = commandsForInputKey(context.bindings, target, parsed.key);
    context.addConsoleLine(
      bound.length
        ? t("commands.bind.listItem", {
            device: target,
            key: parsed.key,
            commands: bound.join(" + "),
          })
        : t("commands.bind.unbound", { device: target, key: parsed.key }),
    );
    return;
  }

  const unknown = parsed.commands.find(
    (command) => !findBuiltinCommand(String(command).trim().split(/\s+/)[0]),
  );
  if (unknown) {
    context.addConsoleLine(
      t("commands.bind.unknownCommand", { command: unknown }),
      "error",
    );
    context.addConsoleLine(
      t("commands.bind.available", {
        commands: builtinCommandNames().join(", "),
      }),
    );
    return;
  }

  const target = device || deviceForInputKey(parsed.key);
  // Rebinding a key while it is held would strand its hold sources.
  releaseInputHolds({ device: target, key: parsed.key });
  bindInputKey(context.bindings, target, parsed.key, parsed.commands);
  context.onBindingsChanged?.();
  context.addConsoleLine(
    t("commands.bind.bound", {
      device: target,
      key: parsed.key,
      commands: parsed.commands.join(" + "),
    }),
  );
}

function executeUnbindCommand(args) {
  const device = takeDeviceToken(args);
  const key = normalizeInputKey(args.shift() || "");
  if (!key) {
    context.addConsoleLine(t("commands.unbind.usage"), "error");
    return;
  }
  const target = device || deviceForInputKey(key);
  releaseInputHolds({ device: target, key });
  unbindInputKey(context.bindings, target, key);
  context.onBindingsChanged?.();
  context.addConsoleLine(t("commands.unbind.unbound", { device: target, key }));
}

function executeUnbindAllCommand(args) {
  const device = takeDeviceToken(args);
  const devices = device ? [device] : ["keyboard", "mouse"];
  releaseInputHolds();
  for (const target of devices) {
    unbindAllInputs(context.bindings, target);
    context.addConsoleLine(t("commands.unbindall.unbound", { device: target }));
  }
  context.onBindingsChanged?.();
}

// `reset <target>`: put a group of client-side state back to its defaults.
function executeResetCommand(args) {
  const target = String(args.shift() || "").toLowerCase();

  if (!target || target === "help") {
    context.addConsoleLine(t("commands.reset.usage"));
    return;
  }

  const resetConfig = () => {
    executeClientSetCommand(["reset", ...args]);
  };
  const resetInputBindings = () => {
    releaseInputHolds();
    resetBindings(context.bindings);
    context.onBindingsChanged?.();
    context.addConsoleLine(t("commands.reset.bindings"));
  };
  const resetPreferences = () => {
    releaseInputHolds();
    resetClientPreferences();
    context.saveSettings();
    context.applyLoginMapView();
    context.refreshLocalizedUi();
    context.addConsoleLine(t("commands.reset.settings"));
  };

  if (target === "config" || target === "set") {
    resetConfig();
    return;
  }
  if (target === "bind" || target === "binds" || target === "bindings" || target === "keys") {
    resetInputBindings();
    return;
  }
  if (target === "settings" || target === "prefs" || target === "preferences") {
    resetPreferences();
    return;
  }
  if (target === "all") {
    args.length = 0;
    resetConfig();
    resetInputBindings();
    resetPreferences();
    context.addConsoleLine(t("commands.reset.all"));
    return;
  }

  context.addConsoleLine(t("commands.reset.unknown", { name: target }), "error");
  context.addConsoleLine(t("commands.reset.usage"));
}

// Exported: the settings panel's language selector routes through the same
// handler as the `language` console command.
export async function executeLanguageCommand(args) {
  const current = getCurrentLocale();
  if (!args.length) {
    context.addConsoleLine(
      t("commands.language.current", {
        locale: current?.id || getLocale(),
        label: current?.label || getLocale(),
      }),
    );
    context.addConsoleLine(
      t("commands.language.available", {
        languages: getAvailableLocales()
          .map((entry) => entry.id + " (" + entry.label + ")")
          .join(", "),
      }),
    );
    context.addConsoleLine(t("commands.language.usage"));
    return;
  }

  const requested = args.join(" ").trim();
  const result = await changeLanguage(requested);
  if (!result.ok) {
    context.addConsoleLine(
      t(
        result.reason === "unknown"
          ? "commands.language.unknown"
          : "commands.language.loadFailed",
        { language: requested },
      ),
      "error",
    );
    return;
  }

  state.locale = result.locale;
  context.saveSettings();
  context.refreshLocalizedUi();
  context.addConsoleLine(
    t("commands.language.changed", {
      locale: result.entry?.id || result.locale,
      label: result.entry?.label || result.locale,
    }),
  );
}

async function executeLoginMapCommand(args) {
  if (state.authenticated) {
    context.addConsoleLine(t("commands.loginMap.loggedOutOnly"), "error");
    return;
  }

  if (!args.length) {
    context.addConsoleLine(
      t("commands.loginMap.current", {
        map: state.loginMapName || defaultMapName,
      }),
    );
    context.addConsoleLine(t("commands.loginMap.usage"));
    return;
  }

  const input = args.join(" ").trim();
  const requestedMap =
    input.toLowerCase() === "reset"
      ? defaultMapName
      : appendMapExtension(input);
  const previousMap = state.loginMapName || defaultMapName;
  const loadedMap = await context.loadMap(requestedMap);

  if (!loadedMap) {
    context.addConsoleLine(
      t("commands.loginMap.unchanged", { map: previousMap }),
      "error",
    );
    if (!state.authenticated) await context.loadMap(previousMap);
    return;
  }

  state.loginMapName = requestedMap;
  context.saveSettings();
  context.addConsoleLine(t("commands.loginMap.changed", { map: requestedMap }));
}

function appendMapExtension(mapName) {
  const normalized = String(mapName || "").trim();
  if (!normalized || /\.[^/\\]+$/.test(normalized)) return normalized;
  return `${normalized}.tmj`;
}

function executeLoginMapViewCommand(name, args) {
  if (state.authenticated) {
    context.addConsoleLine(t("commands.loginMap.viewLoggedOutOnly"), "error");
    return;
  }

  const definitions = {
    login_map_x: {
      property: "loginMapX",
      defaultValue: loginMapDefaultX,
    },
    login_map_y: {
      property: "loginMapY",
      defaultValue: loginMapDefaultY,
    },
    login_map_horizon: {
      property: "loginMapHorizon",
      defaultValue: loginMapDefaultHorizon,
      minimum: 0,
    },
  };
  const definition = definitions[name];
  if (!definition) return;

  if (!args.length) {
    context.addConsoleLine(
      t("commands.config.value", {
        name,
        value: formatClientConfigValue(state[definition.property]),
      }),
    );
    return;
  }

  const rawValue = String(args[0] || "").trim().toLowerCase();
  const value =
    rawValue === "reset" ? definition.defaultValue : Number(rawValue);
  if (
    !Number.isFinite(value) ||
    (Number.isFinite(definition.minimum) && value < definition.minimum)
  ) {
    context.addConsoleLine(
      t("commands.config.invalid", { name, value: args[0] }),
      "error",
    );
    return;
  }

  state[definition.property] = value;
  context.applyLoginMapView();
  context.saveSettings();
  context.addConsoleLine(
    t("commands.config.value", {
      name,
      value: formatClientConfigValue(value),
    }),
  );
}

function executeClientSliderCommand(args) {
  if (args.length < 3) {
    context.addConsoleLine(t("commands.slider.usage"), "error");
    return;
  }

  const key = args[0];
  const entry = getClientConfigEntry(key);
  if (!entry) {
    context.addConsoleLine(t("commands.config.unknown", { name: key }), "error");
    return;
  }

  const start = Number(args[1]);
  const end = Number(args[2]);
  const explicitStep = args.length >= 4 ? Number(args[3]) : NaN;
  if (!Number.isFinite(start) || !Number.isFinite(end) || start === end) {
    context.addConsoleLine(
      t("commands.slider.invalidRange", { name: entry.name }),
      "error",
    );
    return;
  }

  const span = Math.abs(end - start);
  const step =
    Number.isFinite(explicitStep) && explicitStep > 0
      ? explicitStep
      : span / 200;
  state.slider = {
    name: entry.name,
    min: Math.min(start, end),
    max: Math.max(start, end),
    step,
  };
  context.renderSliderPanel();
  context.addConsoleLine(
    t("commands.slider.opened", {
      name: entry.name,
      start: formatClientConfigValue(state.slider.min),
      end: formatClientConfigValue(state.slider.max),
      step: formatClientConfigValue(step),
    }),
  );
}

function executeClientSetCommand(args) {
  if (!args.length) {
    for (const entry of listClientConfigEntries()) {
      const value = getClientConfigValue(state.clientConfig, entry.name);
      context.addConsoleLine(
        t("commands.config.value", {
          name: entry.name,
          value: formatClientConfigValue(value),
        }),
      );
    }
    return;
  }

  const key = args.shift();
  if (String(key || "").toLowerCase() === "reset") {
    const result = resetClientConfig(state.clientConfig, args[0]);
    if (!result.ok) {
      context.addConsoleLine(t("commands.config.unknown", { name: result.name }));
      return;
    }
    state.clientConfig = result.config;
    context.saveSettings();
    context.requestRedraw();
    context.addConsoleLine(
      t(
        result.name === "all"
          ? "commands.config.resetAll"
          : "commands.config.resetOne",
        { name: result.name },
      ),
    );
    return;
  }

  if (!args.length) {
    const entry = getClientConfigEntry(key);
    if (!entry) {
      context.addConsoleLine(t("commands.config.unknown", { name: key }));
      return;
    }
    const value = getClientConfigValue(state.clientConfig, entry.name);
    context.addConsoleLine(
      t("commands.config.value", {
        name: entry.name,
        value: formatClientConfigValue(value),
      }),
    );
    return;
  }

  const rawValue = String(args[0] || "")
    .trim()
    .toLowerCase();
  const result =
    rawValue === "reset"
      ? resetClientConfig(state.clientConfig, key)
      : setClientConfigValue(state.clientConfig, key, rawValue);
  if (!result.ok) {
    context.addConsoleLine(
      t(
        result.error === "unknown"
          ? "commands.config.unknown"
          : "commands.config.invalid",
        { name: result.name, value: args[0] },
      ),
    );
    return;
  }

  state.clientConfig = result.config;
  context.saveSettings();
  context.onClientConfigChanged?.(result.name);
  context.requestRedraw();
  // Petal label config changes need the cards rebuilt in their new size.
  if (
    result.name === "petal_label_max_font_px" ||
    result.name === "petal_label_max_width_px"
  ) {
    context.refreshLocalizedUi();
  }
  context.addConsoleLine(
    t("commands.config.value", {
      name: result.name,
      value: formatClientConfigValue(
        getClientConfigValue(state.clientConfig, result.name),
      ),
    }),
  );
}

function splitCommand(text) {
  const out = [];
  let current = "";
  let quote = "";
  for (const ch of text) {
    if ((ch === '"' || ch === "'") && !quote) {
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

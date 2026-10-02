import { dom, state } from "./app_context.js";
import { clientCommandSpecs } from "./client_commands_spec.js";
import { builtinCommandNames, inputKeyNames } from "./bindings.js";
import { listClientConfigEntries } from "./client_config.js";
import { createGameConsole } from "./game_console.js";
import { getAvailableLocales, t } from "./i18n.js";
import { appDisplayName } from "./version.js";

const { authStatus, canvas, consolePanel, consoleInput } = dom;

const defaultCompletionSources = Object.freeze({
  bool: () => ["0", "1"],
  channel: () => ["global", "local", "squad", "whisper"],
  config: () => listClientConfigEntries().map((entry) => entry.name),
  locale: () => getAvailableLocales().map((entry) => entry.id),
  mobile_mode: () => ["on", "off", "auto", "toggle"],
  bind_device: () => ["keyboard", "mouse"],
  bind_cmd: () => builtinCommandNames(),
  bind_key: () => inputKeyNames(),
  // First token of `bind`/`unbind`: an explicit device or the key itself.
  bind_target: () => ["keyboard", "mouse", ...inputKeyNames()],
  // Second token: still a key when a device was given, already a command
  // otherwise -- offer both rather than guessing wrong.
  bind_key_cmd: () => [...inputKeyNames(), ...builtinCommandNames()],
  reset_target: () => ["config", "bindings", "settings", "all"],
});

export function createConsoleUi({
  beforeOpen = null,
  executeCommand = null,
  focusTarget = canvas,
  panel = null,
  commands = clientCommandSpecs,
  completionSources = {},
  version = appDisplayName,
} = {}) {
  const setStatus = (text) => {
    authStatus.textContent = text;
  };

  const panelRoot = panel?.root || consolePanel;

  const gameConsole = createGameConsole({
    panel: panelRoot,
    input: consoleInput,
    commands,
    completionSources: { ...defaultCompletionSources, ...completionSources },
    executeCommand,
    translate: t,
    version,
    onCloseRequested: () => close(),
  });

  const close = () => {
    if (panel) panel.toggle(false);
    else {
      state.consoleOpen = false;
      consolePanel.classList.remove("open");
    }
    gameConsole.setOpen(false);
    focusTarget?.focus?.();
  };

  const syncObserver = new MutationObserver(() => {
    const openClass = panelRoot.classList.contains("open");
    gameConsole.setOpen(openClass);
    if (openClass) gameConsole.focusInput();
  });
  syncObserver.observe(panelRoot, { attributes: true, attributeFilter: ["class"] });

  const toggle = (force) => {
    const open = panel
      ? panel.toggle(force)
      : typeof force === "boolean"
        ? force
        : !state.consoleOpen;
    if (!panel) {
      state.consoleOpen = open;
      consolePanel.classList.toggle("open", open);
    }
    if (open) {
      beforeOpen?.();
      gameConsole.setOpen(true);
      gameConsole.focusInput();
    } else {
      gameConsole.setOpen(false);
      focusTarget?.focus?.();
    }
    return open;
  };

  const submit = () => {
    gameConsole.submit();
  };

  return {
    addLine: (text, kind = "") => gameConsole.addLine(text, kind),
    clear: () => gameConsole.clear(),
    setStatus,
    submit,
    toggle,
  };
}

import { dom, state } from "./app_context.js";

const {
  authStatus,
  canvas,
  consolePanel,
  consoleLog,
  consoleInput,
} = dom;

export function createConsoleUi({ beforeOpen = null, executeCommand = null, focusTarget = canvas } = {}) {
  const setStatus = (text) => {
    authStatus.textContent = text;
  };

  const addLine = (text, kind = "") => {
    const line = document.createElement("div");
    line.className = `console-line${kind ? ` ${kind}` : ""}`;
    line.textContent = text;
    consoleLog.appendChild(line);
    while (consoleLog.children.length > 90) consoleLog.removeChild(consoleLog.firstChild);
    consoleLog.scrollTop = consoleLog.scrollHeight;
  };

  const toggle = (force) => {
    state.consoleOpen = typeof force === "boolean" ? force : !state.consoleOpen;
    state.debugGrid = state.consoleOpen;
    consolePanel.classList.toggle("open", state.consoleOpen);
    if (state.consoleOpen) {
      beforeOpen?.();
      consoleInput.focus();
      consoleInput.setSelectionRange(consoleInput.value.length, consoleInput.value.length);
    } else {
      consoleInput.blur();
      focusTarget?.focus?.();
    }
  };

  const submit = () => {
    const line = consoleInput.value.trim();
    consoleInput.value = "";
    if (!line) return;
    addLine(`> ${line}`, "command");
    executeCommand?.(line);
  };

  return {
    addLine,
    setStatus,
    submit,
    toggle,
  };
}

import { dom, state } from "./app_context.js";

const settingsKey = "florrbt.web.settings";

const {
  wsUrlInput,
  accountInput,
  passwordInput,
} = dom;

export function normalizeMobileControlMode(value) {
  const text = String(value || "").toLowerCase();
  if (text === "1" || text === "true" || text === "yes" || text === "on") return "on";
  if (text === "0" || text === "false" || text === "no" || text === "off") return "off";
  return "auto";
}

export function loadClientSettings() {
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
  state.mobileControlMode = normalizeMobileControlMode(saved.mobileControlMode || "auto");
}

export function saveClientSettings() {
  localStorage.setItem(settingsKey, JSON.stringify({
    wsUrl: wsUrlInput.value.trim(),
    account: accountInput.value,
    password: passwordInput.value,
    keyboardControl: state.keyboardControl,
    mobileControlMode: state.mobileControlMode,
  }));
}

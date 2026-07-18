import { dom, state } from "./app_context.js";
import { defaultClientConfig, normalizeClientConfig } from "./client_config.js";

const settingsKey = "florrbt.web.settings";
const settingsVersion = 7;
const dandelionRightFacingConfigVersion = 7;

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
  state.clientConfig = normalizeClientConfig(saved.clientConfig);
  if ((saved.version || 0) < dandelionRightFacingConfigVersion) {
    state.clientConfig = {
      ...state.clientConfig,
      dandeMissileScale: defaultClientConfig.dandeMissileScale,
      dandeMissileAngle: defaultClientConfig.dandeMissileAngle,
      dandeMissileOffset: defaultClientConfig.dandeMissileOffset,
      dandeMissileYOffset: defaultClientConfig.dandeMissileYOffset,
      dandeMissileAnchorX: defaultClientConfig.dandeMissileAnchorX,
      dandeMissileAnchorY: defaultClientConfig.dandeMissileAnchorY,
      dandeBaseX: defaultClientConfig.dandeBaseX,
      dandeBaseY: defaultClientConfig.dandeBaseY,
      dandeBaseScale: defaultClientConfig.dandeBaseScale,
      dandeBaseAngle: defaultClientConfig.dandeBaseAngle,
    };
  }
}

export function saveClientSettings() {
  localStorage.setItem(settingsKey, JSON.stringify({
    version: settingsVersion,
    wsUrl: wsUrlInput.value.trim(),
    account: accountInput.value,
    password: passwordInput.value,
    keyboardControl: state.keyboardControl,
    mobileControlMode: state.mobileControlMode,
    clientConfig: normalizeClientConfig(state.clientConfig),
  }));
}

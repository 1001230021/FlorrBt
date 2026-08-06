import { dom, loadoutPresetCount, state } from "./app_context.js";
import {
  clientRuntimeConfig,
  defaultClientConfig,
  normalizeClientConfig,
} from "./client_config.js";

const settingsKey = "florrbt.web.settings";
const settingsVersion = 13;
const dandelionRightFacingConfigVersion = 7;
const loginMapViewDefaultsVersion = 10;
const {
  loginMapDefaultName,
  loginMapDefaultX,
  loginMapDefaultY,
  loginMapDefaultHorizon,
} = clientRuntimeConfig;

const { wsUrlInput, accountInput, passwordInput } = dom;

export function normalizeMobileControlMode(value) {
  const text = String(value || "").toLowerCase();
  if (text === "1" || text === "true" || text === "yes" || text === "on")
    return "on";
  if (text === "0" || text === "false" || text === "no" || text === "off")
    return "off";
  return "auto";
}

function normalizeLoadoutSlot(value) {
  const petalType = Number(value?.petalType);
  const rarity = Number(value?.rarity);
  if (
    !Number.isInteger(petalType) ||
    !Number.isInteger(rarity) ||
    petalType <= 0 ||
    rarity <= 0 ||
    petalType > 255 ||
    rarity > 255
  )
    return { petalType: 0, rarity: 0 };
  return { petalType, rarity };
}

function normalizeLoadoutPreset(value) {
  return {
    primarySlots: Array.isArray(value?.primarySlots)
      ? value.primarySlots.map(normalizeLoadoutSlot)
      : [],
    secondarySlots: Array.isArray(value?.secondarySlots)
      ? value.secondarySlots.map(normalizeLoadoutSlot)
      : [],
  };
}

function normalizeLoadoutPresets(value) {
  const presets = Array.isArray(value) ? value : [];
  return Array.from({ length: loadoutPresetCount }, (_, index) =>
    normalizeLoadoutPreset(presets[index]),
  );
}

export function loadClientSettings() {
  let saved = {};
  try {
    saved = JSON.parse(localStorage.getItem(settingsKey) || "{}");
  } catch {
    saved = {};
  }

  if (Object.prototype.hasOwnProperty.call(saved, "password")) {
    delete saved.password;
    try {
      localStorage.setItem(settingsKey, JSON.stringify(saved));
    } catch {
      // Settings remain usable even when storage is unavailable.
    }
  }

  wsUrlInput.value =
    saved.wsUrl || `ws://${location.host || "127.0.0.1:8080"}/ws`;
  accountInput.value = saved.account || "";
  passwordInput.value = "";
  state.locale = String(saved.locale || "en");
  state.keyboardControl = saved.keyboardControl === true;
  state.mobileControlMode = normalizeMobileControlMode(
    saved.mobileControlMode || "auto",
  );
  state.loadoutPresets = normalizeLoadoutPresets(saved.loadoutPresets);
  const activeLoadoutPreset = Number(saved.activeLoadoutPreset);
  state.activeLoadoutPreset = Number.isInteger(activeLoadoutPreset)
    ? Math.min(loadoutPresetCount - 1, Math.max(0, activeLoadoutPreset))
    : 0;
  state.loginMapName =
    String(saved.loginMapName || loginMapDefaultName).trim() ||
    loginMapDefaultName;
  const useSavedLoginMapView =
    (saved.version || 0) >= loginMapViewDefaultsVersion;
  state.loginMapX = useSavedLoginMapView
    ? finiteSettingNumber(saved.loginMapX, loginMapDefaultX)
    : loginMapDefaultX;
  state.loginMapY = useSavedLoginMapView
    ? finiteSettingNumber(saved.loginMapY, loginMapDefaultY)
    : loginMapDefaultY;
  state.loginMapHorizon = useSavedLoginMapView
    ? Math.max(
        0,
        finiteSettingNumber(saved.loginMapHorizon, loginMapDefaultHorizon),
      )
    : loginMapDefaultHorizon;
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
  localStorage.setItem(
    settingsKey,
    JSON.stringify({
      version: settingsVersion,
      wsUrl: wsUrlInput.value.trim(),
      account: accountInput.value,
      locale: state.locale,
      keyboardControl: state.keyboardControl,
      mobileControlMode: state.mobileControlMode,
      loadoutPresets: normalizeLoadoutPresets(state.loadoutPresets),
      activeLoadoutPreset: state.activeLoadoutPreset,
      loginMapName: state.loginMapName,
      loginMapX: state.loginMapX,
      loginMapY: state.loginMapY,
      loginMapHorizon: state.loginMapHorizon,
      clientConfig: normalizeClientConfig(state.clientConfig),
    }),
  );
}

function finiteSettingNumber(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

import { dom, state, ui } from "./app_context.js";
import { releaseInputHolds } from "./bindings.js";
import { clientRuntimeConfig } from "./client_config.js";
import {
  loadClientSettings,
  normalizeMobileControlMode,
  normalizeMovementMode,
  saveClientSettings,
} from "./client_settings.js";
import { applyDocumentTranslations, t } from "./i18n.js";
import { formatShortCount, hidePetalInfoTooltip } from "./petal_info.js";
import { packChores, packInput, petalTypeName, rarityName } from "./protocol.js";
import { toggleCodex } from "./ui_codex.js";
import {
  authUiMode,
  closeSliderPanel,
  drawScene,
  isOwnerDead,
  renderCraftPanel,
  renderDeathOverlayContent,
  renderEmailSettingsRow,
  renderInventoryPanel,
  renderSliderPanel,
  renderTalentPanel,
  setAuthUiMode,
} from "./game.js";
import { sendBytes } from "./net_outbound.js";

const defaultMapName = clientRuntimeConfig.loginMapDefaultName;
const {
  codexBtn,
  emailBindingStatus,
  loadingScreen,
  quickActions,
  quickBackpackBtn,
  quickCraftBtn,
  quickTalentBtn,
  quickTalentPoints,
  talentPointsLabel,
  topSettingsBtn,
} = dom;

export function loadSettings() {
  loadClientSettings();
  if (!state.loginMapName) state.loginMapName = defaultMapName;
}

export function saveSettings() {
  saveClientSettings();
}

export function setMobileControlMode(value, announce = true) {
  state.mobileControlMode = normalizeMobileControlMode(value);
  ui.mobileControls?.setMode(state.mobileControlMode);
  saveSettings();
  if (announce)
    addConsoleLine(
      t("commands.toggle.mobile", {
        value: t(`commands.toggle.${state.mobileControlMode}`, {}, {
          defaultValue: state.mobileControlMode,
        }),
      }),
    );
}

export function setMovementMode(value, announce = false) {
  state.movementMode = normalizeMovementMode(value);
  releaseInputHolds();
  state.localMoveInput = { x: 0, y: 0 };
  saveSettings();
  if (announce)
    addConsoleLine(
      t("commands.toggle.movement", {
        value: t(`ui.settings.movementModes.${state.movementMode}`),
      }),
    );
}

export function setHeldAction(action, value) {
  const enabled = !!value;
  if (action === "attack") state.holdAttack = enabled;
  else if (action === "defense") state.holdDefense = enabled;
  saveSettings();
}

export async function setFullscreen(enabled) {
  try {
    const element = document.fullscreenElement;
    if (enabled && !element) await document.documentElement.requestFullscreen?.();
    else if (!enabled && element) await document.exitFullscreen?.();
  } catch (error) {
    addConsoleLine(String(error?.message || error || "Fullscreen failed"), "error");
  }
  // Keep the saved preference in line with the actual fullscreen state.
  state.fullscreen = !!document.fullscreenElement;
  saveSettings();
  ui.settingsUi?.render();
}

export function toggleFullscreen() {
  return setFullscreen(!document.fullscreenElement);
}

export function setStatus(text, good = false) {
  ui.consoleUi.setStatus(text, good);
  if (emailBindingStatus) {
    emailBindingStatus.textContent = text;
    emailBindingStatus.classList.toggle("good", good);
  }
}

export function addConsoleLine(text, kind = "") {
  ui.consoleUi.addLine(text, kind);
}

export function formatPetalItem(petalType, rarity, count = null) {
  const params = {
    petal: petalTypeName(petalType),
    rarity: rarityName(rarity),
    count: count === null ? "" : formatShortCount(count),
  };
  return t(count === null ? "formats.item" : "formats.itemCount", params);
}

export function formatRarityPetal(petalType, rarity) {
  return t("formats.rarityPetal", {
    petal: petalTypeName(petalType),
    rarity: rarityName(rarity),
  });
}

export function localizationSlug(value) {
  return String(value || "")
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .replace(/[^A-Za-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "")
    .toLowerCase();
}

export function localizedTalentName(node) {
  return t("talents.names." + localizationSlug(node?.name), {}, {
    defaultValue: node?.name || "",
  });
}

export function toggleConsole(force) {
  ui.consoleUi.toggle(force);
  if (!state.consoleOpen) closeSliderPanel();
}

export function updateQuickActionButtons() {
  const points = Math.max(0, Math.floor(state.talentPoints || 0));
  quickBackpackBtn?.classList.toggle("active", state.backpackOpen);
  quickCraftBtn?.classList.toggle("active", state.craftOpen);
  quickCraftBtn?.classList.toggle("forge-mode", state.forgeMode);
  quickTalentBtn?.classList.toggle("active", state.talentOpen);
  topSettingsBtn?.classList.toggle("active", state.settingsOpen);
  codexBtn?.classList.toggle("active", state.codexOpen);
  if (talentPointsLabel)
    talentPointsLabel.textContent = t("ui.talents.points", { count: points });
  if (quickTalentPoints) {
    quickTalentPoints.textContent = formatShortCount(points);
    quickTalentPoints.classList.toggle("hidden", points <= 0);
    quickTalentBtn?.setAttribute(
      "title",
      points > 0
        ? t("ui.quick.talentsWithPointsTitle", { count: points })
        : t("ui.quick.talentsTitle"),
    );
  }
}

export function updatePlayUiVisibility() {
  const visible = state.authenticated;
  quickActions?.classList.toggle("hidden", !visible);
  const topLeftActions = document.getElementById("topLeftActions");
  topLeftActions?.classList.toggle("hidden", !visible);
  ui.chatUi.setVisible(visible);
  ui.mobileControls?.setMode(state.mobileControlMode);
  ui.mobileControls?.setEnabled(visible);
  renderEmailSettingsRow();
}

export function setLoadingScreenVisible(visible) {
  if (!loadingScreen) return;
  loadingScreen.classList.toggle("hidden", !visible);
  loadingScreen.setAttribute("aria-busy", visible ? "true" : "false");
  if (!visible) return;

  releaseInputHolds();
  state.localMoveInput = { x: 0, y: 0 };
  state.digging = false;
  ui.mobileControls?.reset();
  if (state.authenticated) {
    sendBytes(packInput(0, 0));
    sendBytes(packChores(false, false, false, false, false));
  }
}

export function refreshLocalizedUi() {
  applyDocumentTranslations();
  setAuthUiMode(authUiMode);
  ui.chatUi.refreshTranslations();
  ui.mobileControls?.refreshTranslations();
  ui.settingsUi.refreshTranslations();
  renderEmailSettingsRow();
  updateQuickActionButtons();
  hidePetalInfoTooltip();
  renderInventoryPanel({ forceInventory: true });
  if (state.craftOpen) renderCraftPanel();
  if (state.talentOpen) renderTalentPanel();
  if (isOwnerDead()) renderDeathOverlayContent();
  if (state.slider) renderSliderPanel();
  setStatus(
    t(state.connected ? "status.connected" : "status.disconnected"),
    state.connected,
  );
  requestAnimationFrame(drawScene);
}

export function toggleBackpack(force) {
  if (ui.backpackUiPanel.toggle(force) && force !== false) toggleCodex(false);
}

export function toggleCraft(force) {
  if (ui.craftUiPanel.toggle(force) && force !== false) toggleCodex(false);
}

export function toggleTalent(force) {
  if (ui.talentUiPanel.toggle(force) && force !== false) toggleCodex(false);
}

export function toggleSettings(force) {
  const opened = ui.settingsUi.toggle(force);
  if (opened && force !== false) toggleCodex(false);
  return state.settingsOpen;
}

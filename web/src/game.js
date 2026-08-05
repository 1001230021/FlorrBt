import {
  AuthMode,
  AuthResultCode,
  ChatFlag,
  FULL_SNAPSHOT_BASE_ID,
  NETWORK_PETAL_TYPE_OFFSET,
  PetalNames,
  PetalSlotCopyState,
  RarityNames,
  ServerType,
  appendBytes,
  clamp,
  isDropEntity,
  isPetalEntity,
  mobTypeName,
  packAuth,
  packChat,
  packChores,
  packCraft,
  packForge,
  packEquip,
  packInput,
  packInputFrame,
  packSecondarySlot,
  packSnapshotAck,
  packStateRequest,
  packTalentRequest,
  packUnequip,
  parseServerMessage,
  petalTypeFromEntity,
  petalTypeName,
  popFrame,
  rarityColor,
  rarityName,
} from "./protocol.js";
import { clearLadybugPattern, drawNormalLadybug } from "./ladybug_sprite.js";
import { drawBeetle } from "./beetle_sprite.js";
import { drawSoldierAnt } from "./soldier_ant_sprite.js";
import {
  drawBee,
  drawBumbleBee,
  drawHornet,
  drawHornetMissile,
  drawPollen,
} from "./bee_sprite.js";
import {
  drawAntEggMob,
  drawAntHole,
  drawBabyAnt,
  drawBabyFireAnt,
  drawBabyTermite,
  drawDandelion,
  drawFireQueenAnt,
  drawLeafcutterSoldier,
  drawLeafPiece,
  drawPortal,
  drawQueenAnt,
  drawRock,
  drawSandstorm,
  drawSoldierFireAntMob,
  drawSoldierTermite,
  drawTermiteOvermind,
  drawWorkerAnt,
  drawWorkerFireAnt,
  drawWorkerTermite,
} from "./garden_mob_sprite.js";
import { drawSpider, drawSpiderWeb } from "./spider_sprite.js";
import {
  dom,
  emptySlot,
  loadoutPresetCount,
  slotHasItem,
  state,
} from "./app_context.js";
import { createChatUi } from "./chat_ui.js";
import {
  loadClientSettings,
  normalizeMobileControlMode,
  saveClientSettings,
} from "./client_settings.js";
import {
  clientRuntimeConfig,
  formatClientConfigValue,
  getClientConfigEntry,
  getClientConfigValue,
  listClientConfigEntries,
  resetClientConfig,
  setClientConfigValue,
} from "./client_config.js";
import { createConsoleUi } from "./console_ui.js";
import { createMapRenderer } from "./map_renderer.js";
import { createMobileControls } from "./mobile_controls.js";
import { createMysteryAudio } from "./mystery_audio.js";
import {
  drawSolidProgressBar,
  traceSolidProgressBarPath,
} from "./render_primitives.js";
import { createTextInput, isTextInputActive } from "./text_input.js";
import {
  applyDocumentTranslations,
  changeLanguage,
  compareLocalized,
  getAvailableLocales,
  getCurrentLocale,
  getLocale,
  initI18n,
  t,
} from "./i18n.js";
import { appDisplayName, appVersion } from "./version.js";
import {
  PetalIconIds,
  antEggMobType,
  antHoleType,
  babyAntType,
  babyFireAntType,
  babyTermiteType,
  bandageBeetleType,
  beeType,
  beetleSpriteForwardOffsetScale,
  beetleType,
  bloodSacrificeEffectType,
  bossRarity,
  bumbleBeeType,
  compassUltraIconMinRarity,
  dandelionMissileType,
  dandelionType,
  dummyType,
  fireAntEggType,
  fireQueenAntType,
  flagAntennae,
  flagAttacking,
  flagCarryingLeafPiece,
  flagDead,
  flagDefending,
  flagAttached,
  flagOwner,
  flagSummoned,
  entityHasState,
  flowerTextureVersion,
  hornetMissileType,
  hornetType,
  leafcutterSoldierType,
  leafPieceType,
  maxBossBars,
  mechaFlowerType,
  mobSpriteCoverScale,
  mobSpriteEffectiveBox,
  mobSpriteViewBox,
  nonStackPetalTypes,
  normalLadybugType,
  petalAirType,
  petalAntEggType,
  petalAntennaeType,
  petalBandageType,
  petalBasilType,
  petalBeetleEggType,
  petalBlackFungusType,
  petalBroccoliType,
  petalBasicType,
  petalBloodSacrificeType,
  petalBoneType,
  petalBrokenEggType,
  petalBubbleType,
  petalCactusType,
  petalCarrotType,
  petalCogwheelType,
  petalCoinType,
  petalCompassType,
  petalCornType,
  petalCorruptionType,
  petalDahliaType,
  petalDandelionType,
  petalDouliType,
  petalDiscType,
  petalDustType,
  petalFasterType,
  petalFragmentType,
  petalGlassType,
  petalGoldenLeafType,
  petalHeavyType,
  petalHoneyType,
  petalIrisType,
  petalLeafType,
  petalLentilType,
  petalLightType,
  petalMimicType,
  petalMissileType,
  petalMoonType,
  petalNullificationType,
  petalOrangeType,
  petalPincerType,
  petalPollenType,
  petalRelicType,
  petalRiceType,
  petalRockType,
  petalRoseType,
  petalSawbladeType,
  petalShovelType,
  petalSoilType,
  petalStingerType,
  petalThirdEyeType,
  petalTriangleType,
  petalTrapperType,
  petalWaxType,
  petalWebType,
  petalWhiteFungusType,
  petalWingType,
  petalYuccaType,
  petalYggdrasilType,
  petalYinYangType,
  petalCardIconScale,
  playerFlowerType,
  pollenProjectileType,
  portalType,
  queenAntType,
  rarityDisplayOrder,
  rarityEternal,
  rarityExotic,
  rarityPrimordial,
  rarityShortNames,
  raritySortRanks,
  raritySuper,
  rarityUnique,
  rockType,
  sandstormType,
  skillWindupIdFromFlags,
  soldierAntType,
  soldierFireAntType,
  soldierTermiteType,
  spiderWebZoneType,
  spiderType,
  stateCorruption,
  stateDigging,
  stateInvincible,
  statePoison,
  statePsionicConnection,
  stateUndead,
  stingerSplitIconMinRarity,
  titanType,
  trapProjectileType,
  trapperLivePetalViewBox,
  summonedBeetleType,
  summonedSoldierAntType,
  queenAntEggType,
  queenFireAntEggType,
  termiteEggType,
  termiteOvermindType,
  workerAntType,
  workerFireAntType,
  workerTermiteType,
  waxLivePetalViewBox,
  waxLivePetalVisualScale,
  worldDropSizeScale,
  worldPetalSizeScale,
} from "./game_ids.js";
import { collectSceneRenderPasses } from "./scene_passes.js";
import {
  TalentId,
  talentChildrenByBase,
  talentKey,
  talentNodeByKey,
  talentRootNodes,
} from "./talent_data.js";

const {
  canvas,
  loadingScreen,
  ctx,
  authPanel,
  wsUrlInput,
  accountInput,
  passwordInput,
  emailInput,
  verificationCodeInput,
  authPasswordRow,
  authEmailRow,
  authCodeRow,
  bindingNotice,
  registerModeRow,
  registerModeInput,
  sendCodeBtn,
  connectBtn,
  deathOverlay,
  deathSource,
  deathLoot,
  reviveBtn,
  deathCloseBtn,
  quickActions,
  quickBackpackBtn,
  quickTalentBtn,
  quickCraftBtn,
  quickTalentPoints,
  chatChannels,
  chatHint,
  chatInput,
  petalInfoTooltip,
  sliderPanel,
  sliderTitle,
  sliderValue,
  sliderInput,
  sliderCloseBtn,
  debugInfo,
  consoleInput,
  backpackPanel,
  backpackCloseBtn,
  craftPanel,
  craftTitle,
  craftCloseBtn,
  craftStage,
  craftResultSlot,
  craftList,
  craftSelection,
  craftChance,
  craftOnceBtn,
  talentPanel,
  talentCloseBtn,
  talentPointsLabel,
  talentTree,
  primarySlots,
  secondarySlots,
  inventoryList,
  inventoryStacking,
  inventorySearch,
} = dom;

const wsUrlField = createTextInput(wsUrlInput, {
  onSubmit: () => connectAndAuth(),
});
const accountField = createTextInput(accountInput, {
  onSubmit: () => connectAndAuth(),
});
const passwordField = createTextInput(passwordInput, {
  onSubmit: () => connectAndAuth(),
});
const emailField = createTextInput(emailInput, {
  onSubmit: () => connectAndAuth(),
});
const verificationCodeField = createTextInput(verificationCodeInput, {
  onSubmit: () => connectAndAuth(),
});

let mobileControls = null;
const chatUi = createChatUi({
  beforeOpen: () => {
    if (state.consoleOpen) toggleConsole(false);
    state.attacking = false;
    state.defending = false;
    state.digging = false;
    mobileControls?.reset();
    sendNeutralGameplayState();
  },
  executeCommand: (line) => executeClientCommand(line),
  focusTarget: canvas,
});
const consoleUi = createConsoleUi({
  beforeOpen: () => closeChat(),
  executeCommand: (line) =>
    executeClientCommand(line.startsWith("/") ? line.slice(1) : line),
  focusTarget: canvas,
});
const mysteryAudio = createMysteryAudio({
  playerEntityType: playerFlowerType,
  douliPetalType: petalDouliType,
});

const defaultMapName = clientRuntimeConfig.loginMapDefaultName;
const mapRenderer = createMapRenderer({
  ctx,
  defaultMapName,
  addConsoleLine: (text, kind = "") => addConsoleLine(text, kind),
  requestDraw: () => requestAnimationFrame(drawScene),
  setLoadingVisible: (visible) => setLoadingScreenVisible(visible),
  worldScale,
  worldToScreen,
});
const viewRadiusTransitionDurationMs = 100;
let pendingSnapshot = null;
const snapshotHistory = new Map();
const snapshotHistoryLimit = 90;
const scheduledInputLeadTicks = 2;
let snapshotResetRequested = false;
const reconnectInitialDelayMs = 250;
const reconnectMaxDelayMs = 5000;
const douliOverlaySizeScale = 3.4;
const douliOverlayOffsetYScale = -0.55;
const douliOverlayAngle = -0.34;
let reconnectTimer = null;
let reconnectAttempt = 0;
let reconnectArmed = false;
let resumingConnection = false;
let connectionGeneration = 0;
let pageUnloading = false;
const AuthUiMode = Object.freeze({
  Login: "login",
  Register: "register",
  Binding: "binding",
});
let authUiMode = AuthUiMode.Login;
let pendingAuthMode = null;
let activeAuthMode = null;
const {
  packetInterval,
  deathFadeDuration,
  deathScaleBoost,
  hurtFlashDuration,
  hurtFlashMinDelta,
  missingEntityViewEdgeGrace,
  viewScreenFill,
  viewScreenPadding,
  mouseMoveDeadzonePx,
  entityFrameMinScreenRadius,
  entityPixelMinScreenRadius,
  entityDetailMinScreenRadius,
  petalDetailMinScreenRadius,
  dropDetailMinScreenRadius,
  bloodSacrificeDrawPhaseEnd,
  bloodSacrificeInitialHeading,
  bloodSacrificeInnerRadiusScale,
  bloodSacrificeOuterAlpha,
  bloodSacrificeShakeAmplitudePx,
  bloodSacrificeParticleReferenceRadius,
  bloodSacrificeHeadParticleBurstTicks,
  bloodSacrificeHeadParticleCount,
  bloodSacrificeHeadParticleRadius,
  bloodSacrificeSweepIntervalSeconds,
  bloodSacrificeSweepDurationSeconds,
  bloodSacrificeSweepEmissionSpacingPx,
  bloodSacrificeSweepStepMin,
  bloodSacrificeSweepStepMax,
  bloodSacrificeSweepReferenceStep,
  bloodSacrificeSweepParticleCount,
  bloodSacrificeSweepParticleRadius,
  bloodSacrificeVisionMaxAlpha,
  petalParticleBurstTicks,
  particleLifetimeSeconds,
  particleMaxCount,
  particleSpeedMin,
  particleSpeedMax,
  particleVelocityDampingPerSecond,
  particleSize,
  directionalPetalAngleOffset,
  compassPetalAngleOffset,
  renderCullPaddingPx,
  renderLoadMediumEntityCount,
  renderLoadHighEntityCount,
  ownerRenderLerpRate,
  entityRenderLerpRate,
  ownerAngleLerpRate,
  entityAngleLerpRate,
  renderSnapBaseDistance,
  ownerRenderSnapBaseDistance,
  serverFixedDt,
  ownerBaseMaxVelocity,
  ownerBaseAcceleration,
  ownerDiggingSpeedMultiplier,
  ownerStopDampingPerTick,
  ownerStopVelocityEpsilon,
  ownerSlowToMaxVelocityTime,
  ownerPredictionTeleportDistance,
  ownerPredictionHardCorrectionDistance,
  ownerPredictionCorrectionDeadzone,
  ownerPredictionSoftCorrectionRate,
  ownerPredictionHardCorrectionRate,
  ownerPredictionSpeedSampleMinDt,
  ownerPredictionSnapshotStaleMin,
  ownerPredictionTimingSampleCount,
  ownerPredictionMinTickScale,
  entitySnapshotVelocityBlend,
  entityExtrapolateMaxSeconds,
  entityExtrapolateMaxDistance,
  petalRenderLerpRate,
  petalAngleLerpRate,
  petalExtrapolateMaxSeconds,
  petalExtrapolateMaxDistance,
  slotTransactionTimeoutMs,
  bossBarBandTopRatio,
  bossBarBandBottomRatio,
  bossBarWidthRatio,
  bossBarMinWidthPx,
  bossBarMaxWidthPx,
  bossBarHorizontalMarginPx,
  bossBarHeightPx,
  bossBarTitleFontPx,
  bossBarRarityFontPx,
  bossBarLabelGapPx,
  bossBarTitleOutlinePx,
  bossBarRarityOutlinePx,
  bossBarRadiusPx,
  loginMapDefaultX,
  loginMapDefaultY,
  loginMapDefaultHorizon,
  titanForgeRange,
  minimapMarginPx,
  minimapLocalMaxPx,
  minimapFullMaxPx,
  minimapHudGapPx,
  minimapTileSubdivisions,
  minimapBackgroundColor,
  minimapMaterialColors,
  minimapUnknownMaterialColor,
  minimapMonochromeBackgroundColor,
  minimapMonochromeWallColor,
  checkpointZoneOutlineColor,
  checkpointZoneOutlineWidthPx,
} = clientRuntimeConfig;
const playerTeam = 1;
const craftRarityDisplayOrder = rarityDisplayOrder.filter((rarity) =>
  canCraftRarity(rarity),
);
const ownerMovementTalentMultiplierByRarity = Object.freeze({
  3: 1.1,
  4: 1.2,
  5: 1.3,
  6: 1.4,
  7: 1.5,
  8: 1.75,
  9: 2,
});
let currentRenderTimeSeconds = 0;
let currentVisibleEntityCount = 0;
let currentRenderLoad = 0;
let currentHornetSkill2WindupOwners = [];
let minimapHitRect = null;
let minimapCache = null;
const PetalNameToType = new Map(
  PetalNames.map((name, index) => [normalizePetalName(name), index]),
);
const assetImages = new Map();
const livePetalImages = new Map();
const bandageOverlayImages = new Map();
const transformedPetalImages = new Map();
const strippedPetalIconUrls = new Map();
const petalLabelIconUrls = new Map();
const mimicLabelIconUrls = new Map();
function loadSettings() {
  loadClientSettings();
  if (!state.loginMapName) state.loginMapName = defaultMapName;
}

function saveSettings() {
  saveClientSettings();
}

function setMobileControlMode(value, announce = true) {
  state.mobileControlMode = normalizeMobileControlMode(value);
  mobileControls?.setMode(state.mobileControlMode);
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

function setStatus(text, good = false) {
  consoleUi.setStatus(text, good);
}

function addConsoleLine(text, kind = "") {
  consoleUi.addLine(text, kind);
}

function formatPetalItem(petalType, rarity, count = null) {
  const params = {
    petal: petalTypeName(petalType),
    rarity: rarityName(rarity),
    count: count === null ? "" : formatShortCount(count),
  };
  return t(count === null ? "formats.item" : "formats.itemCount", params);
}

function formatRarityPetal(petalType, rarity) {
  return t("formats.rarityPetal", {
    petal: petalTypeName(petalType),
    rarity: rarityName(rarity),
  });
}

function localizationSlug(value) {
  return String(value || "")
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .replace(/[^A-Za-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "")
    .toLowerCase();
}

function localizedTalentName(node) {
  return t("talents.names." + localizationSlug(node?.name), {}, {
    defaultValue: node?.name || "",
  });
}

function toggleConsole(force) {
  consoleUi.toggle(force);
  if (!state.consoleOpen) closeSliderPanel();
}

function updateQuickActionButtons() {
  const points = Math.max(0, Math.floor(state.talentPoints || 0));
  quickBackpackBtn?.classList.toggle("active", state.backpackOpen);
  quickCraftBtn?.classList.toggle("active", state.craftOpen);
  quickCraftBtn?.classList.toggle("forge-mode", state.forgeMode);
  quickTalentBtn?.classList.toggle("active", state.talentOpen);
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

function updatePlayUiVisibility() {
  const visible = state.authenticated;
  quickActions?.classList.toggle("hidden", !visible);
  chatUi.setVisible(visible);
  mobileControls?.setMode(state.mobileControlMode);
  mobileControls?.setEnabled(visible);
}

function setLoadingScreenVisible(visible) {
  if (!loadingScreen) return;
  loadingScreen.classList.toggle("hidden", !visible);
  loadingScreen.setAttribute("aria-busy", visible ? "true" : "false");
  if (!visible) return;

  state.keys.clear();
  state.localMoveInput = { x: 0, y: 0 };
  state.attacking = false;
  state.defending = false;
  state.digging = false;
  mobileControls?.reset();
  if (state.authenticated) {
    sendBytes(packInput(0, 0));
    sendBytes(packChores(false, false, false, false, false));
  }
}

function refreshLocalizedUi() {
  applyDocumentTranslations();
  setAuthUiMode(authUiMode);
  chatUi.refreshTranslations();
  mobileControls?.refreshTranslations();
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

function toggleBackpack(force) {
  state.backpackOpen = typeof force === "boolean" ? force : !state.backpackOpen;
  if (state.backpackOpen) {
    toggleCraft(false);
    toggleTalent(false);
  }
  backpackPanel.classList.toggle("open", state.backpackOpen);
  updateQuickActionButtons();
  if (state.backpackOpen) renderInventoryPanel({ forceInventory: true });
}

function toggleCraft(force) {
  state.craftOpen = typeof force === "boolean" ? force : !state.craftOpen;
  if (state.craftOpen) {
    toggleBackpack(false);
    toggleTalent(false);
    renderCraftPanel();
  } else {
    clearCraftDisplay();
  }
  craftPanel.classList.toggle("open", state.craftOpen);
  updateQuickActionButtons();
}

function toggleTalent(force) {
  state.talentOpen = typeof force === "boolean" ? force : !state.talentOpen;
  if (state.talentOpen) {
    toggleBackpack(false);
    toggleCraft(false);
    renderTalentPanel();
  }
  talentPanel.classList.toggle("open", state.talentOpen);
  updateQuickActionButtons();
}

function submitConsole() {
  consoleUi.submit();
}

function cancelReconnect() {
  if (reconnectTimer === null) return;
  window.clearTimeout(reconnectTimer);
  reconnectTimer = null;
}

function scheduleReconnect() {
  if (!reconnectArmed || pageUnloading || reconnectTimer !== null) return;
  const delay = Math.min(
    reconnectMaxDelayMs,
    reconnectInitialDelayMs * 2 ** Math.min(reconnectAttempt, 5),
  );
  reconnectAttempt += 1;
  setStatus(t("status.reconnecting"));
  reconnectTimer = window.setTimeout(() => {
    reconnectTimer = null;
    void startConnection(true);
  }, delay);
}

function setAuthUiMode(mode) {
  authUiMode = mode;
  const needsEmail = mode !== AuthUiMode.Login;
  const binding = mode === AuthUiMode.Binding;

  authPasswordRow.classList.toggle("hidden", binding);
  authEmailRow.classList.toggle("hidden", !needsEmail);
  authCodeRow.classList.toggle("hidden", !needsEmail);
  bindingNotice.classList.toggle("hidden", !binding);
  registerModeRow.classList.toggle("hidden", binding);
  accountField.setDisabled(binding);
  registerModeInput.checked = mode === AuthUiMode.Register;

  const buttonKey = binding
    ? "ui.auth.bindEmail"
    : mode === AuthUiMode.Register
      ? "ui.auth.completeRegistration"
      : "ui.auth.connect";
  connectBtn.textContent = t(buttonKey);
}

function setAuthRequestPending(mode) {
  activeAuthMode = mode;
  const busy = mode !== null;
  connectBtn.disabled = busy;
  sendCodeBtn.disabled = busy;
}

function clearAuthRequestPending() {
  pendingAuthMode = null;
  setAuthRequestPending(null);
}

function selectedAuthMode() {
  if (authUiMode === AuthUiMode.Register) return AuthMode.Register;
  if (authUiMode === AuthUiMode.Binding) return AuthMode.ConfirmBinding;
  return AuthMode.Login;
}

function validateAuthFields(mode) {
  if (!accountField.getValue({ trim: true })) {
    setStatus(t("status.accountEmpty"));
    accountField.focus();
    return false;
  }

  const registration =
    mode === AuthMode.Register ||
    mode === AuthMode.RequestRegistrationCode;
  const binding =
    mode === AuthMode.RequestBindingCode || mode === AuthMode.ConfirmBinding;
  if (registration && !passwordField.getValue()) {
    setStatus(t("status.passwordEmpty"));
    passwordField.focus();
    return false;
  }
  if ((registration || binding) && !emailField.getValue({ trim: true })) {
    setStatus(t("status.emailEmpty"));
    emailField.focus();
    return false;
  }
  if (
    (mode === AuthMode.Register || mode === AuthMode.ConfirmBinding) &&
    !verificationCodeField.getValue({ trim: true })
  ) {
    setStatus(t("status.verificationCodeEmpty"));
    verificationCodeField.focus();
    return false;
  }
  return true;
}

function queueAuthRequest(mode) {
  if (activeAuthMode !== null || !validateAuthFields(mode)) return;

  const socketOpen =
    state.ws && state.ws.readyState === WebSocket.OPEN && state.connected;
  if (
    (mode === AuthMode.RequestBindingCode || mode === AuthMode.ConfirmBinding) &&
    !socketOpen
  ) {
    setAuthUiMode(AuthUiMode.Login);
    setStatus(t("status.bindingSessionExpired"));
    return;
  }

  pendingAuthMode = mode;
  setAuthRequestPending(mode);
  if (socketOpen) {
    pendingAuthMode = null;
    sendAuth(mode);
    return;
  }
  return startConnection(false);
}

function connectAndAuth() {
  return queueAuthRequest(selectedAuthMode());
}

function requestVerificationCode() {
  const mode =
    authUiMode === AuthUiMode.Binding
      ? AuthMode.RequestBindingCode
      : AuthMode.RequestRegistrationCode;
  return queueAuthRequest(mode);
}

async function startConnection(automatic) {
  const wsUrl = wsUrlField.getValue({ trim: true });
  if (!wsUrl) {
    setStatus(t("status.socketEmpty"));
    clearAuthRequestPending();
    return;
  }

  if (!automatic) {
    reconnectArmed = false;
    resumingConnection = false;
    reconnectAttempt = 0;
    cancelReconnect();
    saveSettings();
    closeSocket(false);
    const generation = ++connectionGeneration;
    setStatus(t("status.preparingMap"));
    await loadLoginMap();
    if (generation !== connectionGeneration || pageUnloading) return;
    return openConnection(wsUrl, false, generation);
  } else if (!reconnectArmed || pageUnloading) {
    return;
  } else {
    resumingConnection = true;
  }

  const generation = ++connectionGeneration;
  return openConnection(wsUrl, true, generation);
}

function openConnection(wsUrl, automatic, generation) {
  setStatus(
    t(automatic ? "status.reconnecting" : "status.connecting"),
  );

  let ws;
  try {
    ws = new WebSocket(wsUrl);
  } catch (error) {
    clearAuthRequestPending();
    setStatus(t(automatic ? "status.reconnectFailed" : "status.socketError"));
    if (!automatic)
      addConsoleLine(String(error || t("status.socketError")), "error");
    scheduleReconnect();
    return;
  }
  state.ws = ws;
  ws.binaryType = "arraybuffer";

  ws.addEventListener("open", () => {
    if (state.ws !== ws || generation !== connectionGeneration) {
      ws.close();
      return;
    }
    state.connected = true;
    setStatus(t(automatic ? "status.reconnected" : "status.connected"), true);
    if (!automatic) addConsoleLine(t("status.connectedBridge"));
    const mode = automatic ? AuthMode.Login : pendingAuthMode ?? selectedAuthMode();
    pendingAuthMode = null;
    if (activeAuthMode === null) setAuthRequestPending(mode);
    sendAuth(mode);
  });

  ws.addEventListener("message", (event) => {
    if (state.ws !== ws || generation !== connectionGeneration) return;
    if (typeof event.data === "string") {
      setStatus(event.data);
      addConsoleLine(event.data, "error");
      return;
    }
    receiveBytes(new Uint8Array(event.data));
  });

  ws.addEventListener("close", () => {
    if (state.ws !== ws || generation !== connectionGeneration) return;
    state.ws = null;
    state.connected = false;
    state.authenticated = false;
    clearAuthRequestPending();
    setAuthUiMode(AuthUiMode.Login);
    resetNetworkScene({ resetRunCollection: !reconnectArmed });
    updatePlayUiVisibility();
    if (reconnectArmed && !pageUnloading) {
      if (reconnectAttempt === 0)
        addConsoleLine(t("status.connectionLost"), "error");
      scheduleReconnect();
    } else {
      resumingConnection = false;
      authPanel.classList.remove("hidden");
      loadLoginMap();
      setStatus(t("status.disconnected"));
      addConsoleLine(t("status.disconnected"), "error");
    }
  });

  ws.addEventListener("error", () => {
    if (state.ws !== ws || generation !== connectionGeneration) return;
    clearAuthRequestPending();
    setStatus(t(automatic ? "status.reconnectFailed" : "status.socketError"));
    if (!automatic) addConsoleLine(t("status.socketError"), "error");
  });
}

function closeSocket(sendDisconnect = true) {
  reconnectArmed = false;
  resumingConnection = false;
  reconnectAttempt = 0;
  cancelReconnect();
  connectionGeneration += 1;
  if (state.ws && state.ws.readyState === WebSocket.OPEN && sendDisconnect) {
    sendBytes(packChores(false, false, false, true));
  }
  const ws = state.ws;
  state.ws = null;
  if (ws) ws.close();
  state.connected = false;
  state.authenticated = false;
  resetNetworkScene();
  updatePlayUiVisibility();
  loadLoginMap();
}

function sendBytes(bytes) {
  if (!bytes || !state.ws || state.ws.readyState !== WebSocket.OPEN)
    return false;
  state.ws.send(bytes);
  return true;
}

function sendNeutralGameplayState({ includeInput = false } = {}) {
  if (!state.connected || !state.authenticated) return false;
  if (includeInput) sendBytes(packInput(0, 0));
  return sendBytes(packChores(false, false, false, false, false));
}

function sendPacketBatch(...packets) {
  const parts = packets.filter((packet) => packet && packet.length > 0);
  if (parts.length === 0) return false;
  if (parts.length === 1) return sendBytes(parts[0]);

  const totalBytes = parts.reduce((sum, packet) => sum + packet.length, 0);
  const out = new Uint8Array(totalBytes);
  let offset = 0;
  for (const packet of parts) {
    out.set(packet, offset);
    offset += packet.length;
  }
  return sendBytes(out);
}

function sendAuth(mode = AuthMode.Login) {
  if (!validateAuthFields(mode)) {
    clearAuthRequestPending();
    return;
  }
  const packet = packAuth({
    mode,
    name: accountField.getValue({ trim: true }),
    password:
      mode === AuthMode.Login ||
      mode === AuthMode.Register ||
      mode === AuthMode.RequestRegistrationCode
        ? passwordField.getValue()
        : "",
    email:
      mode === AuthMode.Login ? "" : emailField.getValue({ trim: true }),
    code:
      mode === AuthMode.Register || mode === AuthMode.ConfirmBinding
        ? verificationCodeField.getValue({ trim: true })
        : "",
  });
  if (!packet) {
    setStatus(t("status.accountEmpty"));
    clearAuthRequestPending();
    return;
  }
  if (!sendBytes(packet)) {
    setStatus(t("status.socketError"));
    clearAuthRequestPending();
    return;
  }
  setStatus(t("status.authSent"), true);
  addConsoleLine(t("status.authSent"));
}

function receiveBytes(bytes) {
  state.receiveBuffer = appendBytes(state.receiveBuffer, bytes);
  let latestSnapshot = null;
  for (;;) {
    const frame = popFrame(state.receiveBuffer);
    if (!frame) break;
    state.receiveBuffer = frame.rest;
    if (frame.payload.length === 0) continue;
    const msg = parseServerMessage(frame.payload);
    if (msg?.type === ServerType.Snapshot) {
      const resolved = resolveSnapshot(msg);
      if (!resolved) {
        requestFullSnapshot();
        continue;
      }
      if (
        !latestSnapshot ||
        (resolved.snapshotId || 0) >= (latestSnapshot.snapshotId || 0)
      )
        latestSnapshot = resolved;
      continue;
    }
    if (latestSnapshot) {
      applySnapshotAndAck(latestSnapshot);
      latestSnapshot = null;
      pendingSnapshot = null;
    }
    handleServerMessage(msg);
  }
  if (latestSnapshot) {
    applySnapshotAndAck(latestSnapshot);
    pendingSnapshot = null;
  }
}

function handleServerMessage(msg) {
  if (!msg || msg.type === 0xff) return;

  if (msg.type === ServerType.Welcome) {
    const previousOwner = state.entities.get(state.ownerEntityId);
    const previousOwnerWasDead =
      !!previousOwner && (previousOwner.snapshot.flags & flagDead) !== 0;
    resetNetworkScene({
      resetRunCollection:
        !state.authenticated ||
        !state.runCollectionBaseline ||
        state.wasOwnerDead ||
        previousOwnerWasDead,
    });
    state.playerId = msg.playerId;
    state.ownerEntityId = msg.ownerEntityId;
    state.serverTickInterval =
      msg.tickRate > 0 ? 1 / msg.tickRate : serverFixedDt;
    resetOwnerPredictionTiming(state.serverTickInterval);
    if (msg.mapName) loadMap(msg.mapName);
    setStatus(t("status.player", { id: msg.playerId }), true);
    return;
  }

  if (msg.type === ServerType.AuthResult) {
    const resultCode = msg.resultCode ?? AuthResultCode.Failed;
    const authenticated =
      resultCode === AuthResultCode.Authenticated ||
      resultCode === AuthResultCode.EmailBound;
    const informational =
      resultCode === AuthResultCode.VerificationCodeSending ||
      resultCode === AuthResultCode.VerificationCodeSent;
    if (resultCode !== AuthResultCode.VerificationCodeSending)
      clearAuthRequestPending();

    state.authenticated = authenticated;
    setStatus(
      msg.message ||
        t(authenticated ? "status.authenticated" : "status.authFailed"),
      authenticated || informational,
    );
    if (authenticated) {
      passwordField.clear();
      verificationCodeField.clear();
      setAuthUiMode(AuthUiMode.Login);
      const resumed = resumingConnection;
      reconnectArmed = true;
      reconnectAttempt = 0;
      cancelReconnect();
      resumingConnection = false;
      state.ownerStateLoaded = false;
      state.inventoryLoaded = false;
      if (!resumed) {
        state.runCollectionBaseline = null;
        state.runCollectionBaselinePending = true;
        state.squadMemberNames.clear();
      }
      renderInventoryPanel();
      if (state.craftOpen) renderCraftPanel();
      sendBytes(packStateRequest());
      authPanel.classList.add("hidden");
    } else if (resultCode === AuthResultCode.EmailBindingRequired) {
      reconnectArmed = false;
      resumingConnection = false;
      passwordField.clear();
      verificationCodeField.clear();
      setAuthUiMode(AuthUiMode.Binding);
      authPanel.classList.remove("hidden");
      emailField.focus();
      loadLoginMap();
    } else if (informational) {
      reconnectArmed = false;
      resumingConnection = false;
      authPanel.classList.remove("hidden");
      if (resultCode === AuthResultCode.VerificationCodeSent)
        verificationCodeField.focus();
    } else {
      reconnectArmed = false;
      resumingConnection = false;
      authPanel.classList.remove("hidden");
      loadLoginMap();
    }
    updatePlayUiVisibility();
    addConsoleLine(
      msg.message ||
        t(authenticated ? "status.authenticated" : "status.authFailed"),
      resultCode === AuthResultCode.Failed ? "error" : "",
    );
    return;
  }

  if (msg.type === ServerType.Snapshot) {
    const resolved = resolveSnapshot(msg);
    if (resolved) applySnapshotAndAck(resolved);
    else requestFullSnapshot();
    pendingSnapshot = null;
    return;
  }

  if (msg.type === ServerType.OwnerState) {
    const previousMovementMultiplier = state.ownerMovementMultiplier || 1;
    state.ownerLevel = Math.max(1, msg.level || 1);
    state.ownerExpProgress = clamp(msg.expProgress || 0, 0, 1);
    state.talentPoints = Math.max(0, msg.talentPoints || 0);
    state.talents = msg.talents || [];
    const movementMultiplier = ownerMovementTalentMultiplier(state.talents);
    state.ownerMovementMultiplier = movementMultiplier;
    if (movementMultiplier !== previousMovementMultiplier) {
      const owner = state.entities.get(state.ownerEntityId);
      if (owner?.snapshot) initOwnerPrediction(owner, owner.snapshot, true);
    }
    updateQuickActionButtons();
    const primaryCount = Math.max(
      1,
      (msg.ownerSlots || []).length || state.ownerSlots.length || 5,
    );
    state.ownerSlots = normalizeSlots(msg.ownerSlots, primaryCount);
    state.secondarySlots = normalizeSlots(msg.secondarySlots, primaryCount);
    state.selectedSlot = clamp(
      state.selectedSlot,
      0,
      state.ownerSlots.length - 1,
    );
    state.ownerStateLoaded = true;
    pruneSlotTransactions();
    maybeCaptureRunCollectionBaseline();
    renderInventoryPanel();
    if (state.craftOpen) renderCraftPanel();
    if (state.talentOpen) renderTalentPanel();
    return;
  }

  if (msg.type === ServerType.Inventory) {
    state.inventory = msg.inventory || [];
    state.inventoryLoaded = true;
    pruneSlotTransactions();
    maybeCaptureRunCollectionBaseline();
    renderInventoryPanel();
    if (state.craftOpen) renderCraftPanel();
    return;
  }

  if (msg.type === ServerType.CraftResult) {
    handleCraftResult(msg);
    return;
  }

  if (msg.type === ServerType.Chat) {
    const chat = consumeSquadMetadata(msg.chat);
    if (chat.message) {
      mysteryAudio.handleChat(chat);
      chatUi.append(chat);
    }
    addConsoleLine(
      `${msg.chat.playerName || msg.chat.playerId}: ${msg.chat.message || ""}`,
    );
  }
}

function resetNetworkScene({ resetRunCollection = true } = {}) {
  pendingSnapshot = null;
  snapshotHistory.clear();
  snapshotResetRequested = false;
  mysteryAudio.reset();
  clearSlotTransactions();
  state.receiveBuffer = new Uint8Array();
  state.entities.clear();
  state.snapshotId = -1;
  state.serverTick = -1;
  state.serverTickReceivedAt = 0;
  state.serverTickInterval = serverFixedDt;
  state.inputSequence = 0;
  state.lastInput = {
    x: 99,
    y: 99,
    attacking: false,
    defending: false,
    digging: false,
  };
  state.sendTimer = 1;
  resetOwnerPredictionTiming(state.serverTickInterval);
  state.ownerStateLoaded = false;
  state.ownerSlotRuntime = [];
  state.inventoryLoaded = false;
  state.localMoveInput = { x: 0, y: 0 };
  state.ownerMovementMultiplier = 1;
  state.predictionDebug = {
    errorDistance: 0,
    speedScale: 1,
    tickScale: 1,
    snapshotInterval: state.ownerPredictionTiming.expectedSnapshotInterval,
    staleTime: 0,
    snapped: false,
  };
  state.screenShake = { x: 0, y: 0 };
  state.particles = [];
  state.forgeMode = false;
  craftPanel.classList.remove("forge-mode");
  state.deathOverlayClosed = false;
  state.wasOwnerDead = false;
  state.squadMemberNames.clear();
  if (resetRunCollection) {
    state.runCollectionBaseline = null;
    state.runCollectionBaselinePending = true;
  }
}

function queueSnapshot(msg) {
  if (!msg || msg.type !== ServerType.Snapshot) return;
  const resolved = resolveSnapshot(msg);
  if (!resolved) {
    requestFullSnapshot();
    return;
  }
  if ((resolved.snapshotId || 0) <= (state.snapshotId || 0)) return;
  if (
    !pendingSnapshot ||
    (resolved.snapshotId || 0) >= (pendingSnapshot.snapshotId || 0)
  )
    pendingSnapshot = resolved;
}

function flushPendingSnapshot() {
  if (!pendingSnapshot) return;
  const msg = pendingSnapshot;
  pendingSnapshot = null;
  applySnapshotAndAck(msg);
}

function resolveSnapshot(msg) {
  if (!msg || msg.type !== ServerType.Snapshot) return null;

  let entities;
  if (msg.baseSnapshotId === FULL_SNAPSHOT_BASE_ID) {
    entities = new Map();
    snapshotResetRequested = false;
  } else {
    const base = snapshotHistory.get(msg.baseSnapshotId);
    if (!base) return null;
    entities = new Map(base);
    for (const entityId of msg.removedEntityIds || []) entities.delete(entityId);
  }

  for (const snap of msg.entities || []) entities.set(snap.entityId, snap);
  snapshotHistory.set(msg.snapshotId, entities);
  while (snapshotHistory.size > snapshotHistoryLimit) {
    const oldest = snapshotHistory.keys().next().value;
    snapshotHistory.delete(oldest);
  }

  return { ...msg, entities: Array.from(entities.values()) };
}

function requestFullSnapshot() {
  if (snapshotResetRequested) return;
  if (sendBytes(packSnapshotAck(FULL_SNAPSHOT_BASE_ID))) snapshotResetRequested = true;
}

function applySnapshotAndAck(msg) {
  if (!applySnapshot(msg)) return false;
  sendBytes(packSnapshotAck(msg.snapshotId));
  return true;
}

function normalizeSlots(slots, size) {
  const out = Array.from({ length: Math.max(0, size) }, () => emptySlot());
  (slots || []).forEach((slot, index) => {
    if (index < out.length)
      out[index] = { petalType: slot.petalType || 0, rarity: slot.rarity || 0 };
  });
  return out;
}

function sameSlot(a, b) {
  return (
    (a?.petalType || 0) === (b?.petalType || 0) &&
    (a?.rarity || 0) === (b?.rarity || 0)
  );
}

function copySlots(slots, size = slots?.length || 0) {
  return normalizeSlots(slots, size);
}

function inventoryKey(petalType, rarity) {
  return `${petalType || 0}:${rarity || 0}`;
}

function inventoryCountForKey(items, key) {
  let total = 0;
  for (const item of items || []) {
    if (!slotHasItem(item) || (item.count || 0) <= 0) continue;
    if (inventoryKey(item.petalType, item.rarity) === key)
      total += item.count || 0;
  }
  return total;
}

function copyInventoryItems(items) {
  const merged = new Map();
  for (const item of items || []) {
    if (!slotHasItem(item) || (item.count || 0) <= 0) continue;
    const key = inventoryKey(item.petalType, item.rarity);
    const existing = merged.get(key) || {
      petalType: item.petalType,
      rarity: item.rarity,
      count: 0,
    };
    existing.count = clamp(
      existing.count + Math.max(0, item.count || 0),
      0,
      1000000000,
    );
    merged.set(key, existing);
  }
  return Array.from(merged.values()).filter((item) => item.count > 0);
}

function currentRunCollectionItems() {
  const display = makeDisplaySlotState();
  const counts = new Map();
  const add = (item, count) => {
    if (!slotHasItem(item) || count <= 0) return;
    const key = inventoryKey(item.petalType, item.rarity);
    const existing = counts.get(key) || {
      petalType: item.petalType,
      rarity: item.rarity,
      count: 0,
    };
    existing.count += count;
    counts.set(key, existing);
  };

  for (const item of display.inventory || []) add(item, item.count || 0);
  for (const slot of display.ownerSlots || []) add(slot, 1);
  for (const slot of display.secondarySlots || []) add(slot, 1);
  return Array.from(counts.values()).filter((item) => item.count > 0);
}

function startRunCollectionBaseline() {
  state.runCollectionBaseline = null;
  state.runCollectionBaselinePending = true;
  maybeCaptureRunCollectionBaseline();
}

function maybeCaptureRunCollectionBaseline() {
  if (!state.runCollectionBaselinePending) return;
  if (!state.ownerStateLoaded || !state.inventoryLoaded || !hasLivingOwner())
    return;
  state.runCollectionBaseline = copyInventoryItems(currentRunCollectionItems());
  state.runCollectionBaselinePending = false;
}

function collectedThisRunItems() {
  const current = currentRunCollectionItems();
  const baseline = state.runCollectionBaseline || current;
  const baselineCounts = new Map(
    baseline.map((item) => [inventoryKey(item.petalType, item.rarity), item.count || 0]),
  );

  return current
    .map((item) => ({
      ...item,
      count: Math.max(
        0,
        (item.count || 0) -
          (baselineCounts.get(inventoryKey(item.petalType, item.rarity)) || 0),
      ),
    }))
    .filter((item) => item.count > 0);
}

function consumeSquadMetadata(chat) {
  if (!chat || chat.flag !== ChatFlag.Server) return chat;

  const message = String(chat.message || "");
  const marker = "<unseen>(";
  const markerStart = message.indexOf(marker);
  if (markerStart < 0 || !message.endsWith(")")) return chat;

  const payload = message.slice(markerStart + marker.length, -1);
  const squadNames = payload
    ? payload
        .split("\x1f")
        .map((name) => name.trim())
        .filter(Boolean)
    : [];
  state.squadMemberNames = new Set(squadNames);

  const visibleMessage = message.slice(0, markerStart);

  return {
    ...chat,
    message: visibleMessage.replace(/\s{2,}/g, " ").trim(),
  };
}

function touchedInventoryKeysFrom(...items) {
  const keys = new Set();
  for (const item of items) {
    if (slotHasItem(item)) keys.add(inventoryKey(item.petalType, item.rarity));
  }
  return Array.from(keys);
}

function inventoryTouchedEqual(current, target, keys = []) {
  if (!keys.length) return inventoryArraysEqual(current, target);
  return keys.every(
    (key) =>
      inventoryCountForKey(current, key) === inventoryCountForKey(target, key),
  );
}

function mergeTouchedInventory(current, target, keys = []) {
  if (!keys.length) return copyInventoryItems(target);
  const out = copyInventoryItems(current);
  for (const key of keys) {
    const [petalText, rarityText] = String(key).split(":");
    const petalType = Number(petalText) || 0;
    const rarity = Number(rarityText) || 0;
    if (!petalType || !rarity) continue;
    const targetCount = inventoryCountForKey(target, key);
    const existing = out.find(
      (item) => item.petalType === petalType && item.rarity === rarity,
    );
    if (existing) existing.count = targetCount;
    else if (targetCount > 0)
      out.push({ petalType, rarity, count: targetCount });
  }
  return out.filter((item) => item.count > 0);
}

function applyInventoryDelta(items, petalType, rarity, delta) {
  if (!petalType || !rarity || delta === 0) return items;
  const out = copyInventoryItems(items);
  const existing = out.find(
    (item) => item.petalType === petalType && item.rarity === rarity,
  );
  if (existing) {
    existing.count = clamp(
      Math.max(0, (existing.count || 0) + delta),
      0,
      1000000000,
    );
    return out.filter((item) => item.count > 0);
  }
  if (delta > 0)
    out.push({ petalType, rarity, count: clamp(delta, 0, 1000000000) });
  return out;
}

function slotArraysEqual(a, b) {
  if ((a?.length || 0) !== (b?.length || 0)) return false;
  for (let i = 0; i < (a?.length || 0); i += 1) {
    if (!sameSlot(a[i], b[i])) return false;
  }
  return true;
}

function inventoryArraysEqual(a, b) {
  const counts = new Map();
  for (const item of a || []) {
    if (!slotHasItem(item) || (item.count || 0) <= 0) continue;
    const key = inventoryKey(item.petalType, item.rarity);
    counts.set(key, (counts.get(key) || 0) + (item.count || 0));
  }
  for (const item of b || []) {
    if (!slotHasItem(item) || (item.count || 0) <= 0) continue;
    const key = inventoryKey(item.petalType, item.rarity);
    counts.set(key, (counts.get(key) || 0) - (item.count || 0));
  }
  return Array.from(counts.values()).every((count) => count === 0);
}

function pruneSlotTransactions(now = performance.now()) {
  const ownerCount = Math.max(1, state.ownerSlots.length || 5);
  const currentOwner = copySlots(state.ownerSlots, ownerCount);
  const currentSecondary = copySlots(state.secondarySlots, ownerCount);
  const currentInventory = copyInventoryItems(state.inventory);
  state.slotTransactions = (state.slotTransactions || []).filter((tx) => {
    if (!tx) return false;
    if (now - (tx.createdAt || 0) > slotTransactionTimeoutMs) return false;
    const targetOwner = copySlots(tx.ownerSlots || [], ownerCount);
    const targetSecondary = copySlots(tx.secondarySlots || [], ownerCount);
    const targetInventory = copyInventoryItems(tx.inventory || []);
    const ownerConfirmed =
      slotArraysEqual(currentOwner, targetOwner) &&
      slotArraysEqual(currentSecondary, targetSecondary);
    const inventoryConfirmed = inventoryTouchedEqual(
      currentInventory,
      targetInventory,
      tx.inventoryTouchedKeys || [],
    );
    if (ownerConfirmed && inventoryConfirmed) return false;

    return true;
  });
}

function makeDisplaySlotState() {
  pruneSlotTransactions();
  const ownerCount = Math.max(1, state.ownerSlots.length || 5);
  if (!state.slotTransactions.length) {
    return {
      ownerSlots: copySlots(state.ownerSlots, ownerCount),
      secondarySlots: copySlots(state.secondarySlots, ownerCount),
      inventory: copyInventoryItems(state.inventory),
    };
  }
  const latest = state.slotTransactions[state.slotTransactions.length - 1];
  return {
    ownerSlots: copySlots(latest.ownerSlots, ownerCount),
    secondarySlots: copySlots(latest.secondarySlots, ownerCount),
    inventory: mergeTouchedInventory(
      state.inventory,
      latest.inventory,
      latest.inventoryTouchedKeys || [],
    ),
  };
}

function displayOwnerSlots() {
  return makeDisplaySlotState().ownerSlots;
}

function displaySecondarySlots() {
  return makeDisplaySlotState().secondarySlots;
}

function highestSlotRarity(slots) {
  let best = 0;
  for (const slot of slots || []) {
    if (!slotHasItem(slot)) continue;
    best = Math.max(best, slot.rarity || 0);
  }
  return best;
}

function highestOwnerSlotRarity() {
  return Math.max(
    highestSlotRarity(state.ownerSlots),
    highestSlotRarity(state.secondarySlots),
  );
}

function playerLevelRarity(snap) {
  const cachedRarity = snap?.entityId
    ? state.entities.get(snap.entityId)?.playerPrimarySlotRarity || 0
    : 0;
  const ownerRarity =
    snap?.entityId === state.ownerEntityId ? highestOwnerSlotRarity() : 0;
  return Math.max(
    1,
    highestSlotRarity(snap?.primarySlots),
    cachedRarity,
    ownerRarity,
  );
}

function displayInventory() {
  return makeDisplaySlotState().inventory;
}

function enqueueSlotTransaction(
  ownerSlots,
  secondarySlots,
  inventory,
  touchedItems = [],
) {
  const tx = {
    id: ++state.slotTxSeq,
    createdAt: performance.now(),
    ownerSlots: copySlots(
      ownerSlots,
      Math.max(1, ownerSlots?.length || state.ownerSlots.length || 5),
    ),
    secondarySlots: copySlots(
      secondarySlots,
      Math.max(1, secondarySlots?.length || state.ownerSlots.length || 5),
    ),
    inventory: copyInventoryItems(inventory),
    inventoryTouchedKeys: touchedInventoryKeysFrom(...touchedItems),
  };
  state.slotTransactions = [tx];
  window.setTimeout(() => {
    if (!state.slotTransactions.some((entry) => entry?.id === tx.id)) return;
    pruneSlotTransactions();
    renderInventoryPanel();
    if (state.craftOpen) renderCraftPanel();
  }, slotTransactionTimeoutMs + 25);
  return tx;
}

function clearSlotTransactions() {
  state.slotTransactions = [];
}

async function loadMap(mapName) {
  return mapRenderer.loadMap(mapName);
}

function loadLoginMap() {
  if (state.authenticated) return Promise.resolve(null);
  applyLoginMapView();
  return loadMap(state.loginMapName || defaultMapName);
}

function applyLoginMapView() {
  if (state.authenticated) return;
  state.camera.x = state.loginMapX;
  state.camera.y = state.loginMapY;
  setViewRadiusImmediate(state.loginMapHorizon);
  minimapCache = null;
}

function setViewRadiusImmediate(viewRadius) {
  const value = Number(viewRadius);
  if (!Number.isFinite(value) || value <= 0) return;
  state.viewRadius = value;
  state.targetViewRadius = value;
  state.viewRadiusTransition = null;
}

function setTargetViewRadius(viewRadius, now = performance.now()) {
  const target = Number(viewRadius);
  if (!Number.isFinite(target) || target <= 0) return;
  if (!Number.isFinite(state.viewRadius) || state.viewRadius <= 0) {
    setViewRadiusImmediate(target);
    return;
  }
  if (
    Math.abs(target - state.targetViewRadius) <=
    Math.max(0.001, target * 0.000001)
  )
    return;

  state.targetViewRadius = target;
  state.viewRadiusTransition = {
    from: state.viewRadius,
    target,
    startedAt: now,
  };
}

function updateViewRadius(now) {
  const transition = state.viewRadiusTransition;
  if (!transition) return;
  const progress = clamp(
    (now - transition.startedAt) / viewRadiusTransitionDurationMs,
    0,
    1,
  );
  const eased = progress * progress * (3 - 2 * progress);
  state.viewRadius =
    transition.from + (transition.target - transition.from) * eased;
  if (progress >= 1) {
    state.viewRadius = transition.target;
    state.viewRadiusTransition = null;
  }
}

function applySnapshot(msg) {
  if ((msg.snapshotId || 0) <= (state.snapshotId || 0)) return false;
  const snapshotNowMs = performance.now();
  const snapshotNow = snapshotNowMs / 1000;
  updateOwnerPredictionTiming(msg.snapshotId, snapshotNow);
  state.snapshotId = msg.snapshotId;
  if (Number.isSafeInteger(msg.serverTick)) {
    state.serverTick = msg.serverTick;
    state.serverTickReceivedAt = snapshotNowMs;
  }
  const previousOwnerEntityId = state.ownerEntityId;
  const nextOwnerEntityId = msg.ownerEntityId || previousOwnerEntityId;
  const previousOwner = state.entities.get(previousOwnerEntityId);
  const previousOwnerWasDead =
    !!previousOwner && (previousOwner.snapshot.flags & flagDead) !== 0;
  if (
    nextOwnerEntityId !== previousOwnerEntityId &&
    (previousOwnerWasDead || state.wasOwnerDead || !state.runCollectionBaseline)
  )
    startRunCollectionBaseline();
  state.ownerEntityId = nextOwnerEntityId;
  const nextViewRadius = msg.viewRadius || state.viewRadius;
  if (nextViewRadius > 0) setTargetViewRadius(nextViewRadius, snapshotNowMs);

  const live = new Set();
  for (const snap of msg.entities || []) {
    live.add(snap.entityId);
    const existing = state.entities.get(snap.entityId);
    if (existing) {
      const previousSnapshot = existing.snapshot;
      const wasDead = (existing.snapshot.flags & flagDead) !== 0;
      const isDead = (snap.flags & flagDead) !== 0;
      const previousHp = Number.isFinite(existing.snapshot.hpPercent)
        ? existing.snapshot.hpPercent
        : snap.hpPercent;
      existing.snapshotMove = Math.hypot(
        snap.pos.x - existing.snapshot.pos.x,
        snap.pos.y - existing.snapshot.pos.y,
      );
      existing.lastSnapshotAt = existing.snapshotAt || snapshotNow;
      existing.snapshotAt = snapshotNow;
      updateEntitySnapshotVelocity(existing, snap, previousSnapshot);
      existing.snapshot = snap;
      if (!Number.isFinite(existing.particleBurstNextAt))
        existing.particleBurstNextAt = null;
      if (snap.entityType === bloodSacrificeEffectType) {
        if (!Number.isFinite(existing.bloodSacrificeAngle))
          existing.bloodSacrificeAngle = Math.random() * Math.PI * 2;
        if (!Array.isArray(existing.bloodSacrificeSweeps))
          existing.bloodSacrificeSweeps = [];
      }
      const slotRarity = highestSlotRarity(snap.primarySlots);
      if (snap.entityType === playerFlowerType && slotRarity > 0)
        existing.playerPrimarySlotRarity = slotRarity;
      if (snap.entityId === state.ownerEntityId)
        handleOwnerSnapshot(existing, snap, snapshotNow, wasDead, isDead);
      if (
        !existing.renderPos ||
        !Number.isFinite(existing.renderPos.x) ||
        !Number.isFinite(existing.renderPos.y)
      )
        syncEntityRenderToSnapshot(existing, snap);
      existing.dying = false;
      existing.deathAge = 0;
      if (!isDead && snap.hpPercent < previousHp - hurtFlashMinDelta)
        existing.hurtFlashAge = 0;
      if (isDead && !wasDead) existing.deathAngle = Math.random() * Math.PI * 2;
      if (!isDead) existing.deathAngle = null;
    } else {
      const isDead = (snap.flags & flagDead) !== 0;
      state.entities.set(snap.entityId, {
        snapshot: snap,
        renderPos: { x: snap.pos.x, y: snap.pos.y },
        renderAngle: snap.angle || 0,
        snapshotMove: 0,
        snapshotVelocity: { x: 0, y: 0 },
        snapshotAt: snapshotNow,
        lastSnapshotAt: snapshotNow,
        playerPrimarySlotRarity:
          snap.entityType === playerFlowerType
            ? highestSlotRarity(snap.primarySlots)
            : 0,
        motionBlend: 0,
        dying: false,
        deathAge: 0,
        hurtFlashAge: hurtFlashDuration,
        deathAngle: isDead ? Math.random() * Math.PI * 2 : null,
        particleBurstNextAt: null,
        bloodSacrificeAngle:
          snap.entityType === bloodSacrificeEffectType
            ? Math.random() * Math.PI * 2
            : null,
        bloodSacrificeSweepNextAt: null,
        bloodSacrificeSweeps: [],
      });
      const created = state.entities.get(snap.entityId);
      if (snap.entityId === state.ownerEntityId)
        initOwnerPrediction(created, snap, true);
    }
  }
  const owner = state.entities.get(state.ownerEntityId);
  state.digging = entityHasState(owner?.snapshot, stateDigging);
  syncOwnerSlotsFromSnapshot(owner?.snapshot);
  maybeCaptureRunCollectionBaseline();
  if (owner?.renderPos && !owner.dying) {
    state.camera.x = owner.renderPos.x;
    state.camera.y = owner.renderPos.y;
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
    entity.hurtFlashAge = hurtFlashDuration;
    if (entity.deathAngle == null)
      entity.deathAngle = Math.random() * Math.PI * 2;
  }
  updateForgeModeFromSnapshot();
  return true;
}

function ownerIsNearTitan() {
  if (!state.authenticated) return false;
  const owner = state.entities.get(state.ownerEntityId);
  const ownerPos = owner?.snapshot?.pos;
  if (!ownerPos || owner.dying) return false;

  for (const entity of state.entities.values()) {
    const snap = entity?.snapshot;
    if (
      !snap ||
      entity.dying ||
      snap.entityType !== titanType ||
      (snap.flags & flagDead) !== 0
    )
      continue;
    const reach = Math.max(0, snap.radius || 0) + titanForgeRange;
    if (Math.hypot(snap.pos.x - ownerPos.x, snap.pos.y - ownerPos.y) <= reach)
      return true;
  }
  return false;
}

function updateForgeModeFromSnapshot() {
  if (state.craftPhase === "spinning") return;
  const forgeMode = ownerIsNearTitan();
  if (forgeMode === state.forgeMode) return;

  clearCraftDisplay();
  state.forgeMode = forgeMode;
  updateQuickActionButtons();
  if (state.craftOpen) renderCraftPanel();
}

function syncOwnerSlotsFromSnapshot(ownerSnap) {
  syncOwnerSlotRuntimeFromSnapshot(ownerSnap);
  if (
    state.ownerStateLoaded ||
    !ownerSnap ||
    !Array.isArray(ownerSnap.primarySlots) ||
    ownerSnap.primarySlots.length <= 0
  )
    return;

  const primaryCount = Math.max(1, ownerSnap.primarySlots.length);
  state.ownerSlots = normalizeSlots(ownerSnap.primarySlots, primaryCount);
  state.secondarySlots = normalizeSlots(state.secondarySlots, primaryCount);
  state.selectedSlot = clamp(
    state.selectedSlot,
    0,
    state.ownerSlots.length - 1,
  );
  renderInventoryPanel();
  renderCraftPanel();
}

function syncOwnerSlotRuntimeFromSnapshot(ownerSnap) {
  if (!ownerSnap || !Array.isArray(ownerSnap.primarySlots)) return;

  state.ownerSlotRuntime = ownerSnap.primarySlots.map((slot) => ({
    petalType: slot?.petalType || 0,
    rarity: slot?.rarity || 0,
    copies: (slot?.copies || []).map((copy) => ({
      loading: copy?.state === PetalSlotCopyState.Loading,
      progress: clamp(copy?.progress || 0, 0, 1),
    })),
  }));
  updateOwnerSlotRuntimeOverlays();
}

function syncEntityRenderToSnapshot(entity, snap) {
  if (!entity || !snap?.pos) return;
  if (!entity.renderPos) entity.renderPos = { x: snap.pos.x, y: snap.pos.y };
  else {
    entity.renderPos.x = snap.pos.x;
    entity.renderPos.y = snap.pos.y;
  }
  if (Number.isFinite(snap.angle)) entity.renderAngle = snap.angle;
}

function updateEntitySnapshotVelocity(entity, snap, previousSnapshot) {
  if (!entity || !snap?.pos || !previousSnapshot?.pos) return;
  const dt = Math.max(
    ownerPredictionSpeedSampleMinDt,
    (entity.snapshotAt || 0) - (entity.lastSnapshotAt || 0),
  );
  const sample = {
    x: (snap.pos.x - previousSnapshot.pos.x) / dt,
    y: (snap.pos.y - previousSnapshot.pos.y) / dt,
  };
  const previous = entity.snapshotVelocity || { x: sample.x, y: sample.y };
  entity.snapshotVelocity = {
    x: previous.x + (sample.x - previous.x) * entitySnapshotVelocityBlend,
    y: previous.y + (sample.y - previous.y) * entitySnapshotVelocityBlend,
  };
}

function ownerMovementTalentMultiplier(talents) {
  let multiplier = 1;
  for (const talent of talents || []) {
    if (talent?.id !== TalentId.Movement) continue;
    multiplier = Math.max(
      multiplier,
      ownerMovementTalentMultiplierByRarity[talent.rarity] || 1,
    );
  }
  return multiplier;
}

function resetOwnerPredictionTiming(tickInterval = serverFixedDt) {
  const safeTickInterval = clamp(tickInterval || serverFixedDt, 0.001, 0.25);
  const ticksPerSnapshot = Math.max(
    1,
    Math.ceil(packetInterval / safeTickInterval - 0.000001),
  );
  const expectedSnapshotInterval = ticksPerSnapshot * safeTickInterval;
  state.ownerPredictionTiming = {
    expectedSnapshotInterval,
    snapshotInterval: expectedSnapshotInterval,
    simulationScale: 1,
    samples: [],
    lastSnapshotAt: 0,
    lastSnapshotId: -1,
  };
}

function updateOwnerPredictionTiming(snapshotId, snapshotNow) {
  const timing = state.ownerPredictionTiming;
  if (!timing || !Number.isFinite(snapshotNow)) return;

  const idDelta = snapshotId - timing.lastSnapshotId;
  const elapsed = snapshotNow - timing.lastSnapshotAt;
  if (
    timing.lastSnapshotId >= 0 &&
    idDelta > 0 &&
    idDelta <= 120 &&
    elapsed > 0
  ) {
    const sample = elapsed / idDelta;
    const expected = timing.expectedSnapshotInterval;
    if (sample >= expected * 0.5 && sample <= Math.max(2, expected * 12)) {
      timing.samples.push(sample);
      if (timing.samples.length > ownerPredictionTimingSampleCount)
        timing.samples.shift();

      const sorted = [...timing.samples].sort((a, b) => a - b);
      const middle = Math.floor(sorted.length / 2);
      timing.snapshotInterval =
        sorted.length % 2 === 0
          ? (sorted[middle - 1] + sorted[middle]) * 0.5
          : sorted[middle];

      if (timing.samples.length >= 3) {
        const targetScale = clamp(
          expected / timing.snapshotInterval,
          ownerPredictionMinTickScale,
          1,
        );
        const blend = targetScale < timing.simulationScale ? 0.32 : 0.08;
        timing.simulationScale +=
          (targetScale - timing.simulationScale) * blend;
      }
    }
  }

  timing.lastSnapshotAt = snapshotNow;
  timing.lastSnapshotId = snapshotId;
}

function initOwnerPrediction(
  entity,
  snap = entity?.snapshot,
  forceSnap = false,
) {
  if (!entity || !snap?.pos) return null;
  const prediction = entity.ownerPrediction || {};
  const shouldSnap =
    forceSnap ||
    !prediction.pos ||
    !Number.isFinite(prediction.pos.x) ||
    !Number.isFinite(prediction.pos.y);

  if (shouldSnap) {
    prediction.pos = { x: snap.pos.x, y: snap.pos.y };
    prediction.vel = { x: 0, y: 0 };
    prediction.correction = { x: 0, y: 0 };
    prediction.lastServerPos = { x: snap.pos.x, y: snap.pos.y };
    prediction.lastSnapshotAt = entity.snapshotAt || performance.now() / 1000;
    prediction.staleTime = 0;
    prediction.snapped = true;
    entity.renderPos = { x: snap.pos.x, y: snap.pos.y };
    entity.serverRenderPos = { x: snap.pos.x, y: snap.pos.y };
  } else {
    if (!prediction.vel) prediction.vel = { x: 0, y: 0 };
    if (!prediction.correction) prediction.correction = { x: 0, y: 0 };
    if (!prediction.lastServerPos)
      prediction.lastServerPos = { x: snap.pos.x, y: snap.pos.y };
  }

  entity.ownerPrediction = prediction;
  if (!Number.isFinite(entity.renderAngle) && Number.isFinite(snap.angle))
    entity.renderAngle = snap.angle;
  return prediction;
}

function handleOwnerSnapshot(entity, snap, snapshotNow, wasDead, isDead) {
  if (wasDead && !isDead) startRunCollectionBaseline();
  const prediction = initOwnerPrediction(entity, snap, false);
  if (!prediction || !snap?.pos) return;

  const dx = snap.pos.x - prediction.pos.x;
  const dy = snap.pos.y - prediction.pos.y;
  const errorDistance = Math.hypot(dx, dy);
  const largeStateChange = wasDead !== isDead || (snap.flags & flagDead) !== 0;
  const shouldSnap =
    largeStateChange ||
    errorDistance >= ownerPredictionTeleportDistance ||
    !Number.isFinite(errorDistance);

  if (shouldSnap) {
    initOwnerPrediction(entity, snap, true);
    state.predictionDebug = {
      errorDistance: Number.isFinite(errorDistance) ? errorDistance : 0,
      speedScale: state.ownerMovementMultiplier || 1,
      tickScale: state.ownerPredictionTiming?.simulationScale || 1,
      snapshotInterval: state.ownerPredictionTiming?.snapshotInterval || 0,
      staleTime: 0,
      snapped: true,
    };
    return;
  }

  prediction.correction =
    errorDistance <= ownerPredictionCorrectionDeadzone
      ? { x: 0, y: 0 }
      : { x: dx, y: dy };
  prediction.lastServerPos = { x: snap.pos.x, y: snap.pos.y };
  prediction.lastSnapshotAt = snapshotNow;
  prediction.staleTime = 0;
  prediction.snapped = false;
  state.predictionDebug = {
    errorDistance,
    speedScale: state.ownerMovementMultiplier || 1,
    tickScale: state.ownerPredictionTiming?.simulationScale || 1,
    snapshotInterval: state.ownerPredictionTiming?.snapshotInterval || 0,
    staleTime: 0,
    snapped: false,
  };
}

function stepOwnerPrediction(entity, dt) {
  const snap = entity?.snapshot;
  if (!snap?.pos || dt <= 0) return;
  const prediction = initOwnerPrediction(entity, snap, false);
  if (!prediction) return;

  const staleNow = Math.max(
    0,
    performance.now() / 1000 -
      (prediction.lastSnapshotAt || entity.snapshotAt || 0),
  );
  prediction.staleTime = staleNow;
  const timing = state.ownerPredictionTiming;
  const snapshotInterval = Math.max(
    timing?.expectedSnapshotInterval || 0,
    timing?.snapshotInterval || 0,
  );
  const staleGrace = Math.max(
    ownerPredictionSnapshotStaleMin,
    snapshotInterval * 2.5,
  );
  const staleFadeDuration = Math.max(0.08, snapshotInterval);
  const staleMultiplier =
    staleNow <= staleGrace
      ? 1
      : clamp(1 - (staleNow - staleGrace) / staleFadeDuration, 0, 1);
  const simulationScale = timing?.simulationScale || 1;
  const simulationDt = dt * simulationScale;
  const diggingMultiplier =
    entityHasState(snap, stateDigging) ? ownerDiggingSpeedMultiplier : 1;
  const speedScale = state.ownerMovementMultiplier || 1;
  const maxVelocity =
    ownerBaseMaxVelocity * diggingMultiplier * speedScale * staleMultiplier;
  const acceleration =
    ownerBaseAcceleration * diggingMultiplier * speedScale * staleMultiplier;

  let vel = prediction.vel || { x: 0, y: 0 };
  const speed = Math.hypot(vel.x, vel.y);
  if (speed > maxVelocity && ownerSlowToMaxVelocityTime > 0) {
    const slowAmount =
      ((speed - maxVelocity) * simulationDt) / ownerSlowToMaxVelocityTime;
    const nextSpeed = Math.max(maxVelocity, speed - slowAmount);
    const scale = speed > 0 ? nextSpeed / speed : 0;
    vel = { x: vel.x * scale, y: vel.y * scale };
  }

  const move = state.localMoveInput || { x: 0, y: 0 };
  const moveLength = Math.hypot(move.x, move.y);
  if (moveLength > 0.0001 && maxVelocity > 0 && acceleration > 0) {
    const desired = {
      x: (move.x / moveLength) * maxVelocity,
      y: (move.y / moveLength) * maxVelocity,
    };
    const diff = { x: desired.x - vel.x, y: desired.y - vel.y };
    const diffLength = Math.hypot(diff.x, diff.y);
    const maxAccel = acceleration * simulationDt;
    if (diffLength <= maxAccel || diffLength <= 0.000001) {
      vel = desired;
    } else {
      vel = {
        x: vel.x + (diff.x / diffLength) * maxAccel,
        y: vel.y + (diff.y / diffLength) * maxAccel,
      };
    }
  } else {
    const tickInterval = state.serverTickInterval || serverFixedDt;
    const damping = Math.pow(
      ownerStopDampingPerTick,
      simulationDt / tickInterval,
    );
    vel = { x: vel.x * damping, y: vel.y * damping };
    if (vel.x * vel.x + vel.y * vel.y <= ownerStopVelocityEpsilon)
      vel = { x: 0, y: 0 };
  }

  prediction.vel = vel;
  const radius = Math.max(0, Number(snap.radius) || 0);
  const predictedStart = { x: prediction.pos.x, y: prediction.pos.y };
  const predictedEnd = {
    x: predictedStart.x + vel.x * simulationDt,
    y: predictedStart.y + vel.y * simulationDt,
  };
  const predictedMotion = resolveOwnerPredictedMotion(
    predictedStart,
    predictedEnd,
    radius,
  );
  prediction.pos = predictedMotion.pos;
  if (predictedMotion.normals?.length) {
    for (const normal of predictedMotion.normals) {
      const intoWall = vel.x * normal.x + vel.y * normal.y;
      if (intoWall < 0) {
        vel = {
          x: vel.x - normal.x * intoWall,
          y: vel.y - normal.y * intoWall,
        };
      }
    }
    prediction.vel = vel;
  }

  const correctionDistance = Math.hypot(
    prediction.correction?.x || 0,
    prediction.correction?.y || 0,
  );
  if (correctionDistance > 0) {
    const correctionRate =
      correctionDistance >= ownerPredictionHardCorrectionDistance
        ? ownerPredictionHardCorrectionRate
        : ownerPredictionSoftCorrectionRate;
    const amount = smoothFactor(correctionRate, dt);
    const correctionStart = { x: prediction.pos.x, y: prediction.pos.y };
    const correctionEnd = {
      x: correctionStart.x + prediction.correction.x * amount,
      y: correctionStart.y + prediction.correction.y * amount,
    };
    prediction.pos = resolveOwnerPredictedMotion(
      correctionStart,
      correctionEnd,
      radius,
    ).pos;
    prediction.correction.x *= 1 - amount;
    prediction.correction.y *= 1 - amount;
    if (Math.hypot(prediction.correction.x, prediction.correction.y) < 0.01)
      prediction.correction = { x: 0, y: 0 };
  }

  entity.renderPos = { x: prediction.pos.x, y: prediction.pos.y };
  entity.serverRenderPos = { x: snap.pos.x, y: snap.pos.y };
  if (Number.isFinite(snap.angle)) {
    if (!Number.isFinite(entity.renderAngle)) entity.renderAngle = snap.angle;
    else
      entity.renderAngle = lerpAngle(
        entity.renderAngle,
        snap.angle,
        smoothFactor(ownerAngleLerpRate, dt),
      );
  }

  state.predictionDebug = {
    errorDistance: Math.hypot(
      snap.pos.x - prediction.pos.x,
      snap.pos.y - prediction.pos.y,
    ),
    speedScale,
    tickScale: simulationScale,
    snapshotInterval,
    staleTime: prediction.staleTime || 0,
    snapped: !!prediction.snapped,
  };
  prediction.snapped = false;
}

function stepEntityRenderToSnapshot(entity, dt, isOwner = false) {
  const snap = entity?.snapshot;
  if (!snap?.pos) return;

  if (
    !entity.renderPos ||
    !Number.isFinite(entity.renderPos.x) ||
    !Number.isFinite(entity.renderPos.y)
  ) {
    syncEntityRenderToSnapshot(entity, snap);
    return;
  }

  const petalType = isPetalEntity(snap.entityType)
    ? petalTypeFromEntity(snap.entityType)
    : 0;
  const target = entityInterpolationTarget(entity, snap, isOwner, petalType);
  const dx = target.x - entity.renderPos.x;
  const dy = target.y - entity.renderPos.y;
  const dist = Math.hypot(dx, dy);
  const snapDistance =
    (isOwner ? ownerRenderSnapBaseDistance : renderSnapBaseDistance) +
    Math.max(0, snap.radius || 0) * 3;
  if (dist >= snapDistance) {
    syncEntityRenderToSnapshot(entity, snap);
  } else if (dist > 0) {
    const renderRate =
      petalType > 0
        ? petalRenderLerpRate
        : isOwner
          ? ownerRenderLerpRate
          : entityRenderLerpRate;
    const factor = smoothFactor(renderRate, dt);
    entity.renderPos.x += dx * factor;
    entity.renderPos.y += dy * factor;
  }

  if (Number.isFinite(snap.angle)) {
    if (!Number.isFinite(entity.renderAngle)) {
      entity.renderAngle = snap.angle;
    } else {
      const angleRate =
        petalType > 0
          ? petalAngleLerpRate
          : isOwner
            ? ownerAngleLerpRate
            : entityAngleLerpRate;
      entity.renderAngle = lerpAngle(
        entity.renderAngle,
        snap.angle,
        smoothFactor(angleRate, dt),
      );
    }
  }
}

function entityInterpolationTarget(entity, snap, isOwner, petalType = 0) {
  if (isOwner || !entity?.snapshotVelocity || !snap?.pos)
    return snap?.pos || { x: 0, y: 0 };

  const age = Math.max(0, performance.now() / 1000 - (entity.snapshotAt || 0));
  if (petalType === petalYinYangType) return snap.pos;
  const maxLead =
    petalType > 0 ? petalExtrapolateMaxSeconds : entityExtrapolateMaxSeconds;
  const lead = Math.min(maxLead, age + serverFixedDt);
  const vx = entity.snapshotVelocity.x || 0;
  const vy = entity.snapshotVelocity.y || 0;
  let ex = vx * lead;
  let ey = vy * lead;
  const distance = Math.hypot(ex, ey);
  const maxDistance =
    petalType > 0
      ? Math.max(
          petalExtrapolateMaxDistance,
          Math.max(0, snap.radius || 0) * 0.4,
        )
      : Math.max(
          entityExtrapolateMaxDistance,
          Math.max(0, snap.radius || 0) * 1.5,
        );
  if (distance > maxDistance && distance > 0) {
    const scale = maxDistance / distance;
    ex *= scale;
    ey *= scale;
  }
  return { x: snap.pos.x + ex, y: snap.pos.y + ey };
}

function shouldFadeMissingEntity(entity) {
  const owner = state.entities.get(state.ownerEntityId);
  if (!entity?.snapshot) return false;
  if ((entity.snapshot.flags & flagDead) !== 0) return true;
  if (isNullVisualEntity(entity)) return true;
  if (!owner || !state.viewRadius) return false;

  const origin = owner.renderPos || owner.snapshot?.pos;
  if (!origin || !entity.renderPos) return false;

  const dx = entity.renderPos.x - origin.x;
  const dy = entity.renderPos.y - origin.y;
  const distance = Math.hypot(dx, dy);
  const radius = Math.max(0, entity.snapshot.radius || 0);
  const visibleEdge = state.viewRadius + radius;
  const edgeGrace = Math.max(missingEntityViewEdgeGrace, radius * 2);
  return distance <= Math.max(0, visibleEdge - edgeGrace);
}

function isNullVisualEntity(entity) {
  if (!entity || !entity.snapshot) return false;
  const isOwner =
    (entity.snapshot.flags & flagOwner) !== 0 ||
    entity.snapshot.entityId === state.ownerEntityId;
  return isOwner && ownerHasPetal(petalNullificationType);
}

function readKeyboardMoveInput() {
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

function readMouseMoveInput() {
  if (!state.mouse.seen || state.mouse.inUi) return { x: 0, y: 0 };

  const owner = state.entities.get(state.ownerEntityId);
  const center = owner
    ? worldToScreen(owner.renderPos)
    : { x: state.canvasWidth * 0.5, y: state.canvasHeight * 0.5 };
  let x = state.mouse.x - center.x;
  let y = state.mouse.y - center.y;
  const length = Math.hypot(x, y);
  if (length <= mouseMoveDeadzonePx) return { x: 0, y: 0 };

  const fullSpeedDistance = Math.max(
    80,
    Math.min(state.canvasWidth, state.canvasHeight) * 0.18,
  );
  const scale = Math.min(1, length / fullSpeedDistance) / length;
  return { x: x * scale, y: y * scale };
}

function isMainPanelOpen() {
  return !!(state.backpackOpen || state.craftOpen || state.talentOpen);
}

function readMoveInput() {
  if (isMainPanelOpen()) return { x: 0, y: 0 };
  if (mobileControls?.hasMovement()) return mobileControls.getMove();
  if (mobileControls?.isMobileMode()) return { x: 0, y: 0 };
  return state.keyboardControl ? readKeyboardMoveInput() : readMouseMoveInput();
}

function updateMousePointer(event) {
  state.mouse.x = event.clientX;
  state.mouse.y = event.clientY;
  state.mouse.seen = true;
  state.mouse.inUi = !!isUiTarget(event.target);
}

function estimateInputTargetTick() {
  if (!Number.isSafeInteger(state.serverTick) || state.serverTick < 0)
    return 0;

  const tickInterval = Math.max(
    0.001,
    state.serverTickInterval || serverFixedDt,
  );
  const elapsedSeconds = Math.max(
    0,
    (performance.now() - state.serverTickReceivedAt) / 1000,
  );
  return (
    Math.floor(state.serverTick + elapsedSeconds / tickInterval) +
    scheduledInputLeadTicks
  );
}

function flushInput(dt) {
  if (!state.connected || !state.authenticated || isTyping() || isOwnerDead()) {
    state.localMoveInput = { x: 0, y: 0 };
    return;
  }

  state.sendTimer += dt;
  const move = readMoveInput();
  state.localMoveInput = { x: clamp(move.x, -1, 1), y: clamp(move.y, -1, 1) };
  const packetMoveX = move.y;
  const packetMoveY = move.x;
  const px = Math.round(clamp(packetMoveX, -1, 1) * 127);
  const py = Math.round(clamp(packetMoveY, -1, 1) * 127);
  const moveChanged = px !== state.lastInput.x || py !== state.lastInput.y;
  const attacking =
    state.attacking ||
    state.keys.has("space") ||
    !!mobileControls?.isAttacking();
  const defending =
    state.defending ||
    state.keys.has("shift") ||
    !!mobileControls?.isDefending();
  const digging = false;
  const effectiveAttacking = attacking;
  const effectiveDefending = defending;
  const choresChanged =
    effectiveAttacking !== state.lastInput.attacking ||
    effectiveDefending !== state.lastInput.defending ||
    digging !== state.lastInput.digging;
  if (moveChanged || choresChanged || state.sendTimer >= packetInterval) {
    const sequence = state.inputSequence >>> 0;
    const sent = sendBytes(
      packInputFrame(
        sequence,
        estimateInputTargetTick(),
        packetMoveX,
        packetMoveY,
        effectiveAttacking,
        effectiveDefending,
        digging,
      ),
    );
    if (!sent) return;

    state.inputSequence = (sequence + 1) >>> 0;
    state.lastInput.x = px;
    state.lastInput.y = py;
    state.lastInput.attacking = effectiveAttacking;
    state.lastInput.defending = effectiveDefending;
    state.lastInput.digging = digging;
    state.sendTimer = 0;
  }
}

function isOwnerDead() {
  const owner = state.authenticated
    ? state.entities.get(state.ownerEntityId)
    : null;
  return !!owner && (owner.snapshot.flags & flagDead) !== 0;
}

function hasLivingOwner() {
  const owner = state.authenticated
    ? state.entities.get(state.ownerEntityId)
    : null;
  return !!owner && (owner.snapshot.flags & flagDead) === 0;
}

function updateDeathOverlay() {
  const dead = isOwnerDead();
  if (dead && !state.wasOwnerDead) {
    state.deathOverlayClosed = false;
    renderDeathOverlayContent();
  }
  if (!dead) state.deathOverlayClosed = false;
  state.wasOwnerDead = dead;
  deathOverlay.classList.toggle("hidden", !dead);
  deathOverlay.classList.toggle("closed", dead && state.deathOverlayClosed);
}

function renderDeathOverlayContent() {
  if (deathSource) deathSource.textContent = t("ui.death.unknownSource");
  if (!deathLoot) return;

  deathLoot.replaceChildren();
  const items = collectedThisRunItems()
    .sort(
      (a, b) =>
        raritySortRank(b.rarity) - raritySortRank(a.rarity) ||
        (b.count || 0) - (a.count || 0) ||
        compareLocalized(
          petalTypeName(a.petalType),
          petalTypeName(b.petalType),
        ),
    )
    .slice(0, 8);

  deathLoot.classList.toggle("empty", items.length === 0);
  for (const item of items) {
    const card = document.createElement("div");
    card.className = "death-loot-item";
    card.title = formatPetalItem(item.petalType, item.rarity, item.count);
    card.appendChild(makePetalStack(item.petalType, item.rarity, item.count));
    attachPetalInfoTooltip(card, item.petalType, item.rarity);
    deathLoot.appendChild(card);
  }
}

function requestRevive() {
  if (!state.connected || !state.authenticated || !isOwnerDead()) return;
  state.attacking = false;
  state.defending = false;
  state.digging = false;
  mobileControls?.reset();
  state.keys.clear();
  sendBytes(packChores(false, false, true, false, false));
}

function closeDeathOverlay() {
  if (!isOwnerDead()) return;
  state.deathOverlayClosed = true;
  updateDeathOverlay();
}

function isTyping() {
  return isTextInputActive();
}

function openChat(prefix = "") {
  chatUi.open(prefix);
}

function closeChat() {
  chatUi.close();
}

function submitChat() {
  chatUi.submit();
}

function executeClientCommand(line) {
  const args = splitCommand(line.trim());
  if (args.length === 0) return;

  const name = args.shift().replace(/^\//, "").toLowerCase();
  if (name === "version" || name === "ver") {
    addConsoleLine(appDisplayName);
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
      sendChat(ChatFlag.Global, message);
    else if (flagText === "local" || flagText === "l")
      sendChat(ChatFlag.Local, message);
    else if (flagText === "squad" || flagText === "sq")
      sendChat(ChatFlag.Local, `/sq ${message}`);
    else if (flagText === "whisper" || flagText === "w")
      sendChat(ChatFlag.Whisper, `/whisper ${message}`);
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
    addConsoleLine(
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
      state.keyboardControl = !(
        value === "0" ||
        value === "off" ||
        value === "false"
      );
      saveSettings();
    }
    addConsoleLine(
      t("commands.toggle.keyboard", {
        value: t(
          state.keyboardControl ? "commands.toggle.on" : "commands.toggle.off",
        ),
      }),
    );
    return;
  }
  if (name === "mobile" || name === "mobile_mode" || name === "touch") {
    if (args.length) {
      const value = String(args[0]).toLowerCase();
      if (value === "toggle") {
        setMobileControlMode(state.mobileControlMode === "on" ? "off" : "on");
      } else {
        setMobileControlMode(value);
      }
    } else {
      addConsoleLine(
        t("commands.toggle.mobile", { value: state.mobileControlMode }),
      );
    }
    return;
  }

  sendChat(ChatFlag.Local, `/${line}`);
}

async function executeLanguageCommand(args) {
  const current = getCurrentLocale();
  if (!args.length) {
    addConsoleLine(
      t("commands.language.current", {
        locale: current?.id || getLocale(),
        label: current?.label || getLocale(),
      }),
    );
    addConsoleLine(
      t("commands.language.available", {
        languages: getAvailableLocales()
          .map((entry) => entry.id + " (" + entry.label + ")")
          .join(", "),
      }),
    );
    addConsoleLine(t("commands.language.usage"));
    return;
  }

  const requested = args.join(" ").trim();
  const result = await changeLanguage(requested);
  if (!result.ok) {
    addConsoleLine(
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
  saveSettings();
  refreshLocalizedUi();
  addConsoleLine(
    t("commands.language.changed", {
      locale: result.entry?.id || result.locale,
      label: result.entry?.label || result.locale,
    }),
  );
}

async function executeLoginMapCommand(args) {
  if (state.authenticated) {
    addConsoleLine(t("commands.loginMap.loggedOutOnly"), "error");
    return;
  }

  if (!args.length) {
    addConsoleLine(
      t("commands.loginMap.current", {
        map: state.loginMapName || defaultMapName,
      }),
    );
    addConsoleLine(t("commands.loginMap.usage"));
    return;
  }

  const input = args.join(" ").trim();
  const requestedMap =
    input.toLowerCase() === "reset"
      ? defaultMapName
      : appendMapExtension(input);
  const previousMap = state.loginMapName || defaultMapName;
  const loadedMap = await loadMap(requestedMap);

  if (!loadedMap) {
    addConsoleLine(
      t("commands.loginMap.unchanged", { map: previousMap }),
      "error",
    );
    if (!state.authenticated) await loadMap(previousMap);
    return;
  }

  state.loginMapName = requestedMap;
  saveSettings();
  addConsoleLine(t("commands.loginMap.changed", { map: requestedMap }));
}

function appendMapExtension(mapName) {
  const normalized = String(mapName || "").trim();
  if (!normalized || /\.[^/\\]+$/.test(normalized)) return normalized;
  return `${normalized}.tmj`;
}

function executeLoginMapViewCommand(name, args) {
  if (state.authenticated) {
    addConsoleLine(t("commands.loginMap.viewLoggedOutOnly"), "error");
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
    addConsoleLine(
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
    addConsoleLine(
      t("commands.config.invalid", { name, value: args[0] }),
      "error",
    );
    return;
  }

  state[definition.property] = value;
  applyLoginMapView();
  saveSettings();
  addConsoleLine(
    t("commands.config.value", {
      name,
      value: formatClientConfigValue(value),
    }),
  );
}

function executeClientSliderCommand(args) {
  if (args.length < 3) {
    addConsoleLine(t("commands.slider.usage"), "error");
    return;
  }

  const key = args[0];
  const entry = getClientConfigEntry(key);
  if (!entry) {
    addConsoleLine(t("commands.config.unknown", { name: key }), "error");
    return;
  }

  const start = Number(args[1]);
  const end = Number(args[2]);
  const explicitStep = args.length >= 4 ? Number(args[3]) : NaN;
  if (!Number.isFinite(start) || !Number.isFinite(end) || start === end) {
    addConsoleLine(
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
  renderSliderPanel();
  addConsoleLine(
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
      addConsoleLine(
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
      addConsoleLine(t("commands.config.unknown", { name: result.name }));
      return;
    }
    state.clientConfig = result.config;
    saveSettings();
    requestAnimationFrame(drawScene);
    addConsoleLine(
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
      addConsoleLine(t("commands.config.unknown", { name: key }));
      return;
    }
    const value = getClientConfigValue(state.clientConfig, entry.name);
    addConsoleLine(
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
    addConsoleLine(
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
  saveSettings();
  requestAnimationFrame(drawScene);
  addConsoleLine(
    t("commands.config.value", {
      name: result.name,
      value: formatClientConfigValue(
        getClientConfigValue(state.clientConfig, result.name),
      ),
    }),
  );
}

function closeSliderPanel() {
  state.slider = null;
  sliderPanel?.classList.add("hidden");
  sliderPanel?.setAttribute("aria-hidden", "true");
}

function renderSliderPanel() {
  if (
    !state.slider ||
    !sliderPanel ||
    !sliderInput ||
    !sliderTitle ||
    !sliderValue
  )
    return;

  const { name, min, max, step } = state.slider;
  const current = getClientConfigValue(state.clientConfig, name);
  const value = Math.max(
    min,
    Math.min(max, Number.isFinite(current) ? current : min),
  );

  sliderPanel.classList.remove("hidden");
  sliderPanel.setAttribute("aria-hidden", "false");
  sliderTitle.textContent = name;
  sliderValue.textContent = formatClientConfigValue(value);
  sliderInput.min = String(min);
  sliderInput.max = String(max);
  sliderInput.step = String(step);
  sliderInput.value = String(value);
}

function applySliderValue(rawValue) {
  if (!state.slider) return;
  const result = setClientConfigValue(
    state.clientConfig,
    state.slider.name,
    rawValue,
  );
  if (!result.ok) return;

  state.clientConfig = result.config;
  saveSettings();
  sliderValue.textContent = formatClientConfigValue(
    getClientConfigValue(state.clientConfig, result.name),
  );
  requestAnimationFrame(drawScene);
}

function stopSliderEvent(event) {
  event.stopPropagation();
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

function renderChat(now = performance.now()) {
  chatUi.render(now);
}

function updateChatVisibility(now) {
  chatUi.updateVisibility(now);
}

function copySlot(slot) {
  return slotHasItem(slot)
    ? { petalType: slot.petalType, rarity: slot.rarity }
    : emptySlot();
}

function shouldSuppressClick() {
  return performance.now() < state.suppressClickUntil;
}

function renderInventoryPanel(options = {}) {
  const forceInventory = !!options.forceInventory;
  hidePetalInfoTooltip();
  const display = makeDisplaySlotState();
  renderSlotRow(
    primarySlots,
    display.ownerSlots,
    "primary",
    display.ownerSlots,
  );
  renderSlotRow(
    secondarySlots,
    display.secondarySlots,
    "secondary",
    display.ownerSlots,
  );

  if (state.backpackOpen || forceInventory) {
    inventoryList.replaceChildren();
    renderInventoryGroups(
      inventoryList,
      inventoryPanelItems(display.inventory),
    );
  }
}

function slotButtonRenderKey(slot, index, kind, primaryForVisual) {
  const petalType = slot?.petalType || 0;
  const rarity = slot?.rarity || 0;
  const count = slot?.count || 0;
  const visualType = slotHasItem(slot)
    ? slotVisualPetalType(slot, index, kind, primaryForVisual)
    : 0;
  return `${kind}:${petalType}:${rarity}:${count}:${visualType}`;
}

function renderSlotRow(container, slots, kind, primaryForVisual) {
  const slotCount = Math.max(0, primaryForVisual?.length || slots?.length || 0);
  const rowSlots = normalizeSlots(slots, slotCount);
  container.style.setProperty("--slot-count", rowSlots.length);

  for (let index = 0; index < rowSlots.length; index += 1) {
    const slot = rowSlots[index];
    const key = slotButtonRenderKey(slot, index, kind, primaryForVisual);
    let button = container.children[index];
    if (!button || button.dataset.renderKey !== key) {
      const nextButton = makeSlotButton(slot, index, kind, primaryForVisual);
      nextButton.dataset.renderKey = key;
      if (button) container.replaceChild(nextButton, button);
      else container.appendChild(nextButton);
      button = nextButton;
    }
    button.classList.toggle(
      "selected",
      kind === "primary" && index === state.selectedSlot,
    );
    updateSlotDurabilityOverlay(button, slot, index, kind);
  }

  while (container.children.length > rowSlots.length) {
    container.lastElementChild?.remove();
  }
}

function updateOwnerSlotRuntimeOverlays() {
  for (let index = 0; index < primarySlots.children.length; index += 1) {
    updateSlotDurabilityOverlay(
      primarySlots.children[index],
      displayOwnerSlots()[index],
      index,
      "primary",
    );
  }
}

function updateSlotDurabilityOverlay(button, slot, index, kind) {
  if (!button) return;

  const runtime = kind === "primary" ? state.ownerSlotRuntime[index] : null;
  const copies =
    runtime &&
    runtime.petalType === (slot?.petalType || 0) &&
    runtime.rarity === (slot?.rarity || 0)
      ? runtime.copies || []
      : [];
  let overlays = button.querySelector(":scope > .slot-durability-overlays");
  if (!slotHasItem(slot) || copies.length === 0) {
    overlays?.remove();
    return;
  }

  if (!overlays) {
    overlays = document.createElement("span");
    overlays.className = "slot-durability-overlays";
    button.appendChild(overlays);
  }
  while (overlays.children.length < copies.length) {
    const layer = document.createElement("span");
    layer.className = "slot-durability-layer";
    overlays.appendChild(layer);
  }
  while (overlays.children.length > copies.length)
    overlays.lastElementChild?.remove();

  const layerOpacity = 0.1 / copies.length;
  copies.forEach((copy, copyIndex) => {
    const layer = overlays.children[copyIndex];
    layer.classList.toggle("loading", !!copy.loading);
    layer.style.height = `${(1 - clamp(copy.progress || 0, 0, 1)) * 100}%`;
    layer.style.opacity = String(layerOpacity);
  });
}

function renderCraftPanel() {
  renderCraftMode();
  hidePetalInfoTooltip();
  craftList.replaceChildren();
  renderCraftStage();
  renderCraftMatrix();
}

function renderCraftMode() {
  const keyRoot = state.forgeMode ? "ui.forge" : "ui.craft";
  craftPanel.classList.toggle("forge-mode", state.forgeMode);
  if (craftTitle) craftTitle.textContent = t(`${keyRoot}.title`);
  craftOnceBtn.textContent = t(`${keyRoot}.once`);
  craftCloseBtn.setAttribute("aria-label", t(`${keyRoot}.close`));
  craftStage.setAttribute("aria-label", t(`${keyRoot}.slotsAriaLabel`));
}

function renderTalentPanel() {
  if (!talentTree) return;
  talentPointsLabel.textContent = t("ui.talents.points", {
    count: state.talentPoints,
  });
  talentTree.replaceChildren();

  const groups = new Map();
  for (const node of talentRootNodes) {
    if (!groups.has(node.group)) groups.set(node.group, []);
    groups.get(node.group).push(node);
  }

  const ownedKeys = ownedTalentKeys();
  for (const [groupName, nodes] of groups) {
    const section = document.createElement("section");
    section.className = "talent-group";

    const header = document.createElement("div");
    header.className = "talent-group-title";
    header.textContent = t(
      "talents.groups." + localizationSlug(groupName),
      {},
      { defaultValue: groupName },
    );
    section.appendChild(header);

    const roots = document.createElement("div");
    roots.className = "talent-root-list";
    for (const node of nodes) {
      const branch = renderTalentBranch(node, ownedKeys);
      if (branch) roots.appendChild(branch);
    }
    section.appendChild(roots);
    talentTree.appendChild(section);
  }
}

function renderTalentBranch(node, ownedKeys, path = new Set()) {
  if (!node || path.has(node.key)) return null;

  const nextPath = new Set(path);
  nextPath.add(node.key);

  const branch = document.createElement("div");
  branch.className = "talent-branch";

  const nodeWrap = document.createElement("div");
  nodeWrap.className = "talent-node-wrap";
  nodeWrap.appendChild(makeTalentButton(node, ownedKeys));
  branch.appendChild(nodeWrap);

  const children = talentChildrenByBase.get(node.key) || [];
  const childBranches = [];
  for (const child of children) {
    const childBranch = renderTalentBranch(child, ownedKeys, nextPath);
    if (childBranch) childBranches.push(childBranch);
  }

  if (childBranches.length > 0) {
    const childrenWrap = document.createElement("div");
    childrenWrap.className = `talent-children${childBranches.length === 1 ? " single" : ""}`;
    for (const childBranch of childBranches) {
      const childWrap = document.createElement("div");
      childWrap.className = "talent-child";
      childWrap.appendChild(childBranch);
      childrenWrap.appendChild(childWrap);
    }
    branch.appendChild(childrenWrap);
  }

  return branch;
}

function makeTalentButton(node, ownedKeys = ownedTalentKeys()) {
  const owned = ownedKeys.has(node.key);
  const addChain = owned ? [] : collectTalentAddChain(node, ownedKeys);
  const chainCost = addChain.reduce(
    (sum, talent) => sum + (talent.cost || 0),
    0,
  );
  const ready =
    owned || (addChain.length > 0 && chainCost <= state.talentPoints);

  const button = document.createElement("button");
  button.type = "button";
  button.className = `talent-node rarity-${node.rarity}${owned ? " owned" : ""}${ready ? "" : " blocked"}`;
  button.style.setProperty("--talent-color", rarityColor(node.rarity, 1));
  button.title = owned
    ? t("ui.talents.holdRemove", { name: localizedTalentName(node) })
    : t("ui.talents.holdLearn", {
        names: addChain.map(localizedTalentName).join(" -> "),
        cost: chainCost,
      });

  const rarity = document.createElement("span");
  rarity.className = "talent-rarity";
  rarity.textContent =
    rarityShortNames[node.rarity] || rarityName(node.rarity).slice(0, 2);
  button.appendChild(rarity);

  const name = document.createElement("span");
  name.className = "talent-name";
  name.textContent = talentDisplayName(node);
  button.appendChild(name);

  const cost = document.createElement("span");
  cost.className = "talent-cost";
  cost.textContent = t("ui.talents.cost", { cost: node.cost });
  button.appendChild(cost);

  attachTalentLongPress(button, node);
  return button;
}

function talentDisplayName(node) {
  const name = localizedTalentName(node);
  if (node.id === TalentId.PoisonDamage && node.rarity === 3)
    return name + " " + (node.rank + 1);
  return name;
}

function ownedTalentKeys() {
  return new Set(
    (state.talents || []).map((talent) =>
      talentKey(talent.id, talent.rarity, talent.rank || 0),
    ),
  );
}

function isTalentOwned(node, ownedKeys = ownedTalentKeys()) {
  return !!node && ownedKeys.has(node.key);
}

function collectTalentAddChain(node, ownedKeys = ownedTalentKeys()) {
  const chain = [];
  const seen = new Set();
  let current = node;
  while (current && !ownedKeys.has(current.key) && !seen.has(current.key)) {
    seen.add(current.key);
    chain.push(current);
    current = current.base ? talentNodeByKey.get(current.base) : null;
  }
  return chain.reverse();
}

function attachTalentLongPress(button, node) {
  let timer = 0;
  let fired = false;

  const clear = () => {
    if (timer) window.clearTimeout(timer);
    timer = 0;
    button.classList.remove("holding");
  };

  button.addEventListener("pointerdown", (event) => {
    if (event.button !== undefined && event.button !== 0) return;
    event.preventDefault();
    event.stopPropagation();
    fired = false;
    button.classList.add("holding");
    timer = window.setTimeout(() => {
      fired = true;
      clear();
      submitTalentNode(node);
    }, 520);
  });
  button.addEventListener("pointerup", clear);
  button.addEventListener("pointerleave", clear);
  button.addEventListener("pointercancel", clear);
  button.addEventListener("click", (event) => {
    event.preventDefault();
    event.stopPropagation();
    if (!fired) addConsoleLine(t("ui.talents.holdHint"));
  });
}

function submitTalentNode(node) {
  if (!state.authenticated || !node) return;

  const ownedKeys = ownedTalentKeys();
  const owned = isTalentOwned(node, ownedKeys);
  const talents = owned ? [node] : collectTalentAddChain(node, ownedKeys);
  if (talents.length === 0) return;

  const packet = packTalentRequest(owned ? "remove" : "add", talents);
  if (!packet) return;
  sendBytes(packet);
  addConsoleLine(
    t(owned ? "commands.talent.removing" : "commands.talent.learning", {
      name: talentDisplayName(node),
    }),
  );
}

function selectCraftPetal(petalType, rarity, options = {}) {
  if (state.craftPhase === "spinning") return;
  if (state.forgeMode) {
    if (rarity !== raritySuper || getInventoryCount(petalType, raritySuper) < 5)
      return;
    if (state.craftPhase === "success" || state.craftPhase === "returned")
      clearCraftDisplay();
    state.craftPhase = "staged";
    state.craftSource = { petalType, rarity: raritySuper };
    state.craftResult = null;
    state.craftSlots = distributeCraftCount(petalType, raritySuper, 5);
    renderCraftPanel();
    return;
  }
  if (!canCraftRarity(rarity)) return;
  if (state.craftPhase === "success" || state.craftPhase === "returned")
    clearCraftDisplay();
  const sameSource =
    state.craftSource &&
    state.craftSource.petalType === petalType &&
    state.craftSource.rarity === rarity;
  const stagedSame = sameSource ? getCraftSlotCount(petalType, rarity) : 0;
  const available = Math.max(
    0,
    getInventoryCount(petalType, rarity) - stagedSame,
  );
  if (available <= 0) return;
  if (available < 5 && stagedSame + available < 5) return;

  const addCount = options.all ? available : Math.min(5, available);
  const total = stagedSame + addCount;

  state.craftPhase = "staged";
  state.craftSource = { petalType, rarity };
  state.craftResult = null;
  state.craftSlots = distributeCraftCount(petalType, rarity, total);
  renderCraftPanel();
}

function inventoryItemVariants(item) {
  return Array.isArray(item?.typeStack) && item.typeStack.length
    ? item.typeStack
    : [item];
}

function makeInventoryTypeStack(item) {
  const variants = inventoryItemVariants(item);
  if (variants.length <= 1)
    return makePetalStack(item.petalType, item.rarity, item.count);

  const stack = document.createElement("span");
  stack.className = "inventory-type-stack";
  for (let depth = Math.min(variants.length, 3) - 1; depth >= 0; depth -= 1) {
    const variant = variants[depth];
    const layer = makePetalStack(
      variant.petalType,
      variant.rarity,
      depth === 0 ? variant.count : 0,
    );
    layer.classList.add("inventory-type-stack-layer");
    layer.style.setProperty("--type-stack-depth", String(depth));
    stack.appendChild(layer);
  }
  return stack;
}

function renderInventoryGroups(container, items) {
  const groups = groupInventoryItems(items);
  container.classList.toggle("hidden", groups.length === 0);

  for (const group of groups) {
    const section = document.createElement("section");
    section.className = "inventory-group";

    if (group.showHeader) {
      const header = document.createElement("div");
      header.className = "inventory-group-title";
      header.style.color = rarityColor(group.rarity, 1);
      header.textContent = rarityName(group.rarity);
      section.appendChild(header);
    }

    const grid = document.createElement("div");
    grid.className = "inventory-grid";
    for (const item of group.items) {
      const variants = inventoryItemVariants(item);
      const button = document.createElement("button");
      button.className = `inventory-item${variants.length > 1 ? " type-stacked" : ""}`;
      button.type = "button";
      button.title = variants
        .map(
          (variant) =>
            formatPetalItem(
              variant.petalType,
              variant.rarity,
              variant.count,
            ),
        )
        .join("\n");
      button.appendChild(makeInventoryTypeStack(item));
      attachPetalInfoTooltip(button, item.petalType, item.rarity);
      button.addEventListener("pointerdown", (event) =>
        beginDrag(event, button, "inventory", -1, item),
      );
      button.addEventListener("click", () => {
        if (shouldSuppressClick()) return;
        equipFromInventory(item);
      });
      grid.appendChild(button);
    }
    section.appendChild(grid);
    container.appendChild(section);
  }
}

function renderCraftMatrix() {
  if (state.forgeMode) {
    renderForgeMatrix();
    return;
  }

  const rows = buildCraftRows();
  craftList.classList.toggle("hidden", rows.length === 0);
  if (rows.length === 0) return;

  const matrix = document.createElement("div");
  matrix.className = "craft-matrix";
  matrix.style.setProperty(
    "--craft-rarity-count",
    String(craftRarityDisplayOrder.length),
  );

  for (const row of rows) {
    for (const rarity of craftRarityDisplayOrder) {
      const baseCount = row.counts.get(rarity) || 0;
      const heldCount = getCraftHeldCount(row.petalType, rarity);
      const displayCount = Math.max(0, baseCount - heldCount);
      const lit = canLightCraftItem(row.petalType, rarity);
      const selected =
        state.craftSource &&
        state.craftSource.petalType === row.petalType &&
        state.craftSource.rarity === rarity;

      const cell = document.createElement(baseCount > 0 ? "button" : "div");
      cell.className = `craft-matrix-cell${baseCount > 0 ? "" : " empty"}${lit ? " lit" : " locked"}${selected ? " selected" : ""}`;
      if (baseCount > 0) {
        cell.type = "button";
        cell.title = formatPetalItem(
          row.petalType,
          rarity,
          displayCount,
        );
        cell.appendChild(
          makePetalStack(row.petalType, rarity, displayCount),
        );
        attachPetalInfoTooltip(cell, row.petalType, rarity);
        cell.addEventListener("pointerdown", (event) => {
          if (event.button !== undefined && event.button !== 0) return;
          event.preventDefault();
          event.stopPropagation();
          if (lit)
            selectCraftPetal(row.petalType, rarity, { all: event.shiftKey });
        });
      }
      matrix.appendChild(cell);
    }
  }

  craftList.appendChild(matrix);
}

function renderForgeMatrix() {
  const counts = new Map();
  for (const item of displayInventory()) {
    if (!slotHasItem(item) || item.rarity !== raritySuper || item.count <= 0)
      continue;
    counts.set(
      item.petalType,
      (counts.get(item.petalType) || 0) + Math.max(0, item.count || 0),
    );
  }
  const items = Array.from(counts, ([petalType, count]) => ({
    petalType,
    count,
  })).sort(
    (a, b) =>
      compareLocalized(
        petalTypeName(a.petalType),
        petalTypeName(b.petalType),
      ) || a.petalType - b.petalType,
  );

  craftList.classList.toggle("hidden", items.length === 0);
  if (items.length === 0) return;

  const matrix = document.createElement("div");
  matrix.className = "craft-matrix forge-matrix";
  for (const item of items) {
    const displayCount = item.count;
    const lit = state.craftPhase !== "spinning" && displayCount >= 5;
    const selected =
      state.craftSource?.petalType === item.petalType &&
      state.craftSource?.rarity === raritySuper;

    const cell = document.createElement("button");
    cell.type = "button";
    cell.className = `craft-matrix-cell${lit ? " lit" : " locked"}${selected ? " selected" : ""}`;
    cell.title = formatPetalItem(
      item.petalType,
      raritySuper,
      displayCount,
    );
    cell.appendChild(makeForgePetalStack(item.petalType, displayCount));
    attachPetalInfoTooltip(cell, item.petalType, raritySuper);
    cell.addEventListener("pointerdown", (event) => {
      if (event.button !== undefined && event.button !== 0) return;
      event.preventDefault();
      event.stopPropagation();
      if (lit) selectCraftPetal(item.petalType, raritySuper);
    });
    matrix.appendChild(cell);
  }
  craftList.appendChild(matrix);
}

function makeForgePetalStack(petalType, count) {
  const stack = makePetalStack(petalType, raritySuper, count);
  let badge = stack.querySelector(".petal-count");
  if (!badge) {
    badge = document.createElement("span");
    badge.className = "petal-count";
    stack.appendChild(badge);
  }
  badge.textContent = `${formatShortCount(count)}/5`;
  return stack;
}

function buildCraftRows() {
  const rows = new Map();
  for (const item of displayInventory()) {
    if (!slotHasItem(item) || item.count <= 0 || !canCraftRarity(item.rarity))
      continue;
    if (!rows.has(item.petalType)) rows.set(item.petalType, new Map());
    rows.get(item.petalType).set(item.rarity, item.count);
  }
  return Array.from(rows, ([petalType, counts]) => ({ petalType, counts })).sort(
    (a, b) =>
      compareLocalized(
        petalTypeName(a.petalType),
        petalTypeName(b.petalType),
      ) || a.petalType - b.petalType,
  );
}

function inventoryPanelItems(items) {
  const query = normalizePetalName(state.inventorySearchQuery);
  const filtered = (items || []).filter((item) => {
    if (!slotHasItem(item) || item.count <= 0) return false;
    if (!query) return true;
    const searchable = [
      petalTypeName(item.petalType),
      rarityName(item.rarity),
      PetalNames[item.petalType] || "",
      RarityNames[item.rarity] || "",
    ].join(" ");
    return normalizePetalName(searchable).includes(query);
  });
  const itemsByTypeAndRarity = new Map();
  for (const item of filtered) {
    const key = `${item.petalType}:${item.rarity}`;
    const existing = itemsByTypeAndRarity.get(key);
    if (existing) existing.count += Math.max(0, Math.floor(item.count || 0));
    else
      itemsByTypeAndRarity.set(key, {
        ...item,
        count: Math.max(0, Math.floor(item.count || 0)),
      });
  }
  const merged = Array.from(itemsByTypeAndRarity.values());

  if (state.inventoryStacked) {
    const variantsByType = new Map();
    for (const item of merged) {
      if (!variantsByType.has(item.petalType))
        variantsByType.set(item.petalType, []);
      variantsByType.get(item.petalType).push({ ...item });
    }

    return Array.from(variantsByType.values(), (variants) => {
      variants.sort(
        (a, b) =>
          raritySortRank(b.rarity) - raritySortRank(a.rarity) ||
          b.rarity - a.rarity,
      );
      return { ...variants[0], typeStack: variants };
    });
  }

  return merged;
}

function compareInventoryItemsByName(a, b) {
  return (
    compareLocalized(
      petalTypeName(a.petalType),
      petalTypeName(b.petalType),
    ) ||
    a.petalType - b.petalType ||
    raritySortRank(b.rarity) - raritySortRank(a.rarity) ||
    b.rarity - a.rarity
  );
}

function groupInventoryItems(items) {
  const visibleItems = items
    .filter((item) => slotHasItem(item) && item.count > 0)
    .map((item) => ({ ...item }));

  if (state.inventoryStacked) {
    return visibleItems.length
      ? [
          {
            showHeader: false,
            items: visibleItems.sort(compareInventoryItemsByName),
          },
        ]
      : [];
  }

  const groups = new Map();
  for (const item of visibleItems) {
    if (!groups.has(item.rarity)) groups.set(item.rarity, []);
    groups.get(item.rarity).push(item);
  }
  return Array.from(groups, ([rarity, groupItems]) => ({
    showHeader: true,
    rarity,
    items: groupItems.sort(compareInventoryItemsByName),
  })).sort(
    (a, b) =>
      raritySortRank(b.rarity) - raritySortRank(a.rarity) ||
      b.rarity - a.rarity,
  );
}

function getInventoryCount(petalType, rarity) {
  const item = displayInventory().find(
    (entry) => entry.petalType === petalType && entry.rarity === rarity,
  );
  return item ? item.count || 0 : 0;
}

function getCraftSlotCount(petalType, rarity) {
  return state.craftSlots.reduce(
    (sum, slot) =>
      slotHasItem(slot) &&
      slot.petalType === petalType &&
      slot.rarity === rarity
        ? sum + Math.max(1, slot.count || 1)
        : sum,
    0,
  );
}

function getActiveCraftStagedCount(petalType, rarity) {
  if (state.craftPhase !== "staged" && state.craftPhase !== "spinning")
    return 0;
  return getCraftSlotCount(petalType, rarity);
}

function getCraftHeldCount(petalType, rarity) {
  if (state.craftPhase === "idle") return 0;
  if (state.craftPhase === "success") {
    return slotHasItem(state.craftResult) &&
      state.craftResult.petalType === petalType &&
      state.craftResult.rarity === rarity
      ? Math.max(1, state.craftResult.count || 1)
      : 0;
  }
  return getCraftSlotCount(petalType, rarity);
}

function canLightCraftItem(petalType, rarity) {
  if (state.craftPhase === "spinning") return false;
  if (!canCraftRarity(rarity)) return false;
  const visible = Math.max(
    0,
    getInventoryCount(petalType, rarity) - getCraftHeldCount(petalType, rarity),
  );
  return visible > 0 && visible + getCraftSlotCount(petalType, rarity) >= 5;
}

function canCraftRarity(rarity) {
  return rarity >= 1 && rarity <= 9;
}

function renderCraftStage() {
  const keyRoot = state.forgeMode ? "ui.forge" : "ui.craft";
  craftStage.classList.toggle("spinning", state.craftPhase === "spinning");
  craftStage.classList.toggle("success", state.craftPhase === "success");
  craftStage.classList.toggle(
    "burst",
    state.craftPhase === "returned" &&
      performance.now() < state.craftBurstUntil,
  );

  const showRing = state.craftPhase !== "success";
  craftStage.querySelectorAll("[data-craft-slot]").forEach((button) => {
    const index = Number(button.dataset.craftSlot || 0);
    const slot = state.craftSlots[index];
    button.replaceChildren();
    clearPetalInfoTooltip(button);
    button.classList.toggle("hidden", !showRing);
    button.classList.toggle("filled", slotHasItem(slot));
    button.disabled = state.craftPhase === "spinning";
    button.title = slotHasItem(slot)
      ? formatPetalItem(
          slot.petalType,
          slot.rarity,
          slot.count || 1,
        )
      : t(`${keyRoot}.slot`);
    if (slotHasItem(slot)) {
      button.appendChild(
        makePetalStack(slot.petalType, slot.rarity, slot.count || 1),
      );
      attachPetalInfoTooltip(button, slot.petalType, slot.rarity);
    }
    button.onclick = (event) => {
      event.preventDefault();
      clearCraftDisplay();
      renderCraftPanel();
    };
  });

  craftResultSlot.replaceChildren();
  clearPetalInfoTooltip(craftResultSlot);
  craftResultSlot.classList.toggle(
    "hidden",
    state.craftPhase !== "success" || !slotHasItem(state.craftResult),
  );
  if (state.craftPhase === "success" && slotHasItem(state.craftResult)) {
    craftResultSlot.title = formatPetalItem(
      state.craftResult.petalType,
      state.craftResult.rarity,
      state.craftResult.count || 1,
    );
    craftResultSlot.appendChild(
      makePetalStack(
        state.craftResult.petalType,
        state.craftResult.rarity,
        state.craftResult.count || 1,
      ),
    );
    attachPetalInfoTooltip(
      craftResultSlot,
      state.craftResult.petalType,
      state.craftResult.rarity,
    );
  }
  craftResultSlot.onclick = (event) => {
    event.preventDefault();
    clearCraftDisplay();
    renderCraftPanel();
  };

  const stagedCount = state.craftSource
    ? getCraftSlotCount(state.craftSource.petalType, state.craftSource.rarity)
    : 0;
  craftOnceBtn.disabled = !(
    state.authenticated &&
    state.craftPhase === "staged" &&
    (state.forgeMode ? stagedCount === 5 : stagedCount >= 5)
  );
  craftChance.textContent = state.forgeMode
    ? ""
    : state.craftSource
    ? t("ui.craft.successChance", {
        chance: formatCraftChance(state.craftSource.rarity),
      })
    : "";
  if (state.craftPhase === "spinning") {
    craftSelection.textContent = t(`${keyRoot}.working`);
  } else if (state.craftPhase === "success") {
    craftSelection.textContent = t(`${keyRoot}.success`);
  } else if (state.craftPhase === "returned") {
    craftSelection.textContent = t(`${keyRoot}.failed`);
  } else if (state.craftSource) {
    craftSelection.textContent = formatPetalItem(
      state.craftSource.petalType,
      state.craftSource.rarity,
      stagedCount,
    );
  } else {
    craftSelection.textContent = t(`${keyRoot}.instruction`);
  }
}

function formatCraftChance(rarity) {
  if (rarity === 8) return "0.33%";
  if (rarity === 9) return "0.1%";
  if (rarity >= 1 && rarity < 8) {
    const chance = 64 / 2 ** (rarity - 1);
    return `${chance.toFixed(chance >= 1 ? 0 : 2)}%`;
  }
  return "0%";
}

function submitCraft() {
  if (
    !state.authenticated ||
    state.craftPhase !== "staged" ||
    !state.craftSource
  )
    return;
  const { petalType, rarity } = state.craftSource;
  const count = getCraftSlotCount(petalType, rarity);
  if (state.forgeMode) {
    if (
      rarity !== raritySuper ||
      count !== 5 ||
      count > getInventoryCount(petalType, raritySuper)
    )
      return;
    state.craftPhase = "spinning";
    state.craftSpinStarted = performance.now();
    renderCraftPanel();
    sendBytes(packForge(petalType));
    return;
  }

  if (!canCraftRarity(rarity)) return;
  if (count < 5 || count > getInventoryCount(petalType, rarity)) return;
  state.craftPhase = "spinning";
  state.craftSpinStarted = performance.now();
  renderCraftPanel();
  sendBytes(packCraft(petalType, rarity, count));
}

function handleCraftResult(msg) {
  const finish = () => {
    const items = compactCraftItems(msg.items || []);
    if (msg.success) {
      state.craftPhase = "success";
      state.craftSlots = Array.from({ length: 5 }, () => null);
      state.craftResult = pickCraftSuccessItem(items, msg) || items[0] || null;
    } else {
      state.craftPhase = "returned";
      state.craftResult = null;
      state.craftSlots = distributeCraftItems(items);
      state.craftBurstUntil = performance.now() + 420;
    }
    state.craftSource = null;
    renderCraftPanel();
  };

  const elapsed = performance.now() - (state.craftSpinStarted || 0);
  window.setTimeout(finish, Math.max(0, 700 - elapsed));
}

function compactCraftItems(items) {
  const compacted = [];
  for (const item of items) {
    if (!slotHasItem(item) || item.count <= 0) continue;
    const existing = compacted.find(
      (entry) =>
        entry.petalType === item.petalType && entry.rarity === item.rarity,
    );
    if (existing)
      existing.count = clamp(existing.count + item.count, 0, 1000000000);
    else
      compacted.push({
        petalType: item.petalType,
        rarity: item.rarity,
        count: item.count,
      });
  }
  return compacted.sort(
    (a, b) =>
      raritySortRank(b.rarity) - raritySortRank(a.rarity) ||
      compareLocalized(
        petalTypeName(a.petalType),
        petalTypeName(b.petalType),
      ),
  );
}

function pickCraftSuccessItem(items, msg) {
  return (
    items.find(
      (item) => item.petalType === msg.petalType && item.rarity !== msg.rarity,
    ) || null
  );
}

function distributeCraftItems(items) {
  if (items.length === 1) {
    const item = items[0];
    return distributeCraftCount(item.petalType, item.rarity, item.count || 0);
  }

  const slots = Array.from({ length: 5 }, () => null);
  let slotIndex = 0;
  for (const item of items) {
    let remaining = Math.max(0, item.count || 0);
    while (remaining > 0 && slotIndex < slots.length) {
      const count = slotIndex === slots.length - 1 ? remaining : 1;
      slots[slotIndex] = {
        petalType: item.petalType,
        rarity: item.rarity,
        count,
      };
      remaining -= count;
      slotIndex += 1;
    }
  }
  return slots;
}

function distributeCraftCount(petalType, rarity, count) {
  const slots = Array.from({ length: 5 }, () => null);
  const total = Math.max(0, Math.min(1000000000, Math.floor(count || 0)));
  const base = Math.floor(total / slots.length);
  const extra = total % slots.length;
  for (let i = 0; i < slots.length; i += 1) {
    const slotCount = base + (i < extra ? 1 : 0);
    if (slotCount > 0) slots[i] = { petalType, rarity, count: slotCount };
  }
  return slots;
}

function clearCraftDisplay() {
  if (state.craftPhase === "spinning") return;
  state.craftSlots = Array.from({ length: 5 }, () => null);
  state.craftSource = null;
  state.craftResult = null;
  state.craftPhase = "idle";
  state.craftBurstUntil = 0;
}

function makeSlotButton(
  slot,
  index,
  kind,
  primaryForVisual = displayOwnerSlots(),
) {
  const button = document.createElement("button");
  button.className = `slot${slotHasItem(slot) ? "" : " empty"}${kind === "primary" && index === state.selectedSlot ? " selected" : ""}`;
  button.type = "button";
  button.dataset.dropKind = kind;
  button.dataset.dropIndex = String(index);

  if (slotHasItem(slot)) {
    button.title = formatPetalItem(slot.petalType, slot.rarity);
    button.appendChild(
      makePetalStack(
        slot.petalType,
        slot.rarity,
        0,
        slotVisualPetalType(slot, index, kind),
        {
          kind,
          slotIndex: index,
        },
      ),
    );
    attachPetalInfoTooltip(button, slot.petalType, slot.rarity);
    button.addEventListener("pointerdown", (event) =>
      beginDrag(event, button, kind, index, slot),
    );
  }

  if (kind === "primary") {
    button.addEventListener("click", () => {
      if (shouldSuppressClick()) return;
      state.selectedSlot = index;
      quickSwapSlot(index);
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

function slotVisualPetalType(
  slot,
  index,
  kind,
  primaryForVisual = displayOwnerSlots(),
) {
  if (!slotHasItem(slot) || slot.petalType !== petalMimicType)
    return slot ? slot.petalType : 0;
  if (kind === "secondary") return slot.petalType;

  const slots = primaryForVisual;
  if (!slots.length) return slot.petalType;

  const targetIndex = index <= 0 ? slots.length - 1 : index - 1;
  const target = slots[targetIndex];
  if (slotHasItem(target) && target.petalType !== petalMimicType)
    return target.petalType;
  return slot.petalType;
}

function petalBaseCopy(petalType, rarity) {
  const level = rarityLevel(rarity);
  switch (petalType) {
    case petalAntEggType:
      return 4;
    case petalDustType:
      return 3;
    case petalDahliaType:
      return 3;
    case petalDandelionType:
      return level >= rarityLevel(8) ? 3 : level >= rarityLevel(6) ? 2 : 1;
    case petalOrangeType:
      return 3;
    case petalShovelType:
      return 1;
    case petalStingerType:
      return level >= rarityLevel(6) ? 3 : 1;
    case petalLightType:
      return level >= 6 ? 5 : level >= 4 ? 3 : level >= 2 ? 2 : 1;
    case petalPollenType:
      return level >= 4 ? 3 : level >= 2 ? 2 : 1;
    case 34:
      return rarity === 10 ? 1 : level >= 7 ? 3 : 1;
    case petalMimicType:
    case petalDouliType:
    case petalNullificationType:
    case petalThirdEyeType:
    case 1:
    case 3:
    case 18:
    case 24:
    case 25:
    case 26:
      return 0;
    default:
      return 1;
  }
}

function petalHasMultiCopyVisual(petalType, rarity) {
  return effectivePetalCardCopy(petalType, rarity) > 1;
}

function talentOwned(id, rarity, rank = 0) {
  return (state.talents || []).some(
    (talent) =>
      talent.id === id &&
      talent.rarity === rarity &&
      (talent.rank || 0) === rank,
  );
}

function canDuplicatorAffect(petalType, copy) {
  return copy > 0 && !nonStackPetalTypes.has(petalType);
}

function effectivePetalCardCopy(petalType, rarity, options = {}) {
  let copy = petalBaseCopy(petalType, rarity);
  if (!canDuplicatorAffect(petalType, copy)) return copy;

  if (copy >= 2 && talentOwned(TalentId.PetalSplit, 5)) copy += 1;
  if (copy >= 2 && talentOwned(TalentId.PetalSplit, 7)) copy += 1;

  const inPrimarySlot =
    options.kind === "primary" && Number.isFinite(options.slotIndex);
  if (inPrimarySlot && options.slotIndex === 0) {
    if (rarity !== 9 && rarity !== 10 && talentOwned(TalentId.PetalSplit, 8))
      copy += 1;
    if ((rarity === 9 || rarity === 10) && talentOwned(TalentId.PetalSplit, 9))
      copy += 1;
  }
  return copy;
}

function attachPetalInfoTooltip(element, petalType, rarity) {
  if (!element || !petalInfoTooltip || !petalType || !rarity) return;
  element.dataset.petalInfoType = String(petalType);
  element.dataset.petalInfoRarity = String(rarity);

  if (element.dataset.petalInfoBound === "1") return;
  element.dataset.petalInfoBound = "1";

  element.addEventListener("pointerenter", (event) =>
    showPetalInfoTooltipFromElement(element, event),
  );
  element.addEventListener("pointermove", movePetalInfoTooltip);
  element.addEventListener("pointerleave", hidePetalInfoTooltip);
  element.addEventListener("pointercancel", hidePetalInfoTooltip);
  element.addEventListener("blur", hidePetalInfoTooltip);
}

function clearPetalInfoTooltip(element) {
  if (!element) return;
  if (state.petalInfoHoverElement === element) hidePetalInfoTooltip();
  delete element.dataset.petalInfoType;
  delete element.dataset.petalInfoRarity;
}

function showPetalInfoTooltipFromElement(element, event) {
  const petalType = Number(element?.dataset?.petalInfoType || 0);
  const rarity = Number(element?.dataset?.petalInfoRarity || 0);
  if (!petalType || !rarity) return;
  state.petalInfoHoverElement = element;
  showPetalInfoTooltip(petalType, rarity, event);
}

function showPetalInfoTooltip(petalType, rarity, event) {
  if (!petalInfoTooltip) return;
  const info = buildPetalInfo(petalType, rarity);
  if (!info || info.rows.length === 0) {
    hidePetalInfoTooltip();
    return;
  }

  renderPetalInfoTooltip(info);
  petalInfoTooltip.classList.remove("hidden");
  petalInfoTooltip.setAttribute("aria-hidden", "false");
  movePetalInfoTooltip(event);
}

function hidePetalInfoTooltip() {
  if (!petalInfoTooltip) return;
  state.petalInfoHoverElement = null;
  petalInfoTooltip.classList.add("hidden");
  petalInfoTooltip.setAttribute("aria-hidden", "true");
}

function syncPetalInfoTooltipHover() {
  const element = state.petalInfoHoverElement;
  if (
    !element ||
    !petalInfoTooltip ||
    petalInfoTooltip.classList.contains("hidden")
  )
    return;
  if (!element.isConnected || !element.matches(":hover"))
    hidePetalInfoTooltip();
}

function movePetalInfoTooltip(event) {
  if (
    !petalInfoTooltip ||
    petalInfoTooltip.classList.contains("hidden") ||
    !event
  )
    return;
  syncPetalInfoTooltipHover();
  if (petalInfoTooltip.classList.contains("hidden")) return;
  const offset = 14;
  const pad = 8;
  const rect = petalInfoTooltip.getBoundingClientRect();
  let x = event.clientX + offset;
  let y = event.clientY + offset;
  if (x + rect.width + pad > window.innerWidth)
    x = event.clientX - rect.width - offset;
  if (y + rect.height + pad > window.innerHeight)
    y = event.clientY - rect.height - offset;
  x = clamp(x, pad, Math.max(pad, window.innerWidth - rect.width - pad));
  y = clamp(y, pad, Math.max(pad, window.innerHeight - rect.height - pad));
  petalInfoTooltip.style.left = `${x}px`;
  petalInfoTooltip.style.top = `${y}px`;
}

function renderPetalInfoTooltip(info) {
  petalInfoTooltip.replaceChildren();

  const title = document.createElement("div");
  title.className = "petal-info-title";
  title.style.color = rarityColor(info.rarity, 1);
  title.textContent = formatRarityPetal(info.petalType, info.rarity);
  petalInfoTooltip.appendChild(title);

  const subtitle = document.createElement("div");
  subtitle.className = "petal-info-subtitle";
  subtitle.textContent = t("ui.petalInfo.single");
  petalInfoTooltip.appendChild(subtitle);

  for (const item of info.rows) {
    const row = document.createElement("div");
    row.className = "petal-info-row";

    const label = document.createElement("span");
    label.className = "petal-info-label";
    label.textContent = item.label;
    row.appendChild(label);

    const value = document.createElement("span");
    value.className = "petal-info-value";
    value.textContent = item.value;
    row.appendChild(value);

    petalInfoTooltip.appendChild(row);
  }
}

const petalInfoStatKeys = Object.freeze({
  Petals: "petals",
  Damage: "damage",
  Durability: "durability",
  Armor: "armor",
  Reload: "reload",
  Preload: "preload",
  Vision: "vision",
  "Attack range": "attackRange",
  "Reload cut": "reloadCut",
  Undead: "undead",
  "Summon HP": "summonHp",
  "Summon DMG": "summonDmg",
  "Linked HP": "linkedHp",
  "Flower body": "flowerBody",
  Delay: "delay",
  Effect: "effect",
  Mass: "mass",
  Regen: "regen",
  "Defend regen": "defendRegen",
  "Petal hits/tick": "petalHitsPerTick",
  "Compression power": "compressionPower",
  "Petal HP healing/s": "petalHpHealingPerSecond",
  "Throw CD": "throwCooldown",
  "Web time": "webTime",
  "Web radius": "webRadius",
  "Flower HP": "flowerHp",
  "Healing taken": "healingTaken",
  "Flower radius": "flowerRadius",
  Attracts: "attracts",
  Burrow: "burrow",
  Heal: "heal",
  "Anti-heal": "antiHeal",
  Healing: "healing",
  Lifetime: "lifetime",
  Magnet: "magnet",
  Poison: "poison",
  "Poison time": "poisonTime",
  "Attract range": "attractRange",
  Rotation: "rotation",
  Revive: "revive",
  "Tri bonus": "triBonus",
  Copies: "copies",
  "Fire interval": "fireInterval",
  "Trap damage": "trapDamage",
  "Trap durability": "trapDurability",
});

const petalInfoValueKeys = Object.freeze({
  Corrupted: "corrupted",
  Nullify: "nullify",
  "left petal": "leftPetal",
  "x0.8 per hit": "healingPerHit",
});

function localizePetalInfoLabel(label) {
  const key = petalInfoStatKeys[label];
  return key
    ? t("ui.petalInfo.stats." + key, {}, { defaultValue: label })
    : label;
}

function localizePetalInfoValue(value) {
  const text = String(value);
  const key = petalInfoValueKeys[text];
  return key
    ? t("ui.petalInfo.values." + key, {}, { defaultValue: text })
    : text;
}

function buildPetalInfo(petalType, rarity) {
  const rows = [];
  const level = rarityValueLevel(rarity);
  const scale = petalRarityScale(rarity);
  const addRow = (label, value) => {
    if (value === null || value === undefined || value === "") return;
    rows.push({
      label: localizePetalInfoLabel(label),
      value: localizePetalInfoValue(value),
    });
  };
  const addNumber = (label, value, suffix = "") => {
    if (!Number.isFinite(value) || Math.abs(value) <= 0.000001) return;
    addRow(label, `${formatPetalStat(value)}${suffix}`);
  };
  const addSeconds = (label, value) => addNumber(label, value, "s");
  const addPercent = (label, value, options = {}) => {
    if (!Number.isFinite(value) || Math.abs(value) <= 0.000001) return;
    const sign = options.sign && value > 0 ? "+" : "";
    addRow(label, `${sign}${formatPetalStat(value * 100)}%`);
  };
  const addCombat = (damage, health, reload, options = {}) => {
    const copy = Math.max(1, options.copy || 1);
    if (copy > 1) addRow("Petals", `x${copy}`);
    addNumber("Damage", damage / copy);
    addNumber("Durability", health / copy);
    addNumber("Armor", (options.armor || 0) / copy);
    addSeconds("Reload", reload);
    if (options.preload && Math.abs(options.preload - reload) > 0.0001)
      addSeconds("Preload", options.preload);
  };

  switch (petalType) {
    case petalAntennaeType:
      addPercent("Vision", antennaeVisionBonus(rarity), { sign: true });
      break;
    case petalThirdEyeType:
      addNumber("Attack range", thirdEyeReachBonus(rarity), "");
      break;
    case petalGoldenLeafType:
      addPercent("Reload cut", goldenLeafReloadReduction(rarity));
      break;
    case petalDustType:
      addPercent("Reload cut", dustReloadReduction(rarity));
      break;
    case petalBandageType:
      addSeconds("Undead", bandageUndeadDuration(rarity));
      break;
    case petalBrokenEggType:
      addPercent("Summon HP", brokenEggSummonHealthBonus(rarity), {
        sign: true,
      });
      addPercent("Summon DMG", brokenEggSummonDamageBonus(rarity), {
        sign: true,
      });
      break;
    case petalRelicType:
      addPercent("Linked HP", relicHealthBonus(rarity), { sign: true });
      break;
    case petalCorruptionType:
      addRow("Flower body", "Corrupted");
      break;
    case petalBloodSacrificeType:
      addSeconds("Delay", 10);
      break;
    case petalNullificationType:
      addRow("Effect", "Nullify");
      break;
    case petalAirType:
      addRow("Mass", "16");
      break;
    case petalBasicType:
      addCombat(10 * scale, 10 * scale, 2.5);
      break;
    case petalLightType: {
      const copy = lightCopy(rarity);
      addCombat(13 * scale, 5 * scale, 0.8, { copy });
      break;
    }
    case petalLeafType:
      addCombat(16 * scale, 12 * scale, 1.8);
      addNumber("Regen", roseMedicineScale(rarity), "/s");
      break;
    case petalYuccaType:
      addCombat(16 * scale, 12 * scale, 1.8);
      addNumber("Defend regen", 3 * roseMedicineScale(rarity), "/s");
      break;
    case petalWhiteFungusType: {
      const hitBonus = Math.round(2 * rarityValueLevel(rarity));
      addCombat(8 * scale, 10 * scale, 1);
      addRow("Petal hits/tick", `+${hitBonus}`);
      break;
    }
    case petalBlackFungusType: {
      const compressionPower = Math.round(2 * rarityValueLevel(rarity));
      addCombat(8 * scale, 10 * scale, 1);
      addRow("Compression power", `+${compressionPower}`);
      break;
    }
    case petalBroccoliType: {
      const health = Math.round((20 * roseMedicineScale(rarity)) / 10) * 10;
      addCombat(2.5 * scale, health, 5);
      addPercent("Petal HP healing/s", 0.2);
      break;
    }
    case petalRockType:
      addCombat(25 * scale, 30 * scale, 3);
      break;
    case petalWebType:
      addCombat(5 * scale, 5 * scale, 3);
      addSeconds("Throw CD", 0.5);
      addSeconds("Web time", 10);
      addNumber("Web radius", webRadiusValue(rarity));
      break;
    case petalCactusType:
      addCombat(7 * scale, 15 * scale, 1);
      addNumber("Flower HP", 30 * scale);
      break;
    case petalPollenType: {
      const copy = pollenCopy(rarity);
      addCombat(40 * scale, 10 * scale, 2, { copy });
      addSeconds("Throw CD", 0.5);
      break;
    }
    case petalCornType:
      addCombat(8 * scale, 200 * scale, 7);
      break;
    case petalRiceType:
      addCombat(4 * scale, scale, 0.1);
      break;
    case petalBasilType:
      addNumber("Durability", 10 * scale);
      addSeconds("Reload", 2.5);
      addPercent("Healing taken", basilHealingBonus(rarity), { sign: true });
      break;
    case petalSoilType:
      addCombat(10 * scale, 10 * scale, 2.5);
      addNumber("Flower HP", 50 * scale);
      addNumber("Flower radius", 10);
      break;
    case petalHoneyType:
      addNumber("Durability", 50 * scale);
      addSeconds("Reload", 2);
      addSeconds("Throw CD", 0.5);
      addRow("Attracts", `<= ${rarityName(previousDisplayRarity(rarity))}`);
      break;
    case petalWaxType:
      addNumber("Durability", 1000 * scale);
      addSeconds("Reload", 30);
      break;
    case petalOrangeType:
      addCombat(8 * scale, 6.7 * scale, 3.5, { copy: 3 });
      break;
    case petalShovelType:
      addSeconds("Preload", shovelPreload(rarity));
      addSeconds("Burrow", 2);
      break;
    case petalRoseType:
      addCombat(5 * scale, 5 * scale, 3.5, { preload: 1.5 });
      addNumber("Heal", 7.5 * roseMedicineScale(rarity));
      break;
    case petalDahliaType:
      addCombat(1.7 * scale, 1.7 * scale, 1.5, { copy: 3 });
      addNumber("Heal", (1.2 * roseMedicineScale(rarity)) / 3);
      break;
    case petalDandelionType: {
      const copy = petalBaseCopy(petalType, rarity);
      addCombat(8 * scale, 8 * scale, 1, { copy });
      addSeconds("Anti-heal", 30);
      addRow("Healing", "x0.8 per hit");
      break;
    }
    case petalStingerType: {
      const copy = petalBaseCopy(petalType, rarity);
      addCombat(100 * scale, 1, 10, { copy });
      break;
    }
    case petalMissileType:
      addCombat(35 * scale, 2 * scale, 1.5);
      addSeconds("Lifetime", 5);
      break;
    case petalTrapperType:
      addCombat(2.5 * scale, 25 * scale, 4);
      addSeconds("Fire interval", 2);
      addNumber("Trap damage", 40 * scale);
      addNumber("Trap durability", 40 * scale);
      addSeconds("Lifetime", 5);
      break;
    case petalAntEggType:
      addRow("Petals", "x4");
      addNumber("Durability", 1);
      addSeconds("Reload", antEggReload(rarity));
      break;
    case petalBeetleEggType:
      addNumber("Durability", 1);
      addSeconds("Reload", beetleEggReload(rarity));
      break;
    case petalBubbleType:
      addNumber("Durability", 1);
      addSeconds("Reload", bubbleReload(rarity));
      break;
    case petalCarrotType:
      addCombat(18 * scale, 18 * scale, 1);
      break;
    case petalBoneType:
      addCombat(14 * scale, 10 * scale, 2.5, { armor: (20 / 3) * scale });
      break;
    case petalCoinType:
      addCombat(15 * scale, 10 * scale, 2.5);
      break;
    case petalCompassType:
      addCombat(scale, 40 * scale, 2.5);
      addNumber("Magnet", 1024);
      break;
    case petalCogwheelType:
      addCombat(19 * scale, 13 * scale, 2.5);
      break;
    case petalIrisType:
      addCombat(5 * scale, 5 * scale, 4);
      addNumber("Poison", 70);
      addSeconds("Poison time", 3);
      break;
    case petalLentilType:
      addCombat(13 * scale, 12 * scale, 2);
      addNumber("Attract range", 8 * level);
      break;
    case petalMoonType:
      addCombat(3 * scale, 5000 * scale, 100);
      break;
    case petalPincerType:
      addCombat(5 * scale, 5 * scale, 2.5);
      addNumber("Poison", 15 * scale);
      addSeconds("Poison time", 0.75);
      break;
    case petalYinYangType:
      addCombat(10 * scale, 10 * scale, 2);
      break;
    case petalHeavyType:
      addCombat(10 * scale, 150 * scale, 10);
      break;
    case petalFasterType:
      addCombat(12 * scale, 5 * scale, 2.5);
      addNumber("Rotation", 0.3 + 0.2 * level, " rad/s");
      break;
    case petalYggdrasilType:
      addNumber("Durability", 10 * scale);
      addSeconds("Revive", yggdrasilChannelTime(rarity));
      break;
    case petalWingType:
      addCombat(20 * scale, 10 * scale, 3);
      break;
    case petalTriangleType:
      addCombat(6 * scale, 10 * scale, 2);
      addNumber("Tri bonus", triangleBonusDamage(rarity));
      break;
    case petalSawbladeType:
      addCombat(40 * scale, 25 * scale, 2.5);
      break;
    case petalFragmentType: {
      const value = fragmentValue(rarity);
      const copy = petalBaseCopy(petalType, rarity);
      addCombat(value, value, rarity === 10 ? 8 : 10, { copy });
      break;
    }
    case petalGlassType:
      addCombat(22 * scale, 10 * scale, 2.5);
      break;
    case petalMimicType:
      addNumber("Durability", 10);
      addSeconds("Reload", 2.5);
      addRow("Copies", "left petal");
      break;
    default:
      break;
  }

  return { petalType, rarity, rows };
}

function rarityLevel(rarity) {
  const value = Math.max(1, Math.floor(Number(rarity) || 1));
  if (value === rarityExotic) return 1;
  if (value === 10) return 9;
  if (value >= 11) return 10;
  return clamp(value, 1, 9);
}

function rarityValueLevel(rarity) {
  return rarityLevel(rarity);
}

function raritySpecialLevel(rarity) {
  return rarityLevel(rarity);
}

function raritySortRank(rarity) {
  const value = Math.floor(Number(rarity) || 0);
  return raritySortRanks.get(value) || 0;
}

function petalRarityScale(rarity) {
  return 3 ** (rarityLevel(rarity) - 1);
}

function lightCopy(rarity) {
  const level = rarityLevel(rarity);
  if (level <= 1) return 1;
  if (level <= 3) return 2;
  if (level <= 5) return 3;
  return 5;
}

function pollenCopy(rarity) {
  return rarityLevel(rarity) <= 1 ? 1 : rarityLevel(rarity) <= 3 ? 2 : 3;
}

function roseMedicineScale(rarity) {
  const sqrt3 = Math.sqrt(3);
  switch (Math.floor(Number(rarity) || 1)) {
    case 1:
      return 1;
    case 2:
      return 3;
    case 3:
      return 9;
    case 4:
      return 27;
    case 5:
      return 81;
    case 6:
      return 243;
    case 7:
      return 243 * sqrt3;
    case rarityExotic:
      return 1;
    case 8:
      return 243 * 3;
    case 9:
    case 10:
      return 243 * 3 * sqrt3;
    case 11:
      return 243 * 9 * sqrt3;
    default:
      return 1;
  }
}

function antennaeVisionBonus(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 0.111, 0.176, 0.25, 0.333, 0.429, 1, 1.857, 4, 13.5, 13.5, 38],
  );
}

function thirdEyeReachBonus(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 0, 0, 0, 0, 40, 90, 140, 190, 240, 240, 350],
  );
}

function goldenLeafReloadReduction(rarity) {
  return 1 - Math.max(0.05, 1 - raritySpecialLevel(rarity) * 0.033);
}

function dustReloadReduction(rarity) {
  return 1 - Math.max(0.05, 1 - raritySpecialLevel(rarity) * 0.03);
}

function bandageUndeadDuration(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 1.5, 2.1, 2.9, 4.1, 5.8, 8.1, 11.3, 15.8, 22.1, 22.1, 31.3],
  );
}

function brokenEggSummonHealthBonus(rarity) {
  const level = clamp(raritySpecialLevel(rarity), 1, 10);
  if (level <= 8) {
    const t = (level - 1) / 7;
    return 0.9 + (0.25 - 0.9) * t - 1;
  }
  const t = (level - 8) / 2;
  return 0.25 + (0.02 - 0.25) * t - 1;
}

function brokenEggSummonDamageBonus(rarity) {
  const level = clamp(raritySpecialLevel(rarity), 1, 10);
  if (level <= 8) {
    const t = (level - 1) / 7;
    return 1.12 + (7.5 - 1.12) * t - 1;
  }
  const t = (level - 8) / 2;
  return 7.5 + (25.0 - 7.5) * t - 1;
}

function basilHealingBonus(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.75, 1.0, 1.0, 2.0],
  );
}

function relicHealthBonus(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.75, 1.0, 1.5, 1.5, 2.5],
  );
}

function antEggReload(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 24, 8, 9, 10, 12, 16, 24, 60, 180, 180, 480],
  );
}

function beetleEggReload(rarity) {
  return bySpecialRarity(rarity, [0, 12, 4, 4.5, 5, 6, 8, 12, 30, 90, 90, 240]);
}

function bubbleReload(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 1, 0.8, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1, 0.05, 0.05, 0.025],
  );
}

function shovelPreload(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 15, 13.1, 11.8, 10.1, 8.5, 6.9, 5.2, 3.6, 2, 2, 1],
  );
}

function webRadiusValue(rarity) {
  return bySpecialRarity(
    rarity,
    [0, 50, 60, 70, 80, 100, 150, 200, 250, 350, 350, 500],
  );
}

function yggdrasilChannelTime(rarity) {
  if (Math.floor(Number(rarity) || 1) === rarityExotic) return 600;
  if (rarity >= 11) return 0.016;
  if (rarity >= 9) return 0.33;
  if (rarity === 8) return 1;
  const t = (rarityLevel(rarity) - 1) / 6;
  return 600 + (1 - 600) * clamp(t, 0, 1);
}

function triangleBonusDamage(rarity) {
  return 3.75 * petalRarityScale(rarity);
}

function fragmentValue(rarity) {
  switch (Math.floor(Number(rarity) || 1)) {
    case 7:
      return 364.5;
    case rarityExotic:
      return petalRarityScale(1);
    case 8:
      return 729;
    case 9:
      return 2187;
    case 10:
      return 328000;
    case 11:
      return 6561;
    default:
      return petalRarityScale(rarity);
  }
}

function previousDisplayRarity(rarity) {
  const value = Math.floor(Number(rarity) || 1);
  if (value === rarityExotic) return 1;
  const index = rarityDisplayOrder.indexOf(value);
  if (index > 0) return rarityDisplayOrder[index - 1];
  return Math.max(1, value - 1);
}

function byRarity(rarity, values) {
  return byRarityWeighted(rarity, values);
}

function bySpecialRarity(rarity, values) {
  return byRarityWeighted(rarity, values);
}

function byRarityWeighted(rarity, values) {
  if (Math.floor(Number(rarity) || 1) === rarityExotic) {
    return values[1] ?? 0;
  }
  const index = clamp(Math.floor(Number(rarity) || 1), 1, values.length - 1);
  return values[index] ?? values[1] ?? 0;
}

function formatPetalStat(value) {
  const abs = Math.abs(value);
  if (abs >= 1000000) return `${formatCompact(value / 1000000)}m`;
  if (abs >= 10000) return `${formatCompact(value / 1000)}k`;
  if (abs >= 100) return String(Math.round(value));
  if (abs >= 10) return trimNumber(value, 1);
  if (abs >= 1) return trimNumber(value, 2);
  return trimNumber(value, 3);
}

function trimNumber(value, digits) {
  return Number(value.toFixed(digits)).toString();
}

function petalSingleRadiusFactor(petalType) {
  switch (petalType) {
    case petalDustType:
      return 0.24;
    case petalAntEggType:
      return 0.36;
    case petalDahliaType:
      return 0.3;
    case petalStingerType:
      return 0.38;
    default:
      return 0.42;
  }
}

function petalCopyVisualScale(visualCopies, petalType) {
  if (visualCopies <= 2) return 0.86;
  if (petalType === petalAntEggType) return 0.82;
  if (petalType === petalDustType) return 0.9;
  if (petalType === petalDahliaType) return 0.78;
  if (visualCopies <= 4) return 0.76;
  if (visualCopies <= 6) return 0.66;
  return 0.6;
}

function petalCopyChordFactor(visualCopies, petalType) {
  if (petalType === petalAntEggType) return 0.46;
  if (petalType === petalDustType) return 0.92;
  if (petalType === petalDahliaType) return 0.63;
  if (petalType === petalStingerType) return 0.66;
  if (visualCopies <= 2) return 0.82;
  if (visualCopies <= 4) return 0.72;
  return 0.68;
}

function petalCopyMaxRadius(visualCopies, petalType, singleRadius) {
  if (petalType === petalDustType) return 6.4;
  if (petalType === petalAntEggType) return singleRadius * 0.82;
  if (visualCopies <= 2) return 8.5;
  if (visualCopies <= 4) return 8.8;
  return 10.5;
}

function petalFixedCopyRadius(petalType) {
  switch (petalType) {
    case petalDustType:
      return 4.2;
    case petalDahliaType:
      return 5.0;
    case petalLightType:
      return 5.4;
    default:
      return 0;
  }
}

function petalCopyLayout(copy, petalType = 0) {
  const visualCopies = Math.max(1, Math.min(8, Math.floor(copy || 1)));
  if (visualCopies === 1) return [{ x: 0, y: 0, angle: 0, scale: 1 }];

  const scale = petalCopyVisualScale(visualCopies, petalType);
  const fixedRadius = petalFixedCopyRadius(petalType);
  let radius = fixedRadius;
  if (radius <= 0) {
    const singleRadius =
      50 * petalCardIconScale * scale * petalSingleRadiusFactor(petalType);
    const chord =
      singleRadius * 2 * petalCopyChordFactor(visualCopies, petalType);
    const ringRadius = chord / (2 * Math.sin(Math.PI / visualCopies));
    const copyGrowth = 1 + Math.max(0, visualCopies - 3) * 0.035;
    const minRadius = petalType === petalDustType ? 2.4 : 2.8;
    radius = clamp(
      ringRadius * copyGrowth,
      minRadius,
      petalCopyMaxRadius(visualCopies, petalType, singleRadius),
    );
  }
  const start = 0;
  const points = [];
  for (let i = 0; i < visualCopies; i += 1) {
    const angle = start + (Math.PI * 2 * i) / visualCopies;
    points.push({
      x: Math.cos(angle) * radius,
      y: Math.sin(angle) * radius,
      angle: angle + Math.PI * 0.5,
      scale,
    });
  }
  return points;
}

function makePetalStack(
  petalType,
  rarity,
  count = 0,
  iconPetalType = petalType,
  options = {},
) {
  const stack = document.createElement("span");
  stack.className = "petal-stack";
  const mimicVisual =
    petalType === petalMimicType && iconPetalType !== petalType;
  const copy =
    options.copy ?? effectivePetalCardCopy(iconPetalType, rarity, options);
  const splitLabel = copy > 1;
  if (mimicVisual) stack.classList.add("mimic-visual");
  if (copy > 1) stack.classList.add("multi-copy");

  const base = document.createElement("img");
  base.className = "petal-bg";
  base.alt = "";
  base.draggable = false;
  base.src = petalBasePath(rarity);
  stack.appendChild(base);

  const layout = petalCopyLayout(copy, iconPetalType);
  for (const part of layout) {
    const icon = makePetalIcon(iconPetalType, rarity, {
      stripped: mimicVisual || splitLabel,
    });
    if (!icon) continue;
    if (layout.length > 1) {
      icon.classList.add("petal-icon-copy");
      icon.style.setProperty("--copy-x", `${part.x * 0.54}px`);
      icon.style.setProperty("--copy-y", `${part.y * 0.54}px`);
      icon.style.setProperty("--copy-angle", `${part.angle}rad`);
      icon.style.setProperty("--copy-size", `${Math.round(part.scale * 100)}%`);
    }
    stack.appendChild(icon);
  }
  if (mimicVisual) {
    const label = makeMimicLabelIcon();
    if (label) stack.appendChild(label);
  } else if (splitLabel) {
    const label = makePetalLabelIcon(iconPetalType, rarity);
    if (label) stack.appendChild(label);
  }
  if (count > 1) {
    const badge = document.createElement("span");
    badge.className = "petal-count";
    badge.textContent = `x${formatShortCount(count)}`;
    stack.appendChild(badge);
  }
  return stack;
}

function makePetalLabelIcon(petalType, rarity = 0) {
  const src = petalIconPath(petalType, rarity);
  if (!src) return null;
  const image = document.createElement("img");
  image.className = "petal-card-label";
  image.alt = "";
  image.draggable = false;
  setPetalLabelIconSource(image, src);
  image.addEventListener("error", () => image.remove(), { once: true });
  return image;
}

function setPetalLabelIconSource(image, src) {
  let entry = petalLabelIconUrls.get(src);
  if (entry && entry.url) {
    image.src = entry.url;
    return;
  }

  if (!entry) {
    entry = { url: "", waiters: [] };
    petalLabelIconUrls.set(src, entry);
    fetch(src)
      .then((response) => {
        if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
        return response.text();
      })
      .then((svgText) => {
        const blob = new Blob([extractPetalLabelSvg(svgText)], {
          type: "image/svg+xml",
        });
        entry.url = URL.createObjectURL(blob);
        for (const waiter of entry.waiters) waiter.src = entry.url;
        entry.waiters = [];
      })
      .catch(() => {
        entry.url = "";
        for (const waiter of entry.waiters) waiter.remove();
        entry.waiters = [];
      });
  }

  entry.waiters.push(image);
}

function formatShortCount(value) {
  const count = Math.max(
    0,
    Math.min(1000000000, Math.floor(Number(value) || 0)),
  );
  if (count >= 1000000000) return "1b";
  if (count >= 1000000) return `${formatCompact(count / 1000000)}m`;
  if (count >= 1000) return `${formatCompact(count / 1000)}k`;
  return String(count);
}

function formatCompact(value) {
  if (value >= 100) return String(Math.floor(value));
  if (value >= 10)
    return (Math.floor(value * 10) / 10).toFixed(1).replace(/\.0$/, "");
  return (Math.floor(value * 10) / 10).toFixed(1).replace(/\.0$/, "");
}

function makePetalIcon(petalType, rarity = 0, options = {}) {
  const src = petalIconPath(petalType, rarity);
  if (!src) return null;
  const image = document.createElement("img");
  image.className = "petal-icon";
  image.alt = "";
  image.draggable = false;
  if (options.stripped) setStrippedPetalIconSource(image, src);
  else image.src = src;
  image.addEventListener("error", () => image.remove(), { once: true });
  return image;
}

function setStrippedPetalIconSource(image, src) {
  let entry = strippedPetalIconUrls.get(src);
  if (entry && entry.url) {
    image.src = entry.url;
    return;
  }

  if (!entry) {
    entry = { url: "", waiters: [] };
    strippedPetalIconUrls.set(src, entry);
    fetch(src)
      .then((response) => {
        if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
        return response.text();
      })
      .then((svgText) => {
        const blob = new Blob([stripLivePetalLabel(svgText)], {
          type: "image/svg+xml",
        });
        entry.url = URL.createObjectURL(blob);
        for (const waiter of entry.waiters) waiter.src = entry.url;
        entry.waiters = [];
      })
      .catch(() => {
        entry.url = src;
        for (const waiter of entry.waiters) waiter.src = src;
        entry.waiters = [];
      });
  }

  entry.waiters.push(image);
}

function makeMimicLabelIcon() {
  const src = petalIconPath(petalMimicType);
  if (!src) return null;
  const image = document.createElement("img");
  image.className = "petal-mimic-label";
  image.alt = "";
  image.draggable = false;
  setMimicLabelIconSource(image, src);
  image.addEventListener("error", () => image.remove(), { once: true });
  return image;
}

function setMimicLabelIconSource(image, src) {
  let entry = mimicLabelIconUrls.get(src);
  if (entry && entry.url) {
    image.src = entry.url;
    return;
  }

  if (!entry) {
    entry = { url: "", waiters: [] };
    mimicLabelIconUrls.set(src, entry);
    fetch(src)
      .then((response) => {
        if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
        return response.text();
      })
      .then((svgText) => {
        const blob = new Blob([extractMimicLabelSvg(svgText)], {
          type: "image/svg+xml",
        });
        entry.url = URL.createObjectURL(blob);
        for (const waiter of entry.waiters) waiter.src = entry.url;
        entry.waiters = [];
      })
      .catch(() => {
        entry.url = "";
        for (const waiter of entry.waiters) waiter.remove();
        entry.waiters = [];
      });
  }

  entry.waiters.push(image);
}

function rarityBaseIndex(rarity) {
  return clamp((rarity || 1) - 1, 0, 10);
}

function petalBasePath(rarity) {
  return `./assets/petals/0_${rarityBaseIndex(rarity)}.svg`;
}

function petalIconPath(petalType, rarity = 0, options = {}) {
  if (
    !options.live &&
    petalType === petalStingerType &&
    rarityLevel(rarity) >= rarityLevel(stingerSplitIconMinRarity)
  )
    return "./assets/petals/stinger_split.svg";
  if (
    petalType === petalCompassType &&
    rarityLevel(rarity) >= rarityLevel(compassUltraIconMinRarity)
  )
    return "./assets/petals/compass_ultra.svg";

  const iconId = PetalIconIds[petalType] || 0;
  if (!iconId) return "";
  return `./assets/petals/${iconId}.svg`;
}

function normalizePetalName(name) {
  return String(name || "")
    .toLowerCase()
    .replace(/[^a-z0-9]/g, "");
}

function petalTypeFromSnap(snap) {
  const fromEntity = petalTypeFromEntity(snap.entityType);
  if (fromEntity > 0 && fromEntity < PetalIconIds.length) return fromEntity;

  const fromName = PetalNameToType.get(normalizePetalName(snap.name));
  return fromName || fromEntity;
}

function assetImage(src) {
  if (!src) return null;
  let image = assetImages.get(src);
  if (!image) {
    image = new Image();
    image.decoding = "async";
    image.addEventListener("load", () => requestAnimationFrame(drawScene), {
      once: true,
    });
    image.addEventListener(
      "error",
      () => {
        image.failed = true;
        requestAnimationFrame(drawScene);
      },
      { once: true },
    );
    image.src = src;
    assetImages.set(src, image);
  }
  return image;
}

function imageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

function slotsHavePetal(slots, petalType, rarity = null) {
  return (slots || []).some(
    (slot) =>
      slotHasItem(slot) &&
      slot.petalType === petalType &&
      (rarity === null || slot.rarity === rarity),
  );
}

function ownerHasPetal(petalType, rarity = null) {
  return slotsHavePetal(displayOwnerSlots(), petalType, rarity);
}

function ownerCollidesWithWalls() {
  return !slotsHavePetal(
    state.ownerSlots,
    petalNullificationType,
    rarityPrimordial,
  );
}

function resolveOwnerPredictedMotion(start, end, radius) {
  if (!ownerCollidesWithWalls()) {
    return { pos: { x: end.x, y: end.y }, normals: [] };
  }
  return mapRenderer.resolvePredictedCircleMotion(start, end, radius);
}

function playerSnapHasPetal(snap, petalType) {
  return (snap.primarySlots || []).some(
    (slot) => slotHasItem(slot) && slot.petalType === petalType,
  );
}

function equipFromInventory(item) {
  const slots = displayOwnerSlots();
  const index = clamp(state.selectedSlot, 0, slots.length - 1);
  equipFromInventoryAt(index, item);
}

function equipFromInventoryAt(index, item) {
  if (!state.authenticated || !slotHasItem(item)) return;
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  let inventory = displayInventory();
  if (index < 0 || index >= ownerSlots.length) return;
  if (getDisplayInventoryCount(item.petalType, item.rarity, inventory) <= 0)
    return;
  const oldSlot = copySlot(ownerSlots[index]);
  sendBytes(packEquip(index, item.petalType, item.rarity));
  ownerSlots[index] = copySlot(item);
  inventory = applyInventoryDelta(inventory, item.petalType, item.rarity, -1);
  if (slotHasItem(oldSlot))
    inventory = applyInventoryDelta(
      inventory,
      oldSlot.petalType,
      oldSlot.rarity,
      1,
    );
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [
    item,
    oldSlot,
  ]);
  renderInventoryPanel();
}

function unequipSlot(index) {
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  let inventory = displayInventory();
  if (
    !state.authenticated ||
    !ownerSlots[index] ||
    !slotHasItem(ownerSlots[index])
  )
    return;
  const oldSlot = copySlot(ownerSlots[index]);
  sendBytes(packUnequip(index));
  ownerSlots[index] = emptySlot();
  inventory = applyInventoryDelta(
    inventory,
    oldSlot.petalType,
    oldSlot.rarity,
    1,
  );
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [oldSlot]);
  renderInventoryPanel();
}

function setSecondaryFromInventory(index, item) {
  if (!state.authenticated || !slotHasItem(item)) return;
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  let inventory = displayInventory();
  if (index < 0 || index >= ownerSlots.length) return;
  if (getDisplayInventoryCount(item.petalType, item.rarity, inventory) <= 0)
    return;
  const oldSlot = copySlot(secondarySlots[index]);
  sendBytes(packSecondarySlot(index, item.petalType, item.rarity));
  secondarySlots[index] = copySlot(item);
  inventory = applyInventoryDelta(inventory, item.petalType, item.rarity, -1);
  if (slotHasItem(oldSlot))
    inventory = applyInventoryDelta(
      inventory,
      oldSlot.petalType,
      oldSlot.rarity,
      1,
    );
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [
    item,
    oldSlot,
  ]);
  renderInventoryPanel();
}

function clearSecondarySlot(index) {
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  let inventory = displayInventory();
  if (
    !state.authenticated ||
    !secondarySlots[index] ||
    !slotHasItem(secondarySlots[index])
  )
    return;
  const oldSlot = copySlot(secondarySlots[index]);
  sendBytes(packSecondarySlot(index, 0, 0));
  secondarySlots[index] = emptySlot();
  inventory = applyInventoryDelta(
    inventory,
    oldSlot.petalType,
    oldSlot.rarity,
    1,
  );
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [oldSlot]);
  renderInventoryPanel();
}

function quickSwapSlot(index) {
  const display = makeDisplaySlotState();
  const ownerSlots = display.ownerSlots;
  const secondarySlots = display.secondarySlots;
  const inventory = display.inventory;
  if (index < 0 || index >= ownerSlots.length) return;
  if (!state.authenticated) return;

  const oldPrimary = copySlot(ownerSlots[index]);
  const oldSecondary = copySlot(secondarySlots[index]);
  const hasPrimary = slotHasItem(oldPrimary);
  const hasSecondary = slotHasItem(oldSecondary);
  state.selectedSlot = index;
  if (!hasPrimary && !hasSecondary) {
    renderInventoryPanel();
    return;
  }
  if (hasPrimary && !hasSecondary) {
    sendPacketBatch(
      packUnequip(index),
      packSecondarySlot(index, oldPrimary.petalType, oldPrimary.rarity),
    );
    ownerSlots[index] = emptySlot();
    secondarySlots[index] = oldPrimary;
    enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [oldPrimary]);
    renderInventoryPanel();
    return;
  }
  if (!hasPrimary && hasSecondary) {
    sendPacketBatch(
      packSecondarySlot(index, 0, 0),
      packEquip(index, oldSecondary.petalType, oldSecondary.rarity),
    );
    ownerSlots[index] = oldSecondary;
    secondarySlots[index] = emptySlot();
    enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [
      oldSecondary,
    ]);
    renderInventoryPanel();
    return;
  }

  sendPacketBatch(
    packSecondarySlot(index, 0, 0),
    packEquip(index, oldSecondary.petalType, oldSecondary.rarity),
    packSecondarySlot(index, oldPrimary.petalType, oldPrimary.rarity),
  );

  ownerSlots[index] = oldSecondary;
  secondarySlots[index] = oldPrimary;
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [
    oldPrimary,
    oldSecondary,
  ]);
  renderInventoryPanel();
}

function quickSwapAllSlots() {
  if (!state.authenticated) return;
  const display = makeDisplaySlotState();
  const slotCount = display.ownerSlots.length;
  if (slotCount <= 0) return;

  const desiredOwnerSlots = copySlots(display.secondarySlots, slotCount);
  const desiredSecondarySlots = copySlots(display.ownerSlots, slotCount);
  if (
    slotArraysEqual(display.ownerSlots, desiredOwnerSlots) &&
    slotArraysEqual(display.secondarySlots, desiredSecondarySlots)
  ) {
    return;
  }

  const packets = [];
  for (let index = 0; index < slotCount; index += 1) {
    if (slotHasItem(display.ownerSlots[index]))
      packets.push(packUnequip(index));
  }
  for (let index = 0; index < slotCount; index += 1) {
    if (slotHasItem(display.secondarySlots[index]))
      packets.push(packSecondarySlot(index, 0, 0));
  }
  for (let index = 0; index < slotCount; index += 1) {
    const slot = desiredOwnerSlots[index];
    if (slotHasItem(slot))
      packets.push(packEquip(index, slot.petalType, slot.rarity));
  }
  for (let index = 0; index < slotCount; index += 1) {
    const slot = desiredSecondarySlots[index];
    if (slotHasItem(slot))
      packets.push(packSecondarySlot(index, slot.petalType, slot.rarity));
  }

  if (!sendPacketBatch(...packets)) return;

  enqueueSlotTransaction(
    desiredOwnerSlots,
    desiredSecondarySlots,
    display.inventory,
    [...display.ownerSlots, ...display.secondarySlots],
  );
  renderInventoryPanel();
}

function saveActiveLoadoutPreset() {
  if (!state.authenticated || !state.ownerStateLoaded) {
    addConsoleLine(t("commands.loadout.notReady"), "error");
    return false;
  }

  const index = clamp(
    Number(state.activeLoadoutPreset) || 0,
    0,
    loadoutPresetCount - 1,
  );
  const display = makeDisplaySlotState();
  state.loadoutPresets[index] = {
    primarySlots: display.ownerSlots.map(copySlot),
    secondarySlots: display.secondarySlots.map(copySlot),
  };
  saveSettings();
  addConsoleLine(t("commands.loadout.saved", { index: index + 1 }));
  return true;
}

function applyLoadoutPreset(index) {
  if (
    !state.authenticated ||
    !state.ownerStateLoaded ||
    !state.inventoryLoaded
  ) {
    addConsoleLine(t("commands.loadout.notReady"), "error");
    return false;
  }

  const display = makeDisplaySlotState();
  const slotCount = display.ownerSlots.length;
  const preset = state.loadoutPresets[index] || {};
  const presetOwnerSlots = copySlots(preset.primarySlots || [], slotCount);
  const presetSecondarySlots = copySlots(
    preset.secondarySlots || [],
    slotCount,
  );

  let inventory = copyInventoryItems(display.inventory);
  const touchedItems = [];
  for (const slot of [...display.ownerSlots, ...display.secondarySlots]) {
    if (!slotHasItem(slot)) continue;
    inventory = applyInventoryDelta(
      inventory,
      slot.petalType,
      slot.rarity,
      1,
    );
    touchedItems.push(slot);
  }

  const resolvePresetSlot = (slot) => {
    if (!slotHasItem(slot)) return emptySlot();
    for (let rarity = slot.rarity; rarity >= 1; rarity -= 1) {
      if (getDisplayInventoryCount(slot.petalType, rarity, inventory) <= 0)
        continue;
      const resolved = { petalType: slot.petalType, rarity };
      inventory = applyInventoryDelta(
        inventory,
        resolved.petalType,
        resolved.rarity,
        -1,
      );
      touchedItems.push(resolved);
      return resolved;
    }
    return emptySlot();
  };
  const desiredOwnerSlots = presetOwnerSlots.map(resolvePresetSlot);
  const desiredSecondarySlots = presetSecondarySlots.map(resolvePresetSlot);

  if (
    slotArraysEqual(display.ownerSlots, desiredOwnerSlots) &&
    slotArraysEqual(display.secondarySlots, desiredSecondarySlots)
  ) {
    addConsoleLine(t("commands.loadout.applied", { index: index + 1 }));
    return true;
  }

  const packets = [];
  for (let slotIndex = 0; slotIndex < slotCount; slotIndex += 1) {
    if (slotHasItem(display.ownerSlots[slotIndex]))
      packets.push(packUnequip(slotIndex));
  }
  for (let slotIndex = 0; slotIndex < slotCount; slotIndex += 1) {
    if (slotHasItem(display.secondarySlots[slotIndex]))
      packets.push(packSecondarySlot(slotIndex, 0, 0));
  }
  for (let slotIndex = 0; slotIndex < slotCount; slotIndex += 1) {
    const slot = desiredOwnerSlots[slotIndex];
    if (slotHasItem(slot))
      packets.push(packEquip(slotIndex, slot.petalType, slot.rarity));
  }
  for (let slotIndex = 0; slotIndex < slotCount; slotIndex += 1) {
    const slot = desiredSecondarySlots[slotIndex];
    if (slotHasItem(slot))
      packets.push(
        packSecondarySlot(slotIndex, slot.petalType, slot.rarity),
      );
  }

  if (!sendPacketBatch(...packets)) {
    addConsoleLine(
      t("commands.loadout.applyFailed", { index: index + 1 }),
      "error",
    );
    return false;
  }

  enqueueSlotTransaction(
    desiredOwnerSlots,
    desiredSecondarySlots,
    inventory,
    touchedItems,
  );
  renderInventoryPanel();
  addConsoleLine(t("commands.loadout.applied", { index: index + 1 }));
  return true;
}

function selectLoadoutPreset(index) {
  if (index < 0 || index >= loadoutPresetCount) return false;
  state.activeLoadoutPreset = index;
  saveSettings();
  if (!state.authenticated) {
    addConsoleLine(t("commands.loadout.selected", { index: index + 1 }));
    return true;
  }
  return applyLoadoutPreset(index);
}

function getDisplayInventoryCount(
  petalType,
  rarity,
  items = displayInventory(),
) {
  const item = items.find(
    (entry) => entry.petalType === petalType && entry.rarity === rarity,
  );
  return item ? item.count || 0 : 0;
}

function movePrimaryToPrimary(fromIndex, toIndex) {
  if (fromIndex === toIndex) return;
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  const inventory = displayInventory();
  if (!state.authenticated || !slotHasItem(ownerSlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= ownerSlots.length) return;

  const source = copySlot(ownerSlots[fromIndex]);
  const target = copySlot(ownerSlots[toIndex]);
  sendPacketBatch(
    packUnequip(fromIndex),
    slotHasItem(target) ? packUnequip(toIndex) : null,
    packEquip(toIndex, source.petalType, source.rarity),
    slotHasItem(target)
      ? packEquip(fromIndex, target.petalType, target.rarity)
      : null,
  );

  ownerSlots[toIndex] = source;
  ownerSlots[fromIndex] = target;
  state.selectedSlot = toIndex;
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [
    source,
    target,
  ]);
  renderInventoryPanel();
}

function movePrimaryToSecondary(fromIndex, toIndex) {
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  const inventory = displayInventory();
  if (!state.authenticated || !slotHasItem(ownerSlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= ownerSlots.length) return;

  const source = copySlot(ownerSlots[fromIndex]);
  const oldSecondary = copySlot(secondarySlots[toIndex]);
  sendPacketBatch(
    packUnequip(fromIndex),
    packSecondarySlot(toIndex, source.petalType, source.rarity),
    slotHasItem(oldSecondary)
      ? packEquip(fromIndex, oldSecondary.petalType, oldSecondary.rarity)
      : null,
  );

  ownerSlots[fromIndex] = oldSecondary;
  secondarySlots[toIndex] = source;
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [
    source,
    oldSecondary,
  ]);
  renderInventoryPanel();
}

function moveSecondaryToPrimary(fromIndex, toIndex) {
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  const inventory = displayInventory();
  if (!state.authenticated || !slotHasItem(secondarySlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= ownerSlots.length) return;

  const source = copySlot(secondarySlots[fromIndex]);
  const oldPrimary = copySlot(ownerSlots[toIndex]);
  sendPacketBatch(
    packSecondarySlot(fromIndex, 0, 0),
    packEquip(toIndex, source.petalType, source.rarity),
    slotHasItem(oldPrimary)
      ? packSecondarySlot(fromIndex, oldPrimary.petalType, oldPrimary.rarity)
      : null,
  );

  ownerSlots[toIndex] = source;
  secondarySlots[fromIndex] = oldPrimary;
  state.selectedSlot = toIndex;
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [
    source,
    oldPrimary,
  ]);
  renderInventoryPanel();
}

function moveSecondaryToSecondary(fromIndex, toIndex) {
  if (fromIndex === toIndex) return;
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  const inventory = displayInventory();
  if (!state.authenticated || !slotHasItem(secondarySlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= ownerSlots.length) return;

  const source = copySlot(secondarySlots[fromIndex]);
  const target = copySlot(secondarySlots[toIndex]);
  sendPacketBatch(
    packSecondarySlot(fromIndex, 0, 0),
    packSecondarySlot(toIndex, source.petalType, source.rarity),
    slotHasItem(target)
      ? packSecondarySlot(fromIndex, target.petalType, target.rarity)
      : null,
  );

  secondarySlots[fromIndex] = target;
  secondarySlots[toIndex] = source;
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [
    source,
    target,
  ]);
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
  const target =
    element && element.closest ? element.closest("[data-drop-kind]") : null;
  if (!target) return null;
  const kind = target.dataset.dropKind;
  if (kind !== "primary" && kind !== "secondary" && kind !== "backpack")
    return null;
  return {
    element: target,
    kind,
    index: Number(target.dataset.dropIndex ?? -1),
  };
}

function highlightDropTarget(target) {
  const drag = state.drag;
  const oldTarget = drag && drag.targetElement;
  if (oldTarget && (!target || target.element !== oldTarget))
    oldTarget.classList.remove("drag-target");
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
    if (target.kind === "primary")
      equipFromInventoryAt(target.index, drag.item);
    else if (target.kind === "secondary")
      setSecondaryFromInventory(target.index, drag.item);
    return;
  }

  if (drag.source === "primary") {
    if (target.kind === "primary")
      movePrimaryToPrimary(drag.index, target.index);
    else if (target.kind === "secondary")
      movePrimaryToSecondary(drag.index, target.index);
    return;
  }

  if (drag.source === "secondary") {
    if (target.kind === "primary")
      moveSecondaryToPrimary(drag.index, target.index);
    else if (target.kind === "secondary")
      moveSecondaryToSecondary(drag.index, target.index);
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
  updateParticles(dt);
  const owner = state.entities.get(state.ownerEntityId);
  if (owner && !owner.dying) {
    updateEntityMotion(owner, dt);
    stepOwnerPrediction(owner, dt);
    if (owner.renderPos) {
      state.camera.x = owner.renderPos.x;
      state.camera.y = owner.renderPos.y;
    }
  }

  const dead = [];
  for (const [id, entity] of state.entities) {
    if (entity.hurtFlashAge < hurtFlashDuration)
      entity.hurtFlashAge = Math.min(
        hurtFlashDuration,
        entity.hurtFlashAge + dt,
      );

    if (entity.dying) {
      entity.deathAge = (entity.deathAge || 0) + dt;
      if (entity.deathAge >= deathFadeDuration) dead.push(id);
      continue;
    }
    updateEntityMotion(entity, dt);
    if (id === state.ownerEntityId) continue;
    stepEntityRenderToSnapshot(entity, dt, false);
  }

  lockTrapperRenderPositionsToMounts();

  for (const id of dead) {
    state.entities.delete(id);
    clearLadybugPattern(id);
  }
}

function lockTrapperRenderPositionsToMounts() {
  const trappers = [];
  const mounts = [];

  for (const entity of state.entities.values()) {
    const snap = entity?.snapshot;
    if (!snap?.pos || entity.dying) continue;

    if (
      isPetalEntity(snap.entityType) &&
      petalTypeFromEntity(snap.entityType) === petalTrapperType
    ) {
      trappers.push(entity);
    } else if (
      snap.entityType === playerFlowerType ||
      (isPetalEntity(snap.entityType) &&
        petalTypeFromEntity(snap.entityType) === petalMoonType)
    ) {
      mounts.push(entity);
    }
  }

  for (const trapper of trappers) {
    const mount = findTrapperMountEntity(trapper, mounts);
    if (!mount?.renderPos) {
      trapper.trapperMountEntityId = null;
      continue;
    }

    const trapperSnap = trapper.snapshot;
    const mountSnap = mount.snapshot;
    trapper.trapperMountEntityId = mountSnap.entityId;
    trapper.renderPos.x =
      mount.renderPos.x + (trapperSnap.pos.x - mountSnap.pos.x);
    trapper.renderPos.y =
      mount.renderPos.y + (trapperSnap.pos.y - mountSnap.pos.y);
  }
}

function findTrapperMountEntity(trapper, mounts) {
  let best = null;
  let bestScore = Number.POSITIVE_INFINITY;
  const previousMountId = trapper.trapperMountEntityId;

  for (const mount of mounts) {
    const score = trapperMountScore(trapper, mount);
    if (!Number.isFinite(score)) continue;

    const stickyScore =
      mount.snapshot.entityId === previousMountId ? score * 0.75 : score;
    if (stickyScore >= bestScore) continue;
    best = mount;
    bestScore = stickyScore;
  }

  return best;
}

function trapperMountScore(trapper, mount) {
  const trapperSnap = trapper?.snapshot;
  const mountSnap = mount?.snapshot;
  if (!trapperSnap?.pos || !mountSnap?.pos || trapper === mount)
    return Number.POSITIVE_INFINITY;
  if (trapperSnap.team !== mountSnap.team) return Number.POSITIVE_INFINITY;

  const distance = Math.hypot(
    trapperSnap.pos.x - mountSnap.pos.x,
    trapperSnap.pos.y - mountSnap.pos.y,
  );
  let score = Math.abs(distance - Math.max(0, mountSnap.radius || 0));

  if (
    mountSnap.entityType === playerFlowerType &&
    playerSnapHasPetal(mountSnap, petalTrapperType)
  )
    score *= 0.5;
  return score;
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

function normalizeAngle(angle) {
  const full = Math.PI * 2;
  let value = angle % full;
  if (value < 0) value += full;
  return value;
}

function lerpAngle(start, end, amount) {
  const full = Math.PI * 2;
  const t = clamp(amount, 0, 1);
  let from = normalizeAngle(start);
  let to = normalizeAngle(end);
  if (Math.abs(to - from) > Math.PI) {
    if (to > from) from += full;
    else to += full;
  }
  return normalizeAngle(from + (to - from) * t);
}

function screenViewRadius() {
  const shortSide = Math.max(
    1,
    Math.min(state.canvasWidth, state.canvasHeight),
  );
  return Math.max(96, shortSide * viewScreenFill - viewScreenPadding);
}

function scaleForViewRadius(viewRadius) {
  if (!viewRadius || viewRadius <= 0) return 1;
  return screenViewRadius() / viewRadius;
}

function worldScale() {
  return scaleForViewRadius(state.viewRadius);
}

function worldLengthToScreen(value) {
  return value * worldScale();
}

function currentScreenShake() {
  const shake = state.screenShake || { x: 0, y: 0 };
  return {
    x: Number.isFinite(shake.x) ? shake.x : 0,
    y: Number.isFinite(shake.y) ? shake.y : 0,
  };
}

function worldToScreen(pos) {
  const scale = worldScale();
  const shake = currentScreenShake();
  return {
    x: (pos.x - state.camera.x) * scale + state.canvasWidth * 0.5 + shake.x,
    y: (pos.y - state.camera.y) * scale + state.canvasHeight * 0.5 + shake.y,
  };
}

function screenToWorld(pos) {
  const scale = worldScale();
  const shake = currentScreenShake();
  return {
    x: (pos.x - state.canvasWidth * 0.5 - shake.x) / scale + state.camera.x,
    y: (pos.y - state.canvasHeight * 0.5 - shake.y) / scale + state.camera.y,
  };
}

function debugNumber(value, digits = 1) {
  return Number.isFinite(value) ? value.toFixed(digits) : "-";
}

function updateDebugInfo() {
  if (!debugInfo) return;
  if (!state.consoleOpen) {
    debugInfo.textContent = "";
    return;
  }

  const owner = state.entities.get(state.ownerEntityId);
  const ownerPos = owner?.renderPos || owner?.snapshot?.pos || null;
  const ownerSnap = owner?.snapshot || null;
  const mouseWorld = state.mouse.seen ? screenToWorld(state.mouse) : null;
  const map = state.map;
  const chunkCache = map?.chunkCache?.size || 0;
  const chunkQueue = map?.chunkBuildQueue?.length || 0;
  const visibleChunks = map?.visibleChunkKeys?.size || 0;
  const chunkProfile = map?.chunkProfileLabel || "-";
  const inputMode = state.keyboardControl ? "keyboard" : "mouse";
  const socketState = state.ws
    ? ["connecting", "open", "closing", "closed"][state.ws.readyState] ||
      state.ws.readyState
    : "none";
  const prediction = state.predictionDebug || {};

  debugInfo.textContent = [
    `pos (${debugNumber(ownerPos?.x)}, ${debugNumber(ownerPos?.y)})  id ${state.ownerEntityId || "-"}  hp ${debugNumber(ownerSnap?.hpPercent * 100, 0)}%`,
    `cam (${debugNumber(state.camera.x)}, ${debugNumber(state.camera.y)})  mouse ${mouseWorld ? `(${debugNumber(mouseWorld.x)}, ${debugNumber(mouseWorld.y)})` : "(-, -)"}`,
    `view ${debugNumber(state.viewRadius, 0)}  scale ${debugNumber(worldScale(), 4)}  fps ${debugNumber(state.fps, 1)}  dpr ${debugNumber(state.dpr, 2)}  pred err ${debugNumber(prediction.errorDistance)} mmt ${debugNumber(prediction.speedScale, 2)} tick ${debugNumber(prediction.tickScale, 2)} pkt ${debugNumber((prediction.snapshotInterval || 0) * 1000, 0)}ms stale ${debugNumber(prediction.staleTime, 2)}${prediction.snapped ? " snap" : ""}`,
    `entities ${state.entities.size}/${currentVisibleEntityCount}  render load ${currentRenderLoad}  map ${map?.path || "-"}  chunks ${chunkCache}/${visibleChunks} q${chunkQueue} ${chunkProfile}  input ${inputMode}  ws ${socketState}`,
  ].join("\n");
}

function drawGrid() {
  mapRenderer.drawGrid();
}

function drawPixelEntity(snap, pos, radius, isOwner, isPetal, isDrop) {
  const size = Math.max(1, Math.min(2, Math.round(radius * 2)));
  const x = Math.round(pos.x - size * 0.5);
  const y = Math.round(pos.y - size * 0.5);
  const alpha = clamp(radius / entityDetailMinScreenRadius, 0.18, 0.72);

  ctx.save();
  ctx.globalAlpha *= alpha;
  if (isOwner) ctx.fillStyle = "#ffd446";
  else if (isPetal || isDrop)
    ctx.fillStyle = rarityColor(snap.rarity || 1, 0.9);
  else ctx.fillStyle = rarityColor(snap.rarity || 1, 0.72);
  ctx.fillRect(x, y, size, size);
  ctx.restore();
}

function hurtFlashAmount(entity) {
  const age = Number.isFinite(entity?.hurtFlashAge)
    ? entity.hurtFlashAge
    : hurtFlashDuration;
  if (age >= hurtFlashDuration) return 0;
  const progress = clamp(age / hurtFlashDuration, 0, 1);
  return (1 - progress) * (1 - progress);
}

function drawDeathEffect(pos, radius, progress, rarity) {
  if (radius < entityDetailMinScreenRadius) return;

  const eased = progress * progress * (3 - progress * 2);
  const alpha = 1 - eased;
  const ringRadius = radius * (0.72 + eased * 1.36);
  const ringWidth = Math.max(1.2, radius * (0.13 - eased * 0.07));
  const glowColor = rarityColor(rarity || 1, alpha * 0.26);

  ctx.save();
  ctx.filter = "none";
  ctx.globalCompositeOperation = "lighter";
  ctx.globalAlpha *= alpha;
  ctx.strokeStyle = glowColor;
  ctx.lineWidth = ringWidth;
  ctx.beginPath();
  ctx.arc(pos.x, pos.y, ringRadius, 0, Math.PI * 2);
  ctx.stroke();

  const shardCount = 7;
  ctx.fillStyle = rarityColor(rarity || 1, alpha * 0.42);
  for (let i = 0; i < shardCount; i += 1) {
    const phase = i / shardCount;
    const angle = phase * Math.PI * 2 + progress * 0.9;
    const spread = radius * (0.34 + eased * 1.12);
    const size = Math.max(1.1, radius * (0.08 + (1 - progress) * 0.05));
    const x = pos.x + Math.cos(angle) * spread;
    const y = pos.y + Math.sin(angle) * spread;
    ctx.beginPath();
    ctx.arc(x, y, size, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

function particleBurstIntervalSeconds(ticks) {
  return Math.max(0.001, state.serverTickInterval || serverFixedDt) * ticks;
}

function shouldEmitParticleBurst(entity, ticks) {
  if (!entity) return false;

  const now = currentRenderTimeSeconds;
  const interval = particleBurstIntervalSeconds(ticks);
  if (!Number.isFinite(entity.particleBurstNextAt)) {
    entity.particleBurstNextAt = now + interval;
    return false;
  }
  if (now < entity.particleBurstNextAt) return false;

  entity.particleBurstNextAt = now + interval;
  return true;
}

function emitParticleBurst(
  origin,
  color,
  radius = 0,
  compositeOperation = "lighter",
  options = {},
) {
  if (
    !origin ||
    !Number.isFinite(origin.x) ||
    !Number.isFinite(origin.y) ||
    !color
  )
    return;

  const ringRadius = Math.max(0, Number(radius) || 0);
  const particleCount = Math.max(
    0,
    Math.floor(Number.isFinite(options.count) ? options.count : 1),
  );
  const alphaScale = Number.isFinite(options.alphaScale)
    ? options.alphaScale
    : 1;
  const sizeScale = Number.isFinite(options.sizeScale)
    ? options.sizeScale
    : 1;
  const lifetime = Number.isFinite(options.lifetime)
    ? options.lifetime
    : particleLifetimeSeconds;

  for (let i = 0; i < particleCount; i += 1) {
    const ringAngle = Math.random() * Math.PI * 2;
    const directionAngle = ringAngle + (Math.random() - 0.5) * 0.7;
    const speed =
      particleSpeedMin + Math.random() * (particleSpeedMax - particleSpeedMin);
    state.particles.push({
      pos: {
        x: origin.x + Math.cos(ringAngle) * ringRadius,
        y: origin.y + Math.sin(ringAngle) * ringRadius,
      },
      velocity: {
        x: Math.cos(directionAngle) * speed,
        y: Math.sin(directionAngle) * speed,
      },
      color,
      compositeOperation,
      age: 0,
      lifetime,
      size: particleSize * sizeScale,
      alphaScale,
    });
  }
  if (state.particles.length > particleMaxCount)
    state.particles.splice(0, state.particles.length - particleMaxCount);
}

function updateParticles(dt) {
  if (!state.particles.length) return;
  for (let i = state.particles.length - 1; i >= 0; i -= 1) {
    const particle = state.particles[i];
    particle.age += dt;
    const lifetime = Math.max(
      0.001,
      particle.lifetime || particleLifetimeSeconds,
    );
    if (particle.age >= lifetime) {
      state.particles.splice(i, 1);
      continue;
    }
    if (particle.kind === "portal") {
      const centerEntity = state.entities.get(particle.entityId);
      const center = centerEntity?.renderPos || particle.center;
      const progress = clamp(particle.age / lifetime, 0, 1);
      const radius = particle.orbitRadius * Math.pow(1 - progress, 0.72);
      const angle = particle.angle + particle.angularVelocity * particle.age;
      particle.pos.x = center.x + Math.cos(angle) * radius;
      particle.pos.y = center.y + Math.sin(angle) * radius;
      continue;
    }
    particle.pos.x += particle.velocity.x * dt;
    particle.pos.y += particle.velocity.y * dt;
    const velocityRetention = Math.exp(
      -Math.max(0, particleVelocityDampingPerSecond) * dt,
    );
    particle.velocity.x *= velocityRetention;
    particle.velocity.y *= velocityRetention;
  }
}

function drawParticlePass() {
  if (!state.particles.length) return;
  ctx.save();
  for (const particle of state.particles) {
    const lifetime = Math.max(
      0.001,
      particle.lifetime || particleLifetimeSeconds,
    );
    const progress = clamp(particle.age / lifetime, 0, 1);
    const pos = worldToScreen(particle.pos);
    const baseSize = particle.size || particleSize;
    const size = Math.max(
      0,
      worldLengthToScreen(baseSize) * Math.pow(1 - progress, 0.82),
    );
    if (size <= 0.05) continue;
    ctx.globalCompositeOperation =
      particle.compositeOperation || "lighter";
    const alphaScale = Number.isFinite(particle.alphaScale)
      ? particle.alphaScale
      : 1;
    ctx.globalAlpha =
      ((particle.kind === "portal"
        ? Math.min(1, progress * 8) * (1 - progress) * 0.9
        : 1 - progress) * alphaScale);
    ctx.fillStyle = particle.color;
    ctx.beginPath();
    ctx.arc(pos.x, pos.y, size, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

function emitPortalParticleBurst(entity, snap) {
  if (
    !entity?.renderPos ||
    entity.dying ||
    !snap ||
    (snap.flags & flagDead) !== 0
  )
    return;
  if (!shouldEmitParticleBurst(entity, 2)) return;

  const radius = Math.max(4, Number(snap.radius) || 0);
  const angle = Math.random() * Math.PI * 2;
  const orbitRadius = radius * (0.78 + Math.random() * 0.38);
  const lifetime = 0.48 + Math.random() * 0.34;
  const direction = (snap.entityId & 1) === 0 ? 1 : -1;
  const colors = ["#bce8ff", "#8fb7ff", "#d8c6ff", "#ffffff"];
  const center = { x: entity.renderPos.x, y: entity.renderPos.y };
  state.particles.push({
    kind: "portal",
    entityId: snap.entityId,
    center,
    pos: {
      x: center.x + Math.cos(angle) * orbitRadius,
      y: center.y + Math.sin(angle) * orbitRadius,
    },
    velocity: { x: 0, y: 0 },
    angle,
    orbitRadius,
    angularVelocity: direction * (3.6 + Math.random() * 2.4),
    color: colors[Math.floor(Math.random() * colors.length)],
    age: 0,
    lifetime,
    size: clamp(radius * (0.028 + Math.random() * 0.022), 1.8, 5.5),
  });
  if (state.particles.length > particleMaxCount)
    state.particles.splice(0, state.particles.length - particleMaxCount);
}

function isPetalBurstRarity(rarity) {
  return (
    rarity === rarityEternal ||
    rarity === rarityUnique ||
    rarity === rarityPrimordial
  );
}

function emitRarityParticleBurst(origin, rarity, radius) {
  const colors =
    rarity === rarityPrimordial
      ? ["#444444", "#989898"]
      : rarity === rarityUnique
        ? ["#000000"]
        : ["#ffffff"];
  for (const color of colors) {
    emitParticleBurst(
      origin,
      color,
      radius,
      rarity === rarityPrimordial || color === "#000000"
        ? "source-over"
        : "lighter",
    );
  }
}

function emitPetalParticleBurst(entity, snap) {
  if (
    !isPetalBurstRarity(snap?.rarity) ||
    entity?.dying ||
    !entity?.renderPos ||
    (snap.flags & flagDead) !== 0
  )
    return;
  if (!shouldEmitParticleBurst(entity, petalParticleBurstTicks)) return;
  emitRarityParticleBurst(
    entity.renderPos,
    snap.rarity,
    Math.max(0, snap.radius || 0),
  );
}

function emitZeroCopyFlowerParticleBurst(entity, snap) {
  if (
    snap?.entityType !== playerFlowerType ||
    entity?.dying ||
    !entity?.renderPos ||
    (snap.flags & flagDead) !== 0
  )
    return;

  const slots = snap.primarySlots || [];
  const eligibleRarities = [];
  for (let index = 0; index < slots.length; index += 1) {
    const slot = slots[index];
    if (!slotHasItem(slot) || !isPetalBurstRarity(slot.rarity)) continue;
    const visualPetalType = slotVisualPetalType(slot, index, "primary", slots);
    if (petalBaseCopy(visualPetalType, slot.rarity) !== 0) continue;
    eligibleRarities.push(slot.rarity);
  }

  if (
    !eligibleRarities.length ||
    !shouldEmitParticleBurst(entity, petalParticleBurstTicks)
  )
    return;
  for (const rarity of eligibleRarities)
    emitRarityParticleBurst(
      entity.renderPos,
      rarity,
      Math.max(0, snap.radius || 0),
    );
}

function bloodSacrificePhase(progress) {
  const value = clamp(Number.isFinite(progress) ? progress : 0, 0, 1);
  if (value < bloodSacrificeDrawPhaseEnd) {
    return {
      drawProgress: clamp(value / bloodSacrificeDrawPhaseEnd, 0, 1),
      fadeProgress: 0,
      alpha: 1,
    };
  }
  const fadeProgress = clamp(
    (value - bloodSacrificeDrawPhaseEnd) / (1 - bloodSacrificeDrawPhaseEnd),
    0,
    1,
  );
  return {
    drawProgress: 1,
    fadeProgress,
    alpha: 1 - fadeProgress,
  };
}

function bloodSacrificeStarPoints(pos, radius, angle = 0) {
  const vertices = [];
  for (let i = 0; i < 5; i += 1) {
    const theta = bloodSacrificeInitialHeading + angle + (i * Math.PI * 2) / 5;
    vertices.push({
      x: pos.x + Math.cos(theta) * radius,
      y: pos.y - Math.sin(theta) * radius,
    });
  }
  return [0, 2, 4, 1, 3, 0].map((index) => vertices[index]);
}

function measureBloodSacrificePath(points) {
  if (!points || points.length < 2) return null;
  let totalLength = 0;
  const segmentLengths = [];
  for (let i = 0; i < points.length - 1; i += 1) {
    const length = Math.hypot(
      points[i + 1].x - points[i].x,
      points[i + 1].y - points[i].y,
    );
    segmentLengths.push(length);
    totalLength += length;
  }
  if (totalLength <= 0) return null;

  return { points, segmentLengths, totalLength };
}

function bloodSacrificePathPoint(path, distance) {
  if (!path || path.totalLength <= 0) return null;

  const targetDistance = clamp(distance, 0, path.totalLength);
  let travelled = 0;
  for (let i = 0; i < path.segmentLengths.length; i += 1) {
    const segmentLength = path.segmentLengths[i];
    const start = path.points[i];
    const end = path.points[i + 1];
    if (travelled + segmentLength < targetDistance) {
      travelled += segmentLength;
      continue;
    }

    const segmentProgress =
      segmentLength > 0 ? (targetDistance - travelled) / segmentLength : 0;
    return {
      x: start.x + (end.x - start.x) * clamp(segmentProgress, 0, 1),
      y: start.y + (end.y - start.y) * clamp(segmentProgress, 0, 1),
    };
  }

  return path.points[path.points.length - 1];
}

function traceBloodSacrificeStar(path, progress) {
  if (!path || progress <= 0) return null;

  const targetLength = path.totalLength * clamp(progress, 0, 1);
  let travelled = 0;
  let latestPoint = path.points[0];
  ctx.beginPath();
  ctx.moveTo(path.points[0].x, path.points[0].y);
  for (let i = 0; i < path.segmentLengths.length; i += 1) {
    const segmentLength = path.segmentLengths[i];
    const start = path.points[i];
    const end = path.points[i + 1];
    if (travelled + segmentLength <= targetLength) {
      ctx.lineTo(end.x, end.y);
      travelled += segmentLength;
      latestPoint = end;
      continue;
    }
    const segmentProgress =
      segmentLength > 0 ? (targetLength - travelled) / segmentLength : 0;
    latestPoint = {
      x: start.x + (end.x - start.x) * clamp(segmentProgress, 0, 1),
      y: start.y + (end.y - start.y) * clamp(segmentProgress, 0, 1),
    };
    ctx.lineTo(latestPoint.x, latestPoint.y);
    break;
  }
  ctx.stroke();
  return latestPoint;
}

function bloodSacrificeParticleRadius(entity, referenceRadius, alpha) {
  const entityRadius = Number(entity?.snapshot?.radius);
  const radiusScale =
    Number.isFinite(entityRadius) && entityRadius > 0
      ? entityRadius / Math.max(1, bloodSacrificeParticleReferenceRadius)
      : 1;
  return Math.max(0.5, referenceRadius * radiusScale * Math.max(0, alpha));
}

function emitBloodSacrificeSweep(entity, path, drawProgress, effectAlpha = 1) {
  if (!entity || !path || drawProgress <= 0) return;

  const now = currentRenderTimeSeconds;
  const sweepDuration = Math.max(
    0.001,
    bloodSacrificeSweepDurationSeconds,
  );
  const sweepInterval = Math.max(
    0.001,
    bloodSacrificeSweepIntervalSeconds,
  );
  if (!Array.isArray(entity.bloodSacrificeSweeps))
    entity.bloodSacrificeSweeps = [];
  if (!Number.isFinite(entity.bloodSacrificeSweepNextAt)) {
    entity.bloodSacrificeSweepNextAt = now;
  }

  if (now >= entity.bloodSacrificeSweepNextAt) {
    const firstStart = entity.bloodSacrificeSweepNextAt;
    const dueCount = Math.floor((now - firstStart) / sweepInterval) + 1;
    const expiredCount = Math.max(
      0,
      Math.floor((now - sweepDuration - firstStart) / sweepInterval) + 1,
    );
    for (let index = expiredCount; index < dueCount; index += 1) {
      const minStep = Math.ceil(
        Math.min(bloodSacrificeSweepStepMin, bloodSacrificeSweepStepMax),
      );
      const maxStep = Math.max(
        minStep,
        Math.floor(
          Math.max(bloodSacrificeSweepStepMin, bloodSacrificeSweepStepMax),
        ),
      );
      entity.bloodSacrificeSweeps.push({
        startedAt: firstStart + index * sweepInterval,
        startProgress: Math.random(),
        speedStep:
          minStep + Math.floor(Math.random() * (maxStep - minStep + 1)),
        nextTravelProgress: 0,
        enteredDrawnPath: false,
      });
    }
    entity.bloodSacrificeSweepNextAt = firstStart + dueCount * sweepInterval;
  }

  const visibleLength = path.totalLength * clamp(drawProgress, 0, 1);
  const emissionProgress =
    Math.max(0.25, bloodSacrificeSweepEmissionSpacingPx) / path.totalLength;
  const referenceStep = Math.max(0.001, bloodSacrificeSweepReferenceStep);
  const drawing = drawProgress < 1;
  for (
    let index = entity.bloodSacrificeSweeps.length - 1;
    index >= 0;
    index -= 1
  ) {
    const sweep = entity.bloodSacrificeSweeps[index];
    const age = Math.max(0, now - sweep.startedAt);
    const lifetimeProgress = clamp(age / sweepDuration, 0, 1);
    const targetTravelProgress =
      lifetimeProgress * (Math.max(0, sweep.speedStep) / referenceStep);
    let hitDrawingHead = false;
    while (sweep.nextTravelProgress <= targetTravelProgress) {
      const pathProgress =
        (sweep.startProgress + sweep.nextTravelProgress) % 1;
      const pathDistance = pathProgress * path.totalLength;
      const onDrawnPath = !drawing || pathDistance <= visibleLength;
      if (drawing && sweep.enteredDrawnPath && !onDrawnPath) {
        hitDrawingHead = true;
        break;
      }
      if (onDrawnPath) {
        sweep.enteredDrawnPath = true;
        const point = bloodSacrificePathPoint(path, pathDistance);
        const particleCount = Math.max(
          1,
          Math.floor(
            bloodSacrificeSweepParticleCount * Math.max(0, effectAlpha),
          ),
        );
        const particleRadius = bloodSacrificeParticleRadius(
          entity,
          bloodSacrificeSweepParticleRadius,
          effectAlpha,
        );
        if (point) {
          emitParticleBurst(
            screenToWorld(point),
            "#ff0000",
            particleRadius,
            "lighter",
            {
              count: particleCount,
              alphaScale: Math.max(0, effectAlpha),
              sizeScale: Math.max(0.25, effectAlpha),
              lifetime: Math.min(
                particleLifetimeSeconds,
                Math.max(0.001, sweepDuration - age),
              ),
            },
          );
        }
      }
      sweep.nextTravelProgress += emissionProgress;
    }

    if (hitDrawingHead || age >= sweepDuration) {
      entity.bloodSacrificeSweeps.splice(index, 1);
    }
  }
}

function fillBloodSacrificeStar(points) {
  if (!points || points.length < 2) return;
  ctx.beginPath();
  ctx.moveTo(points[0].x, points[0].y);
  for (let i = 1; i < points.length; i += 1)
    ctx.lineTo(points[i].x, points[i].y);
  ctx.closePath();
  ctx.fill();
}

function drawBloodSacrificeEffect(
  pos,
  outerRadius,
  progress,
  angle = 0,
  entity = null,
) {
  const phase = bloodSacrificePhase(progress);
  if (phase.alpha <= 0 || outerRadius < entityPixelMinScreenRadius) return;

  const innerRadius = Math.max(1, outerRadius * bloodSacrificeInnerRadiusScale);
  const outerPoints = bloodSacrificeStarPoints(pos, outerRadius, angle);
  const innerPoints = bloodSacrificeStarPoints(pos, innerRadius, angle);
  const innerPath = measureBloodSacrificePath(innerPoints);
  const lineWidth = Math.max(2, Math.min(20, innerRadius * 0.02));

  ctx.save();
  ctx.filter = "none";
  ctx.globalCompositeOperation = "source-over";
  ctx.fillStyle = `rgba(128, 128, 128, ${bloodSacrificeOuterAlpha * phase.alpha})`;
  fillBloodSacrificeStar(outerPoints);

  ctx.globalAlpha *= phase.alpha;
  ctx.strokeStyle = "#ff0000";
  ctx.lineWidth = lineWidth;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  const latestPoint = traceBloodSacrificeStar(innerPath, phase.drawProgress);
  ctx.restore();

  emitBloodSacrificeSweep(entity, innerPath, phase.drawProgress, phase.alpha);

  if (
    phase.drawProgress < 1 &&
    latestPoint &&
    shouldEmitParticleBurst(entity, bloodSacrificeHeadParticleBurstTicks)
  ) {
    const origin = screenToWorld(latestPoint);
    const particleCount = Math.max(
      1,
      Math.floor(bloodSacrificeHeadParticleCount * Math.max(0, phase.alpha)),
    );
    const particleRadius = bloodSacrificeParticleRadius(
      entity,
      bloodSacrificeHeadParticleRadius,
      phase.alpha,
    );
    emitParticleBurst(origin, "#ff0000", particleRadius, "lighter", {
      count: particleCount,
      alphaScale: Math.max(0, phase.alpha),
      sizeScale: Math.max(0.25, phase.alpha),
    });
  }
}

function hornetSkill2WindupMissileWobble(entity, time) {
  const snap = entity?.snapshot;
  if (
    !snap ||
    snap.entityType !== hornetMissileType ||
    (snap.flags & flagAttached) === 0
  )
    return 0;
  let bestDistance = Infinity;
  let owner = null;
  for (const candidate of currentHornetSkill2WindupOwners) {
    const ownerSnap = candidate?.snapshot;
    const ownerPos = candidate.renderPos || ownerSnap.pos;
    const missilePos = entity.renderPos || snap.pos;
    if (!ownerPos || !missilePos) continue;
    const distance = Math.hypot(
      missilePos.x - ownerPos.x,
      missilePos.y - ownerPos.y,
    );
    const maxDistance = Math.max(ownerSnap.radius * 4.5 + snap.radius * 2, 1);
    if (distance <= maxDistance && distance < bestDistance) {
      bestDistance = distance;
      owner = candidate;
    }
  }
  if (!owner) return 0;
  return Math.sin((time || 0) * Math.PI * 4) * 0.42;
}

function detailMinRadiusForEntity(snap, isOwner, isPetal, isDrop) {
  const base = isPetal
    ? petalDetailMinScreenRadius
    : isDrop
      ? dropDetailMinScreenRadius
      : entityDetailMinScreenRadius;
  if (currentRenderLoad <= 0 || isOwner || shouldAlwaysDrawDetailed(snap))
    return base;
  if (isPetal) return currentRenderLoad >= 2 ? base * 3.8 : base * 2.2;
  if (isDrop) return currentRenderLoad >= 2 ? base * 3.2 : base * 2.0;
  return currentRenderLoad >= 2 ? base * 3.0 : base * 1.8;
}

function shouldAlwaysDrawDetailed(snap) {
  if (!snap) return false;
  if (
    snap.entityType === playerFlowerType ||
    snap.entityType === spiderWebZoneType ||
    snap.entityType === bloodSacrificeEffectType
  )
    return true;
  return raritySortRank(snap.rarity) >= raritySortRank(bossRarity);
}

function flowerMobVisualScale(entityType) {
  if (entityType === titanType)
    return getClientConfigValue(state.clientConfig, "titan_sprite_scale");
  if (entityType === dummyType)
    return getClientConfigValue(state.clientConfig, "dummy_sprite_scale");
  return 1;
}

function flowerMobCullScale(entityType) {
  if (entityType !== titanType && entityType !== dummyType) return 1;
  return Math.max(1, flowerMobVisualScale(entityType) * 1.35);
}

function drawEntity(entity) {
  const snap = entity.snapshot;
  const pos = worldToScreen(entity.renderPos);
  const radius = Math.max(0, worldLengthToScreen(snap.radius));
  const isOwner =
    (snap.flags & flagOwner) !== 0 || snap.entityId === state.ownerEntityId;
  const isPetal = isPetalEntity(snap.entityType);
  const isDrop = isDropEntity(snap.entityType);
  const dead = (snap.flags & flagDead) !== 0;
  const deathProgress = entity.dying
    ? clamp((entity.deathAge || 0) / deathFadeDuration, 0, 1)
    : 0;
  const deathEase = deathProgress * deathProgress * (3 - deathProgress * 2);
  const deathScale = entity.dying ? 1 + deathEase * deathScaleBoost : 1;
  const deathAlpha = entity.dying ? 1 - deathEase : 1;
  const hurtFlash = entity.dying ? 0 : hurtFlashAmount(entity);
  const spriteScale = flowerMobVisualScale(snap.entityType);

  if (isPetal) emitPetalParticleBurst(entity, snap);
  else if (snap.entityType === playerFlowerType)
    emitZeroCopyFlowerParticleBurst(entity, snap);

  ctx.save();
  ctx.globalAlpha *= deathAlpha;
  if (hurtFlash > 0)
    ctx.filter = `brightness(${1 + hurtFlash * 3.5}) saturate(${1 - hurtFlash * 0.92})`;

  const visibleRadius = radius * spriteScale * deathScale;
  if (visibleRadius < entityPixelMinScreenRadius) {
    ctx.restore();
    return;
  }

  const detailMinRadius = detailMinRadiusForEntity(
    snap,
    isOwner,
    isPetal,
    isDrop,
  );
  if (visibleRadius < detailMinRadius) {
    drawPixelEntity(snap, pos, visibleRadius, isOwner, isPetal, isDrop);
    ctx.restore();
    return;
  }

  if (!isPetal && !isDrop && snap.entityType === bloodSacrificeEffectType) {
    drawBloodSacrificeEffect(
      pos,
      visibleRadius,
      snap.hpPercent,
      entity.bloodSacrificeAngle ?? Math.random() * Math.PI * 2,
      entity,
    );
    ctx.restore();
    return;
  }

  if (entity.dying)
    drawDeathEffect(pos, visibleRadius, deathProgress, snap.rarity);

  if (
    !isPetal &&
    (snap.entityType === beetleType || snap.entityType === summonedBeetleType)
  ) {
    const summoned = snap.entityType === summonedBeetleType;
    drawBeetle(
      ctx,
      "./assets/beetle.svg",
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
      {
        summoned,
        forwardOffset: radius * deathScale * beetleSpriteForwardOffsetScale,
      },
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === normalLadybugType) {
    drawNormalLadybug(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === rockType) {
    drawRock(ctx, pos, radius * deathScale, snap.entityId, snap.radius);
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === leafPieceType) {
    drawLeafPiece(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === babyAntType) {
    drawBabyAnt(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === workerAntType) {
    drawWorkerAnt(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === queenAntType) {
    drawQueenAnt(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (
    !isPetal &&
    (snap.entityType === antEggMobType || snap.entityType === queenAntEggType)
  ) {
    drawAntEggMob(ctx, pos, radius * deathScale, "normal");
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (
    !isPetal &&
    (snap.entityType === fireAntEggType ||
      snap.entityType === queenFireAntEggType)
  ) {
    drawAntEggMob(ctx, pos, radius * deathScale, "fire");
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === termiteEggType) {
    drawAntEggMob(ctx, pos, radius * deathScale, "termite");
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === babyFireAntType) {
    drawBabyFireAnt(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === workerFireAntType) {
    drawWorkerFireAnt(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === fireQueenAntType) {
    drawFireQueenAnt(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === babyTermiteType) {
    drawBabyTermite(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === workerTermiteType) {
    drawWorkerTermite(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === termiteOvermindType) {
    drawTermiteOvermind(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === antHoleType) {
    drawAntHole(ctx, pos, radius * deathScale);
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === spiderType) {
    drawSpider(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === sandstormType) {
    drawSandstorm(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && !isDrop && snap.entityType === portalType) {
    emitPortalParticleBurst(entity, snap);
    drawPortal(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      currentRenderTimeSeconds,
      entity.renderAngle ?? snap.angle,
    );
    ctx.restore();
    return;
  }

  if (
    !isPetal &&
    (snap.entityType === soldierAntType ||
      snap.entityType === leafcutterSoldierType ||
      snap.entityType === summonedSoldierAntType ||
      snap.entityType === soldierFireAntType ||
      snap.entityType === soldierTermiteType)
  ) {
    const summoned = snap.entityType === summonedSoldierAntType;
    if (snap.entityType === soldierFireAntType) {
      drawSoldierFireAntMob(
        ctx,
        pos,
        radius * deathScale,
        snap.entityId,
        entity.renderAngle ?? snap.angle,
        entity.motionBlend || 0,
        currentRenderTimeSeconds,
      );
    } else if (snap.entityType === soldierTermiteType) {
      drawSoldierTermite(
        ctx,
        pos,
        radius * deathScale,
        snap.entityId,
        entity.renderAngle ?? snap.angle,
        entity.motionBlend || 0,
        currentRenderTimeSeconds,
      );
    } else if (snap.entityType === leafcutterSoldierType) {
      drawLeafcutterSoldier(
        ctx,
        pos,
        radius * deathScale,
        snap.entityId,
        entity.renderAngle ?? snap.angle,
        entity.motionBlend || 0,
        currentRenderTimeSeconds,
        (snap.flags & flagCarryingLeafPiece) !== 0,
      );
    } else {
      drawSoldierAnt(
        ctx,
        pos,
        radius * deathScale,
        snap.entityId,
        entity.renderAngle ?? snap.angle,
        entity.motionBlend || 0,
        currentRenderTimeSeconds,
        { summoned },
      );
    }
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === bandageBeetleType) {
    drawBeetle(
      ctx,
      "./assets/bandage_beetle.svg",
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
      { forwardOffset: radius * deathScale * beetleSpriteForwardOffsetScale },
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === beeType) {
    drawBee(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === bumbleBeeType) {
    drawBumbleBee(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === hornetType) {
    const windupId = skillWindupIdFromFlags(snap.flags);
    const antennaWobble =
      windupId === 2
        ? Math.sin((currentRenderTimeSeconds || 0) * Math.PI * 4) * 0.34
        : 0;
    drawHornet(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      currentRenderTimeSeconds,
      {
        antennaWobble,
        spriteScale: getClientConfigValue(
          state.clientConfig,
          "hornet_sprite_scale",
        ),
        spriteOffset: {
          x: getClientConfigValue(state.clientConfig, "hornet_body_offset_x"),
          y: getClientConfigValue(state.clientConfig, "hornet_body_offset_y"),
        },
      },
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === dandelionType) {
    drawDandelion(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      {
        scale: getClientConfigValue(state.clientConfig, "dande_base_scale"),
        angle: getClientConfigValue(state.clientConfig, "dande_base_angle"),
        x: getClientConfigValue(state.clientConfig, "dande_base_x"),
        y: getClientConfigValue(state.clientConfig, "dande_base_y"),
      },
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && !isDrop && snap.entityType === pollenProjectileType) {
    drawPollen(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      currentRenderTimeSeconds,
    );
    ctx.restore();
    return;
  }

  if (!isPetal && !isDrop && snap.entityType === trapProjectileType) {
    drawTrapProjectile(
      pos,
      radius * deathScale,
      entity.renderAngle ?? snap.angle,
    );
    ctx.restore();
    return;
  }

  if (!isPetal && !isDrop && snap.entityType === spiderWebZoneType) {
    drawSpiderWeb(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      currentRenderTimeSeconds,
      {
        playerOwned: snap.team === playerTeam,
      },
    );
    ctx.restore();
    return;
  }

  if (!isPetal && !isDrop && snap.entityType === hornetMissileType) {
    const wobble = hornetSkill2WindupMissileWobble(
      entity,
      currentRenderTimeSeconds,
    );
    drawHornetMissile(
      ctx,
      pos,
      radius * deathScale,
      (entity.renderAngle ?? snap.angle) + wobble,
    );
    ctx.restore();
    return;
  }

  if (!isPetal && !isDrop && snap.entityType === dandelionMissileType) {
    drawDandelionMissile(
      pos,
      radius * deathScale,
      entity.renderAngle ?? snap.angle,
      snap.rarity,
      snap.entityId,
    );
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === playerFlowerType) {
    const flowerAngle = dead
      ? (entity.deathAngle ?? entity.renderAngle ?? snap.angle)
      : (entity.renderAngle ?? snap.angle);
    drawPlayerFlower(snap, pos, radius * deathScale, flowerAngle, isOwner);
    if (!entity.dying && !entityHasState(snap, stateDigging))
      drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === mechaFlowerType) {
    drawMechaFlower(
      snap,
      pos,
      radius * deathScale,
      entity.renderAngle ?? snap.angle,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === titanType) {
    const visualRadius = radius * spriteScale;
    drawPlayerFlower(
      snap,
      pos,
      visualRadius * deathScale,
      entity.renderAngle ?? snap.angle,
      false,
    );
    if (!entity.dying) drawMobFrame(snap, pos, visualRadius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === dummyType) {
    const visualRadius = radius * spriteScale;
    drawPlayerFlower(
      snap,
      pos,
      visualRadius * deathScale,
      entity.renderAngle ?? snap.angle,
      false,
    );
    if (!entity.dying) drawMobFrame(snap, pos, visualRadius);
    ctx.restore();
    return;
  }

  if (isDrop) {
    drawDropPetalCard(
      petalTypeFromSnap(snap),
      snap.rarity,
      pos,
      radius * deathScale,
    );
    ctx.restore();
    return;
  }

  if (isPetal) {
    const petalType = petalTypeFromSnap(snap);
    drawLivePetal(
      petalType,
      snap.rarity,
      pos,
      radius * deathScale,
      entity.renderAngle ?? snap.angle,
      snap.entityId,
    );
    ctx.restore();
    return;
  }

  ctx.beginPath();
  ctx.arc(pos.x, pos.y, radius * deathScale, 0, Math.PI * 2);

  if (isOwner) ctx.fillStyle = "#ffe0e7";
  else if (isPetal || isDrop) ctx.fillStyle = rarityColor(snap.rarity, 0.82);
  else ctx.fillStyle = rarityColor(snap.rarity, 0.72);
  ctx.fill();

  ctx.lineWidth = (isOwner ? 3 : 1.5) * deathScale;
  if (isOwner && snap.flags & flagDefending) ctx.strokeStyle = "#467acd";
  else if (isOwner && snap.flags & flagAttacking) ctx.strokeStyle = "#be5e7a";
  else
    ctx.strokeStyle = isPetal || isDrop ? "#4f6680" : "rgba(48, 62, 82, 0.52)";
  ctx.stroke();

  if (!isPetal && !isDrop && !entity.dying) drawMobFrame(snap, pos, radius);
  ctx.restore();
}

function drawPlayerFlower(snap, pos, radius, angle, isOwner) {
  drawFlower(snap, pos, radius, angle, isOwner, "player");
}

function drawMechaFlower(snap, pos, radius, angle) {
  drawFlower(snap, pos, radius, angle, false, "mecha");
}

function drawFlower(snap, pos, radius, angle, isOwner, bodyKind) {
  const flags = snap.flags || 0;
  const digging = entityHasState(snap, stateDigging);
  if (digging) {
    ctx.save();
    ctx.translate(pos.x, pos.y);
    ctx.globalAlpha *= 0.58;
    ctx.fillStyle = "#6b7280";
    ctx.beginPath();
    ctx.arc(0, 0, Math.max(0.5, radius * 1.02), 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
    return;
  }

  const hasAntennae =
    (flags & flagAntennae) !== 0 ||
    playerSnapHasPetal(snap, petalAntennaeType) ||
    (isOwner && ownerHasPetal(petalAntennaeType));
  const hasDouli =
    playerSnapHasPetal(snap, petalDouliType) ||
    (isOwner && ownerHasPetal(petalDouliType));
  const hasThirdEye =
    playerSnapHasPetal(snap, petalThirdEyeType) ||
    (isOwner && ownerHasPetal(petalThirdEyeType));
  const hasBandage =
    playerSnapHasPetal(snap, petalBandageType) ||
    (isOwner && ownerHasPetal(petalBandageType));
  const nullified =
    playerSnapHasPetal(snap, petalNullificationType) ||
    (isOwner && ownerHasPetal(petalNullificationType));
  const undead = entityHasState(snap, stateUndead);
  const dead = (flags & flagDead) !== 0;
  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (dead && Number.isFinite(angle)) ctx.rotate(angle);
  if (nullified) ctx.globalAlpha *= 0.58;
  drawFlowerBody(bodyKind, radius, snap);
  if (hasBandage) drawBandagePetalLayer(radius);
  const attacking = (flags & flagAttacking) !== 0;
  const defending = (flags & flagDefending) !== 0;
  drawPlayerFlowerFace(
    radius,
    angle,
    undead ? "undead" : "normal",
    dead ? "dead" : attacking ? "attack" : defending ? "defend" : "normal",
  );
  if (hasAntennae) drawAntennaeOverlay(radius);
  if (hasThirdEye) drawThirdEyeOverlay(radius);
  if (hasDouli) drawDouliOverlay(radius);
  ctx.restore();
}

function drawFlowerBody(bodyKind, radius, snap) {
  const image = assetImage(flowerBodyTexturePath(bodyKind));
  const size = Math.max(0.5, radius * 2.36);

  ctx.save();
  ctx.filter = flowerBodyFilter(snap, bodyKind === "mecha", ctx.filter);
  if (imageReady(image)) {
    ctx.drawImage(image, -size * 0.5, -size * 0.5, size, size);
  } else if (bodyKind === "mecha") {
    drawMechaFlowerFallback(radius);
  } else {
    drawPlayerFlowerFallback(radius);
  }
  ctx.restore();
}

function flowerBodyFilter(snap, mechanical, inheritedFilter) {
  const filters = [];
  const corrupted = entityHasState(snap, stateCorruption);
  const undead = entityHasState(snap, stateUndead);
  const poisoned = entityHasState(snap, statePoison);
  const psionic = entityHasState(snap, statePsionicConnection);

  if (mechanical && (corrupted || undead || poisoned || psionic))
    filters.push("sepia(0.55)");
  if (psionic)
    filters.push("saturate(0.82) brightness(0.88)");
  if (poisoned)
    filters.push("hue-rotate(-165deg) saturate(2.4) brightness(0.94)");
  if (undead)
    filters.push("hue-rotate(30deg) saturate(0.7) brightness(0.96)");
  if (corrupted)
    filters.push(
      "hue-rotate(-60deg) saturate(2.1) brightness(0.62) contrast(1.45)",
    );
  if (inheritedFilter && inheritedFilter !== "none")
    filters.push(inheritedFilter);

  return filters.length > 0 ? filters.join(" ") : "none";
}

function drawMobSvg(src, pos, radius, angle) {
  const image = assetImage(src);
  const size = Math.max(1, radius * 2 * mobSpriteCoverScale);

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (Number.isFinite(angle)) ctx.rotate(angle);

  if (imageReady(image)) {
    ctx.drawImage(image, -size * 0.5, -size * 0.5, size, size);
  } else {
    ctx.fillStyle = "#7f678f";
    ctx.beginPath();
    ctx.arc(0, 0, radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = Math.max(1.5, radius * 0.12);
    ctx.strokeStyle = "rgba(35, 44, 54, 0.45)";
    ctx.stroke();
  }

  ctx.restore();
}

function flowerBodyTexturePath(bodyKind) {
  const textureFile = bodyKind === "mecha" ? "mecha_flower" : "player_flower";
  return `./assets/${textureFile}.svg?v=${flowerTextureVersion}`;
}

function drawBandagePetalLayer(radius) {
  const icon = bandageOverlayImage();
  const size = Math.max(0.5, radius * 4.48);
  const offsetY = radius * 0.22;
  ctx.save();
  ctx.beginPath();
  ctx.arc(0, 0, radius * 1.08, 0, Math.PI * 2);
  ctx.clip();
  if (imageReady(icon)) {
    ctx.drawImage(icon, -size * 0.5, -size * 0.5 + offsetY, size, size);
  } else {
    drawBandagePetalLayerFallback(radius);
  }
  ctx.restore();
}

function drawBandagePetalLayerFallback(radius) {
  ctx.save();
  ctx.rotate(-0.26);
  ctx.fillStyle = "#e8dba6";
  ctx.strokeStyle = "#a89d73";
  ctx.lineJoin = "round";
  ctx.lineWidth = Math.max(1.1, radius * 0.07);
  const strips = [
    { y: -radius * 0.48, w: radius * 1.62 },
    { y: -radius * 0.08, w: radius * 1.94 },
    { y: radius * 0.34, w: radius * 1.18 },
  ];
  for (const strip of strips) {
    ctx.beginPath();
    ctx.rect(-strip.w * 0.5, strip.y - radius * 0.12, strip.w, radius * 0.24);
    ctx.fill();
    ctx.stroke();
    ctx.strokeStyle = "rgba(251, 241, 191, 0.82)";
    ctx.lineWidth = Math.max(1, radius * 0.055);
    ctx.beginPath();
    ctx.moveTo(-strip.w * 0.42, strip.y - radius * 0.04);
    ctx.lineTo(strip.w * 0.42, strip.y - radius * 0.04);
    ctx.stroke();
    ctx.strokeStyle = "#a89d73";
    ctx.lineWidth = Math.max(1.1, radius * 0.07);
  }
  ctx.restore();
}

function drawAntennaeOverlay(radius) {
  const icon = antennaeOverlayImage();
  const size = Math.max(0.5, radius * 2.8);
  ctx.save();
  ctx.translate(0, -radius);
  ctx.globalAlpha *= 0.92;
  if (imageReady(icon)) {
    ctx.drawImage(icon, -size * 0.5, -size * 0.5, size, size);
  } else {
    ctx.strokeStyle = "#111";
    ctx.lineWidth = Math.max(2, radius * 0.14);
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(-radius * 0.22, -radius * 0.08);
    ctx.quadraticCurveTo(
      -radius * 0.66,
      -radius * 0.9,
      -radius * 1.08,
      -radius * 1.24,
    );
    ctx.moveTo(radius * 0.22, -radius * 0.08);
    ctx.quadraticCurveTo(
      radius * 0.66,
      -radius * 0.9,
      radius * 1.08,
      -radius * 1.24,
    );
    ctx.stroke();
  }
  ctx.restore();
}

function drawDouliOverlay(radius) {
  const icon = douliOverlayImage();
  const size = Math.max(0.5, radius * douliOverlaySizeScale);
  ctx.save();
  ctx.translate(0, radius * douliOverlayOffsetYScale);
  ctx.rotate(douliOverlayAngle);
  if (imageReady(icon)) {
    ctx.drawImage(icon, -size * 0.5, -size * 0.5, size, size);
  }
  ctx.restore();
}

function drawThirdEyeOverlay(radius) {
  const icon = thirdEyeOverlayImage();
  const size = Math.max(0.5, radius * 0.82);
  const offsetY = -radius * 0.52;
  ctx.save();
  ctx.translate(0, offsetY);
  ctx.globalAlpha *= 0.96;
  if (imageReady(icon)) {
    ctx.drawImage(icon, -size * 0.5, -size * 0.5, size, size);
  } else {
    ctx.fillStyle = "#f4f0e9";
    ctx.strokeStyle = "#322a35";
    ctx.lineWidth = Math.max(1, radius * 0.055);
    ctx.beginPath();
    ctx.ellipse(0, 0, radius * 0.28, radius * 0.18, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = "#333127";
    ctx.beginPath();
    ctx.arc(0, 0, radius * 0.095, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

function drawPlayerFlowerFallback(radius) {
  ctx.fillStyle = "#d7bd3c";
  ctx.beginPath();
  ctx.arc(0, 0, radius * 0.9, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = "#f4dc59";
  ctx.beginPath();
  ctx.arc(0, 0, radius * 0.74, 0, Math.PI * 2);
  ctx.fill();
}

function drawMechaFlowerFallback(radius) {
  ctx.fillStyle = "#706959";
  for (let index = 0; index < 5; index += 1) {
    const angle = -Math.PI * 0.5 + (index * Math.PI * 2) / 5;
    ctx.beginPath();
    ctx.arc(
      Math.cos(angle) * radius * 0.75,
      Math.sin(angle) * radius * 0.75,
      radius * 0.2,
      0,
      Math.PI * 2,
    );
    ctx.fill();
  }

  ctx.fillStyle = "#999";
  ctx.strokeStyle = "#7c7c7c";
  ctx.lineWidth = Math.max(0.75, radius * 0.08);
  ctx.beginPath();
  for (let index = 0; index < 16; index += 1) {
    const angle = -Math.PI * 0.5 + (index * Math.PI * 2) / 16;
    const bodyRadius = radius * (index % 2 === 0 ? 0.66 : 0.63);
    const x = Math.cos(angle) * bodyRadius;
    const y = Math.sin(angle) * bodyRadius;
    if (index === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
}

function drawPlayerFlowerFace(radius, angle, faceStyle, expression = "normal") {
  const look = Number.isFinite(angle)
    ? { x: Math.cos(angle), y: Math.sin(angle) }
    : { x: 0, y: 0 };
  const eyeFill = faceStyle === "undead" ? "#27351f" : "#333127";
  const smileFill = faceStyle === "undead" ? "#35472a" : "#5a4a26";

  ctx.save();
  ctx.translate(0, radius * 0.06);
  ctx.scale(0.86, 0.86);
  if (expression === "dead") drawPlayerFlowerDeadEyes(radius, eyeFill);
  else if (expression === "attack")
    drawPlayerFlowerAngryEyes(radius, look, eyeFill);
  else drawPlayerFlowerNormalEyes(radius, look, eyeFill);

  if (expression === "dead") drawPlayerFlowerDeadMouth(radius, smileFill);
  else if (expression === "attack" || expression === "defend")
    drawPlayerFlowerAngryMouth(radius, smileFill);
  else drawPlayerFlowerSmile(radius, faceStyle, smileFill);
  ctx.restore();
}

function drawPlayerFlowerNormalEyes(radius, look, eyeFill) {
  const eyeOffset = radius * 0.055;
  const leftEye = { x: -radius * 0.23, y: -radius * 0.31 };
  const rightEye = { x: radius * 0.23, y: -radius * 0.31 };

  ctx.fillStyle = eyeFill;
  ctx.beginPath();
  ctx.ellipse(
    leftEye.x,
    leftEye.y,
    radius * 0.105,
    radius * 0.27,
    0,
    0,
    Math.PI * 2,
  );
  ctx.ellipse(
    rightEye.x,
    rightEye.y,
    radius * 0.105,
    radius * 0.27,
    0,
    0,
    Math.PI * 2,
  );
  ctx.fill();

  ctx.fillStyle = "rgba(255, 255, 222, 0.92)";
  ctx.beginPath();
  ctx.ellipse(
    leftEye.x - radius * 0.035 + look.x * eyeOffset,
    leftEye.y - radius * 0.09 + look.y * eyeOffset,
    radius * 0.032,
    radius * 0.065,
    0,
    0,
    Math.PI * 2,
  );
  ctx.ellipse(
    rightEye.x - radius * 0.035 + look.x * eyeOffset,
    rightEye.y - radius * 0.09 + look.y * eyeOffset,
    radius * 0.032,
    radius * 0.065,
    0,
    0,
    Math.PI * 2,
  );
  ctx.fill();
}

function drawPlayerFlowerAngryEyes(radius, look, eyeFill) {
  ctx.save();
  ctx.beginPath();
  ctx.moveTo(-radius * 0.48, -radius * 0.62);
  ctx.lineTo(0, -radius * 0.29);
  ctx.lineTo(radius * 0.48, -radius * 0.62);
  ctx.lineTo(radius * 0.48, radius * 0.16);
  ctx.lineTo(-radius * 0.48, radius * 0.16);
  ctx.closePath();
  ctx.clip();

  drawPlayerFlowerNormalEyes(radius, look, eyeFill);
  ctx.restore();
}

function drawPlayerFlowerDeadEyes(radius, eyeFill) {
  const centers = [
    { x: -radius * 0.23, y: -radius * 0.31 },
    { x: radius * 0.23, y: -radius * 0.31 },
  ];
  ctx.strokeStyle = eyeFill;
  ctx.lineWidth = Math.max(0.45, radius * 0.115);
  ctx.lineCap = "round";
  for (const eye of centers) {
    const dx = radius * 0.12;
    const dy = radius * 0.16;
    ctx.beginPath();
    ctx.moveTo(eye.x - dx, eye.y - dy);
    ctx.lineTo(eye.x + dx, eye.y + dy);
    ctx.moveTo(eye.x + dx, eye.y - dy);
    ctx.lineTo(eye.x - dx, eye.y + dy);
    ctx.stroke();
  }
}

function drawPlayerFlowerSmile(radius, faceStyle, smileFill) {
  ctx.strokeStyle = smileFill;
  ctx.lineWidth = Math.max(0.45, radius * 0.075);
  ctx.lineCap = "round";
  ctx.beginPath();
  if (faceStyle === "undead") {
    ctx.arc(0, radius * 0.37, radius * 0.2, Math.PI * 1.16, Math.PI * 1.84);
  } else {
    ctx.arc(0, radius * 0.02, radius * 0.29, 0.22 * Math.PI, 0.78 * Math.PI);
  }
  ctx.stroke();
}

function drawPlayerFlowerAngryMouth(radius, smileFill) {
  ctx.strokeStyle = smileFill;
  ctx.lineWidth = Math.max(0.45, radius * 0.075);
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.beginPath();
  ctx.moveTo(-radius * 0.22, radius * 0.27);
  ctx.quadraticCurveTo(0, radius * 0.16, radius * 0.22, radius * 0.27);
  ctx.stroke();
}

function drawPlayerFlowerDeadMouth(radius, smileFill) {
  ctx.strokeStyle = smileFill;
  ctx.lineWidth = Math.max(0.45, radius * 0.075);
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.beginPath();
  ctx.moveTo(-radius * 0.24, radius * 0.34);
  ctx.quadraticCurveTo(0, radius * 0.17, radius * 0.24, radius * 0.34);
  ctx.stroke();
}

function drawLivePetal(petalType, rarity, pos, radius, angle, entityId = 0) {
  const size = radius * worldPetalSizeScale;
  if (size < 0.5) return;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (petalHasMultiCopyVisual(petalType, rarity))
    ctx.translate(0, -size * 0.035);
  if (petalType !== petalMoonType && Number.isFinite(angle))
    ctx.rotate(livePetalRenderAngle(petalType, angle));

  if (petalType === petalDustType) {
    drawDustParticlePetal(size, rarity);
    ctx.restore();
    return;
  }

  if (petalType === petalMoonType) {
    drawProceduralMoonPetal(size, rarity, entityId);
    ctx.restore();
    return;
  }

  let icon = null;
  if (petalType === petalAntEggType) {
    icon = livePetalIconImage(
      petalType,
      rarity,
      extractSingleAntEggLiveSvg,
      "single-ant-egg",
    );
  } else if (petalType === petalWaxType) {
    icon = livePetalIconImage(
      petalType,
      rarity,
      extractWaxLiveSvg,
      "wax-petal",
    );
  } else if (petalType === petalShovelType) {
    icon = livePetalIconImage(petalType, rarity);
  } else if (petalType === petalDandelionType) {
    icon = livePetalIconImage(
      petalType,
      rarity,
      extractDandelionLiveSvg,
      "dandelion-petal",
    );
  } else if (petalType === petalTrapperType) {
    icon = livePetalIconImage(
      petalType,
      rarity,
      extractTrapperLiveSvg,
      "trapper-petal-centered",
    );
  } else {
    icon = livePetalIconImage(petalType, rarity);
  }

  if (imageReady(icon)) {
    drawCenteredImage(
      icon,
      size,
      petalType === petalWaxType ? waxLivePetalVisualScale : 1,
    );
  } else {
    drawPetalSilhouette(size, rarity);
  }

  ctx.restore();
}

function drawDandelionMissile(pos, radius, angle, rarity, entityId = 0) {
  const safeRarity = rarity || 1;
  const visualScale = getClientConfigValue(
    state.clientConfig,
    "dande_missile_scale",
  );
  const angleOffset = getClientConfigValue(
    state.clientConfig,
    "dande_missile_angle",
  );
  const radialOffset = getClientConfigValue(
    state.clientConfig,
    "dande_missile_offset",
  );
  const yOffset = getClientConfigValue(
    state.clientConfig,
    "dande_missile_y_offset",
  );
  const anchorX = getClientConfigValue(
    state.clientConfig,
    "dande_missile_anchor_x",
  );
  const anchorY = getClientConfigValue(
    state.clientConfig,
    "dande_missile_anchor_y",
  );
  const size = Math.max(1, radius * visualScale);
  const radialAngle = Number.isFinite(angle) ? angle : 0;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  ctx.rotate(radialAngle);
  ctx.translate(radius * radialOffset, radius * yOffset);
  ctx.rotate(angleOffset);
  ctx.translate(radius * anchorX, radius * anchorY);

  const icon = livePetalIconImage(
    petalDandelionType,
    safeRarity,
    extractDandelionLiveSvg,
    "dandelion-missile",
  );
  if (imageReady(icon)) {
    drawCenteredImage(icon, size, 1);
  } else {
    drawPetalSilhouette(size, safeRarity);
  }

  ctx.restore();
}

function drawTrapProjectile(pos, radius, angle) {
  const image = assetImage("./assets/trap.svg?v=2");
  const imageSize = Math.max(1, radius * 3);
  ctx.save();
  ctx.translate(pos.x, pos.y);
  ctx.rotate(Number.isFinite(angle) ? angle : 0);

  if (imageReady(image)) {
    ctx.drawImage(
      image,
      -imageSize * 0.5,
      -imageSize * 0.5,
      imageSize,
      imageSize,
    );
    ctx.restore();
    return;
  }

  const size = Math.max(1, radius * 1.35);
  ctx.lineJoin = "round";
  ctx.lineWidth = Math.max(1.2, radius * 0.24);
  ctx.strokeStyle = "#d7bd3c";
  ctx.fillStyle = "#f4dc59";
  ctx.beginPath();
  ctx.moveTo(size, 0);
  ctx.quadraticCurveTo(0, 0, -size * 0.72, size * 0.86);
  ctx.quadraticCurveTo(0, 0, -size * 0.72, -size * 0.86);
  ctx.quadraticCurveTo(0, 0, size, 0);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
  ctx.restore();
}

function livePetalRenderAngle(petalType, angle) {
  if (petalType === petalCompassType) return angle + compassPetalAngleOffset;
  if (
    petalType === petalMissileType ||
    petalType === petalCarrotType ||
    petalType === petalDandelionType
  )
    return angle + directionalPetalAngleOffset;
  return angle;
}

function drawProceduralMoonPetal(size, rarity, entityId) {
  const radius = size * 0.16;
  const borderWidth = 5.2;
  const innerRadius = Math.max(1, radius - borderWidth);
  const seed = moonPatternSeed(entityId, rarity);
  const rand = seededRandom(seed);

  ctx.fillStyle = moonTone("border");
  ctx.beginPath();
  ctx.arc(0, 0, radius, 0, Math.PI * 2);
  ctx.fill();

  ctx.save();
  ctx.beginPath();
  ctx.arc(0, 0, innerRadius, 0, Math.PI * 2);
  ctx.clip();
  ctx.fillStyle = moonTone("base");
  ctx.fillRect(-innerRadius, -innerRadius, innerRadius * 2, innerRadius * 2);

  const craterCount = 5 + Math.floor(rand() * 5);
  for (let i = 0; i < craterCount; i += 1) {
    const angle = rand() * Math.PI * 2;
    const dist = Math.sqrt(rand()) * innerRadius * 0.9;
    const x = Math.cos(angle) * dist;
    const y = Math.sin(angle) * dist;
    const craterRadius = innerRadius * (0.08 + rand() * 0.13);
    const strokeWidth = Math.max(1.8, borderWidth * 0.45);

    ctx.lineWidth = strokeWidth;
    ctx.strokeStyle = moonTone("craterBorder");
    ctx.fillStyle = moonTone(rand() > 0.36 ? "crater" : "craterLight");
    ctx.beginPath();
    ctx.arc(x, y, craterRadius, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
  }

  ctx.restore();

  ctx.lineWidth = Math.max(1.5, borderWidth * 0.34);
  ctx.strokeStyle = moonTone("outerEdge");
  ctx.beginPath();
  ctx.arc(0, 0, Math.max(0, radius - ctx.lineWidth * 0.5), 0, Math.PI * 2);
  ctx.stroke();
}

function moonPatternSeed(entityId, rarity) {
  let seed = ((entityId || 0) * 1103515245 + (rarity || 0) * 2654435761) >>> 0;
  seed ^= seed >>> 16;
  seed = Math.imul(seed, 2246822507) >>> 0;
  seed ^= seed >>> 13;
  seed = Math.imul(seed, 3266489909) >>> 0;
  return seed >>> 0;
}

function seededRandom(seed) {
  let stateValue = seed || 0x6d2b79f5;
  return () => {
    stateValue = (stateValue + 0x6d2b79f5) >>> 0;
    let t = stateValue;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function moonTone(part) {
  switch (part) {
    case "border":
      return "#6b6b6b";
    case "outerEdge":
      return "#676767";
    case "craterBorder":
      return "#838383";
    case "craterLight":
      return "#a0a0a0";
    case "crater":
      return "#8a8a8a";
    case "base":
    default:
      return "#939393";
  }
}

function drawDustParticlePetal(size, rarity) {
  const drawSize = size * 0.52;
  const dustPoints = [
    [65.073, 47.857],
    [60.089, 56.514],
    [49.849, 55.46],
    [44.936, 47.857],
    [50.186, 39.004],
    [59.562, 39.732],
  ];
  const sourceCenterX = 55;
  const sourceCenterY = 55;
  const scale = drawSize / 24;

  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-sourceCenterX, -sourceCenterY);
  ctx.fillStyle = "rgba(102, 102, 102, 0.72)";
  ctx.strokeStyle = "rgba(0, 0, 0, 0.24)";
  ctx.lineWidth = 1.35;
  ctx.beginPath();
  for (let i = 0; i < dustPoints.length; i += 1) {
    const [x, y] = dustPoints[i];
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
  ctx.restore();
}

function drawDropPetalCard(petalType, rarity, pos, radius) {
  const size = radius * worldDropSizeScale;
  if (size < 0.5) return;
  const base = petalBaseImage(rarity);
  const copy = effectivePetalCardCopy(petalType, rarity);
  const splitLabel = copy > 1;
  const icon = petalCardIconImage(petalType, rarity, { stripped: splitLabel });
  const label = splitLabel ? petalLabelImage(petalType, rarity) : null;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  drawPetalCardShadow(size);

  if (imageReady(base)) {
    drawCenteredImage(base, size, 1);
  } else {
    drawPetalCardFallback(size, rarity);
  }

  drawPetalCardCopies(icon, size, rarity, copy, petalType);
  if (splitLabel && imageReady(label)) {
    drawCenteredImage(label, size, petalCardIconScale);
  }

  ctx.restore();
}

function drawPetalCardCopies(icon, size, rarity, copy, petalType = 0) {
  const layout = petalCopyLayout(copy, petalType);
  for (const part of layout) {
    ctx.save();
    if (layout.length > 1) {
      ctx.translate((size * part.x) / 100, (size * part.y) / 100);
      ctx.rotate(part.angle);
    }
    const scale = petalCardIconScale * (layout.length > 1 ? part.scale : 1);
    if (imageReady(icon)) {
      drawCenteredImage(icon, size, scale);
    } else {
      drawPetalSilhouette(size * scale, rarity);
    }
    ctx.restore();
  }
}

function petalIconImage(petalType, rarity = 0) {
  return assetImage(petalIconPath(petalType, rarity));
}

function petalCardIconImage(petalType, rarity = 0, options = {}) {
  const src = petalIconPath(petalType, rarity);
  if (!src) return null;
  if (options.stripped)
    return transformedPetalImage(src, stripLivePetalLabel, "card-stripped");
  return assetImage(src);
}

function petalLabelImage(petalType, rarity = 0) {
  const src = petalIconPath(petalType, rarity);
  if (!src) return null;
  return transformedPetalImage(src, extractPetalLabelSvg, "card-label");
}

function transformedPetalImage(src, transformSvg, cachePrefix) {
  const cacheKey = `${cachePrefix}:${src}`;
  let entry = transformedPetalImages.get(cacheKey);
  if (entry) return entry.image;

  const image = new Image();
  image.decoding = "async";
  entry = { image, url: "" };
  transformedPetalImages.set(cacheKey, entry);

  fetch(src)
    .then((response) => {
      if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
      return response.text();
    })
    .then((svgText) => {
      const blob = new Blob([transformSvg(svgText)], { type: "image/svg+xml" });
      const url = URL.createObjectURL(blob);
      entry.url = url;
      image.addEventListener("load", () => requestAnimationFrame(drawScene), {
        once: true,
      });
      image.addEventListener(
        "error",
        () => {
          image.failed = true;
          if (entry.url) URL.revokeObjectURL(entry.url);
          requestAnimationFrame(drawScene);
        },
        { once: true },
      );
      image.src = url;
    })
    .catch(() => {
      image.failed = true;
      requestAnimationFrame(drawScene);
    });

  return image;
}

function livePetalIconImage(
  petalType,
  rarity = 0,
  transformSvg = stripLivePetalLabel,
  cacheSuffix = "live",
) {
  const src = petalIconPath(petalType, rarity, { live: true });
  if (!src) return null;

  const cacheKey = `${cacheSuffix}:${src}`;
  let entry = livePetalImages.get(cacheKey);
  if (entry) return entry.image;

  const image = new Image();
  image.decoding = "async";
  entry = { image, url: "" };
  livePetalImages.set(cacheKey, entry);

  fetch(src)
    .then((response) => {
      if (!response.ok) throw new Error(`Petal SVG failed: ${src}`);
      return response.text();
    })
    .then((svgText) => {
      const blob = new Blob([transformSvg(svgText)], { type: "image/svg+xml" });
      const url = URL.createObjectURL(blob);
      entry.url = url;
      image.addEventListener("load", () => requestAnimationFrame(drawScene), {
        once: true,
      });
      image.addEventListener(
        "error",
        () => {
          image.failed = true;
          if (entry.url) URL.revokeObjectURL(entry.url);
          requestAnimationFrame(drawScene);
        },
        { once: true },
      );
      image.src = url;
    })
    .catch(() => {
      image.failed = true;
      requestAnimationFrame(drawScene);
    });

  return image;
}

function extractSingleAntEggLiveSvg(svgText) {
  return extractFirstLivePetalPathsSvg(svgText, 2, "18 10 74 74");
}

function extractWaxLiveSvg(svgText) {
  return extractFirstLivePetalPathsSvg(svgText, 2, waxLivePetalViewBox);
}

function extractDandelionLiveSvg(svgText) {
  return extractFirstLivePetalPathsSvg(svgText, 3, "22 14 68 68");
}

function extractTrapperLiveSvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return stripLivePetalLabel(svgText);

  const doc = new DOMParser().parseFromString(
    stripLivePetalLabel(svgText),
    "image/svg+xml",
  );
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg")
    return stripLivePetalLabel(svgText);

  root.setAttribute("viewBox", trapperLivePetalViewBox);
  return new XMLSerializer().serializeToString(root);
}

function extractFirstLivePetalPathsSvg(svgText, pathCount, viewBox) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return stripLivePetalLabel(svgText);

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg")
    return stripLivePetalLabel(svgText);

  const paths = Array.from(root.querySelectorAll("path")).slice(0, pathCount);
  if (!paths.length) return stripLivePetalLabel(svgText);

  const serializer = new XMLSerializer();
  const body = paths.map((node) => serializer.serializeToString(node)).join("");
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}">${body}</svg>`;
}

function bandageOverlayImage() {
  const src = petalIconPath(petalBandageType);
  if (!src) return null;

  let entry = bandageOverlayImages.get(src);
  if (entry) return entry.image;

  const image = new Image();
  image.decoding = "async";
  entry = { image, url: "" };
  bandageOverlayImages.set(src, entry);

  fetch(src)
    .then((response) => {
      if (!response.ok) throw new Error(`Bandage SVG failed: ${src}`);
      return response.text();
    })
    .then((svgText) => {
      const blob = new Blob([extractBandageOverlaySvg(svgText)], {
        type: "image/svg+xml",
      });
      const url = URL.createObjectURL(blob);
      entry.url = url;
      image.addEventListener("load", () => requestAnimationFrame(drawScene), {
        once: true,
      });
      image.addEventListener(
        "error",
        () => {
          image.failed = true;
          if (entry.url) URL.revokeObjectURL(entry.url);
          requestAnimationFrame(drawScene);
        },
        { once: true },
      );
      image.src = url;
    })
    .catch(() => {
      image.failed = true;
      requestAnimationFrame(drawScene);
    });

  return image;
}

function antennaeOverlayImage() {
  const src = petalIconPath(petalAntennaeType);
  if (!src) return null;
  return transformedPetalImage(src, stripLivePetalLabel, "antennae-overlay");
}

function douliOverlayImage() {
  const src = petalIconPath(petalDouliType);
  if (!src) return null;
  return transformedPetalImage(src, stripLivePetalLabel, "douli-overlay");
}

function thirdEyeOverlayImage() {
  const src = petalIconPath(petalThirdEyeType);
  if (!src) return null;
  return transformedPetalImage(src, stripLivePetalLabel, "third-eye-overlay");
}

function stripLivePetalLabel(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  root.querySelectorAll("text").forEach((node) => node.remove());

  const paths = Array.from(root.querySelectorAll("path"));
  const remove = new Set();
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      remove.add(outline);
      remove.add(fill);
      i += 1;
    }
  }
  remove.forEach((node) => node.remove());

  return new XMLSerializer().serializeToString(root);
}

function extractBandageOverlaySvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  root.querySelectorAll("text").forEach((node) => node.remove());
  root.querySelectorAll("path[fill-opacity]").forEach((node) => node.remove());

  const paths = Array.from(root.querySelectorAll("path"));
  const remove = new Set();
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      remove.add(outline);
      remove.add(fill);
      i += 1;
    }
  }
  remove.forEach((node) => node.remove());

  return new XMLSerializer().serializeToString(root);
}

function extractPetalLabelSvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 110 110"></svg>`;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg")
    return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 110 110"></svg>`;

  const serializer = new XMLSerializer();
  const labelNodes = [];
  root.querySelectorAll("text").forEach((node) => labelNodes.push(node));

  const paths = Array.from(root.querySelectorAll("path"));
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      labelNodes.push(outline, fill);
      i += 1;
    }
  }

  const viewBox = root.getAttribute("viewBox") || "0 0 110 110";
  const body = labelNodes
    .map((node) => serializer.serializeToString(node))
    .join("");
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}">${body}</svg>`;
}

function extractMimicLabelSvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return svgText;

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  const paths = Array.from(root.querySelectorAll("path"));
  const labelPaths = [];
  for (let i = 0; i < paths.length - 1; i += 1) {
    const outline = paths[i];
    const fill = paths[i + 1];
    if (isLabelOutlinePath(outline) && isMatchingLabelFillPath(outline, fill)) {
      labelPaths.push(outline, fill);
      i += 1;
    }
  }

  const serializer = new XMLSerializer();
  const body = labelPaths
    .map((node) => serializer.serializeToString(node))
    .join("");
  if (!body) return svgText;
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="24 68 64 22">${body}</svg>`;
}

function isLabelOutlinePath(path) {
  const stroke = normalizeColorAttr(path.getAttribute("stroke"));
  const fill = normalizeColorAttr(path.getAttribute("fill"));
  const strokeWidth = Number.parseFloat(
    path.getAttribute("stroke-width") || "0",
  );
  return (
    (stroke === "#000" || stroke === "#000000") &&
    (!fill || fill === "none") &&
    strokeWidth >= 1.5
  );
}

function isMatchingLabelFillPath(outline, fill) {
  if (!fill) return false;
  const color = normalizeColorAttr(fill.getAttribute("fill"));
  return (
    (color === "#fff" || color === "#ffffff") &&
    outline.getAttribute("d") === fill.getAttribute("d")
  );
}

function normalizeColorAttr(value) {
  return String(value || "")
    .trim()
    .toLowerCase();
}

function petalBaseImage(rarity) {
  return assetImage(petalBasePath(rarity));
}

function drawCenteredImage(image, size, scale) {
  const drawSize = size * scale;
  if (drawSize < 0.5) return;
  ctx.drawImage(image, -drawSize * 0.5, -drawSize * 0.5, drawSize, drawSize);
}

function drawPetalCardShadow(size) {
  const half = size * 0.5;
  ctx.save();
  ctx.globalAlpha *= 0.18;
  ctx.fillStyle = "#142033";
  ctx.fillRect(-half + size * 0.05, -half + size * 0.07, size, size);
  ctx.restore();
}

function drawPetalCardFallback(size, rarity) {
  const half = size * 0.5;
  ctx.fillStyle = rarityColor(rarity || 1, 0.95);
  ctx.fillRect(-half, -half, size, size);
  ctx.lineWidth = Math.max(2, size * 0.08);
  ctx.strokeStyle = "rgba(45, 55, 68, 0.42)";
  ctx.strokeRect(-half, -half, size, size);
}

function drawPetalSilhouette(size, rarity) {
  const scale = size / 110;
  ctx.save();
  ctx.scale(scale, scale);
  ctx.fillStyle = rarityColor(rarity || 1, 0.82);
  ctx.strokeStyle = "rgba(35, 44, 54, 0.52)";
  ctx.lineWidth = 4.5;
  ctx.beginPath();
  ctx.ellipse(-12, -2, 15, 30, -0.64, 0, Math.PI * 2);
  ctx.ellipse(12, -2, 15, 30, 0.64, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
  ctx.restore();
}

function isMobSnap(snap) {
  return (
    snap &&
    snap.entityType > 0 &&
    !isPetalEntity(snap.entityType) &&
    !isDropEntity(snap.entityType) &&
    snap.entityType !== playerFlowerType &&
    snap.entityType !== portalType &&
    !isBloodSacrificeEffectSnap(snap) &&
    !isProjectileSnap(snap)
  );
}

function isSummonedMobSnap(snap) {
  return (
    !!snap &&
    (((snap.flags || 0) & flagSummoned) !== 0 ||
      snap.entityType === summonedBeetleType ||
      snap.entityType === summonedSoldierAntType)
  );
}

function isProjectileSnap(snap) {
  return (
    !!snap &&
    (snap.entityType === dandelionMissileType ||
      snap.entityType === hornetMissileType ||
      snap.entityType === pollenProjectileType ||
      snap.entityType === trapProjectileType ||
      snap.entityType === spiderWebZoneType)
  );
}

function isBloodSacrificeEffectSnap(snap) {
  return !!snap && snap.entityType === bloodSacrificeEffectType;
}

function isEffectZoneSnap(snap) {
  return !!snap && snap.entityType === spiderWebZoneType;
}

function isMobHitboxSnap(snap) {
  return (
    snap &&
    snap.entityType > 0 &&
    !isPetalEntity(snap.entityType) &&
    !isDropEntity(snap.entityType) &&
    snap.entityType !== portalType &&
    !isBloodSacrificeEffectSnap(snap) &&
    !isProjectileSnap(snap)
  );
}

function hasHealthFrame(snap) {
  return (
    snap &&
    snap.entityType > 0 &&
    !isPetalEntity(snap.entityType) &&
    !isDropEntity(snap.entityType) &&
    snap.entityType !== portalType &&
    !isBloodSacrificeEffectSnap(snap) &&
    !isProjectileSnap(snap)
  );
}

function mobDisplayName(snap) {
  return snap.name || mobTypeName(snap.entityType);
}

function rarityOrLevelLabel(snap) {
  if (snap.entityType === playerFlowerType) return `Lvl ${snap.rarity || 1}`;
  return rarityName(snap.rarity);
}

function drawCapsuleBar(
  x,
  y,
  width,
  height,
  progress,
  fill,
  border = 4,
  track = "#09090b",
) {
  const safeWidth = Math.max(1, width);
  const safeHeight = Math.max(1, height);
  const inset = Math.min(border, safeWidth * 0.35, safeHeight * 0.35);
  const innerX = x + inset;
  const innerY = y + inset;
  const innerW = Math.max(0, safeWidth - inset * 2);
  const innerH = Math.max(0, safeHeight - inset * 2);
  const amount = clamp(progress || 0, 0, 1);

  drawSolidProgressBar(ctx, {
    x,
    y,
    maxLength: safeWidth,
    width: safeHeight,
    color: "#050506",
  });

  if (innerW <= 0 || innerH <= 0) return;

  drawSolidProgressBar(ctx, {
    x: innerX,
    y: innerY,
    maxLength: innerW,
    width: innerH,
    color: track,
  });
  drawSolidProgressBar(ctx, {
    x: innerX,
    y: innerY,
    maxLength: innerW,
    width: innerH,
    progress: amount,
    color: fill,
  });
}

function healthBarColor(snap) {
  return entityHasState(snap, stateInvincible) ? "#e8d64b" : "#68d443";
}

function drawShieldBar(x, y, width, height, shieldPercent) {
  const shield = clamp(shieldPercent || 0, 0, 1);
  if (shield <= 0) return;

  const shieldLength = width * 0.94;
  const shieldWidth = height * 0.72;
  drawCapsuleBar(
    x + (width - shieldLength) * 0.5,
    y + (height - shieldWidth) * 0.5,
    shieldLength,
    shieldWidth,
    shield,
    "#65c9f2",
    Math.max(0.5, shieldWidth * 0.16),
    "#123141",
  );
}

function drawLayeredCapsuleBar(x, y, width, height, members, fill, border = 5) {
  const entries = (members || [])
    .map((member) => ({
      hp: clamp(member?.hp || 0, 0, 1),
      count: Math.max(1, Math.floor(member?.count || 1)),
    }))
    .filter((member) => Number.isFinite(member.hp) && member.count > 0);
  const inset = Math.min(border, width * 0.35, height * 0.35);
  const innerX = x + inset;
  const innerY = y + inset;
  const innerW = Math.max(0, width - inset * 2);
  const innerH = Math.max(0, height - inset * 2);

  drawSolidProgressBar(ctx, {
    x,
    y,
    maxLength: width,
    width: height,
    color: "#050506",
  });

  if (innerW <= 0 || innerH <= 0) return;

  drawSolidProgressBar(ctx, {
    x: innerX,
    y: innerY,
    maxLength: innerW,
    width: innerH,
    color: "#111115",
  });

  const totalCount = entries.reduce((sum, member) => sum + member.count, 0);
  if (totalCount <= 0) return;

  for (const member of entries) {
    ctx.save();
    const geometry = traceSolidProgressBarPath(ctx, {
      x: innerX,
      y: innerY,
      maxLength: innerW,
      width: innerH,
      progress: member.hp,
    });
    if (!geometry) {
      ctx.restore();
      continue;
    }

    // A short capsule contributes (m * p) / (d * copy_num) per logical copy.
    const perCopyAlpha = geometry.alpha / totalCount;
    const bucketAlpha = 1 - Math.pow(1 - perCopyAlpha, member.count);
    ctx.globalAlpha *= bucketAlpha;
    ctx.fillStyle = fill;
    ctx.fill();
    ctx.restore();
  }
}

function drawOutlinedText(
  text,
  x,
  y,
  {
    font = "700 14px Segoe UI, Microsoft YaHei, sans-serif",
    fill = "#fff",
    stroke = "#050506",
    lineWidth = 4,
    align = "center",
    baseline = "middle",
  } = {},
) {
  ctx.font = font;
  ctx.textAlign = align;
  ctx.textBaseline = baseline;
  ctx.lineJoin = "round";
  ctx.lineWidth = lineWidth;
  ctx.strokeStyle = stroke;
  ctx.fillStyle = fill;
  ctx.strokeText(text, x, y);
  ctx.fillText(text, x, y);
}

function hitboxStyleForSnap(snap) {
  if (isBloodSacrificeEffectSnap(snap)) {
    return { stroke: "rgba(118, 0, 0, 0.96)", fill: "rgba(118, 0, 0, 0.13)" };
  }
  if (isEffectZoneSnap(snap)) {
    return {
      stroke: "rgba(36, 255, 112, 0.96)",
      fill: "rgba(36, 255, 112, 0.10)",
    };
  }
  if (isPetalEntity(snap?.entityType) || isDropEntity(snap?.entityType)) {
    return {
      stroke: rarityColor(snap.rarity || 1, 0.96),
      fill: rarityColor(snap.rarity || 1, 0.12),
    };
  }
  if (isProjectileSnap(snap)) {
    return {
      stroke: "rgba(190, 82, 255, 0.96)",
      fill: "rgba(190, 82, 255, 0.10)",
    };
  }
  if (isMobHitboxSnap(snap) || snap?.entityType === playerFlowerType) {
    return {
      stroke: "rgba(255, 48, 48, 0.96)",
      fill: "rgba(255, 48, 48, 0.10)",
    };
  }
  return {
    stroke: "rgba(116, 198, 255, 0.86)",
    fill: "rgba(116, 198, 255, 0.08)",
  };
}

function drawCollisionHitbox(pos, radius, style = hitboxStyleForSnap(null)) {
  if (radius < 0.5) return;

  ctx.save();
  ctx.lineWidth = Math.max(1.25, Math.min(3, radius * 0.08));
  ctx.strokeStyle = style.stroke;
  ctx.fillStyle = style.fill;
  ctx.beginPath();
  ctx.arc(pos.x, pos.y, radius, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();

  const cross = Math.max(3, Math.min(10, radius * 0.38));
  ctx.strokeStyle = "rgba(255, 255, 255, 0.95)";
  ctx.lineWidth = Math.max(1, Math.min(2, radius * 0.05));
  ctx.beginPath();
  ctx.moveTo(pos.x - cross, pos.y);
  ctx.lineTo(pos.x + cross, pos.y);
  ctx.moveTo(pos.x, pos.y - cross);
  ctx.lineTo(pos.x, pos.y + cross);
  ctx.stroke();
  ctx.restore();
}

function drawMobFrame(snap, pos, radius) {
  if (radius < entityFrameMinScreenRadius) return;
  if (shouldSkipMobFrame(snap, radius)) return;

  if (!hasHealthFrame(snap)) {
    drawEntityLabel(snap, pos, radius);
    return;
  }

  const width = Math.max(10, radius * 2.08);
  const barHeight = Math.max(3, radius * 0.14);
  const y = pos.y + radius + Math.max(4, radius * 0.2);
  const x = pos.x - width * 0.5;
  const hp = clamp(snap.hpPercent, 0, 1);
  const isPlayerFlower = snap.entityType === playerFlowerType;
  const labelColor = isPlayerFlower
    ? rarityColor(playerLevelRarity(snap), 1)
    : rarityColor(snap.rarity, 1);
  const textSize = Math.max(4, radius * (isPlayerFlower ? 0.24 : 0.2));
  const strokeColor = "#050506";

  drawOutlinedText(mobDisplayName(snap), x, y - Math.max(1, radius * 0.05), {
    font: `800 ${textSize}px Segoe UI, Microsoft YaHei, sans-serif`,
    fill: playerNameColor(snap, "#f7f7f7"),
    stroke: strokeColor,
    lineWidth: Math.max(1.1, textSize * 0.42),
    align: "left",
    baseline: "bottom",
  });

  drawCapsuleBar(
    x,
    y,
    width,
    barHeight,
    hp,
    healthBarColor(snap),
    Math.max(1, barHeight * 0.28),
  );
  drawShieldBar(x, y, width, barHeight, snap.shieldPercent);

  drawOutlinedText(
    rarityOrLevelLabel(snap),
    x + width,
    y + barHeight + Math.max(1, radius * 0.05),
    {
      font: `800 ${textSize}px Segoe UI, Microsoft YaHei, sans-serif`,
      fill: labelColor,
      stroke: strokeColor,
      lineWidth: Math.max(1.1, textSize * 0.42),
      align: "right",
      baseline: "top",
    },
  );
}

function drawEntityLabel(snap, pos, radius) {
  if (!snap.name || radius < entityFrameMinScreenRadius) return;
  if (shouldSkipMobFrame(snap, radius)) return;
  ctx.font = "12px Segoe UI, Microsoft YaHei, sans-serif";
  ctx.textAlign = "center";
  ctx.textBaseline = "bottom";
  ctx.fillStyle = playerNameColor(snap, "rgba(39, 50, 65, 0.86)");
  ctx.fillText(snap.name, pos.x, pos.y - radius - 10);
}

function playerNameColor(snap, fallback) {
  if (
    snap?.entityType === playerFlowerType &&
    snap.entityId !== state.ownerEntityId &&
    state.squadMemberNames?.has(snap.name)
  )
    return "#f17cab";
  return fallback;
}

function currentSquadMemberEntities() {
  if (!state.squadMemberNames?.size) return [];

  const membersByName = new Map();
  for (const entity of state.entities.values()) {
    const snap = entity?.snapshot;
    if (
      !snap ||
      entity.dying ||
      snap.entityType !== playerFlowerType ||
      snap.entityId === state.ownerEntityId ||
      !state.squadMemberNames.has(snap.name)
    )
      continue;
    membersByName.set(snap.name, entity);
  }

  return Array.from(state.squadMemberNames, (name) => membersByName.get(name)).filter(
    Boolean,
  );
}

function shouldSkipMobFrame(snap, radius) {
  if (currentRenderLoad <= 0 || !snap) return false;
  if (
    snap.entityType === playerFlowerType ||
    snap.entityId === state.ownerEntityId
  )
    return false;
  if (raritySortRank(snap.rarity) >= raritySortRank(bossRarity)) return false;
  const minRadius =
    currentRenderLoad >= 2
      ? entityFrameMinScreenRadius * 2.5
      : entityFrameMinScreenRadius * 1.5;
  return radius < minRadius;
}

function bossBarCandidates(entities) {
  const owner = state.entities.get(state.ownerEntityId);
  const origin = owner ? owner.renderPos : state.camera;
  const groups = new Map();

  for (const entity of entities) {
    const snap = entity.snapshot;
    if (!snap || entity.dying || !isMobSnap(snap) || snap.rarity < bossRarity)
      continue;
    if (snap.entityType === dummyType) continue;
    if (isSummonedMobSnap(snap)) continue;
    const invincible = entityHasState(snap, stateInvincible);
    const key = `${snap.entityType}:${snap.rarity}:${invincible ? 1 : 0}`;
    let group = groups.get(key);
    if (!group) {
      group = {
        key,
        entityType: snap.entityType,
        rarity: snap.rarity,
        name: mobDisplayName(snap),
        invincible,
        members: [],
        memberBuckets: new Map(),
        shieldMembers: [],
        shieldBuckets: new Map(),
        hasShield: false,
        count: 0,
        nearestDistance: Number.POSITIVE_INFINITY,
      };
      groups.set(key, group);
    }

    const distance = Math.hypot(
      entity.renderPos.x - origin.x,
      entity.renderPos.y - origin.y,
    );
    group.nearestDistance = Math.min(group.nearestDistance, distance);
    group.count += 1;
    const hp = Math.round(clamp(snap.hpPercent, 0, 1) * 1000) / 1000;
    let member = group.memberBuckets.get(hp);
    if (!member) {
      member = { hp, count: 0 };
      group.memberBuckets.set(hp, member);
      group.members.push(member);
    }
    member.count += 1;

    const shield =
      Math.round(clamp(snap.shieldPercent || 0, 0, 1) * 1000) / 1000;
    let shieldMember = group.shieldBuckets.get(shield);
    if (!shieldMember) {
      shieldMember = { hp: shield, count: 0 };
      group.shieldBuckets.set(shield, shieldMember);
      group.shieldMembers.push(shieldMember);
    }
    shieldMember.count += 1;
    if (shield > 0) group.hasShield = true;
  }

  return [...groups.values()]
    .sort(
      (a, b) =>
        raritySortRank(b.rarity) - raritySortRank(a.rarity) ||
        a.nearestDistance - b.nearestDistance,
    )
    .slice(0, maxBossBars);
}

function drawBossBars(entities) {
  const groups = bossBarCandidates(entities);
  if (groups.length === 0) return;

  const maxScreenWidth = Math.max(
    1,
    state.canvasWidth - bossBarHorizontalMarginPx * 2,
  );
  const width = Math.min(
    maxScreenWidth,
    bossBarMaxWidthPx,
    Math.max(bossBarMinWidthPx, state.canvasWidth * bossBarWidthRatio),
  );
  const bandTop = Math.max(0, state.canvasHeight * bossBarBandTopRatio);
  const bandBottom = Math.max(
    bandTop + 1,
    state.canvasHeight * bossBarBandBottomRatio,
  );
  const slotHeight = (bandBottom - bandTop) / groups.length;
  const outlinePadding =
    Math.max(bossBarTitleOutlinePx, bossBarRarityOutlinePx) * 0.5;
  const naturalBlockHeight =
    outlinePadding * 2 +
    bossBarTitleFontPx +
    bossBarLabelGapPx +
    bossBarHeightPx +
    bossBarLabelGapPx +
    bossBarRarityFontPx;
  const scale = Math.min(1, slotHeight / naturalBlockHeight);
  const height = bossBarHeightPx * scale;
  const titleFontSize = bossBarTitleFontPx * scale;
  const rarityFontSize = bossBarRarityFontPx * scale;
  const labelGap = bossBarLabelGapPx * scale;
  const blockHeight = naturalBlockHeight * scale;
  const x = (state.canvasWidth - width) * 0.5;

  groups.forEach((group, index) => {
    const slotTop = bandTop + index * slotHeight;
    const y =
      slotTop +
      (slotHeight - blockHeight) * 0.5 +
      outlinePadding * scale +
      titleFontSize +
      labelGap;
    const rarityFill = rarityColor(group.rarity, 1);
    const count = Math.max(1, group.count || group.members.length);
    const title = count > 1 ? `${group.name} x${count}` : group.name;

    ctx.save();
    drawOutlinedText(title, x + width * 0.5, y - labelGap, {
      font: `900 ${titleFontSize}px Segoe UI, Microsoft YaHei, sans-serif`,
      fill: "#f7f7f7",
      lineWidth: bossBarTitleOutlinePx * scale,
      baseline: "bottom",
    });
    drawLayeredCapsuleBar(
      x,
      y,
      width,
      height,
      group.members,
      group.invincible ? "#e8d64b" : "#69d33e",
      bossBarRadiusPx * scale,
    );
    if (group.hasShield) {
      const shieldLength = width * 0.94;
      const shieldHeight = height * 0.72;
      drawLayeredCapsuleBar(
        x + (width - shieldLength) * 0.5,
        y + (height - shieldHeight) * 0.5,
        shieldLength,
        shieldHeight,
        group.shieldMembers,
        "#65c9f2",
        Math.max(0.5, shieldHeight * 0.16),
      );
    }
    drawOutlinedText(
      rarityName(group.rarity),
      x + width * 0.5,
      y + height + labelGap,
      {
        font: `900 ${rarityFontSize}px Segoe UI, Microsoft YaHei, sans-serif`,
        fill: rarityFill,
        lineWidth: bossBarRarityOutlinePx * scale,
        baseline: "top",
      },
    );
    ctx.restore();
  });
}

function drawSelfHud() {
  const owner = state.authenticated
    ? state.entities.get(state.ownerEntityId)
    : null;
  if (!owner) return;

  const health = owner ? clamp(owner.snapshot.hpPercent || 0, 0, 1) : 0;
  const expProgress = clamp(state.ownerExpProgress || 0, 0, 1);
  const name = owner.snapshot?.name || "Player";
  const level = Math.max(1, state.ownerLevel || owner?.snapshot?.rarity || 1);
  const levelColor = rarityColor(playerLevelRarity(owner.snapshot), 1);
  const fadeProgress = owner.dying
    ? clamp((owner.deathAge || 0) / deathFadeDuration, 0, 1)
    : 0;
  const fadeEase = fadeProgress * fadeProgress * (3 - fadeProgress * 2);
  const fadeAlpha = owner.dying ? 1 - fadeEase : 1;
  const fadeScale = owner.dying ? 1 + fadeEase * deathScaleBoost : 1;
  const ownerDead = (owner.snapshot.flags & flagDead) !== 0;
  const scale = Math.min(1, Math.max(0.7, state.canvasWidth / 320));
  const flowerPos = { x: 55 * scale, y: 60 * scale };
  const flowerRadius = 30 * scale;
  const healthX = 94 * scale;
  const healthY = 43 * scale;
  const healthW = 220 * scale;
  const healthH = 36 * scale;
  const healthBackX = 57 * scale;
  const healthBackW = healthX + healthW - healthBackX;
  const expX = 112 * scale;
  const expY = 85 * scale;
  const expW = 184 * scale;
  const expH = 20 * scale;

  ctx.save();
  ctx.globalAlpha *= fadeAlpha;
  drawCapsuleBar(
    healthBackX,
    healthY,
    healthBackW,
    healthH,
    0,
    "#69d348",
    6 * scale,
  );
  drawCapsuleBar(
    healthX,
    healthY,
    healthW,
    healthH,
    health,
    healthBarColor(owner.snapshot),
    6 * scale,
  );
  drawShieldBar(
    healthX,
    healthY,
    healthW,
    healthH,
    owner.snapshot.shieldPercent,
  );
  drawCapsuleBar(expX, expY, expW, expH, expProgress, "#e8e95a", 4 * scale);
  drawPlayerFlower(
    owner.snapshot,
    flowerPos,
    flowerRadius * fadeScale,
    ownerDead
      ? (owner.deathAngle ?? owner.renderAngle ?? owner.snapshot.angle)
      : (owner.renderAngle ?? owner.snapshot.angle),
    true,
  );
  drawOutlinedText(name, healthX + healthW * 0.5, healthY + healthH * 0.5, {
    font: `900 ${26 * scale}px Segoe UI, Microsoft YaHei, sans-serif`,
    fill: "#f7f7f7",
    lineWidth: 5 * scale,
  });

  drawOutlinedText(`Lvl ${level}`, expX + expW * 0.5, expY + expH * 0.5, {
    font: `900 ${15.5 * scale}px Segoe UI, Microsoft YaHei, sans-serif`,
    fill: levelColor,
    lineWidth: 3.8 * scale,
  });
  ctx.restore();
}

function drawSquadHud() {
  const members = currentSquadMemberEntities().slice(0, 3);
  if (!members.length) return;

  const scale = Math.min(1, Math.max(0.7, state.canvasWidth / 320));
  const memberHudScale = 0.6;
  const flowerX = 55 * memberHudScale * scale;
  const flowerRadius = 30 * memberHudScale * scale;
  const healthX = 94 * memberHudScale * scale;
  const healthW = 220 * memberHudScale * scale;
  const healthH = 36 * memberHudScale * scale;
  const healthBackX = 57 * memberHudScale * scale;
  const healthBackW = healthX + healthW - healthBackX;
  const healthBorder = 6 * memberHudScale * scale;
  const firstCenterY = 131 * scale;
  const rowGap = 42 * scale;

  for (let index = 0; index < members.length; index += 1) {
    const entity = members[index];
    const snap = entity.snapshot;
    const centerY = firstCenterY + index * rowGap;
    const health = clamp(snap.hpPercent || 0, 0, 1);
    const level = Math.max(1, snap.rarity || 1);
    const levelText = `Lvl ${level}`;
    const levelFont = `900 ${11.5 * scale}px Segoe UI, Microsoft YaHei, sans-serif`;
    const levelX = healthX + healthW - 5 * scale;

    drawCapsuleBar(
      healthBackX,
      centerY - healthH * 0.5,
      healthBackW,
      healthH,
      0,
      "#69d348",
      healthBorder,
    );
    drawCapsuleBar(
      healthX,
      centerY - healthH * 0.5,
      healthW,
      healthH,
      health,
      healthBarColor(snap),
      healthBorder,
    );
    drawShieldBar(
      healthX,
      centerY - healthH * 0.5,
      healthW,
      healthH,
      snap.shieldPercent,
    );
    drawPlayerFlower(
      snap,
      { x: flowerX, y: centerY },
      flowerRadius,
      (snap.flags & flagDead) !== 0
        ? (entity.deathAngle ?? entity.renderAngle ?? snap.angle)
        : (entity.renderAngle ?? snap.angle),
      false,
    );

    ctx.font = levelFont;
    const levelWidth = ctx.measureText(levelText).width;
    const maxNameWidth = Math.max(
      15 * scale,
      healthW - levelWidth - 16 * scale,
    );
    let nameSize = 14 * scale;
    ctx.font = `900 ${nameSize}px Segoe UI, Microsoft YaHei, sans-serif`;
    const measuredNameWidth = ctx.measureText(snap.name || "Player").width;
    if (measuredNameWidth > maxNameWidth)
      nameSize = Math.max(9 * scale, nameSize * (maxNameWidth / measuredNameWidth));

    drawOutlinedText(snap.name || "Player", healthX + 6 * scale, centerY, {
      font: `900 ${nameSize}px Segoe UI, Microsoft YaHei, sans-serif`,
      fill: "#f7f7f7",
      lineWidth: 3 * scale,
      align: "left",
    });
    drawOutlinedText(levelText, levelX, centerY, {
      font: levelFont,
      fill: rarityColor(playerLevelRarity(snap), 1),
      lineWidth: 2.8 * scale,
      align: "right",
    });
  }
}

function minimapLayout() {
  const y = minimapMarginPx;
  const maxByScreen = Math.max(
    96,
    Math.min(
      state.canvasWidth - minimapMarginPx * 2,
      state.canvasHeight - y - minimapMarginPx,
    ),
  );
  const localSize = clamp(
    Math.min(state.canvasWidth * 0.28, minimapLocalMaxPx, maxByScreen),
    104,
    minimapLocalMaxPx,
  );
  const fullSize = clamp(
    Math.min(
      state.canvasWidth * 0.38,
      state.canvasHeight * 0.42,
      minimapFullMaxPx,
      maxByScreen,
    ),
    132,
    minimapFullMaxPx,
  );
  const size = state.minimapMode === "full" ? fullSize : localSize;
  return { x: state.canvasWidth - minimapMarginPx - size, y, size };
}

function minimapWorldBounds(map) {
  const mapWidth = Math.max(
    1,
    (map.width || map.layers?.[0]?.width || 1) * map.tileWidth,
  );
  const mapHeight = Math.max(
    1,
    (map.height || map.layers?.[0]?.height || 1) * map.tileHeight,
  );
  if (state.minimapMode === "full") {
    const side = Math.max(mapWidth, mapHeight);
    return { left: 0, top: 0, width: side, height: side };
  }

  const tileSize = Math.max(map.tileWidth || 1, map.tileHeight || 1);
  const side = Math.max(tileSize * 14, state.viewRadius * 2.35, tileSize);
  return {
    left: state.camera.x - side * 0.5,
    top: state.camera.y - side * 0.5,
    width: side,
    height: side,
  };
}

function drawMinimap() {
  const map = state.map;
  if (!map || !map.tileWidth || !map.tileHeight) {
    minimapHitRect = null;
    return;
  }

  const layout = minimapLayout();
  minimapHitRect = layout;
  const bounds = minimapWorldBounds(map);
  const scale = layout.size / Math.max(bounds.width, bounds.height, 1);
  const cache = getMinimapCache(map);

  ctx.save();
  ctx.beginPath();
  ctx.rect(layout.x, layout.y, layout.size, layout.size);
  ctx.clip();
  ctx.fillStyle = colorfulMinimapEnabled()
    ? minimapBackgroundColor
    : minimapMonochromeBackgroundColor;
  ctx.fillRect(layout.x, layout.y, layout.size, layout.size);
  ctx.imageSmoothingEnabled = false;
  if (cache?.canvas)
    drawMinimapRasterLayer(cache.canvas, cache, layout, bounds);
  if (state.altFeaturesEnabled && cache?.difficultyCanvas)
    drawMinimapRasterLayer(cache.difficultyCanvas, cache, layout, bounds);

  for (const teammate of currentSquadMemberEntities()) {
    const teammatePos = teammate.renderPos || teammate.snapshot?.pos;
    if (!teammatePos) continue;
    const teammateX = layout.x + (teammatePos.x - bounds.left) * scale;
    const teammateY = layout.y + (teammatePos.y - bounds.top) * scale;
    if (
      teammateX < layout.x ||
      teammateX > layout.x + layout.size ||
      teammateY < layout.y ||
      teammateY > layout.y + layout.size
    )
      continue;

    ctx.fillStyle = "#f17cab";
    ctx.strokeStyle = "rgba(45, 24, 38, 0.88)";
    ctx.lineWidth = state.minimapMode === "full" ? 1.5 : 1.8;
    ctx.beginPath();
    ctx.arc(
      teammateX,
      teammateY,
      state.minimapMode === "full" ? 3.5 : 4,
      0,
      Math.PI * 2,
    );
    ctx.fill();
    ctx.stroke();

    if (state.minimapMode === "full")
      drawOutlinedText(
        teammate.snapshot.name || "Player",
        teammateX,
        teammateY - 6,
        {
          font: "800 10px Segoe UI, Microsoft YaHei, sans-serif",
          fill: "#f17cab",
          stroke: "rgba(26, 21, 27, 0.94)",
          lineWidth: 3,
          baseline: "bottom",
        },
      );
  }

  const owner = state.entities.get(state.ownerEntityId);
  const ownerPos = owner?.renderPos || owner?.snapshot?.pos || state.camera;
  const px = layout.x + (ownerPos.x - bounds.left) * scale;
  const py = layout.y + (ownerPos.y - bounds.top) * scale;
  if (
    px >= layout.x &&
    px <= layout.x + layout.size &&
    py >= layout.y &&
    py <= layout.y + layout.size
  ) {
    ctx.fillStyle = "#ffd84f";
    ctx.beginPath();
    ctx.arc(px, py, state.minimapMode === "full" ? 3 : 4, 0, Math.PI * 2);
    ctx.fill();
  }

  ctx.restore();
}

function colorfulMinimapEnabled() {
  return getClientConfigValue(state.clientConfig, "colorful_map") >= 0.5;
}

function minimapGridMetrics(map) {
  const subdivisions = Math.max(
    1,
    Math.floor(map?.minimapSubdivisions || minimapTileSubdivisions),
  );
  return {
    subdivisions,
    cellWidth: map.tileWidth / subdivisions,
    cellHeight: map.tileHeight / subdivisions,
  };
}

function getMinimapCache(map) {
  const grid = minimapGridMetrics(map);
  const columns = Math.max(
    1,
    Math.ceil(
      (map.width || map.layers?.[0]?.width || 1) * grid.subdivisions,
    ),
  );
  const rows = Math.max(
    1,
    Math.ceil(
      (map.height || map.layers?.[0]?.height || 1) * grid.subdivisions,
    ),
  );
  const colorful = colorfulMinimapEnabled();
  const key = [
    map.path,
    colorful ? "color" : "mono",
    grid.subdivisions,
    columns,
    rows,
    map.minimapTileMasks?.size || 0,
    map.minimapCollisionCells?.length || 0,
    map.spawnDifficultyCells?.length || 0,
  ].join(":");
  if (minimapCache?.map === map && minimapCache.key === key)
    return minimapCache;

  const resolveColor = createMinimapColorResolver();
  const backgroundColor = colorful
    ? minimapBackgroundColor
    : minimapMonochromeBackgroundColor;
  const canvas = document.createElement("canvas");
  canvas.width = columns;
  canvas.height = rows;
  const target = canvas.getContext("2d");
  if (!target) return null;
  const terrainPixels = target.createImageData(columns, rows);
  fillMinimapPixels(terrainPixels.data, resolveColor(backgroundColor));
  if (colorful)
    rasterizeMinimapTerrain(terrainPixels.data, columns, rows, map, grid, resolveColor);
  else
    rasterizeMinimapMonochromeTerrain(
      terrainPixels.data,
      columns,
      rows,
      map,
      resolveColor,
    );
  target.putImageData(terrainPixels, 0, 0);

  const difficultyCanvas = document.createElement("canvas");
  difficultyCanvas.width = columns;
  difficultyCanvas.height = rows;
  const difficultyTarget = difficultyCanvas.getContext("2d");
  if (difficultyTarget) {
    const difficultyPixels = difficultyTarget.createImageData(columns, rows);
    rasterizeMinimapDifficulty(
      difficultyPixels.data,
      columns,
      rows,
      map,
      resolveColor,
    );
    difficultyTarget.putImageData(difficultyPixels, 0, 0);
  }

  minimapCache = {
    map,
    key,
    canvas,
    difficultyCanvas: difficultyTarget ? difficultyCanvas : null,
    cellWidth: grid.cellWidth,
    cellHeight: grid.cellHeight,
    renderOriginX: 0,
    renderOriginY: 0,
  };
  return minimapCache;
}

function drawMinimapRasterLayer(canvasLayer, cache, layout, bounds) {
  const sourceX = bounds.left / cache.cellWidth - cache.renderOriginX;
  const sourceY = bounds.top / cache.cellHeight - cache.renderOriginY;
  const sourceWidth = bounds.width / cache.cellWidth;
  const sourceHeight = bounds.height / cache.cellHeight;
  if (sourceWidth <= 0 || sourceHeight <= 0) return;

  const clippedLeft = clamp(sourceX, 0, canvasLayer.width);
  const clippedTop = clamp(sourceY, 0, canvasLayer.height);
  const clippedRight = clamp(sourceX + sourceWidth, 0, canvasLayer.width);
  const clippedBottom = clamp(sourceY + sourceHeight, 0, canvasLayer.height);
  const clippedWidth = clippedRight - clippedLeft;
  const clippedHeight = clippedBottom - clippedTop;
  if (clippedWidth <= 0 || clippedHeight <= 0) return;

  const destinationX =
    layout.x + ((clippedLeft - sourceX) / sourceWidth) * layout.size;
  const destinationY =
    layout.y + ((clippedTop - sourceY) / sourceHeight) * layout.size;
  const destinationWidth = (clippedWidth / sourceWidth) * layout.size;
  const destinationHeight = (clippedHeight / sourceHeight) * layout.size;
  ctx.drawImage(
    canvasLayer,
    clippedLeft,
    clippedTop,
    clippedWidth,
    clippedHeight,
    destinationX,
    destinationY,
    destinationWidth,
    destinationHeight,
  );
}

function minimapTileColor(map, gid) {
  const material = map.minimapTileDefinitions?.get(gid)?.material || "";
  return minimapMaterialColors[material] || minimapUnknownMaterialColor;
}

function minimapCanonicalTileGid(raw) {
  return (raw >>> 0) & 0x1fffffff;
}

function transformedMinimapTileMask(map, raw, subdivisions) {
  const fullMask = 2 ** (subdivisions * subdivisions) - 1;
  const sourceMask =
    map.minimapTileMasks?.get(minimapCanonicalTileGid(raw)) ?? fullMask;
  const transform = minimapTileTransformMatrix(raw, subdivisions);
  if (!transform) return sourceMask;

  let transformed = 0;
  for (let sourceY = 0; sourceY < subdivisions; sourceY += 1) {
    for (let sourceX = 0; sourceX < subdivisions; sourceX += 1) {
      const sourceBit = 1 << (sourceY * subdivisions + sourceX);
      if ((sourceMask & sourceBit) === 0) continue;
      const targetX =
        transform.xx * sourceX +
        transform.xy * sourceY +
        transform.offsetX;
      const targetY =
        transform.yx * sourceX +
        transform.yy * sourceY +
        transform.offsetY;
      transformed |= 1 << (targetY * subdivisions + targetX);
    }
  }
  return transformed;
}

function composeMinimapTransform(outer, inner) {
  return {
    xx: outer.xx * inner.xx + outer.xy * inner.yx,
    xy: outer.xx * inner.xy + outer.xy * inner.yy,
    yx: outer.yx * inner.xx + outer.yy * inner.yx,
    yy: outer.yx * inner.xy + outer.yy * inner.yy,
    offsetX:
      outer.xx * inner.offsetX +
      outer.xy * inner.offsetY +
      outer.offsetX,
    offsetY:
      outer.yx * inner.offsetX +
      outer.yy * inner.offsetY +
      outer.offsetY,
  };
}

function minimapTileTransformMatrix(raw, subdivisions) {
  const unsigned = raw >>> 0;
  const flipH = (unsigned & 0x80000000) !== 0;
  const flipV = (unsigned & 0x40000000) !== 0;
  const flipD = (unsigned & 0x20000000) !== 0;
  if (!flipH && !flipV && !flipD) return null;

  const edge = subdivisions - 1;
  let transform = {
    xx: 1,
    xy: 0,
    yx: 0,
    yy: 1,
    offsetX: 0,
    offsetY: 0,
  };
  if (flipD)
    transform = composeMinimapTransform(
      { xx: 0, xy: 1, yx: 1, yy: 0, offsetX: 0, offsetY: 0 },
      transform,
    );
  if (flipH)
    transform = composeMinimapTransform(
      { xx: -1, xy: 0, yx: 0, yy: 1, offsetX: edge, offsetY: 0 },
      transform,
    );
  if (flipV)
    transform = composeMinimapTransform(
      { xx: 1, xy: 0, yx: 0, yy: -1, offsetX: 0, offsetY: edge },
      transform,
    );
  return transform;
}

function createMinimapColorResolver() {
  const parser = document.createElement("canvas");
  parser.width = 1;
  parser.height = 1;
  const parserContext = parser.getContext("2d", { willReadFrequently: true });
  const colors = new Map();

  return (color) => {
    const key = String(color || "transparent");
    const cached = colors.get(key);
    if (cached) return cached;
    if (!parserContext) return [0, 0, 0, 255];

    parserContext.clearRect(0, 0, 1, 1);
    parserContext.fillStyle = "#000000";
    parserContext.fillStyle = key;
    parserContext.fillRect(0, 0, 1, 1);
    const rgba = Array.from(parserContext.getImageData(0, 0, 1, 1).data);
    colors.set(key, rgba);
    return rgba;
  };
}

function fillMinimapPixels(pixels, rgba) {
  for (let offset = 0; offset < pixels.length; offset += 4) {
    pixels[offset] = rgba[0];
    pixels[offset + 1] = rgba[1];
    pixels[offset + 2] = rgba[2];
    pixels[offset + 3] = rgba[3];
  }
}

function setMinimapPixel(pixels, width, height, x, y, rgba) {
  if (x < 0 || x >= width || y < 0 || y >= height) return;
  const offset = (y * width + x) * 4;
  pixels[offset] = rgba[0];
  pixels[offset + 1] = rgba[1];
  pixels[offset + 2] = rgba[2];
  pixels[offset + 3] = rgba[3];
}

function rasterizeMinimapTerrain(
  pixels,
  width,
  height,
  map,
  grid,
  resolveColor,
) {
  const tileColors = new Map();
  // Tiled stores visual layers bottom-to-top, so later TMJ layers must win.
  for (const layer of map.layers || []) {
    if (
      layer.visible === false ||
      !layer.width ||
      !layer.height ||
      !layer.tiles?.length
    )
      continue;

    for (let y = 0; y < layer.height; y += 1) {
      for (let x = 0; x < layer.width; x += 1) {
        const raw = layer.tiles[y * layer.width + x] || 0;
        const gid = minimapCanonicalTileGid(raw);
        if (!gid) continue;

        let rgba = tileColors.get(gid);
        if (!rgba) {
          rgba = resolveColor(minimapTileColor(map, gid));
          tileColors.set(gid, rgba);
        }
        const mask = transformedMinimapTileMask(
          map,
          raw,
          grid.subdivisions,
        );
        for (let cellY = 0; cellY < grid.subdivisions; cellY += 1) {
          for (let cellX = 0; cellX < grid.subdivisions; cellX += 1) {
            const bit = 1 << (cellY * grid.subdivisions + cellX);
            if ((mask & bit) === 0) continue;
            setMinimapPixel(
              pixels,
              width,
              height,
              x * grid.subdivisions + cellX,
              y * grid.subdivisions + cellY,
              rgba,
            );
          }
        }
      }
    }
  }
}

function rasterizeMinimapMonochromeTerrain(
  pixels,
  width,
  height,
  map,
  resolveColor,
) {
  const wallColor = resolveColor(minimapMonochromeWallColor);
  for (const cell of map.minimapCollisionCells || [])
    setMinimapPixel(pixels, width, height, cell.x, cell.y, wallColor);
}

function minimapDifficultyRarity(difficulty) {
  const value = Number(difficulty);
  if (!Number.isFinite(value) || value <= 0) return 1;
  return clamp(Math.round(value / 10), 1, rarityPrimordial);
}

function rasterizeMinimapDifficulty(
  pixels,
  width,
  height,
  map,
  resolveColor,
) {
  const rarityColors = new Map();
  for (const cell of map.spawnDifficultyCells || []) {
    const rarity = minimapDifficultyRarity(cell.difficulty);
    let rgba = rarityColors.get(rarity);
    if (!rgba) {
      rgba = resolveColor(rarityColor(rarity, 1));
      rarityColors.set(rarity, rgba);
    }
    setMinimapPixel(pixels, width, height, cell.x, cell.y, rgba);
  }
}

function toggleMinimapMode() {
  state.minimapMode = state.minimapMode === "full" ? "local" : "full";
}

function eventCanvasPoint(event) {
  const rect = canvas.getBoundingClientRect();
  const sx = rect.width > 0 ? state.canvasWidth / rect.width : 1;
  const sy = rect.height > 0 ? state.canvasHeight / rect.height : 1;
  return {
    x: (event.clientX - rect.left) * sx,
    y: (event.clientY - rect.top) * sy,
  };
}

function isMinimapPoint(point) {
  const rect = minimapHitRect;
  return (
    !!rect &&
    point.x >= rect.x &&
    point.x <= rect.x + rect.size &&
    point.y >= rect.y &&
    point.y <= rect.y + rect.size
  );
}

function isEntityInRenderView(entity, scale) {
  const snap = entity?.snapshot;
  const pos = entity?.renderPos;
  if (!snap || !pos || scale <= 0) return false;
  if (snap.entityId === state.ownerEntityId) return true;

  const deathProgress = entity.dying
    ? clamp((entity.deathAge || 0) / deathFadeDuration, 0, 1)
    : 0;
  const deathEase = deathProgress * deathProgress * (3 - deathProgress * 2);
  const radius =
    Math.max(0, snap.radius || 0) *
    flowerMobCullScale(snap.entityType) *
    (entity.dying ? 1 + deathEase * deathScaleBoost : 1);
  const padding = renderCullPaddingPx / scale;
  const halfW = (state.canvasWidth * 0.5) / scale + radius + padding;
  const halfH = (state.canvasHeight * 0.5) / scale + radius + padding;
  return (
    pos.x >= state.camera.x - halfW &&
    pos.x <= state.camera.x + halfW &&
    pos.y >= state.camera.y - halfH &&
    pos.y <= state.camera.y + halfH
  );
}

function drawEntityPass(entities) {
  for (const entity of entities) drawEntity(entity);
}

function drawCheckpointZoneOutlines() {
  if (!state.altFeaturesEnabled) return;
  const zones = state.map?.checkpointZones;
  const scale = worldScale();
  if (!zones?.length || scale <= 0) return;

  const halfWidth = state.canvasWidth / (2 * scale);
  const halfHeight = state.canvasHeight / (2 * scale);
  const left = state.camera.x - halfWidth;
  const right = state.camera.x + halfWidth;
  const top = state.camera.y - halfHeight;
  const bottom = state.camera.y + halfHeight;

  ctx.save();
  ctx.filter = "none";
  ctx.globalAlpha = 1;
  ctx.globalCompositeOperation = "source-over";
  ctx.strokeStyle = checkpointZoneOutlineColor;
  ctx.lineWidth = checkpointZoneOutlineWidthPx;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  for (const zone of zones) {
    if (
      zone.maxX < left ||
      zone.minX > right ||
      zone.maxY < top ||
      zone.minY > bottom ||
      !zone.vertices?.length
    )
      continue;

    const first = worldToScreen(zone.vertices[0]);
    ctx.beginPath();
    ctx.moveTo(first.x, first.y);
    for (let i = 1; i < zone.vertices.length; i += 1) {
      const point = worldToScreen(zone.vertices[i]);
      ctx.lineTo(point.x, point.y);
    }
    if (zone.closed) ctx.closePath();
    ctx.stroke();
  }
  ctx.restore();
}

function drawHitboxPass(entities) {
  if (!state.debugHitbox && !state.altFeaturesEnabled) return;
  for (const entity of entities || []) {
    const snap = entity?.snapshot;
    if (!snap || !entity.renderPos) continue;
    const radius = Math.max(0, worldLengthToScreen(snap.radius));
    if (radius < 0.5) continue;
    drawCollisionHitbox(
      worldToScreen(entity.renderPos),
      radius,
      hitboxStyleForSnap(snap),
    );
  }
}

function drawSceneHitboxes(passes) {
  if (!state.debugHitbox && !state.altFeaturesEnabled) return;
  drawHitboxPass(passes.ground);
  drawHitboxPass(passes.underlay);
  drawHitboxPass(passes.world);
  drawHitboxPass(passes.petals);
  if (passes.owner) drawHitboxPass([passes.owner]);
  drawHitboxPass(passes.overlays);
}

function countScenePassEntities(passes) {
  return (
    (passes.ground?.length || 0) +
    (passes.underlay?.length || 0) +
    (passes.world?.length || 0) +
    (passes.petals?.length || 0) +
    (passes.overlays?.length || 0) +
    (passes.owner ? 1 : 0)
  );
}

function renderLoadForVisibleCount(count) {
  if (count >= renderLoadHighEntityCount) return 2;
  if (count >= renderLoadMediumEntityCount) return 1;
  return 0;
}

function prepareFrameRenderCaches(passes) {
  currentHornetSkill2WindupOwners = [];
  for (const entity of passes.world || []) {
    const snap = entity?.snapshot;
    if (
      snap?.entityType === hornetType &&
      skillWindupIdFromFlags(snap.flags) === 3
    )
      currentHornetSkill2WindupOwners.push(entity);
  }
}

function updateBloodSacrificeScreenShake(now) {
  let strongest = 0;
  for (const entity of state.entities.values()) {
    const snap = entity?.snapshot;
    if (!isBloodSacrificeEffectSnap(snap)) continue;
    const phase = bloodSacrificePhase(snap.hpPercent);
    if (phase.drawProgress >= 1) continue;
    strongest = Math.max(strongest, 1 - phase.drawProgress * 0.35);
  }

  if (strongest <= 0) {
    state.screenShake = { x: 0, y: 0 };
    return;
  }

  const time = (now || performance.now()) / 1000;
  const amplitude = bloodSacrificeShakeAmplitudePx * strongest;
  state.screenShake = {
    x: (Math.sin(time * 73.1) + Math.sin(time * 41.7 + 1.9)) * amplitude * 0.5,
    y:
      (Math.cos(time * 67.3 + 0.7) + Math.sin(time * 53.9 + 2.4)) *
      amplitude *
      0.5,
  };
}

function drawBloodSacrificeVisionDarkness() {
  const owner = state.entities.get(state.ownerEntityId);
  const ownerPos = owner?.renderPos || owner?.snapshot?.pos;
  if (!ownerPos) return;

  let strongest = 0;
  for (const entity of state.entities.values()) {
    const snap = entity?.snapshot;
    if (!isBloodSacrificeEffectSnap(snap)) continue;
    const ritualPos = entity.renderPos || snap.pos;
    const radius = Math.max(0, Number(snap.radius) || 0);
    if (!ritualPos || radius <= 0) continue;

    const distance = Math.hypot(
      ownerPos.x - ritualPos.x,
      ownerPos.y - ritualPos.y,
    );
    const proximity = clamp(1 - distance / (radius * 2), 0, 1);
    if (proximity <= 0) continue;
    const phase = bloodSacrificePhase(snap.hpPercent);
    const ritualStrength =
      phase.drawProgress < 1 ? phase.drawProgress : phase.alpha;
    strongest = Math.max(strongest, proximity * ritualStrength);
  }

  const alpha = clamp(
    strongest * bloodSacrificeVisionMaxAlpha,
    0,
    bloodSacrificeVisionMaxAlpha,
  );
  if (alpha <= 0) return;

  ctx.save();
  ctx.filter = "none";
  ctx.globalAlpha = 1;
  ctx.globalCompositeOperation = "source-over";
  ctx.fillStyle = `rgba(0, 0, 0, ${alpha})`;
  ctx.fillRect(0, 0, state.canvasWidth, state.canvasHeight);
  ctx.restore();
}

function drawScene(now = performance.now()) {
  currentRenderTimeSeconds = now / 1000;
  updateBloodSacrificeScreenShake(now);
  drawGrid();
  const scale = worldScale();
  const passes = collectSceneRenderPasses({
    scale,
    isEntityInRenderView,
    isBoss: (snap) => raritySortRank(snap.rarity) >= raritySortRank(bossRarity),
  });
  currentVisibleEntityCount = countScenePassEntities(passes);
  currentRenderLoad = renderLoadForVisibleCount(currentVisibleEntityCount);
  prepareFrameRenderCaches(passes);

  drawEntityPass(passes.ground);
  drawEntityPass(passes.underlay);
  drawEntityPass(passes.world);
  drawEntityPass(passes.petals);
  if (passes.owner) drawEntity(passes.owner);
  drawEntityPass(passes.overlays);
  drawParticlePass();
  drawBloodSacrificeVisionDarkness();
  drawCheckpointZoneOutlines();
  drawSceneHitboxes(passes);
  drawBossBars(passes.bosses);
  drawSelfHud();
  drawSquadHud();
  drawMinimap();
  updateDeathOverlay();
}

function tick(now) {
  const dt = Math.min(0.05, Math.max(0, (now - state.lastFrameTime) / 1000));
  state.lastFrameTime = now;
  if (dt > 0) {
    const instantFps = 1 / dt;
    state.fps =
      state.fps <= 0 ? instantFps : state.fps + (instantFps - state.fps) * 0.08;
  }
  flushPendingSnapshot();
  updateViewRadius(now);
  flushInput(dt);
  updateRenderPositions(dt);
  mysteryAudio.updateProximity({
    enabled: state.authenticated,
    owner: state.entities.get(state.ownerEntityId),
    entities: state.entities.values(),
    viewRadius: state.viewRadius,
  });
  if (!state.mapLoadPromise) drawScene(now);
  updateChatVisibility(now);
  updateDebugInfo();
  requestAnimationFrame(tick);
}

function isUiTarget(target) {
  return target && target.closest && target.closest(".ui");
}

function inputKeyFromEvent(event) {
  if (event.code === "Space") return "space";
  return String(event.key || "").toLowerCase();
}

function setupEvents() {
  window.addEventListener("pointerdown", () => mysteryAudio.unlock(), {
    capture: true,
    passive: true,
  });
  window.addEventListener("keydown", () => mysteryAudio.unlock(), {
    capture: true,
  });
  window.addEventListener("resize", resizeCanvas);
  connectBtn.addEventListener("click", connectAndAuth);
  sendCodeBtn.addEventListener("click", requestVerificationCode);
  registerModeInput.addEventListener("change", () => {
    if (authUiMode === AuthUiMode.Binding) return;
    verificationCodeField.clear();
    setAuthUiMode(
      registerModeInput.checked ? AuthUiMode.Register : AuthUiMode.Login,
    );
  });
  reviveBtn.addEventListener("click", requestRevive);
  deathCloseBtn?.addEventListener("click", closeDeathOverlay);
  backpackCloseBtn.addEventListener("click", () => toggleBackpack(false));
  if (inventoryStacking)
    inventoryStacking.checked = state.inventoryStacked;
  inventoryStacking?.addEventListener("change", () => {
    state.inventoryStacked = inventoryStacking.checked;
    renderInventoryPanel({ forceInventory: true });
  });
  inventorySearch?.addEventListener("input", () => {
    state.inventorySearchQuery = inventorySearch.value;
    renderInventoryPanel({ forceInventory: true });
  });
  craftCloseBtn.addEventListener("click", () => toggleCraft(false));
  talentCloseBtn.addEventListener("click", () => toggleTalent(false));
  craftOnceBtn.addEventListener("click", () => submitCraft());
  quickBackpackBtn?.addEventListener("click", () => toggleBackpack());
  quickTalentBtn?.addEventListener("click", () => toggleTalent());
  quickCraftBtn?.addEventListener("click", () => toggleCraft());
  chatHint?.addEventListener("click", () => openChat(""));
  chatChannels?.addEventListener("change", chatUi.handleFilterChange);
  window.addEventListener(
    "pointerdown",
    (event) => {
      if (!state.chatOpen) return;
      if (chatUi.contains(event.target)) return;
      closeChat();
      state.suppressClickUntil = Math.max(
        state.suppressClickUntil,
        performance.now() + 120,
      );
      event.preventDefault();
      event.stopImmediatePropagation();
    },
    { capture: true, passive: false },
  );

  mobileControls = createMobileControls({
    isUiTarget,
    onBackpack: () => toggleBackpack(),
    onTalent: () => toggleTalent(),
    onCraft: () => toggleCraft(),
    onChat: () => openChat(""),
  });
  mobileControls.setMode(state.mobileControlMode);
  mobileControls.setEnabled(state.authenticated);

  consoleInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      submitConsole();
    } else if (event.key === "Escape") {
      event.preventDefault();
      toggleConsole(false);
    }
  });
  sliderInput?.addEventListener("input", () =>
    applySliderValue(sliderInput.value),
  );
  sliderInput?.addEventListener("change", () =>
    applySliderValue(sliderInput.value),
  );
  sliderInput?.addEventListener("keydown", (event) => event.stopPropagation());
  sliderCloseBtn?.addEventListener("click", () => closeSliderPanel());
  for (const eventName of [
    "pointerdown",
    "pointermove",
    "pointerup",
    "pointercancel",
    "mousedown",
    "mousemove",
    "mouseup",
    "wheel",
  ]) {
    sliderPanel?.addEventListener(eventName, stopSliderEvent, {
      capture: true,
    });
  }
  for (const eventName of ["touchstart", "touchmove", "touchend"]) {
    sliderPanel?.addEventListener(eventName, stopSliderEvent, {
      capture: true,
      passive: false,
    });
  }

  window.addEventListener("keydown", (event) => {
    if (event.key === "F1") {
      event.preventDefault();
      toggleConsole();
      return;
    }
    if (isTyping()) return;
    const key = inputKeyFromEvent(event);
    if (key === "z") {
      event.preventDefault();
      toggleBackpack();
      return;
    }
    if (key === "c") {
      event.preventDefault();
      toggleCraft();
      return;
    }
    if (key === "x") {
      event.preventDefault();
      toggleTalent();
      return;
    }
    if (key === "m") {
      event.preventDefault();
      toggleMinimapMode();
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
    if (key === "alt") {
      event.preventDefault();
      if (!event.repeat) {
        state.altFeaturesEnabled = !state.altFeaturesEnabled;
        state.keys.delete("alt");
      }
      return;
    }
    if (key === "r") {
      event.preventDefault();
      if (!event.repeat) quickSwapAllSlots();
      return;
    }
    if (key === "o") {
      event.preventDefault();
      if (!event.repeat) saveActiveLoadoutPreset();
      return;
    }
    if (key === "space" || key === "shift") event.preventDefault();
    const slotIndex = slotIndexFromKey(key);
    if (slotIndex !== null && state.keys.has("l")) {
      event.preventDefault();
      if (!event.repeat) selectLoadoutPreset(slotIndex);
      return;
    }
    const ownerSlots = displayOwnerSlots();
    if (slotIndex !== null && slotIndex < ownerSlots.length) {
      state.selectedSlot = slotIndex;
      quickSwapSlot(slotIndex);
      return;
    }
    state.keys.add(key);
  });

  window.addEventListener("keyup", (event) => {
    const key = inputKeyFromEvent(event);
    if (key === "alt") event.preventDefault();
    state.keys.delete(key);
  });

  window.addEventListener(
    "pointermove",
    (event) => {
      updateMousePointer(event);
      syncPetalInfoTooltipHover();
      updateDrag(event);
    },
    { passive: false },
  );
  window.addEventListener("pointerup", finishDrag);
  window.addEventListener("pointercancel", clearDrag);

  window.addEventListener("blur", () => {
    state.keys.clear();
    state.attacking = false;
    state.defending = false;
    state.digging = false;
    mobileControls?.reset();
    sendNeutralGameplayState({ includeInput: true });
  });

  window.addEventListener("mousedown", (event) => {
    updateMousePointer(event);
    if (shouldSuppressClick()) return;
    if (event.button === 0 && isMinimapPoint(eventCanvasPoint(event))) {
      toggleMinimapMode();
      event.preventDefault();
      return;
    }
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

  window.addEventListener("beforeunload", () => {
    pageUnloading = true;
    closeSocket(true);
  });
}

function slotIndexFromKey(key) {
  if (/^[1-9]$/.test(key)) return Number(key) - 1;
  if (key === "0") return 9;
  return null;
}

document.title = appDisplayName;
const appVersionElement = document.getElementById("appVersion");
if (appVersionElement) appVersionElement.textContent = `v${appVersion}`;

loadSettings();
await initI18n(state.locale);
state.locale = getLocale();
applyDocumentTranslations();
setAuthUiMode(AuthUiMode.Login);
setupEvents();
updateQuickActionButtons();
resizeCanvas();
renderInventoryPanel();
setStatus(t("status.disconnected"));
addConsoleLine(t("status.clientReady", { app: appDisplayName }));
loadLoginMap();
requestAnimationFrame(tick);

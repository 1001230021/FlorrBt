import { UI_FONT_FAMILY } from "./ui_typography.js";
import { createMenuBackdrop } from "./menu_backdrop.js";
import { createLoadoutPreview } from "./loadout_preview.js";
import { createInventoryView } from "./inventory_view.js";
import { updateCraftButton } from "./craft_button.js";
import { createPlayerHud } from "./player_hud.js";
import { authRecovery } from "./auth_recovery.js";
import { applyClientFeatures, resetClientFeatures, featureAllowed, FeatureAccess, refreshClientFeatures } from './client_features.js';
import { installPlayerFeatureUi } from './player_feature_ui.js';
import { createAccountUi } from "./account_ui.js";
import { revokeSavedAccount } from "./account_logout.js";
import { createAccountBindings } from './account_bindings.js';
import { createEconomyRequests, EconomyOperation, EconomyOutcome } from "./economy_requests.js";
import { partyRoster, partyMemberForEntity } from "./party_state.js";
import {
  AuthMode,
  AuthResultCode,
  ChatFlag,
  EmailPolicy,
  FULL_SNAPSHOT_BASE_ID,
  NETWORK_DROP_ENTITY_TYPE,
  NETWORK_PETAL_TYPE_OFFSET,
  PetalNames,
  MobNames,
  PetalSlotCopyState,
  PetalSlotVisualType,
  RarityNames,
  ServerType,
  SkillResultCode,
  appendBytes,
  clamp,
  isNewerUint32Sequence,
  isDropEntity,
  isPetalEntity,
  mobTypeName,
  packAuth,
  packChat,
  packChores,
  packSkill,
  packInput,
  packInputFrame,
  packSnapshotAck,
  sameEntityIncarnation,
  packStateRequest,
  packTalentRequest,
  packVersion,
  parseServerMessage,
  petalTypeFromEntity,
  petalTypeName,
  popFrame,
  rarityColor,
  rarityName,
} from "./protocol.js";
import { clearLadybugPattern, drawNormalLadybug, prepareLadybugSprites } from "./ladybug_sprite.js";
import { prepareSvgImage, whenVisualReady, getPreparedVisualImages } from "./visual_assets.js";
import { getRenderFrameTiming, takeRenderPreparationCost } from "./render_timing.js";
import { sceneRasterBudget } from "./raster_budget.js";
import { calculateUiLayout, writeUiLayoutVariables } from "./ui_layout.js";
import { createSlotSwapMotion } from "./slot_swap_motion.js";
import { createCraftMatrix } from "./craft_matrix.js";
import { getPointerMoveIntent, quantizeMoveIntent } from "./movement_input.js";
import { getSlotCounter, getSlotCounterColor } from "./slot_counter.js";
import { drawMoonSurface, moonSvgGeometry, prepareMoonSurface } from "./moon_renderer.js";
import { drawBeetle, prepareBeetleSprites } from "./beetle_sprite.js";
import { drawSoldierAnt, prepareSoldierAntSprites } from "./soldier_ant_sprite.js";
import {
  drawBee,
  drawBumbleBee,
  drawHornet,
  drawHornetMissile,
  drawPollen,
  prepareBeeSprites,
} from "./bee_sprite.js";
import {
  drawAntEggMob,
  drawAntHole,
  drawBabyAnt,
  drawBabyFireAnt,
  drawBabyTermite,
  drawBubble,
  drawDandelion,
  drawFireQueenAnt,
  drawLeafcutterSoldier,
  drawLeafPiece,
  drawPortal,
  drawQueenAnt,
  drawRock,
  drawSandstorm,
  drawFirefly,
  drawSoldierFireAntMob,
  drawSoldierTermite,
  drawTermiteOvermind,
  drawWorkerAnt,
  drawWorkerFireAnt,
  drawWorkerTermite,
  prepareGardenSprites,
} from "./garden_mob_sprite.js";
import { drawSpider, drawSpiderWeb, prepareSpiderSprites } from "./spider_sprite.js";
import { drawCrab, prepareCrabSprites } from "./crab_sprite.js";
import { drawJellyfish, prepareJellyfishSprites } from "./jellyfish_sprite.js";
import { createSceneTerrainStream } from "./scene_terrain_stream.js";
import {
  drawFireflyLarva,
  FireflyLarvaVisualRole,
  prepareFireflyLarvaSprites,
} from "./firefly_larva_sprite.js";
import {
  drawSpriteImage,
  setSpriteHitTint,
  setSpriteRasterFrameTime,
  primeSpriteRasters,
} from "./sprite_raster_cache.js";
import {
  dom,
  emptySlot,
  loadoutPresetCount,
  slotHasItem,
  state,
  ui,
} from "./app_context.js";
import { createChatUi } from "./chat_ui.js";
import { applyChatReplacement } from "./chat_replacement.js";
import {
  executeBinding,
  heldInputSources,
  initBindingContext,
  inputKeyCandidates,
  inputKeyFromEvent,
  loadBindings,
  mouseKeyFromButton,
  pruneStaleInputHolds,
  releaseInputHolds,
} from "./bindings.js";
import {
  loadClientSettings,
  normalizeMovementMode,
  normalizeMobileControlMode,
  normalizeRenderScaleMode,
  saveClientSettings,
} from "./client_settings.js";
import {
  clientRuntimeConfig,
  formatClientConfigValue,
  getClientConfigValue,
  normalizeViewZoomPercent,
  setClientConfigValue,
} from "./client_config.js";
import { createConsoleUi } from "./console_ui.js";
import { createMapRenderer } from "./map_renderer.js";
import { defaultPlayerLightRadius } from "./terrain_rules.js";
import { sampleMinimapTransition } from "./minimap_transition.js";
import { createMobileControls, mobileActionBindings } from "./mobile_controls.js";
import { bindMobileViewport } from "./mobile_viewport.js";
import { bindTouchLongPress } from "./touch_gestures.js";
import { bindPinchGesture } from "./pinch_gestures.js";
import { initCustomScrollbars } from "./custom_scrollbar.js";
import { setAnimatedVisibility } from "./ui_motion.js";
import {
  animatePetalCardVanish,
} from "./card_motion.js";
import { createCraftParticleLayer } from "./craft_particles.js";
import { createBurrowParticles } from "./burrow_particles.js";
import { createRainField } from "./weather_rain.js";
import { createBlockFogField } from "./weather_fog.js";
import { createCraftSpinController } from "./craft_motion.js";
import { createMysteryAudio } from "./mystery_audio.js";
import { SettingsPanel } from "./settings_ui.js";
import { UiPanel, UiPanelManager } from "./ui_panel.js";
import {
  drawSolidProgressBar,
  getMobFrameLayout,
  traceSolidProgressBarPath,
} from "./render_primitives.js";
import { createTextInput, isTextInputActive } from "./text_input.js";
import {
  applyDocumentTranslations,
  compareLocalized,
  getAvailableLocales,
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
  dandelionMissileType,
  dandelionType,
  dummyType,
  fireAntEggType,
  fireflyType,
  magicFireflyType,
  fireflyLarvaType,
  fireQueenAntType,
  hallucinogenicMushroomLiveVisualScale,
  flagAntennae,
  flagAttacking,
  flagCarryingLeafPiece,
  flagDead,
  flagDefending,
  flagAttached,
  flagOwner,
  flagSummoned,
  entityHasState,
  entityState,
  entityVisualVariantFromFlags,
  flowerTextureVersion,
  hornetMissileType,
  hornetType,
  leafcutterSoldierType,
  leafPieceType,
  maxBossBars,
  mechaFlowerType,
  normalFlowerType,
  mobSpriteCoverScale,
  mobSpriteEffectiveBox,
  mobSpriteViewBox,
  nonStackPetalTypes,
  normalLadybugType,
  petalAirType,
  petalAmuletType,
  petalAntEggType,
  petalAntennaeType,
  petalBandageType,
  petalBasilType,
  petalBatteryType,
  petalBeetleEggType,
  petalBlackFungusType,
  petalBroccoliType,
  petalBasicType,
  petalBloodSacrificeType,
  petalBoneType,
  petalBrokenEggType,
  petalBulbType,
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
  petalHallucinogenicMushroomType,
  petalHeavyType,
  petalHoneyType,
  petalIrisType,
  petalLeafType,
  petalLentilType,
  petalLightType,
  petalLightningType,
  petalLinkerType,
  petalMagicEyeType,
  petalMimicType,
  petalMissileType,
  petalMoonType,
  petalOrangeType,
  petalPincerType,
  petalPlankType,
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
  petalTomatoType,
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
  raritySortRanks,
  raritySuper,
  rarityUnique,
  rockType,
  bubbleType,
  crabType,
  chainEntityType,
  jellyfishType,
  sandstormType,
  skillWindupIdFromFlags,
  soldierAntType,
  soldierFireAntType,
  soldierTermiteType,
  spiderWebZoneType,
  spiderType,
  stateCorruption,
  stateDigging,
  stateDropStack,
  stateHallucinogenic,
  stateInvincible,
  stateBanSlot,
  stateNullification,
  statePoison,
  stateAntiHeal,
  statePsionicConnection,
  statePulse,
  stateUndead,
  titanType,
  trapProjectileType,
  summonedBeetleType,
  summonedSoldierAntType,
  queenAntEggType,
  queenFireAntEggType,
  termiteEggType,
  termiteOvermindType,
  workerAntType,
  workerFireAntType,
  workerTermiteType,
  waxLivePetalVisualScale,
  worldDropSizeScale,
  worldPetalSizeScale,
} from "./game_ids.js";
import { collectSceneRenderPasses, createSceneRenderPasses } from "./scene_passes.js";
import { drawChain, parseChainSnapshot } from "./chain_renderer.js";
import { createTalentTree } from "./ui_talents.js";
import { placeCraftReturns } from "./craft_slots.js";
import { drawFlowerDisc, drawFlowerFace, drawFlowerBandage, drawFlowerAntennae, drawTitanBody,
  getFlowerPalette, getTitanCoreRadius, stepFlowerFace, flowerAttachmentLayout,
  getFlowerFrameLayout } from "./flower_renderer.js";
import { createCombatFeedback, stepHealthTrail } from "./combat_feedback.js";
import { drawMovementGuide } from "./movement_guide.js";
import { beginEntityAppearance, beginEntityExit, resumeEntityAppearance, getEntityAppearance, getDropCardTilt } from "./entity_motion.js";
import { clearSpriteMotion, resetSpriteMotion } from "./sprite_motion.js";
import { beginDeathMotion, clearDeathMotion, captureDeathLoadout } from "./death_motion.js";
import { createRespawnRequest } from "./respawn_request.js";
import { drawDeathPortrait } from "./death_portrait.js";
import { ASSET_MANIFEST } from "./asset_manifest.js";
import {
  addConsoleLine,
  formatPetalItem,
  loadSettings,
  installGameUiActions,
  localizationSlug,
  refreshLocalizedUi,
  saveSettings,
  setHeldAction,
  setFullscreen,
  setLoadingScreenVisible,
  setMobileControlMode,
  setMovementMode,
  setStatus,
  toggleBackpack,
  toggleConsole,
  toggleCraft,
  toggleFullscreen,
  toggleSettings,
  toggleTalent,
  updatePlayUiVisibility,
  updateQuickActionButtons,
} from "./ui_settings.js";
import {
  codexKey,
  codexMobIcons,
  hideCodexDetail,
  bindCodexDetailEvents,
  renderCodexPanel,
  toggleCodex,
} from "./ui_codex.js";
import {
  debugNumber,
  drawGrid,
  effectiveViewRadius,
  lerpAngle,
  normalizeAngle,
  resizeCanvas,
  screenToWorld,
  smoothFactor,
  viewZoomPercent,
  updateViewZoom,
  worldLengthToScreen,
  worldScale,
  worldToScreen,
} from "./camera.js";
import {
  hasPlayerSnapPetal,
  syncEntityRenderToSnapshot,
  transformOwnerHallucinationVector,
  updateEntitySnapshotVelocity,
  updateRenderPositions,
} from "./sim_entities.js";
import {
  assetImage,
  attachPetalInfoTooltip,
  bandageOverlayImages,
  clearPetalInfoTooltip,
  effectivePetalCardCopy,
  formatShortCount,
  hidePetalInfoTooltip,
  imageReady,
  livePetalImages,
  makePetalStack,
  refreshPetalCardLabels,
  normalizePetalName,
  getPetalBaseCopy,
  getPetalBasePath,
  getPetalCopyLayout,
  getPetalDisplayName,
  hasPetalMultiCopyVisual,
  getPetalIconPath,
  ensurePetalCopyDistanceData,
  petalBaseImage,
  petalTypeFromSnap,
  preloadAssets,
  rarityLevel,
  recordLivePetalCopyCount,
  raritySortRank,
  syncPetalInfoTooltipHover,
  transformedPetalImages,
} from "./petal_info.js";
import {
  initFreezeWatchdog,
  noteConnectionReset,
  noteServerSnapshot,
} from "./freeze_watchdog.js";
import { drawOutlinedText as drawSharedOutlinedText } from "./render_text.js";
import { withDebugRenderState } from "./dev_debug_model.js";
import {
  executeClientCommand,
  executeLanguageCommand,
  initClientCommands,
} from "./console_commands.js";
import {
  dispatchServerMessage,
  notifyConnectionReset,
  registerConnectionReset,
  registerMessageHandler,
} from "./net_messages.js";
import { sendBytes } from "./net_outbound.js";
import {
  noteFrameDrawn,
  requestRedraw,
  setRedrawHandler,
} from "./render_invalidate.js";
import {
  extractBandageOverlaySvg,
  extractMimicLabelSvg,
  extractPetalLabelSvg,
  measureSvgContentBox,
  centerPetalArtSvg,
  stripLivePetalLabel,
} from "./petal_svg.js";
import {
  initRenderScale,
  noteSceneDrawCost,
  noteRenderFrameCost,
  sceneDrawCostEma,
  setRenderScaleMode,
  updateAdaptiveRenderScale,
} from "./render_scale.js";

const {
  canvas,
  loadingScreen,
  authPanel,
  emailBindingPanel,
  wsUrlInput,
  accountInput,
  passwordInput,
  emailInput,
  verificationCodeInput,
  authPasswordRow,
  bindingNotice,
  registerModeRow,
  registerModeInput,
  sendCodeBtn,
  bindEmailBtn,
  cancelEmailBindingBtn,
  emailBindingStatus,
  connectBtn,
  deathOverlay,
  deathSource,
  deathLoot,
  deathPortrait,
  reviveBtn,
  deathCloseBtn,
  quickActions,
  quickBackpackBtn,
  quickTalentBtn,
  quickCraftBtn,
  topSettingsBtn,
  codexBtn,
  codexPanel,
  codexCloseBtn,
  codexGrid,
  codexDetail,
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
  settingsPanel,
  settingsCloseBtn,
  settingsList,
  emailSettingsRow,
  emailBindingState,
  openEmailBindingBtn,
  primarySlots,
  secondarySlots,
  slotSwapHint,
  slotKeyHints,
  inventoryList,
  inventoryStacking,
  inventorySearch,
} = dom;

let ctx = dom.ctx;
let currentUiLayout = null;
let uiLayoutKey = "";
const inventoryView = createInventoryView({ groupInventoryItems, makePetalStack, effectivePetalCardCopy, getPetalDisplayName, formatPetalItem, formatCount: formatShortCount, attachPetalInfoTooltip, beginDrag, shouldSuppressClick, equipFromInventory });
const renderInventoryGroups = inventoryView.render;
const loadoutPreview = createLoadoutPreview();
const slotSwapMotion = createSlotSwapMotion({ onSettled: () => renderInventoryPanel() });
let craftMatrixUi = null;
const gameRenderPasses = createSceneRenderPasses();
const debugRenderPasses = createSceneRenderPasses();
let devDebugUi = null;
let devDebugRequestAt = -Infinity;

function drawOutlinedText(text, x, y, options) {
  return drawSharedOutlinedText(text, x, y, options, ctx);
}

function syncUiLayout() {
  const primaryCount = primarySlots.children.length || state.ownerSlots?.length || 5;
  const secondaryCount = secondarySlots.children.length || state.secondarySlots?.length || 5;
  const mobile = document.body.classList.contains("mobile-ui");
  const key = `${window.innerWidth}:${window.innerHeight}:${mobile}:${primaryCount}:${secondaryCount}:${state.slotPanelCollapsed}`;
  if (currentUiLayout && uiLayoutKey === key) return currentUiLayout;
  const styles = getComputedStyle(document.documentElement);
  const insets = Object.fromEntries(["top", "right", "bottom", "left"].map(side =>
    [side, Number.parseFloat(styles.getPropertyValue(`--layout-safe-${side}`)) || 0]));
  currentUiLayout = calculateUiLayout({ width: window.innerWidth, height: window.innerHeight,
    mobile, primaryCount, secondaryCount,
    collapsed: state.slotPanelCollapsed, insets });
  uiLayoutKey = key;
  writeUiLayoutVariables(document.documentElement, currentUiLayout);
  document.body.classList.toggle("loadout-overflow", currentUiLayout.loadout.scroll);
  return currentUiLayout;
}

function getGameUiLayout() {
  return currentUiLayout || syncUiLayout();
}

function requestDevDebug() {
  if (!state.authenticated || !state.connected) {
    addConsoleLine("请先连接服务器，在 F1 使用 rcon <密码> 授权，再输入 dev_debug。", "error"); return;
  }
  if (devDebugUi?.isOpen() || performance.now() - devDebugRequestAt < 1000) return;
  devDebugRequestAt = performance.now();
  sendBytes(packChat(ChatFlag.Local, "/dev_debug"));
}

async function openDevDebug(ticket) {
  const originalSocket = state.ws;
  const { createDevDebugUi } = await import("./dev_debug_ui.js");
  if (!state.authenticated || state.ws !== originalSocket || originalSocket?.readyState !== WebSocket.OPEN) return;
  if (!devDebugUi) devDebugUi = createDevDebugUi({
    onOpen: () => {
      state.devDebugOpen = true;
      releaseInputHolds(); clearDrag(); ui.mobileControls?.reset();
      state.localMoveInput = { x: 0, y: 0 }; state.digging = false;
      sendNeutralGameplayState({ includeInput: true });
      toggleConsole(false); closeChat(); hideCodexDetail(); hidePetalInfoTooltip();
    },
    onClose: () => { state.devDebugOpen = false; releaseInputHolds(); canvas.focus(); requestRedraw(); },
    releaseVisual: id => { clearSpriteMotion(id); clearLadybugPattern(id); },
    getCullScale: getFlowerMobCullScale,
    getTypeIcon: entry => entry.kind === "petal" ? getPetalIconPath(entry.id)
      : codexMobIcons[entry.visualType] ? `./assets/${codexMobIcons[entry.visualType]}` : "",
    getTypeName: entry => entry.kind === "petal" ? (PetalNames[entry.id] ? petalTypeName(entry.id) : entry.name)
      : (MobNames[entry.id] ? mobTypeName(entry.id) : entry.name),
    renderEntities: (target, scene, now) => {
      const originalContext = ctx, originalTime = currentRenderTimeSeconds;
      const patch = { entities: scene.entities, camera: scene.camera, canvasWidth: scene.width,
        canvasHeight: scene.height, dpr: scene.dpr, ownerEntityId: -1, viewZoomPercent: 100,
        renderViewZoomPercent: 100,
        deathViewFactor: 1, screenShake: { x: 0, y: 0 },
        viewRadius: Math.max(96, Math.hypot(scene.width, scene.height) / 2) / scene.scale };
      // 此临时上下文内禁止 await；地图加载维护独立状态。
      try {
        ctx = target; currentRenderTimeSeconds = now / 1000;
        withDebugRenderState(state, patch, () => {
          const passes = collectSceneRenderPasses({ scale: scene.scale, isEntityInRenderView, isBoss: () => false,
            passes: debugRenderPasses });
          prepareFrameRenderCaches(passes);
          for (const pass of [passes.ground, passes.underlay, passes.world, passes.petals, passes.overlays])
            for (const entity of pass) drawEntity(entity, { visualOnly: true });
        });
      } finally { ctx = originalContext; currentRenderTimeSeconds = originalTime; }
    },
  });
  devDebugUi.open(originalSocket.url, ticket);
}

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
  onSubmit: () => confirmEmailBinding(),
});
const verificationCodeField = createTextInput(verificationCodeInput, {
  onSubmit: () => confirmEmailBinding(),
});

ui.panelManager = new UiPanelManager();
installPlayerFeatureUi({ state, ui, dom,
  renderInventory: () => renderInventoryPanel({ forceInventory: true }), renderCraft: renderCraftPanel, renderTalents: renderTalentPanel,
  clearCraft: clearCraftDisplay, closeTalents: () => talentTreeUi?.close(), closeCodex: () => toggleCodex(false), changed: updateQuickActionButtons });
ui.settingsUi = new SettingsPanel({
  id: "settings",
  root: settingsPanel,
  list: settingsList,
  state,
  stateKey: "settingsOpen",
  manager: ui.panelManager,
  trigger: topSettingsBtn,
  focusTarget: canvas,
  translate: (key) => t(key),
  onError: (error, definition) =>
    addConsoleLine(
      `${t(definition.labelKey)}: ${String(error?.message || error || "Unable to update setting")}`,
      "error",
    ),
  onChange: () => updateQuickActionButtons(),
  definitions: [
    {
      id: "language",
      type: "choice",
      labelKey: "ui.settings.language",
      getValue: () => getLocale(),
      options: () =>
        getAvailableLocales().map((entry) => ({
          value: entry.id,
          label: entry.label,
        })),
      setValue: (value) => executeLanguageCommand([String(value)]),
    },
    {
      id: "movement",
      type: "choice",
      labelKey: "ui.settings.movement",
      getValue: () => state.movementMode,
      options: () => [
        {
          value: "keyboard",
          labelKey: "ui.settings.movementModes.keyboard",
        },
        { value: "mouse", labelKey: "ui.settings.movementModes.mouse" },
        { value: "none", labelKey: "ui.settings.movementModes.none" },
      ],
      setValue: (value) => setMovementMode(value),
    },
    {
      id: "render-scale",
      type: "choice",
      labelKey: "ui.settings.renderScale",
      getValue: () => String(state.renderScaleMode),
      options: () => [
        { value: "auto", labelKey: "ui.settings.renderScaleModes.auto" },
        { value: "native", labelKey: "ui.settings.renderScaleModes.native" },
        { value: "0.75", label: "0.75x" },
        { value: "1", label: "1x" },
        { value: "1.5", label: "1.5x" },
        { value: "2", label: "2x" },
        { value: "3", label: "3x" },
        { value: "4", label: "4x" },
      ],
      setValue: (value) => setRenderScaleMode(value),
    },
    {
      id: "hold-attack",
      type: "toggle",
      labelKey: "ui.settings.holdAttack",
      getValue: () => state.holdAttack,
      setValue: (value) => setHeldAction("attack", value),
    },
    {
      id: "hold-defense",
      type: "toggle",
      labelKey: "ui.settings.holdDefense",
      getValue: () => state.holdDefense,
      setValue: (value) => setHeldAction("defense", value),
    },
    {
      id: "fullscreen",
      type: "toggle",
      labelKey: "ui.settings.fullscreen",
      getValue: () => !!document.fullscreenElement,
      setValue: (value) => setFullscreen(!!value),
    },
    ...[
      ["damage_numbers", "ui.settings.damageNumbers"],
      ["screen_shake", "ui.settings.screenShake"],
      ["live_icons", "ui.settings.liveIcons"],
      ["menu_petals", "ui.settings.menuPetals"],
      ["movement_arrow", "ui.settings.movementArrow"],
    ].map(([name, labelKey]) => ({
      id: name, type: "toggle", labelKey,
      getValue: () => getClientConfigValue(state.clientConfig, name) >= 0.5,
      setValue: value => {
        const result = setClientConfigValue(state.clientConfig, name, value ? 1 : 0);
        if (result.ok) { state.clientConfig = result.config; saveSettings(); }
      },
    })),
  ],
});
ui.consoleUiPanel = new UiPanel({
  id: "console",
  root: dom.consolePanel,
  state,
  stateKey: "consoleOpen",
  manager: ui.panelManager,
  group: "utility",
  exclusive: false,
});
ui.chatUi = createChatUi({
  beforeOpen: () => {
    if (state.consoleOpen) toggleConsole(false);
    releaseInputHolds();
    state.digging = false;
    ui.mobileControls?.reset();
    sendNeutralGameplayState();
  },
  executeCommand: (line) => executeClientCommand(line),
  focusTarget: canvas,
});
ui.consoleUi = createConsoleUi({
  panel: ui.consoleUiPanel,
  beforeOpen: () => closeChat(),
  executeCommand: (line) =>
    executeClientCommand(line.startsWith("/") ? line.slice(1) : line),
  focusTarget: canvas,
  completionSources: {
    player: () => {
      const names = new Set();
      for (const entity of state.entities.values()) {
        const name = entity.snapshot?.name;
        if (name) names.add(name);
      }
      return [...names];
    },
  },
});
ui.mysteryAudio = createMysteryAudio({
  playerEntityType: playerFlowerType,
  douliPetalType: petalDouliType,
});

const defaultMapName = clientRuntimeConfig.loginMapDefaultName;
const bindings = loadBindings();
setRedrawHandler(drawScene);
ui.mapRenderer = createMapRenderer({
  ctx,
  defaultMapName,
  addConsoleLine: (text, kind = "") => addConsoleLine(text, kind),
  requestDraw: requestRedraw,
  // 切图仅等待关键启动资源，其余贴图在客户端可用后后台预热。
  setLoadingVisible: (visible) => {
    if (visible) {
      setLoadingScreenVisible(true);
      return;
    }
    (criticalAssetsPromise || Promise.resolve()).catch((error) => {
      console.warn("Critical visual preparation failed", error);
    }).then(() => {
      // 旧地图加载完成时，新地图可能仍在准备。
      if (!state.mapLoadPromise) setLoadingScreenVisible(false);
    });
  },
  worldScale,
  worldToScreen,
});
const viewRadiusTransitionDurationMs = 100;
let pendingSnapshot = null;
const snapshotHistory = new Map();
const snapshotHistoryLimit = 90;
let snapshotResetRequested = false;
const reconnectInitialDelayMs = 250;
const reconnectMaxDelayMs = 5000;
let reconnectTimer = null;
let reconnectAttempt = 0;
let reconnectArmed = false;
let resumingConnection = false;
let connectionGeneration = 0;
let pageUnloading = false;
const AuthUiMode = Object.freeze({
  Login: "login",
  Register: "register",
});
export let authUiMode = AuthUiMode.Login;
let pendingAuthMode = null;
let activeAuthMode = null;
let enterGameRequested = false;
let accountManageRequested = false;
let emailBindingReturnToAccount = false;
const accountBindings = createAccountBindings({ state, send: bytes => !!bytes && sendBytes(bytes),
  openEmail: openEmailBinding, ensureSession: ensureAccountSession, onChange: () => renderEmailSettingsRow() });
const accountUi = createAccountUi({
  state, manager: ui.panelManager,
  getAccount: () => (state.authenticated || state.accountSession) ? state.accountName : authRecovery.account(wsUrlField.getValue({ trim: true })),
  onStart: startGame, onLogout: logoutAccount,
  onOpen: () => { releaseInputHolds(); sendNeutralGameplayState({ includeInput: true }); ensureAccountSession(); accountBindings.refresh(); },
});
const {
  packetInterval,
  craftSpinRoundMs,
  craftSpinRevolutionsPerRound,
  craftSpinMinRounds,
  craftSpinMaxRounds,
  craftSpinRampMs,
  hurtFlashDuration,
  hurtFlashInterval,
  hurtFlashMinDelta,
  missingEntityViewEdgeGrace,
  viewZoomStepPercent,
  mouseMoveDeadzonePx,
  entityFrameMinScreenRadius,
  entityNameMinScreenRadius,
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
  entitySnapshotVelocityBlend,
  petalRenderLerpRate,
  petalAngleLerpRate,
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
  minimapTileSubdivisions,
  minimapBackgroundColor,
  minimapMaterialColors,
  minimapUnknownMaterialColor,
  minimapMonochromeBackgroundColor,
  minimapMonochromeWallColor,
  checkpointZoneOutlineColor,
  checkpointZoneOutlineWidthPx,
} = clientRuntimeConfig;

// 合成动画在解构配置后创建，以获得配置中的时序参数。
const craftParticles = createCraftParticleLayer(craftStage);
const craftSpin = createCraftSpinController({
  stage: craftStage,
  particles: craftParticles,
  rarityColor,
  roundMs: craftSpinRoundMs,
  revolutionsPerRound: craftSpinRevolutionsPerRound,
  minRounds: craftSpinMinRounds,
  maxRounds: craftSpinMaxRounds,
  roundScale: clientRuntimeConfig.craftSpinRoundScale,
  rampMs: craftSpinRampMs,
});
let craftResultDismissing = false;
let talentTreeUi = null;
let craftVisualGeneration = 0;
const combatFeedback = createCombatFeedback();
const burrowParticles = createBurrowParticles({ isVisible: (snap, position) => {
  if (state.devDebugOpen || document.hidden) return false;
  const point = worldToScreen(position), radius = worldLengthToScreen(snap.radius || 0);
  return radius >= 3 && point.x + radius >= 0 && point.y + radius >= 0 &&
    point.x - radius <= state.canvasWidth && point.y - radius <= state.canvasHeight;
} });
let lastOwnerDamage = null;
let ownerDeathSummary = null;

const playerTeam = 1;
const craftRarityDisplayOrder = rarityDisplayOrder.filter((rarity) =>
  canCraftRarity(rarity),
);
let currentRenderTimeSeconds = 0;
let currentRenderFrameId = 0;
let currentVisibleEntityCount = 0;
let currentRenderLoad = 0;
// 受击缓存紧随所属身体绘制，保持层次；极小实体和非贴图特效使用轻量白点。
const currentHurtFlashSprites = [];
const currentHurtFlashOverlays = [];
let entityVisualOnlyRenderDepth = 0;
let currentHornetSkill2WindupOwners = [];
let diurnalMaskCanvas = null;
let diurnalMaskContext = null;
let diurnalLightCanvas = null;
let diurnalLightContext = null;
let lastWeatherEffectsAt = 0;
// 平滑服务端逐帧天气强度，避免叠加天气表现闪烁。
let actualRain = 0;
let actualFog = 0;
let actualCloudyTint = 0;
const rainField = createRainField();
const blockFogField = createBlockFogField();
let minimapHitRect = null;
let minimapCache = null;
// 界面过渡状态独立于权威快照，仅将离散血量/首领组更新表现为淡出与插值。
const bossBarMotions = new Map();
const playerHud = createPlayerHud({ state, getContext: () => ctx, getGameUiLayout, getPlayerLevelRarity, motionFactor, approachMotion, drawCapsuleBar, drawTrackedHealthBar, drawShieldBar, healthBarColor, drawActorPortrait, drawOutlinedText });
const healthTrails = new Map();
const bossBarMotionRate = 14;
const bossBarExitLiftPx = 24;
const minimapTransitionDurationMs = 240;
const craftSlotMotionDurationMs = 180;
let minimapTransition = null;


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

export function setAuthUiMode(mode) {
  authUiMode = mode;
  authPasswordRow.classList.remove("hidden");
  registerModeRow.classList.remove("hidden");
  accountField.setDisabled(false);
  registerModeInput.checked = mode === AuthUiMode.Register;
  const buttonKey =
    mode === AuthUiMode.Register ? "ui.auth.register" : "ui.auth.login";
  connectBtn.textContent = t(buttonKey);
}

function isEmailBindingMode(mode) {
  return (
    mode === AuthMode.RequestBindingCode || mode === AuthMode.ConfirmBinding
  );
}

function bindingAccountName() {
  return state.accountName || accountField.getValue({ trim: true });
}

function isEmailBindingPanelOpen() {
  return (
    !!state.emailBindingPanelOpen ||
    emailBindingPanel?.getAttribute("aria-hidden") === "false"
  );
}

function setAuthPanelVisible(visible) {
  accountUi.showHome(visible);
}

export function renderEmailSettingsRow() {
  if (!emailSettingsRow) return;
  const visible =
    (state.authenticated || state.accountSession) && state.emailPolicy !== EmailPolicy.Disabled;
  setAnimatedVisibility(emailSettingsRow, visible, { duration: 160 });
  if (!visible) return;

  emailBindingState.textContent = t(
    state.emailBound
      ? "ui.settings.emailBound"
      : "ui.settings.emailNotBound",
  );
  setAnimatedVisibility(openEmailBindingBtn, !state.emailBound, {
    duration: 140,
  });
}

function setEmailBindingPanel(open, { required = false } = {}) {
  const wasOpen = isEmailBindingPanelOpen();
  state.emailBindingPanelOpen = !!open;
  state.emailBindingRequired = open && required;
  setAnimatedVisibility(emailBindingPanel, open, { duration: 220 });
  emailBindingPanel.classList.toggle("binding-required", open && required);
  cancelEmailBindingBtn.classList.toggle("hidden", open && required);
  bindingNotice.textContent = t(
    required
      ? "ui.auth.bindingRequiredNotice"
      : "ui.auth.bindingOptionalNotice",
  );
  if (open) {
    accountUi.panel.setOpen(false);
    setAuthPanelVisible(false);
    releaseInputHolds();
    state.localMoveInput = { x: 0, y: 0 };
    state.digging = false;
    if (state.authenticated)
      sendNeutralGameplayState({ includeInput: true });
    if (!wasOpen) {
      verificationCodeField.clear();
      emailBindingStatus.textContent = "";
    }
    emailField.focus();
  } else if (wasOpen) {
    emailField.blur();
    verificationCodeField.blur();
    canvas.focus();
  }
  renderEmailSettingsRow();
}

function openEmailBinding() {
  if (
    (!state.authenticated && !state.accountSession) ||
    state.emailBound ||
    state.emailPolicy === EmailPolicy.Disabled
  )
    return;
  emailBindingReturnToAccount = state.accountOpen;
  ui.settingsUi.setOpen(false);
  accountManageRequested = !state.authenticated;
  setEmailBindingPanel(true);
}

function cancelEmailBinding() {
  if (state.emailBindingRequired) return;
  clearAuthRequestPending();
  setEmailBindingPanel(false);
  if (!state.authenticated || emailBindingReturnToAccount) {
    if (!state.authenticated) setAuthPanelVisible(true);
    accountUi.panel.setOpen(true);
  }
  emailBindingReturnToAccount = false;
}

function setAuthRequestPending(mode) {
  activeAuthMode = mode;
  const busy = mode !== null;
  connectBtn.disabled = busy;
  accountUi.setBusy(busy);
  sendCodeBtn.disabled = busy;
  bindEmailBtn.disabled = busy;
  cancelEmailBindingBtn.disabled = busy;
}

function clearAuthRequestPending() {
  pendingAuthMode = null;
  setAuthRequestPending(null);
}

function selectedAuthMode() {
  if (authUiMode === AuthUiMode.Register) return AuthMode.AccountRegister;
  return AuthMode.AccountLogin;
}

function validateAuthFields(mode, { allowRecovery = true } = {}) {
  const binding = isEmailBindingMode(mode);
  if (!(binding ? bindingAccountName() : accountField.getValue({ trim: true }))) {
    setStatus(t("status.accountEmpty"));
    if (!binding) accountField.focus();
    return false;
  }

  const registration = mode === AuthMode.Register || mode === AuthMode.AccountRegister;
  const login = mode === AuthMode.Login || mode === AuthMode.AccountLogin;
  const recoveryAvailable = login && allowRecovery && authRecovery.token(
    state.ws?.url || wsUrlField.getValue({ trim: true }), accountField.getValue({ trim: true }));
  if ((registration || (login && !recoveryAvailable)) && !passwordField.getValue()) {
    setStatus(t("status.passwordEmpty"));
    passwordField.focus();
    return false;
  }
  if (binding && !emailField.getValue({ trim: true })) {
    setStatus(t("status.emailEmpty"));
    emailField.focus();
    return false;
  }
  if (
    mode === AuthMode.ConfirmBinding &&
    !verificationCodeField.getValue({ trim: true })
  ) {
    setStatus(t("status.verificationCodeEmpty"));
    verificationCodeField.focus();
    return false;
  }
  return true;
}

function queueAuthRequest(mode) {
  if (activeAuthMode !== null) return;
  if (!validateAuthFields(mode)) {
    if (mode === AuthMode.AccountLogin || mode === AuthMode.AccountRegister) accountUi.requireLogin();
    return;
  }

  const socketOpen =
    state.ws && state.ws.readyState === WebSocket.OPEN && state.connected;
  if (
    isEmailBindingMode(mode) && !socketOpen
  ) {
    setEmailBindingPanel(false);
    setAuthUiMode(AuthUiMode.Login);
    setAuthPanelVisible(true);
    setStatus(t("status.bindingSessionExpired"));
    return;
  }

  pendingAuthMode = mode;
  setAuthRequestPending(mode);
  if (socketOpen) {
    pendingAuthMode = null;
    sendAuth(mode, { allowRecovery: true });
    return;
  }
  return startConnection(false);
}

function connectAndAuth() {
  enterGameRequested = false;
  accountManageRequested = false;
  return queueAuthRequest(selectedAuthMode());
}

function startGame() {
  if (activeAuthMode !== null || state.authenticated || !state.connected ||
      !state.accountSession || !state.inventoryLoaded || state.ws?.readyState !== WebSocket.OPEN) return;
  const account = state.accountName;
  accountField.setValue(account);
  passwordField.clear();
  enterGameRequested = true;
  accountManageRequested = false;
  accountUi.panel.setOpen(false);
  return queueAuthRequest(AuthMode.EnterGame);
}

function logoutAccount() {
  const endpoint = state.ws?.url || wsUrlField.getValue({ trim: true });
  const account = (state.authenticated || state.accountSession) ? state.accountName : authRecovery.account(endpoint);
  const token = authRecovery.token(endpoint, account);
  const deviceId = token ? authRecovery.device(endpoint, account) : "";
  const connectedAccount = state.ws?.readyState === WebSocket.OPEN && (state.authenticated || state.accountSession);
  closeSocket(true);
  if (!connectedAccount && token) void revokeSavedAccount({ endpoint, account, deviceId, token });
  enterGameRequested = false;
  accountManageRequested = false;
  accountBindings.reset();
  clearAuthRequestPending();
  passwordField.clear();
  accountUi.reset();
  setAuthUiMode(AuthUiMode.Login);
  setAuthPanelVisible(true);
  setStatus(t("ui.auth.loggedOut"), true);
}

function ensureAccountSession({ openPanel = true } = {}) {
  if (state.authenticated || state.accountSession) return;
  const account = authRecovery.account(wsUrlField.getValue({ trim: true }));
  if (!account) return;
  accountManageRequested = openPanel;
  if (activeAuthMode !== null) return;
  accountField.setValue(account); passwordField.clear(); enterGameRequested = false;
  accountUi.restoring();
  queueAuthRequest(AuthMode.AccountLogin);
}

function requestVerificationCode() {
  return queueAuthRequest(AuthMode.RequestBindingCode);
}

function confirmEmailBinding() {
  return queueAuthRequest(AuthMode.ConfirmBinding);
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
    // 登录背景只作装饰，不阻塞连接和认证。
    void loadLoginMap();
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
    if (!automatic) accountUi.requireLogin();
    setStatus(t(automatic ? "status.reconnectFailed" : "status.socketError"));
    if (!automatic)
      addConsoleLine(String(error || t("status.socketError")), "error");
    scheduleReconnect();
    return;
  }
  state.ws = ws;
  state.receiveBuffer = new Uint8Array();
  ws.binaryType = "arraybuffer";

  ws.addEventListener("open", () => {
    if (state.ws !== ws || generation !== connectionGeneration) {
      ws.close();
      return;
    }
    state.connected = true;
    setStatus(t(automatic ? "status.reconnected" : "status.connected"), true);
    if (!automatic) addConsoleLine(t("status.connectedBridge"));
    // 协议版本必须为首包，服务器先拒绝不兼容版本再解析其他消息。
    sendBytes(packVersion());
    const mode = automatic ? (enterGameRequested ? AuthMode.Login : AuthMode.AccountLogin) : pendingAuthMode ?? selectedAuthMode();
    pendingAuthMode = null;
    if (activeAuthMode === null) setAuthRequestPending(mode);
    sendAuth(mode, { allowRecovery: true });
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
    notifyConnectionReset();
    state.ws = null;
    state.receiveBuffer = new Uint8Array();
    state.connected = false;
    state.authenticated = false;
    state.accountSession = false;
    accountBindings.reset();
    state.emailBindingRequired = false;
    clearAuthRequestPending();
    setEmailBindingPanel(false);
    setAuthUiMode(AuthUiMode.Login);
    resetNetworkScene({ resetRunCollection: !reconnectArmed });
    updatePlayUiVisibility();
    if (reconnectArmed && !pageUnloading) {
      if (!enterGameRequested) accountUi.restoring();
      if (reconnectAttempt === 0)
        addConsoleLine(t("status.connectionLost"), "error");
      scheduleReconnect();
    } else {
      resumingConnection = false;
      accountUi.requireLogin();
      setAuthPanelVisible(true);
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
  accountBindings.reset();
  if (sendDisconnect) economyRequests.reset();
  else economyRequests.disconnect();
  if (sendDisconnect) {
    const endpoint = state.ws?.url || wsUrlField.getValue({ trim: true });
    const account = (state.authenticated || state.accountSession) ? state.accountName : authRecovery.account(endpoint) || state.accountName;
    authRecovery.forget(endpoint, account);
  }
  state.receiveBuffer = new Uint8Array();
  reconnectArmed = false;
  resumingConnection = false;
  reconnectAttempt = 0;
  cancelReconnect();
  connectionGeneration += 1;
  notifyConnectionReset();
  if (state.ws && state.ws.readyState === WebSocket.OPEN && sendDisconnect) {
    sendBytes(packChores(false, false, false, true));
  }
  const ws = state.ws;
  state.ws = null;
  if (ws) ws.close();
  state.connected = false;
  state.authenticated = false;
  state.accountSession = false;
  state.emailBindingRequired = false;
  setEmailBindingPanel(false);
  resetNetworkScene();
  updatePlayUiVisibility();
  loadLoginMap();
}

function sendNeutralGameplayState({ includeInput = false } = {}) {
  if (!state.connected || !state.authenticated) return false;
  if (includeInput) sendBytes(packInput(0, 0));
  return sendBytes(packChores(false, false, false, false, false));
}

function requestControlledSkill(skillIndex) {
  if (!state.connected || !state.authenticated || isOwnerDead()) return false;
  return sendBytes(packSkill(skillIndex));
}

function sendAuth(mode = AuthMode.Login, { passwordOverride, allowRecovery = false } = {}) {
  if (!validateAuthFields(mode, { allowRecovery })) {
    clearAuthRequestPending();
    if (mode === AuthMode.AccountLogin) accountUi.requireLogin();
    if (resumingConnection) {
      reconnectArmed = false; resumingConnection = false; cancelReconnect();
      accountUi.requireLogin();
      setAuthPanelVisible(true); updatePlayUiVisibility(); void loadLoginMap();
    }
    return;
  }
  const binding = isEmailBindingMode(mode);
  const accountName = binding
    ? bindingAccountName()
    : accountField.getValue({ trim: true });
  if (!binding) state.accountName = accountName;
  const endpoint = state.ws?.url || wsUrlField.getValue({ trim: true });
  const login = mode === AuthMode.Login || mode === AuthMode.AccountLogin;
  const registration = mode === AuthMode.Register || mode === AuthMode.AccountRegister;
  const password = login || registration ? passwordOverride ?? passwordField.getValue() : "";
  const recoveryDeviceId = binding || mode === AuthMode.EnterGame ? "" : authRecovery.device(endpoint, accountName);
  const recoveryToken = login && !password && allowRecovery ? authRecovery.token(endpoint, accountName) : "";
  const packet = packAuth({
    mode,
    name: accountName,
    password,
    recoveryDeviceId,
    recoveryToken,
    email: binding ? emailField.getValue({ trim: true }) : "",
    code:
      mode === AuthMode.ConfirmBinding
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
      if (!latestSnapshot || isNewerUint32Sequence(resolved.snapshotId, latestSnapshot.snapshotId))
        latestSnapshot = resolved;
      continue;
    }
    if (latestSnapshot) {
      applySnapshotAndAck(latestSnapshot);
      latestSnapshot = null;
      pendingSnapshot = null;
    }
    dispatchServerMessage(msg);
  }
  if (latestSnapshot) {
    applySnapshotAndAck(latestSnapshot);
    pendingSnapshot = null;
  }
}

// 消息处理器按类型注册，便于各模块自行持有处理逻辑。
registerMessageHandler(ServerType.DebugTicket, (msg) => {
  void openDevDebug(msg.ticket).catch(error => {
    devDebugUi?.close(); state.devDebugOpen = false;
    addConsoleLine(`Dev Debug: ${error.message}`, "error");
  });
  msg.ticket = "";
});

registerMessageHandler(ServerType.SkillResult, (msg) => {
  const params = { index: msg.skillIndex, count: msg.skillCount };
  if (msg.resultCode === SkillResultCode.Success) {
    addConsoleLine(t("commands.skill.success", params));
    return;
  }

  const key =
    msg.resultCode === SkillResultCode.NoControlledMob
      ? "commands.skill.noControlledMob"
      : msg.resultCode === SkillResultCode.NoSkills
        ? "commands.skill.noSkills"
        : msg.resultCode === SkillResultCode.InvalidIndex
          ? "commands.skill.outOfRange"
          : "commands.skill.unavailable";
  addConsoleLine(t(key, params), "error");
});

registerMessageHandler(ServerType.Welcome, (msg) => {
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
  state.playerSessionGeneration = msg.playerSessionGeneration;
  state.worldId = msg.worldId;
  state.worldGeneration = msg.worldGeneration;
  state.ownerEntityId = msg.ownerEntityId;
  state.serverTickInterval =
    msg.tickRate > 0 ? 1 / msg.tickRate : serverFixedDt;
  if (msg.mapName) {
    const epoch = sceneTerrain.epoch;
    loadMap(msg.mapName, { freshInstance: true }).then((map) => {
      if (map) sceneTerrain.finish(epoch);
      else if (epoch === sceneTerrain.epoch) state.ws?.close();
    });
  }
  state.weatherEnabled = msg.weatherEnabled !== false;
  setStatus(t("status.player", { id: msg.playerId }), true);
  return;
});

registerMessageHandler(ServerType.AuthResult, (msg) => {
  const resultCode = msg.resultCode ?? AuthResultCode.Failed;
  const completesAuthentication =
    resultCode === AuthResultCode.Authenticated ||
    resultCode === AuthResultCode.EmailBound;
  const informational =
    resultCode === AuthResultCode.VerificationCodeSending ||
    resultCode === AuthResultCode.VerificationCodeSent;
  const wasAuthenticated = state.authenticated;
  const accountOnlyCompletion = completesAuthentication && !wasAuthenticated && !enterGameRequested;
  const bindingPanelOpen = isEmailBindingPanelOpen();
  if (
    Number.isInteger(msg.emailPolicy) &&
    msg.emailPolicy >= EmailPolicy.Disabled &&
    msg.emailPolicy <= EmailPolicy.Required
  )
    state.emailPolicy = msg.emailPolicy;
  if (typeof msg.emailBound === "boolean")
    state.emailBound = msg.emailBound;
  if (resultCode !== AuthResultCode.VerificationCodeSending)
    clearAuthRequestPending();

  const bindingCompletedWhilePlaying =
    wasAuthenticated && resultCode === AuthResultCode.EmailBound;
  state.authenticated =
    (completesAuthentication && !accountOnlyCompletion) ||
    (wasAuthenticated && (bindingPanelOpen || informational));
  setStatus(
    msg.message ||
      t(completesAuthentication ? "status.authenticated" : "status.authFailed"),
    completesAuthentication || informational,
  );
  if (bindingCompletedWhilePlaying) {
    state.emailBound = true;
    verificationCodeField.clear();
    setEmailBindingPanel(false);
  } else if (completesAuthentication) {
    if (msg.recoveryToken) authRecovery.accept(state.ws?.url || wsUrlField.getValue({ trim: true }), state.accountName,
      msg.recoveryDeviceId, msg.recoveryToken, msg.recoveryExpiresAt);
    passwordField.clear();
    verificationCodeField.clear();
    setAuthUiMode(AuthUiMode.Login);
    setEmailBindingPanel(false);
    state.accountSession = true;
    if (accountOnlyCompletion) {
      reconnectArmed = true;
      reconnectAttempt = 0;
      resumingConnection = false;
      cancelReconnect();
      if (accountManageRequested) { accountUi.reset(); accountUi.panel.setOpen(true); }
      else accountUi.signedIn();
      accountManageRequested = false;
      updatePlayUiVisibility();
      return;
    }
    const resumed = resumingConnection;
    reconnectArmed = true;
    reconnectAttempt = 0;
    cancelReconnect();
    resumingConnection = false;
    state.ownerStateLoaded = false;
    state.inventoryLoaded = false;
    economyRequests.connect(JSON.stringify([state.ws?.url, state.accountName]));
    if (!resumed) {
      state.runCollectionBaseline = null;
      state.runCollectionBaselinePending = true;
      state.partyMembers = [];
    }
    renderInventoryPanel();
    if (state.craftOpen) renderCraftPanel();
    sendBytes(packStateRequest());
    setAuthPanelVisible(false);
  } else if (resultCode === AuthResultCode.EmailBindingRequired) {
    state.authenticated = false;
    state.emailBound = false;
    reconnectArmed = false;
    resumingConnection = false;
    passwordField.clear();
    verificationCodeField.clear();
    setEmailBindingPanel(true, { required: true });
    loadLoginMap();
  } else if (informational) {
    if (!wasAuthenticated) {
      reconnectArmed = false;
      resumingConnection = false;
    }
    if (resultCode === AuthResultCode.VerificationCodeSent)
      verificationCodeField.focus();
  } else if (
    bindingPanelOpen &&
    (wasAuthenticated || state.emailBindingRequired)
  ) {
    state.authenticated = wasAuthenticated;
    if (state.emailPolicy === EmailPolicy.Disabled)
      setEmailBindingPanel(false);
  } else {
    state.authenticated = false;
    reconnectArmed = false;
    resumingConnection = false;
    setEmailBindingPanel(false);
    if (!state.accountSession) accountUi.requireLogin();
    setAuthPanelVisible(true);
    loadLoginMap();
  }
  updatePlayUiVisibility();
  addConsoleLine(
    msg.message ||
      t(completesAuthentication ? "status.authenticated" : "status.authFailed"),
    resultCode === AuthResultCode.Failed ? "error" : "",
  );
  return;
});

registerMessageHandler(ServerType.Snapshot, (msg) => {
  const resolved = resolveSnapshot(msg);
  if (resolved) applySnapshotAndAck(resolved);
  else requestFullSnapshot();
  pendingSnapshot = null;
  return;
});

registerMessageHandler(ServerType.Environment, (msg) => {
  state.environment = {
    received: true,
    anchorServerTick: Number.isSafeInteger(msg.anchorServerTick)
      ? msg.anchorServerTick
      : 0,
    timeOfDay: clamp(msg.timeOfDay || 0, 0, 1),
    dayDurationSeconds: Math.max(0.001, msg.dayDurationSeconds || 21600),
    fixedDtSeconds: Math.max(0.000001, msg.fixedDtSeconds || serverFixedDt),
    weatherIntensity: Array.isArray(msg.weatherIntensity)
      ? msg.weatherIntensity.map((value) => clamp(value / 255, 0, 1))
      : [1, 0, 0, 0],
    windDirection: Number.isFinite(msg.windDirection)
      ? (clamp(msg.windDirection / 255, 0, 1) * 360) % 360
      : 0,
    windSpeed: Number.isFinite(msg.windSpeed)
      ? clamp(msg.windSpeed / 255, 0, 1) * 20
      : 0,
    receivedAt: performance.now(),
  };
  return;
});

registerMessageHandler(ServerType.CodexFull, (msg) => {
  state.codex = new Map();
  for (const entry of msg.codexEntries || []) {
    if (!entry.mobType || !entry.rarity) continue;
    state.codex.set(codexKey(entry.mobType, entry.rarity), entry.count || 0);
  }
  renderCodexPanel();
  return;
});

registerMessageHandler(ServerType.CodexDelta, (msg) => {
  for (const entry of msg.codexDeltaEntries || []) {
    if (!entry.mobType || !entry.rarity) continue;
    state.codex.set(codexKey(entry.mobType, entry.rarity), entry.count);
  }
  renderCodexPanel();
  return;
});

registerMessageHandler(ServerType.CodexLoot, (msg) => {
  // 场景屏障重置表，一张完整表可跨帧发送。
  if (!state.codexLoot) state.codexLoot = new Map();
  for (const entry of msg.codexLootEntries || []) {
    const key = codexKey(entry.mobType, entry.mobRarity);
    if (!state.codexLoot.has(key)) state.codexLoot.set(key, []);
    state.codexLoot.get(key).push({
      petalType: entry.petalType,
      dropRarity: entry.dropRarity,
      dropRate: entry.dropRate,
    });
  }
  return;
});

registerMessageHandler(ServerType.LightningChain, (msg) => {
  if (Array.isArray(msg.lightningPoints) && msg.lightningPoints.length > 1) {
    pushLightningChain(msg.lightningPoints, msg.lightningStyle || 0);
  }
  return;
});

registerMessageHandler(ServerType.TerrainWallRemove, (msg) => {
  sceneTerrain.accept({ wallRemove: true, wallRemoveMin: msg.wallRemoveMin, wallRemoveMax: msg.wallRemoveMax });
  return;
});

registerMessageHandler(ServerType.TerrainDelta, (msg) => {
  // Welcome 后地图异步加载，缓存提前到达的地形增量，避免丢失初始化历史。
  sceneTerrain.accept({ layer: msg.terrainLayer, row: msg.terrainRow, col: msg.terrainCol, gid: msg.terrainGid });
  return;
});

registerMessageHandler(ServerType.OwnerState, (msg) => {
  const locked = (msg.flags & 1) !== 0;
  state.npcInventory = (msg.flags & 2) !== 0;
  state.temporaryInventory = (msg.flags & 4) !== 0;
  if (locked || locked !== state.ownerLoadoutLocked) {
    clearSlotTransactions();
    clearDrag();
  }
  state.ownerLoadoutLocked = locked;
  state.ownerLevel = Math.max(1, msg.level || 1);
  state.ownerExpProgress = clamp(msg.expProgress || 0, 0, 1);
  state.talentPoints = Math.max(0, msg.talentPoints || 0);
  state.talents = msg.talents || [];
  talentTreeUi?.acknowledge();
  updateQuickActionButtons();
  const primaryCount = Math.max(
    1,
    (msg.ownerSlots || []).length || state.ownerSlots.length || 5,
  );
  if (primaryCount !== state.ownerSlots.length) clearSlotTransactions();
  state.ownerSlots = normalizeSlots(msg.ownerSlots, primaryCount);
  state.secondarySlots = normalizeSlots(msg.secondarySlots, primaryCount);
  state.selectedSlot = clamp(
    state.selectedSlot,
    0,
    state.ownerSlots.length - 1,
  );
  state.ownerStateLoaded = true;
  economyRequests.owner();
  maybeCaptureRunCollectionBaseline();
  scheduleInventoryUiRender();
  refreshClientFeatures();
  return;
});

registerMessageHandler(ServerType.ClientFeatures, msg => {
  if (applyClientFeatures(msg.data)) { clearDrag(); scheduleInventoryUiRender(); if (state.craftOpen) renderCraftPanel(); if (state.talentOpen) renderTalentPanel(); }
});
registerMessageHandler(ServerType.AccountLinks, msg => accountBindings.receive(msg.data));

registerMessageHandler(ServerType.CombatFeedback, msg => {
  const now = performance.now();
  if (getClientConfigValue(state.clientConfig, "damage_numbers") >= 0.5) combatFeedback.add(msg.hits, now);
  let sourceChanged = false;
  for (const hit of msg.hits || []) {
    const entity = state.entities.get(hit.targetId);
    if (entity && hit.targetGeneration && hit.targetGeneration !== entity.snapshot.entityGeneration) continue;
    if (entity && hit.kind !== 1 && !(entity.snapshot.flags & flagDead) &&
        now / 1000 >= (entity.hurtFlashReadyAt || 0)) {
      entity.hurtFlashAge = 0;
      entity.hurtFlashReadyAt = now / 1000 + hurtFlashInterval;
    }
    if (hit.targetId !== state.ownerEntityId) continue;
    sourceChanged = sourceChanged || hit.sourceType !== lastOwnerDamage?.sourceType || hit.sourceRarity !== lastOwnerDamage?.sourceRarity;
    lastOwnerDamage = { ...hit, receivedAt: now };
    if (ownerDeathSummary && isOwnerDead()) ownerDeathSummary.source = { ...lastOwnerDamage };
  }
  if (sourceChanged && isOwnerDead()) renderDeathOverlayContent();
});

registerMessageHandler(ServerType.Inventory, (msg) => {
  if (!state.authenticated && (!state.accountSession || !state.connected ||
      msg.inventoryScope !== state.accountName || msg.actorScope)) return;
  state.inventory = msg.inventory || [];
  state.inventoryLoaded = true;
  if (!state.authenticated) {
    menuBackdrop.setInventory(state.inventory);
    accountUi.inventoryReady();
    return;
  }
  economyRequests.inventory(msg);
  maybeCaptureRunCollectionBaseline();
  // 首载时预热隐藏列表，首次开背包只需差异更新。
  scheduleInventoryUiRender({
    forceInventory: inventoryList.childElementCount === 0,
  });
  if (isOwnerDead()) renderDeathOverlayContent();
  return;
});

registerMessageHandler(ServerType.EconomyResult, (msg) => {
  economyRequests.result(msg);
  return;
});

registerMessageHandler(ServerType.PartyState, (msg) => {
  const self = msg.members.find(member => member.playerId === state.playerId);
  if (msg.members.length && (!self || self.sessionGeneration !== state.playerSessionGeneration)) return;
  state.partyMembers = msg.members;
});

registerMessageHandler(ServerType.Chat, (msg) => {
  const chat = msg.chat?.flag === ChatFlag.Server ?
    { ...msg.chat, message: localizeSquadEventMessage(msg.chat.message || "") } : { ...msg.chat };
  if (chat.message) chat.message = applyChatReplacement(chat.message);
  if (chat.message) {
    ui.mysteryAudio.handleChat(chat);
    ui.chatUi.append(chat);
  }
  addConsoleLine(
    `${msg.chat.playerName || msg.chat.playerId}: ${chat.message || ""}`,
  );
});

function resetNetworkScene({ resetRunCollection = true } = {}) {
  menuBackdrop.setInventory([]);
  resetClientFeatures();
  state.npcInventory = false; state.temporaryInventory = false;
  sceneTerrain.begin();
  state.codex = new Map();
  state.codexLoot = new Map();
  slotSwapMotion.cancelAll();
  pendingSnapshot = null;
  snapshotHistory.clear();
  snapshotResetRequested = false;
  ui.mysteryAudio.reset();
  clearSlotTransactions();
  state.entities.clear();
  resetSpriteMotion();
  bossBarMotions.clear();
  playerHud.clear();
  healthTrails.clear();
  combatFeedback.clear();
  burrowParticles.clear();
  rainField.reset();
  blockFogField.reset();
  lastWeatherEffectsAt = 0;
  actualRain = 0;
  actualFog = 0;
  actualCloudyTint = 0;
  lastOwnerDamage = null;
  ownerDeathSummary = null;
  respawnRequest.reset();
  state.deathViewFactor = 1;
  state.snapshotId = -1;
  state.serverTick = -1;
  state.serverTickReceivedAt = 0;
  state.serverTickInterval = serverFixedDt;
  state.environment = {
    received: false,
    anchorServerTick: 0,
    timeOfDay: 0.25,
    dayDurationSeconds: 21600,
    fixedDtSeconds: serverFixedDt,
    weather: 0,
    receivedAt: 0,
  };
  state.weatherEnabled = true;
  state.lightningChains.length = 0;
  state.lastInput = {
    x: 99,
    y: 99,
    attacking: false,
    defending: false,
    digging: false,
  };
  state.sendTimer = 1;
  state.ownerStateLoaded = false;
  state.ownerSlotRuntime = [];
  state.inventoryLoaded = false;
  state.localMoveInput = { x: 0, y: 0 };
  releaseInputHolds();
  state.screenShake = { x: 0, y: 0 };
  state.particles = [];
  state.forgeMode = false;
  craftPanel.classList.remove("forge-mode");
  state.deathOverlayClosed = false;
  state.wasOwnerDead = false;
  state.partyMembers = [];
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
  if (state.snapshotId >= 0 && !isNewerUint32Sequence(resolved.snapshotId, state.snapshotId)) return;
  if (!pendingSnapshot || isNewerUint32Sequence(resolved.snapshotId, pendingSnapshot.snapshotId))
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
  noteServerSnapshot();
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

const squadEventKeys = Object.freeze({
  "<sq-invite>": "squad.event.inviteReceived",
  "<sq-invite-sent>": "squad.event.inviteSent",
  "<sq-invite-none>": "squad.event.noInvite",
  "<sq-invite-refused>": "squad.event.inviteRefused",
  "<sq-invite-refused-you>": "squad.event.inviteRefusedYou",
  "<sq-invite-failed>": "squad.event.inviteFailed",
  "<sq-invite-failed-them>": "squad.event.inviteFailedThem",
  "<sq-joined-you>": "squad.event.joinedYou",
  "<sq-joined>": "squad.event.joined",
  "<sq-invite-invalid>": "squad.event.inviteInvalid",
});

function localizeSquadEventMessage(message) {
  const text = String(message || "").trim();
  for (const [marker, key] of Object.entries(squadEventKeys)) {
    if (text === `${marker}()`) return t(key);
    if (text.startsWith(`${marker}(`) && text.endsWith(")"))
      return t(key, { name: text.slice(marker.length + 1, -1) });
  }
  return text;
}


function touchedInventoryKeysFrom(...items) {
  const keys = new Set();
  for (const item of items) {
    if (slotHasItem(item)) keys.add(inventoryKey(item.petalType, item.rarity));
  }
  return Array.from(keys);
}

function mergeTouchedInventory(current, target, keys = []) {
  if (!keys.length) return copyInventoryItems(target);
  const out = copyInventoryItems(current);
  const entries = new Map(out.map(item => [inventoryKey(item.petalType, item.rarity), item]));
  const targetCounts = new Map(copyInventoryItems(target).map(item => [inventoryKey(item.petalType, item.rarity), item.count]));
  for (const key of keys) {
    const [petalText, rarityText] = String(key).split(":");
    const petalType = Number(petalText) || 0;
    const rarity = Number(rarityText) || 0;
    if (!petalType || !rarity) continue;
    const targetCount = targetCounts.get(key) || 0;
    const existing = entries.get(key);
    if (existing) existing.count = targetCount;
    else if (targetCount > 0) {
      const item = { petalType, rarity, count: targetCount };
      out.push(item); entries.set(key, item);
    }
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

function makeDisplaySlotState() {
  const ownerCount = Math.max(1, state.ownerSlots.length || 5);
  const latest = loadoutPreview.current;
  if (!latest) {
    return {
      ownerSlots: copySlots(state.ownerSlots, ownerCount),
      secondarySlots: copySlots(state.secondarySlots, ownerCount),
      inventory: copyInventoryItems(state.inventory),
    };
  }
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
  return copySlots(loadoutPreview.current?.ownerSlots || state.ownerSlots);
}

function displaySecondarySlots() {
  return copySlots(loadoutPreview.current?.secondarySlots || state.secondarySlots);
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

function getPlayerLevelRarity(snap) {
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
  { keepSwapMotion = false } = {},
) {
  const tx = {
    id: ++state.slotTxSeq,
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
  if (!economyRequests.submit({ operation: EconomyOperation.Loadout, primary: ownerSlots, secondary: secondarySlots, clientId: tx.id }))
    return null;
  loadoutPreview.stage(tx);
  if (!keepSwapMotion) slotSwapMotion.cancelAll();
  return tx;
}

function clearSlotTransactions() {
  loadoutPreview.clear();
  slotSwapMotion.cancelAll();
}

const economyRequests = createEconomyRequests({
  send: bytes => state.authenticated && sendBytes(bytes),
  onScopeChanged: () => {
    clearSlotTransactions();
    craftSpin.cancel(); state.craftPhase = "idle"; clearCraftDisplay();
    scheduleInventoryUiRender();
  },
  onResult: (msg, intent) => {
    if (intent.operation === EconomyOperation.Loadout) {
      loadoutPreview.acknowledge(intent.clientId);
      if (msg.outcome !== EconomyOutcome.Completed) {
        clearSlotTransactions();
        respawnRequest.reset();
        addConsoleLine(t("status.loadoutRejected"), "error");
      }
      scheduleInventoryUiRender();
    } else {
      if ([EconomyOutcome.OutcomeExpired, EconomyOutcome.ConflictingRequest, EconomyOutcome.WrongScope].includes(msg.outcome)) {
        craftSpin.cancel(); state.craftPhase = "idle"; clearCraftDisplay(); renderCraftPanel();
        addConsoleLine(t("status.economyUnconfirmed"), "error");
      } else handleCraftResult({ ...msg, petalType: intent.petalType, rarity: intent.rarity });
    }
  },
});
const respawnRequest = createRespawnRequest({
  send: () => state.connected && state.authenticated && isOwnerDead() &&
    sendBytes(packChores(false, false, true, false, false)),
  hasPendingChanges: () => economyRequests.pending,
});
window.setInterval(() => {
  if (!state.authenticated) economyRequests.disconnect();
  else economyRequests.tick();
}, 1000);

const sceneTerrain = createSceneTerrainStream((delta) => {
    if (delta.wallRemove) {
      if (
        ui.mapRenderer.applyWallRemove(
          delta.wallRemoveMin,
          delta.wallRemoveMax,
        )
      )
        minimapCache = null;
      return;
    }
    if (
      ui.mapRenderer.applyTerrainDelta(delta.layer, delta.row, delta.col, delta.gid)
    ) {
      minimapCache = null;
      const map = state.map;
      if (map?.tileWidth && map?.tileHeight) {
        emitTerrainDust(
          {
            x: (delta.col + 0.5) * map.tileWidth,
            y: (delta.row + 0.5) * map.tileHeight,
          },
          Math.max(map.tileWidth, map.tileHeight),
        );
      }
    }
}, { onOverflow: () => state.ws?.close() });

async function loadMap(mapName, options) {
  return ui.mapRenderer.loadMap(mapName, options);
}

function loadLoginMap() {
  if (state.authenticated) return Promise.resolve(null);
  applyLoginMapView();
  return loadMap(state.loginMapName || defaultMapName, { showLoading: false });
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

function setViewZoomPercent(value, persist = true) {
  const next = normalizeViewZoomPercent(value);
  const previous = normalizeViewZoomPercent(state.viewZoomPercent);
  if (!Number.isFinite(state.renderViewZoomPercent)) state.renderViewZoomPercent = previous;
  if (next === previous) {
    state.viewZoomPercent = next;
    return false;
  }
  state.viewZoomPercent = next;
  if (persist) saveSettings();
  requestRedraw();
  return true;
}

function adjustViewZoomPercent(direction) {
  const sign = Number(direction) < 0 ? -1 : 1;
  return setViewZoomPercent(
    normalizeViewZoomPercent(state.viewZoomPercent) +
      sign * viewZoomStepPercent,
  );
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
  if (state.snapshotId >= 0 && !isNewerUint32Sequence(msg.snapshotId, state.snapshotId)) return false;
  const initialScene = state.snapshotId < 0;
  if (
    state.environment &&
    state.environment.received &&
    Array.isArray(msg.weatherIntensity)
  ) {
    state.environment.weatherIntensity = msg.weatherIntensity.map((value) =>
      clamp(value / 255, 0, 1),
    );
    if (Number.isFinite(msg.windDirection)) {
      state.environment.windDirection =
        (clamp(msg.windDirection / 255, 0, 1) * 360) % 360;
    }
    if (Number.isFinite(msg.windSpeed)) {
      state.environment.windSpeed = clamp(msg.windSpeed / 255, 0, 1) * 20;
    }
  }
  const snapshotNowMs = performance.now();
  const snapshotNow = snapshotNowMs / 1000;
  state.snapshotId = msg.snapshotId;
  if (Number.isSafeInteger(msg.serverTick)) {
    state.serverTick = msg.serverTick;
    state.serverTickReceivedAt = snapshotNowMs;
  }
  const previousOwnerEntityId = state.ownerEntityId;
  const nextOwnerEntityId = msg.ownerEntityId ?? previousOwnerEntityId;
  const previousOwner = state.entities.get(previousOwnerEntityId);
  const previousOwnerWasDead =
    !!previousOwner && (previousOwner.snapshot.flags & flagDead) !== 0;
  const nextOwnerSnapshot = (msg.entities || []).find(snap => snap.entityId === nextOwnerEntityId);
  const ownerChanged = nextOwnerEntityId !== previousOwnerEntityId ||
    (!!previousOwner && !!nextOwnerSnapshot && !sameEntityIncarnation(previousOwner.snapshot, nextOwnerSnapshot));
  if (ownerChanged) {
    // 同场景换身体不再清空场景，但旧身体的拖拽和配装预览不能继承。
    clearSlotTransactions();
    clearDrag();
    state.ownerSlotRuntime = [];
  }
  if (
    ownerChanged &&
    (previousOwnerWasDead || state.wasOwnerDead || !state.runCollectionBaseline)
  ) {
    startRunCollectionBaseline();
  }
  state.ownerEntityId = nextOwnerEntityId;
  const nextViewRadius = msg.viewRadius || state.viewRadius;
  if (nextViewRadius > 0) setTargetViewRadius(nextViewRadius, snapshotNowMs);

  const live = new Set();
  for (const snap of msg.entities || []) {
    live.add(snap.entityId);
    let existing = state.entities.get(snap.entityId);
    if (existing && !sameEntityIncarnation(existing.snapshot, snap)) {
      // ID 会复用，新实体不能继承旧实体死亡姿态和速度。
      clearSpriteMotion(snap.entityId);
      clearLadybugPattern(snap.entityId);
      healthTrails.delete(snap.entityId);
      existing = null;
    }
    if (existing) {
      burrowParticles.observe(existing.snapshot, snap, !existing.dying && !existing.missingSnapshotCount, existing.renderPos);
      resumeEntityAppearance(existing);
      const previousSnapshot = existing.snapshot;
      const wasDead = (existing.snapshot.flags & flagDead) !== 0;
      const isDead = (snap.flags & flagDead) !== 0;
      const deathVelocity = snap.entityId === state.ownerEntityId
        ? { ...(existing.snapshotVelocity) }
        : { ...existing.snapshotVelocity };
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
      if (isDead && !wasDead) {
        beginDeathMotion(existing, snapshotNow, { velocity: deathVelocity });
        if (snap.entityId === state.ownerEntityId) {
          ownerDeathSummary = { snapshot: { ...previousSnapshot, flags: snap.flags },
            slots: captureDeathLoadout(previousSnapshot), startedAt: snapshotNowMs,
            source: lastOwnerDamage && snapshotNowMs - lastOwnerDamage.receivedAt < 10000 ? { ...lastOwnerDamage } : null };
          respawnRequest.reset();
        }
      } else if (!isDead && wasDead) {
        clearDeathMotion(existing);
        if (snap.entityId === state.ownerEntityId) lastOwnerDamage = null;
      } else if (isDead && existing.deathMotion &&
        Math.hypot(snap.pos.x - existing.deathMotion.anchor.x, snap.pos.y - existing.deathMotion.anchor.y) > Math.max(200, snap.radius * 6)) {
        clearDeathMotion(existing);
        existing.renderPos = { ...snap.pos };
        beginDeathMotion(existing, snapshotNow, { resting: true });
      }
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
      existing.missingSnapshotCount = 0;
      const hurtFlashReadyAt = Number.isFinite(existing.hurtFlashReadyAt)
        ? existing.hurtFlashReadyAt
        : 0;
      if (
        !isDead &&
        snap.hpPercent < previousHp - hurtFlashMinDelta &&
        snapshotNow >= hurtFlashReadyAt
      ) {
        existing.hurtFlashAge = 0;
        existing.hurtFlashReadyAt =
          snapshotNow + Math.max(0, hurtFlashInterval);
      }
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
        missingSnapshotCount: 0,
        playerPrimarySlotRarity:
          snap.entityType === playerFlowerType
            ? highestSlotRarity(snap.primarySlots)
            : 0,
        motionBlend: 0,
        dying: false,
        deathAge: 0,
        hurtFlashAge: hurtFlashDuration,
        hurtFlashReadyAt: 0,
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
      beginEntityAppearance(created, {
        immediate: initialScene || snap.entityId === state.ownerEntityId || isPetalEntity(snap.entityType),
        card: isDropEntity(snap.entityType),
      });
      if (isDead) {
        beginDeathMotion(created, snapshotNow, { resting: true });
        if (snap.entityId === state.ownerEntityId) ownerDeathSummary = {
          snapshot: { ...snap }, slots: captureDeathLoadout(snap), startedAt: snapshotNowMs,
        };
      }
      if (snap.entityId === state.ownerEntityId)
        syncEntityRenderToSnapshot(created, snap);
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
    entity.missingSnapshotCount = (entity.missingSnapshotCount || 0) + 1;
    if (entity.dying) continue;
    if (!shouldFadeMissingEntity(entity)) {
      state.entities.delete(id);
      clearLadybugPattern(id);
      clearSpriteMotion(id);
      continue;
    }
    // 离开快照表示不可见，不等于死亡或拾取确认；再次可见时从当前淡出姿态逆转。
    beginEntityExit(entity, snapshotNow, {
      card: isDropEntity(entity.snapshot.entityType),
      mob: isMobSnap(entity.snapshot),
      defeated: (entity.snapshot.flags & flagDead) !== 0 || entity.snapshot.hpPercent <= 0,
    });
    entity.hurtFlashAge = hurtFlashDuration;
    if (entity.deathAngle == null)
      entity.deathAngle = Math.random() * Math.PI * 2;
  }
  updateForgeModeFromSnapshot();
  if (initialScene || ownerChanged) refreshClientFeatures();
  return true;
}

function ownerIsNearTitan() {
  if (!state.authenticated) return false;
  const owner = state.entities.get(state.ownerEntityId);
  const ownerPos = owner?.snapshot?.pos;
  if (!ownerPos || owner.dying || (owner.snapshot.flags & flagDead) !== 0) return false;

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

  const bannedSlots = new Set(
    (ownerSnap.states || [])
      .filter((entityState) => entityState?.type === stateBanSlot)
      .map((entityState) => entityState.value),
  );
  const previousRuntime = state.ownerSlotRuntime || [];
  state.ownerSlotRuntime = ownerSnap.primarySlots.map((slot, index) => ({
    petalType: slot?.petalType || 0,
    rarity: slot?.rarity || 0,
    banned: bannedSlots.has(index),
    visualType: slot?.visualType || PetalSlotVisualType.None,
    copies: (slot?.copies || []).map((copy) => ({
      loading: copy?.state === PetalSlotCopyState.Loading,
      progress: clamp(copy?.progress || 0, 0, 1),
      visual: Number.isFinite(copy?.visual) ? copy.visual : null,
    })),
  }));

  // 记录服务器给出的各类型/稀有度副本数，含加载中副本；变化时重建卡片。
  let copyCountsChanged = false;
  state.ownerSlotRuntime.forEach((runtime, index) => {
    const count = runtime.copies.length;
    if (!runtime.petalType || !runtime.rarity || count <= 0) return;
    if (
      recordLivePetalCopyCount(runtime.petalType, runtime.rarity, count, index)
    )
      copyCountsChanged = true;
    const previous = previousRuntime[index];
    if (
      previous &&
      previous.petalType === runtime.petalType &&
      previous.rarity === runtime.rarity &&
      (previous.copies || []).length !== count
    )
      copyCountsChanged = true;
  });
  if (copyCountsChanged) scheduleInventoryUiRender();

  updateOwnerSlotRuntimeOverlays();
}

function handleOwnerSnapshot(entity, snap, snapshotNow, wasDead, isDead) {
  if (wasDead && !isDead) {
    startRunCollectionBaseline();
    syncEntityRenderToSnapshot(entity, snap);
  }
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
  if (!isOwner) return false;
  const nullState = entityState(entity.snapshot, stateNullification);
  return !!nullState && nullState.rarity >= rarityPrimordial;
}

function readKeyboardMoveInput() {
  const { x: rawX, y: rawY } = state.moveVector;
  let x = rawX;
  let y = rawY;

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
  // 控制圆随视口尺寸缩放，不随增益、像素比或相机缩放变化。
  const fullSpeedDistance = Math.max(24,
    Math.min(state.canvasWidth, state.canvasHeight) * clientRuntimeConfig.mouseMoveFullSpeedRadiusRatio);
  return getPointerMoveIntent(state.mouse.x - center.x, state.mouse.y - center.y,
    fullSpeedDistance, mouseMoveDeadzonePx);
}

function isMainPanelOpen() {
  return ui.panelManager.isAnyOpen("main");
}

function readMoveInput() {
  if (isMainPanelOpen()) return { x: 0, y: 0 };
  if (state.movementMode === "none") return { x: 0, y: 0 };
  if (ui.mobileControls?.hasMovement()) return ui.mobileControls.getMove();
  if (ui.mobileControls?.isMobileMode()) return { x: 0, y: 0 };
  return state.movementMode === "keyboard"
    ? readKeyboardMoveInput()
    : readMouseMoveInput();
}

function updateMousePointer(event) {
  state.mouse.x = event.clientX;
  state.mouse.y = event.clientY;
  state.mouse.seen = true;
  state.mouse.inUi = !!isUiTarget(event.target);
}

function flushInput(dt) {
  // 已连发按键突然静默时，按漏收松键处理。
  pruneStaleInputHolds(performance.now());
  const typing = isTyping();
  // 文本框获取焦点前释放键盘来源，避免吞掉松键后残留移动。
  if (typing) releaseInputHolds({ device: "keyboard" });

  if (!state.connected || !state.authenticated || (typing && !state.devDebugOpen) || isOwnerDead()) {
    state.localMoveInput = { x: 0, y: 0 };
    return;
  }

  state.sendTimer += dt;
  const rawMove = state.devDebugOpen ? { x: 0, y: 0 } : readMoveInput();
  const move = quantizeMoveIntent(rawMove.x, rawMove.y);
  state.localMoveInput = move;
  const packetMoveX = move.y;
  const packetMoveY = move.x;
  const px = Math.round(clamp(packetMoveX, -1, 1) * 127);
  const py = Math.round(clamp(packetMoveY, -1, 1) * 127);
  const moveChanged = px !== state.lastInput.x || py !== state.lastInput.y;
  const attacking =
    state.holdAttack ||
    state.attacking ||
    !!ui.mobileControls?.isAttacking();
  const defending =
    state.holdDefense ||
    state.defending ||
    !!ui.mobileControls?.isDefending();
  const digging = false;
  const effectiveAttacking = !state.devDebugOpen && attacking;
  // 攻击和防御来源独立，允许攻击期间使用 Bubble 防御；重复输入由来源和包间隔去重。
  const effectiveDefending = !state.devDebugOpen && defending;
  const choresChanged =
    effectiveAttacking !== state.lastInput.attacking ||
    effectiveDefending !== state.lastInput.defending ||
    digging !== state.lastInput.digging;
  if (moveChanged || choresChanged || state.sendTimer >= packetInterval) {
    const sequence = state.inputSequence >>> 0;
    const sent = sendBytes(
      packInputFrame(
        sequence,
        0,
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

export function isOwnerDead() {
  const owner = state.authenticated
    ? state.entities.get(state.ownerEntityId)
    : null;
  return !!owner && (owner.snapshot.flags & flagDead) !== 0;
}

function loadoutEditingBlocked() {
  if (!featureAllowed('inventory', FeatureAccess.Edit)) return true;
  return !state.connected || !state.authenticated || !state.ownerStateLoaded || !state.inventoryLoaded ||
    state.ownerLoadoutLocked || !state.entities.has(state.ownerEntityId) || respawnRequest.pending;
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
    clearDrag();
    scheduleInventoryUiRender();
    releaseInputHolds();
    state.localMoveInput = { x: 0, y: 0 };
    ui.mobileControls?.reset();
    sendNeutralGameplayState({ includeInput: true });
    renderDeathOverlayContent();
  }
  if (!dead) {
    state.deathOverlayClosed = false;
    ownerDeathSummary = null;
    respawnRequest.reset();
  }
  if (dead) respawnRequest.tick();
  state.wasOwnerDead = dead;
  setAnimatedVisibility(deathOverlay, dead, { enterDuration: 420, exitDuration: 120 });
  deathOverlay.classList.toggle("closed", dead && state.deathOverlayClosed);
  const pending = respawnRequest.pending;
  reviveBtn.disabled = !state.connected || pending;
  const label = t(pending ? "ui.death.returning" : "ui.death.revive");
  if (reviveBtn.textContent !== label) reviveBtn.textContent = label;
}

export function renderDeathOverlayContent() {
  if (deathSource) {
    const hit = ownerDeathSummary?.source || lastOwnerDamage;
    const knownType = hit && (hit.sourceType < NETWORK_PETAL_TYPE_OFFSET
      ? MobNames[hit.sourceType] : PetalNames[hit.sourceType - NETWORK_PETAL_TYPE_OFFSET]);
    const known = hit && hit.sourceType > 0 && knownType &&
      (hit === ownerDeathSummary?.source || performance.now() - hit.receivedAt < 10000);
    deathSource.textContent = known
      ? (hit.sourceType < NETWORK_PETAL_TYPE_OFFSET ? mobTypeName(hit.sourceType) : petalTypeName(hit.sourceType - NETWORK_PETAL_TYPE_OFFSET))
      : t("ui.death.unknownSource");
    deathSource.title = known && hit.sourceRarity ? rarityName(hit.sourceRarity) : "";
  }
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
    .slice(0, 64);

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
  if (!respawnRequest.request()) return;
  releaseInputHolds();
  state.digging = false;
  ui.mobileControls?.reset();
  clearDrag();
  scheduleInventoryUiRender();
  updateDeathOverlay();
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
  ui.chatUi.open(prefix);
}

function closeChat() {
  ui.chatUi.close();
}

function submitChat() {
  ui.chatUi.submit();
}

export function closeSliderPanel() {
  state.slider = null;
  setAnimatedVisibility(sliderPanel, false, { duration: 160 });
  sliderPanel?.setAttribute("aria-hidden", "true");
}

export function renderSliderPanel() {
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

  setAnimatedVisibility(sliderPanel, true, { duration: 160 });
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

function renderChat(now = performance.now()) {
  ui.chatUi.render(now);
}

function updateChatVisibility(now) {
  ui.chatUi.updateVisibility(now);
}

function copySlot(slot) {
  return slotHasItem(slot)
    ? { petalType: slot.petalType, rarity: slot.rarity }
    : emptySlot();
}

function shouldSuppressClick() {
  return performance.now() < state.suppressClickUntil;
}

export function renderInventoryPanel(options = {}) {
  const title = document.getElementById("inventoryTitle");
  if (title) {
    const key = state.npcInventory ? "ui.inventory.npcTitle" : state.temporaryInventory ? "ui.inventory.temporaryTitle" : "ui.inventory.title";
    title.dataset.i18n = key; title.textContent = t(key);
  }
  const forceInventory = !!options.forceInventory;
  hidePetalInfoTooltip();
  const display = makeDisplaySlotState();
  if (loadoutEditingBlocked() || primarySlots.children.length !== display.ownerSlots.length ||
      secondarySlots.children.length !== display.secondarySlots.length) slotSwapMotion.cancelAll();
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
  refreshSlotKeyHints(display.ownerSlots.length);
  syncUiLayout();

  if (state.backpackOpen || forceInventory) {
    renderInventoryGroups(
      inventoryList,
      inventoryPanelItems(display.inventory),
    );
  }
}

// 底栏快捷键提示读取实时绑定，改键后同步更新。
const keyHintAliases = Object.freeze({
  escape: "Esc",
  space: "Space",
  shift: "Shift",
  control: "Ctrl",
  ctrl: "Ctrl",
  alt: "Alt",
  enter: "Enter",
  tab: "Tab",
  backspace: "Bksp",
  delete: "Del",
});

function formatKeyHint(key) {
  const name = String(key || "").toLowerCase();
  if (!name) return "";
  if (keyHintAliases[name]) return keyHintAliases[name];
  if (/^[a-z0-9]$/.test(name)) return name.toUpperCase();
  const numpad = /^numpad(.+)$/.exec(name);
  if (numpad) return `N${numpad[1].slice(0, 1).toUpperCase()}${numpad[1].slice(1, 3)}`;
  return name.charAt(0).toUpperCase() + name.slice(1, 5);
}

// 按完整命令字符串匹配首个绑定，优先普通数字键而非小键盘。
function keyboardKeyForCommand(command) {
  const table = bindings?.keyboard || {};
  let fallback = "";
  for (const key of Object.keys(table)) {
    const commands = table[key];
    if (!commands?.some((entry) => String(entry).trim() === command)) continue;
    if (/^[0-9]$/.test(key)) return key;
    if (!fallback) fallback = key;
  }
  return fallback;
}

function refreshSlotKeyHints(slotCount) {
  for (const [button, command] of [[quickBackpackBtn, "inventory"], [quickTalentBtn, "talent"], [quickCraftBtn, "craft"]]) {
    const label = button?.querySelector("kbd");
    const key = keyboardKeyForCommand(command);
    if (label) label.textContent = key ? `[${formatKeyHint(key)}]` : "";
  }
  if (!slotKeyHints || !slotSwapHint) return;
  const count = Math.max(0, slotCount | 0);
  const swapKey = keyboardKeyForCommand("swap");
  const swapText = swapKey ? `[${formatKeyHint(swapKey)}]` : "";
  if (slotSwapHint.textContent !== swapText) slotSwapHint.textContent = swapText;

  slotKeyHints.style.setProperty("--slot-count", count);
  for (let index = 0; index < count; index += 1) {
    const key = keyboardKeyForCommand(`select_slot ${index}`);
    const text = key ? `[${formatKeyHint(key)}]` : "";
    let cell = slotKeyHints.children[index];
    if (!cell) {
      cell = document.createElement("span");
      cell.className = "slot-key-hint slot-select-hint";
      slotKeyHints.appendChild(cell);
    }
    if (cell.textContent !== text) cell.textContent = text;
  }
  while (slotKeyHints.children.length > count) {
    slotKeyHints.lastElementChild?.remove();
  }
}

let inventoryUiRenderQueued = false;
let inventoryUiRenderForce = false;

// 背包消息可能突发到达，面板重建合并为每渲染帧一次。
function scheduleInventoryUiRender(options = {}) {
  inventoryUiRenderForce = inventoryUiRenderForce || !!options.forceInventory;
  if (inventoryUiRenderQueued) return;
  inventoryUiRenderQueued = true;
  requestAnimationFrame(() => {
    inventoryUiRenderQueued = false;
    const forceInventory = inventoryUiRenderForce;
    inventoryUiRenderForce = false;
    renderInventoryPanel({ forceInventory });
    if (state.craftOpen) renderCraftPanel();
    if (state.talentOpen) renderTalentPanel();
  });
}

function slotButtonRenderKey(slot, index, kind, primaryForVisual) {
  const petalType = slot?.petalType || 0;
  const rarity = slot?.rarity || 0;
  const count = slot?.count || 0;
  const visualType = slotHasItem(slot)
    ? slotVisualPetalType(slot, index, kind, primaryForVisual)
    : 0;
  // 副本数参与缓存键，变化时重建卡片堆叠。
  const copy = slotHasItem(slot)
    ? effectivePetalCardCopy(visualType || petalType, rarity, {
        kind,
        slotIndex: index,
      })
    : 0;
  // 显示名参与缓存键，切换语言时重建标签。
  const name = slotHasItem(slot) ? getPetalDisplayName(visualType || petalType) : "";
  return `${kind}:${petalType}:${rarity}:${count}:${visualType}:${copy}:${name}`;
}

function renderSlotRow(container, slots, kind, primaryForVisual) {
  const slotCount = Math.max(0, primaryForVisual?.length || slots?.length || 0);
  const rowSlots = normalizeSlots(slots, slotCount);
  container.style.setProperty("--slot-count", rowSlots.length);

  for (let index = 0; index < rowSlots.length; index += 1) {
    const slot = rowSlots[index];
    let button = container.children[index];
    // 飞行中保留源节点并隐藏目标卡片，避免网络更新打断动画或出现重复影像。
    if (button && slotSwapMotion.has(index)) {
      button.classList.add("slot-swap-hidden");
      continue;
    }
    const key = slotButtonRenderKey(slot, index, kind, primaryForVisual);
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
  const slots = displayOwnerSlots();
  for (let index = 0; index < primarySlots.children.length; index += 1) {
    if (slotSwapMotion.has(index)) continue;
    updateSlotDurabilityOverlay(
      primarySlots.children[index],
      slots[index],
      index,
      "primary",
    );
  }
}

function updateSlotDurabilityOverlay(button, slot, index, kind) {
  if (!button) return;

  const runtime = kind === "primary" ? state.ownerSlotRuntime[index] : null;
  updateSlotCounter(button, slot, runtime, kind);
  updateSlotBanOverlay(button, !!runtime?.banned);
  const copies =
    runtime &&
    runtime.petalType === (slot?.petalType || 0) &&
    runtime.rarity === (slot?.rarity || 0)
      ? runtime.copies || []
      : [];
  updateSlotLiveVisual(button, runtime, copies);
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

function updateSlotCounter(button, slot, runtime, kind) {
  const value = getSlotCounter(slot, runtime, kind);
  let badge = button.querySelector(":scope > .slot-value");
  if (value === null) {
    badge?.remove();
    return;
  }
  if (!badge) {
    badge = document.createElement("span");
    badge.className = "slot-value";
    button.appendChild(badge);
  }
  const text = value === undefined ? "\u2014" : formatShortCount(value);
  if (badge.textContent !== text) badge.textContent = text;
  const title = kind === "secondary" ? t("ui.slotCounter.inactive")
    : value === undefined ? t("ui.slotCounter.pending") : t("ui.slotCounter.live", { value });
  if (badge.title !== title) badge.title = title;
}

function updateSlotBanOverlay(button, banned) {
  let mark = button.querySelector(":scope > .slot-ban-mark");
  if (!banned) {
    mark?.remove();
    return;
  }
  if (mark) return;

  mark = document.createElement("span");
  mark.className = "slot-ban-mark";
  mark.setAttribute("aria-hidden", "true");
  button.appendChild(mark);
}

function updateSlotLiveVisual(button, runtime, copies) {
  const live = getClientConfigValue(state.clientConfig, "live_icons") >= 0.5;
  const icons = button.querySelectorAll(
    ":scope > .petal-stack > .petal-live-visual",
  );
  for (const icon of icons) {
    const copyIndex = Number(icon.dataset.petalCopyIndex || 0);
    const petalType = Number(icon.dataset.petalVisualType || 0);
    const copy = copies[copyIndex] || null;
    let angle = 0;
    let scale = 1;

    if (
      live && runtime?.visualType === PetalSlotVisualType.Angle &&
      Number.isFinite(copy?.visual)
    ) {
      const targetAngle = livePetalRenderAngle(petalType, copy.visual);
      const previousAngle = Number(icon.dataset.liveRenderAngle);
      if (Number.isFinite(previousAngle)) {
        let delta = normalizeAngle(targetAngle) - normalizeAngle(previousAngle);
        if (delta > Math.PI) delta -= Math.PI * 2;
        else if (delta < -Math.PI) delta += Math.PI * 2;
        angle = previousAngle + delta;
      } else {
        angle = targetAngle;
      }
      icon.dataset.liveRenderAngle = String(angle);
    } else if (
      live && runtime?.visualType === PetalSlotVisualType.Size &&
      Number.isFinite(copy?.visual)
    ) {
      scale = clamp(copy.visual, 0.05, 1);
      delete icon.dataset.liveRenderAngle;
    } else {
      delete icon.dataset.liveRenderAngle;
    }

    icon.style.setProperty("--live-angle", `${angle}rad`);
    icon.style.setProperty("--live-scale", String(scale));
  }
}

export function renderCraftPanel() {
  renderCraftMode();
  hidePetalInfoTooltip();
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

export function renderTalentPanel() {
  if (!talentTree) return;
  if (!talentTreeUi) talentTreeUi = createTalentTree({
    panel: talentPanel, viewport: talentTree, points: talentPointsLabel,
    getState: () => ({ ...state, talentReadOnly: !featureAllowed('talent', FeatureAccess.Edit) }),
    request: (action, nodes) => {
      if (!featureAllowed('talent', FeatureAccess.Edit)) return false;
      const packet = packTalentRequest(action, nodes);
      return !!packet && sendBytes(packet);
    },
  });
  talentTreeUi.render();
}

function selectCraftPetal(petalType, rarity, options = {}) {
  if (state.craftPhase === "spinning" || craftResultDismissing) return;
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

function renderCraftMatrix() {
  if (!craftMatrixUi) craftMatrixUi = createCraftMatrix({
    root: craftList, isOpen: () => state.craftOpen,
    createStack: item => item.forge ? makeForgePetalStack(item.petalType, item.count)
      : makePetalStack(item.petalType, item.rarity, item.count, item.petalType, { showCount: true }),
    visualKey: item => `${item.forge}:${item.name}:${effectivePetalCardCopy(item.petalType, item.rarity)}`,
    describe: item => formatPetalItem(item.petalType, item.rarity, item.count),
    formatCount: item => item.forge ? `${formatShortCount(item.count)}/5` : `x${formatShortCount(item.count)}`,
    attachTooltip: attachPetalInfoTooltip, clearTooltip: clearPetalInfoTooltip,
    vanish: animatePetalCardVanish, select: selectCraftPetal,
    onLongPress: item => selectCraftPetal(item.petalType, item.rarity, { all: true }),
  });
  if (state.forgeMode) {
    renderForgeMatrix();
    return;
  }

  const items = [];
  const held = new Map();
  if (state.craftPhase === "staged" || state.craftPhase === "spinning") {
    for (const slot of state.craftSlots) if (slotHasItem(slot)) {
      const key = `${slot.petalType}:${slot.rarity}`;
      held.set(key, (held.get(key) || 0) + Math.max(1, slot.count || 1));
    }
  }
  // 一次读取乐观背包，避免每个稀有度格重复复制和搜索。
  for (const row of buildCraftRows()) {
    const name = getPetalDisplayName(row.petalType);
    for (const rarity of craftRarityDisplayOrder) {
      const key = `${row.petalType}:${rarity}`;
      const baseCount = row.counts.get(rarity) || 0;
      items.push({ key, petalType: row.petalType, rarity, baseCount, name, forge: false,
        count: Math.max(0, baseCount - (held.get(key) || 0)),
        lit: state.craftPhase !== "spinning" && canCraftRarity(rarity) && baseCount >= 5 });
    }
  }
  craftMatrixUi.render(items, { columns: craftRarityDisplayOrder.length });
}

function renderForgeMatrix() {
  const counts = new Map();
  for (const item of displayInventory()) {
    if (!slotHasItem(item) || item.rarity !== raritySuper || item.count <= 0) continue;
    counts.set(item.petalType, (counts.get(item.petalType) || 0) + Math.max(0, item.count || 0));
  }
  const items = Array.from(counts, ([petalType, count]) => ({
    key: `forge:${petalType}`, petalType, rarity: raritySuper, baseCount: count, count, forge: true,
    name: getPetalDisplayName(petalType), lit: state.craftPhase !== "spinning" && count >= 5,
  })).sort((a, b) => compareLocalized(petalTypeName(a.petalType), petalTypeName(b.petalType)) || a.petalType - b.petalType);
  craftMatrixUi.render(items, { forge: true });
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
  badge.hidden = count <= 1;
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

function canCraftRarity(rarity) {
  return rarity >= 1 && rarity <= 9;
}

function renderCraftStage() {
  const keyRoot = state.forgeMode ? "ui.forge" : "ui.craft";
  craftStage.classList.toggle("spinning", state.craftPhase === "spinning");
  craftStage.classList.toggle("success", state.craftPhase === "success");

  // 结果展示期间保留五个环槽，卡片汇聚/消失时底板已经可见。
  const showRing = true;
  const spinning = state.craftPhase === "spinning";
  const orbitActive = craftSpin.isActive();
  craftStage.querySelectorAll("[data-craft-slot]").forEach((button) => {
    const index = Number(button.dataset.craftSlot || 0);
    const slot = state.craftSlots[index];

    if (spinning && orbitActive) {
      // 动画期间轨道控制器独占卡片节点，背包刷新不能中途重建。
      clearPetalInfoTooltip(button);
      button.disabled = true;
      return;
    }

    setAnimatedVisibility(button, showRing, {
      duration: craftSlotMotionDurationMs,
    });

    if (showRing) {
      const itemKey = slotHasItem(slot) ? `${slot.petalType}:${slot.rarity}` : "empty";
      const sameItem = button.dataset.craftItemKey === itemKey;
      button.dataset.craftItemKey = itemKey;
      const key = slotHasItem(slot)
        ? `${slot.petalType}:${slot.rarity}:${slot.count || 1}:${petalTypeName(slot.petalType)}` : "empty";
      const changed = button.dataset.craftRenderKey !== key;
      if (changed) {
        button.replaceChildren();
        clearPetalInfoTooltip(button);
        button.dataset.craftRenderKey = key;
      }
      const hasSlot = slotHasItem(slot);
      button.classList.toggle("filled", hasSlot);
      button.classList.toggle("empty", !hasSlot);
      // 淡出可能已显示空底板，此时由空槽状态接管外观。
      button.classList.remove("craft-card-fading");
      button.disabled = spinning;
      button.title = slotHasItem(slot)
        ? formatPetalItem(
            slot.petalType,
            slot.rarity,
            slot.count || 1,
          )
        : t(`${keyRoot}.slot`);
      if (slotHasItem(slot) && changed) {
        const stack = makePetalStack(slot.petalType, slot.rarity, slot.count || 1);
        if (!spinning && state.craftPhase === "staged" && !sameItem) stack.classList.add("craft-card-entering");
        button.appendChild(stack);
        attachPetalInfoTooltip(button, slot.petalType, slot.rarity);
      }
    } else {
      // 退出过渡完成前保留原节点，避免先清空再淡出。
      clearPetalInfoTooltip(button);
      button.disabled = true;
    }
    button.onclick = (event) => {
      event.preventDefault();
      if (state.craftPhase === "success" && !slotHasItem(slot)) return;
      clearCraftDisplay();
      renderCraftPanel();
    };
  });

  const showResult =
    state.craftPhase === "success" && slotHasItem(state.craftResult);
  if (craftResultDismissing) {
    // 结果卡原地消失时不重建，避免重渲染使动画复活。
  } else {
    setAnimatedVisibility(craftResultSlot, showResult, {
      duration: craftSlotMotionDurationMs,
    });
    if (showResult) {
      craftResultSlot.replaceChildren();
      clearPetalInfoTooltip(craftResultSlot);
      craftResultSlot.disabled = false;
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
      // 每个结果只播放一次出现动画。
      const popKey = `${state.craftResult.petalType}:${state.craftResult.rarity}`;
      if (craftResultSlot.dataset.popKey !== popKey) {
        craftResultSlot.dataset.popKey = popKey;
        craftResultSlot.classList.remove("craft-result-pop");
        void craftResultSlot.offsetWidth;
        craftResultSlot.classList.add("craft-result-pop");
      }
    } else {
      // 淡出完成前保留结果节点。
      clearPetalInfoTooltip(craftResultSlot);
      craftResultSlot.disabled = true;
      craftResultSlot.classList.remove("craft-result-pop");
      delete craftResultSlot.dataset.popKey;
    }
  }
  craftResultSlot.onclick = (event) => {
    event.preventDefault();
    if (
      state.craftPhase === "success" &&
      slotHasItem(state.craftResult) &&
      !craftResultDismissing
    ) {
      dismissCraftResult();
      return;
    }
    if (craftResultDismissing) return;
    clearCraftDisplay();
    renderCraftPanel();
  };

  const stagedCount = state.craftSource
    ? getCraftSlotCount(state.craftSource.petalType, state.craftSource.rarity)
    : 0;
  craftOnceBtn.disabled = !(
    featureAllowed('craft', state.forgeMode ? FeatureAccess.Forge : FeatureAccess.Edit) &&
    state.authenticated &&
    state.craftPhase === "staged" &&
    (state.forgeMode ? stagedCount === 5 : stagedCount >= 5)
  );
  updateCraftButton(craftOnceBtn, state.craftSource, state.forgeMode);
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
  if (!featureAllowed('craft', state.forgeMode ? FeatureAccess.Forge : FeatureAccess.Edit)) return;
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
    if (!economyRequests.submit({ operation: EconomyOperation.Forge, petalType, rarity, count })) return;
    state.craftPhase = "spinning";
    state.craftSpinStarted = performance.now();
    renderCraftPanel();
    beginCraftSpinMotion();
    return;
  }

  if (!canCraftRarity(rarity)) return;
  if (count < 5 || count > getInventoryCount(petalType, rarity)) return;
  if (!economyRequests.submit({ operation: EconomyOperation.Craft, petalType, rarity, count })) return;
  state.craftPhase = "spinning";
  state.craftSpinStarted = performance.now();
  renderCraftPanel();
  beginCraftSpinMotion();
}

// 超时检测将无服务器结果的旋转恢复为待合成状态。
function beginCraftSpinMotion() {
  craftVisualGeneration += 1;
  craftSpin.begin({
    slotRarities: state.craftSlots.map((slot) =>
      slotHasItem(slot) ? slot.rarity : 1,
    ),
    onWatchdogTimeout: () => {
      if (state.craftPhase !== "spinning") return;
      addConsoleLine(t("status.economyPending"));
      renderCraftPanel();
    },
  });
}

function handleCraftResult(msg) {
  if (state.craftPhase !== "spinning") return;
  if (state.craftSource && (msg.petalType !== state.craftSource.petalType || msg.rarity !== state.craftSource.rarity)) return;
  if (!msg.success && msg.consumed === 0) {
    craftSpin.cancel();
    state.craftPhase = "idle";
    clearCraftDisplay();
    renderCraftPanel();
    addConsoleLine(t("status.craftRejected"), "error");
    return;
  }
  const generation = craftVisualGeneration;
  // 只计算一次结果，确保动画与最终槽位数量一致。
  const items = compactCraftItems(msg.items || []);
  const nextSlots = msg.success
    ? Array.from({ length: 5 }, () => null)
    : placeCraftReturns(state.craftSlots, items);
  const resultItem = msg.success
    ? pickCraftSuccessItem(items, msg) || items[0] || null
    : null;

  const finish = () => {
    if (generation !== craftVisualGeneration || state.craftPhase !== "spinning") return;
    if (msg.success) {
      // 旧卡片已被汇聚隐藏，即使配方相同也要新建可见卡片。
      for (const button of craftStage.querySelectorAll("[data-craft-slot]"))
      {
        delete button.dataset.craftRenderKey;
        delete button.dataset.craftItemKey;
      }
      state.craftPhase = "success";
      state.craftSlots = nextSlots;
      state.craftResult = resultItem;
    } else {
      state.craftPhase = "returned";
      state.craftResult = null;
      state.craftSlots = nextSlots;
    }
    state.craftSource = null;
    renderCraftPanel();
  };

  if (craftSpin.isActive()) {
    craftSpin.resolve({
      success: !!msg.success,
      resultRarity: resultItem?.rarity ?? (msg.rarity || 0) + 1,
      slotWillBeEmpty: nextSlots.map((slot) => !slotHasItem(slot)),
      slotLostCount: state.craftSlots.map((slot, index) =>
        Math.max(0, (slot?.count || 0) - (nextSlots[index]?.count || 0))),
      onFinished: finish,
    });
    return;
  }

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
  craftVisualGeneration += 1;
  craftResultDismissing = false;
  state.craftSlots = Array.from({ length: 5 }, () => null);
  state.craftSource = null;
  state.craftResult = null;
  state.craftPhase = "idle";
}

// 点击结果只播放消失动画，物品已在背包；随后空环槽重新出现。
function dismissCraftResult() {
  const generation = craftVisualGeneration;
  craftResultDismissing = true;
  const stack = craftResultSlot.querySelector(".petal-stack");
  clearPetalInfoTooltip(craftResultSlot);
  craftResultSlot.disabled = true;
  craftResultSlot.classList.remove("craft-result-pop");
  const finishDismiss = () => {
    if (generation !== craftVisualGeneration) return;
    craftResultDismissing = false;
    clearCraftDisplay();
    renderCraftPanel();
  };
  if (stack)
    animatePetalCardVanish(stack, { durationMs: 280, onFinish: finishDismiss });
  else finishDismiss();
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
    button.style.setProperty("--slot-rarity-color", getSlotCounterColor(slot.rarity));
    const visualPetalType = slotVisualPetalType(
      slot,
      index,
      kind,
      primaryForVisual,
    );
    button.appendChild(
      makePetalStack(
        slot.petalType,
        slot.rarity,
        0,
        visualPetalType,
        {
          kind,
          slotIndex: index,
          liveVisual:
            kind === "primary" &&
            (visualPetalType === petalCompassType ||
              visualPetalType === petalTomatoType),
        },
      ),
    );
    attachPetalInfoTooltip(button, slot.petalType, slot.rarity);
    button.addEventListener("pointerdown", (event) =>
      beginDrag(event, button, kind, index, slot),
    );
    bindTouchLongPress(button, () => {
      if (loadoutEditingBlocked()) return;
      hidePetalInfoTooltip();
      ui.mobileControls?.showSlotActions({
        title: formatPetalItem(slot.petalType, slot.rarity),
        onSwap: () => quickSwapSlot(index),
        onRemove: () => kind === "primary" ? unequipSlot(index) : clearSecondarySlot(index),
      });
    });
  }

  if (kind === "primary") {
    button.addEventListener("click", () => {
      if (shouldSuppressClick()) return;
      state.selectedSlot = index;
      quickSwapSlot(index);
    });
    button.addEventListener("contextmenu", (event) => {
      event.preventDefault();
      if (ui.mobileControls?.isMobileMode()) return;
      unequipSlot(index);
    });
  } else {
    button.addEventListener("click", () => {
      if (shouldSuppressClick()) return;
      quickSwapSlot(index);
    });
    button.addEventListener("contextmenu", (event) => {
      event.preventDefault();
      if (ui.mobileControls?.isMobileMode()) return;
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

let criticalAssetsPromise = null;
let assetsPreloadPromise = null;
let backgroundAssetsScheduled = false;

function slotsHavePetal(slots, petalType, rarity = null) {
  return (slots || []).some(
    (slot) =>
      slotHasItem(slot) &&
      slot.petalType === petalType &&
      (rarity === null || slot.rarity === rarity),
  );
}

function hasOwnerPetal(petalType, rarity = null) {
  return slotsHavePetal(displayOwnerSlots(), petalType, rarity);
}

function equipFromInventory(item) {
  if (loadoutEditingBlocked()) return;
  if (!state.authenticated || !slotHasItem(item)) return;
  const primary = displayOwnerSlots();
  for (let i = 0; i < primary.length; i += 1) {
    if (!slotHasItem(primary[i])) {
      equipFromInventoryAt(i, item);
      return;
    }
  }
  const secondary = displaySecondarySlots();
  for (let i = 0; i < secondary.length; i += 1) {
    if (!slotHasItem(secondary[i])) {
      setSecondaryFromInventory(i, item);
      return;
    }
  }
}

function equipFromInventoryAt(index, item) {
  if (loadoutEditingBlocked()) return;
  if (!state.authenticated || !slotHasItem(item)) return;
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  let inventory = displayInventory();
  if (index < 0 || index >= ownerSlots.length) return;
  if (getDisplayInventoryCount(item.petalType, item.rarity, inventory) <= 0)
    return;
  const oldSlot = copySlot(ownerSlots[index]);
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
  if (loadoutEditingBlocked()) return;
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
  if (loadoutEditingBlocked()) return;
  if (!state.authenticated || !slotHasItem(item)) return;
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  let inventory = displayInventory();
  if (index < 0 || index >= ownerSlots.length) return;
  if (getDisplayInventoryCount(item.petalType, item.rarity, inventory) <= 0)
    return;
  const oldSlot = copySlot(secondarySlots[index]);
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
  if (loadoutEditingBlocked()) return;
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

function animateSlotSwapFlight(index) {
  return slotSwapMotion.swap(index, primarySlots.children[index], secondarySlots.children[index]);
}

function animateAllSlotSwapFlight(slotCount) {
  for (let index = 0; index < slotCount; index += 1) animateSlotSwapFlight(index);
}

function quickSwapSlot(index) {
  if (loadoutEditingBlocked() || !state.authenticated) return;
  const display = makeDisplaySlotState();
  if (index < 0 || index >= display.ownerSlots.length) return;
  const primary = copySlot(display.ownerSlots[index]);
  const secondary = copySlot(display.secondarySlots[index]);
  if (!slotHasItem(primary) && !slotHasItem(secondary)) return;
  display.ownerSlots[index] = secondary;
  display.secondarySlots[index] = primary;
  state.selectedSlot = index;
  if (!enqueueSlotTransaction(display.ownerSlots, display.secondarySlots, display.inventory, [primary, secondary], { keepSwapMotion: true })) return;
  animateSlotSwapFlight(index);
  scheduleInventoryUiRender();
}

function quickSwapAllSlots() {
  if (loadoutEditingBlocked()) return;
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

  // 先提交乐观数据，待全部飞行落位后再刷新槽位显示。
  if (!enqueueSlotTransaction(
    desiredOwnerSlots,
    desiredSecondarySlots,
    display.inventory,
    [...display.ownerSlots, ...display.secondarySlots],
    { keepSwapMotion: true },
  )) return;
  animateAllSlotSwapFlight(slotCount);
  scheduleInventoryUiRender();
}

function saveActiveLoadoutPreset() {
  if (loadoutEditingBlocked()) return false;
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
  if (loadoutEditingBlocked()) return false;
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

  if (!enqueueSlotTransaction(
    desiredOwnerSlots,
    desiredSecondarySlots,
    inventory,
    touchedItems,
  )) return false;
  renderInventoryPanel();
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
  if (loadoutEditingBlocked()) return;
  if (fromIndex === toIndex) return;
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  const inventory = displayInventory();
  if (!state.authenticated || !slotHasItem(ownerSlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= ownerSlots.length) return;

  const source = copySlot(ownerSlots[fromIndex]);
  const target = copySlot(ownerSlots[toIndex]);

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
  if (loadoutEditingBlocked()) return;
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  const inventory = displayInventory();
  if (!state.authenticated || !slotHasItem(ownerSlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= ownerSlots.length) return;

  const source = copySlot(ownerSlots[fromIndex]);
  const oldSecondary = copySlot(secondarySlots[toIndex]);

  ownerSlots[fromIndex] = oldSecondary;
  secondarySlots[toIndex] = source;
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [
    source,
    oldSecondary,
  ]);
  renderInventoryPanel();
}

function moveSecondaryToPrimary(fromIndex, toIndex) {
  if (loadoutEditingBlocked()) return;
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  const inventory = displayInventory();
  if (!state.authenticated || !slotHasItem(secondarySlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= ownerSlots.length) return;

  const source = copySlot(secondarySlots[fromIndex]);
  const oldPrimary = copySlot(ownerSlots[toIndex]);

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
  if (loadoutEditingBlocked()) return;
  if (fromIndex === toIndex) return;
  const ownerSlots = displayOwnerSlots();
  const secondarySlots = displaySecondarySlots();
  const inventory = displayInventory();
  if (!state.authenticated || !slotHasItem(secondarySlots[fromIndex])) return;
  if (toIndex < 0 || toIndex >= ownerSlots.length) return;

  const source = copySlot(secondarySlots[fromIndex]);
  const target = copySlot(secondarySlots[toIndex]);

  secondarySlots[fromIndex] = target;
  secondarySlots[toIndex] = source;
  enqueueSlotTransaction(ownerSlots, secondarySlots, inventory, [
    source,
    target,
  ]);
  renderInventoryPanel();
}

function beginDrag(event, element, source, index, item) {
  // 触屏短点装备、移动滚动、长按才拖拽；桌面立即开始拖拽。
  if (event.pointerType === "touch" && source !== "inventory") return;
  if (state.drag) return;
  if (loadoutEditingBlocked()) return;
  if (event.button !== 0 || !slotHasItem(item)) return;
  if (!state.authenticated && source !== "inventory") return;
  // 记录拖前面板状态，普通点击不收起背包。
  state.drag = {
    pointerId: event.pointerId,
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
    backpackWasOpen: state.backpackOpen,
    touchPending: event.pointerType === "touch" && source === "inventory",
    touchTimer: 0,
  };
  if (state.drag.touchPending) {
    const drag = state.drag;
    drag.touchTimer = window.setTimeout(() => {
      if (state.drag === drag && !drag.active) activateDrag(drag);
    }, 180);
  }
}

function activateDrag(drag) {
  if (!drag || drag.active) return;
  drag.active = true;
  drag.element.classList.add("dragging");
  drag.ghost = makeDragGhost(drag.item);
  document.body.appendChild(drag.ghost);
  drag.element.setPointerCapture?.(drag.pointerId);
  // 真正拖动后才收起背包以显示目标，结束时无论落点是否有效都恢复。
  if (drag.backpackWasOpen && state.backpackOpen) toggleBackpack(false);
}

function updateDrag(event) {
  const drag = state.drag;
  if (!drag || event.pointerId !== drag.pointerId) return;

  drag.x = event.clientX;
  drag.y = event.clientY;
  const moved = Math.hypot(drag.x - drag.startX, drag.y - drag.startY);
  if (drag.touchPending && !drag.active) {
    // 手指移动优先交给原生纵向滚动，清理拖拽时取消长按计时。
    if (moved >= 10) clearDrag();
    return;
  }
  if (!drag.active && moved >= 5) {
    activateDrag(drag);
  }

  if (!drag.active) return;
  event.preventDefault();
  positionDragGhost(drag);
  highlightDropTarget(dropTargetFromPoint(drag.x, drag.y));
}

function finishDrag(event) {
  const drag = state.drag;
  if (!drag || (event && event.pointerId !== drag.pointerId)) return;

  if (drag.active) {
    state.suppressClickUntil = performance.now() + 240;
    const target = dropTargetFromPoint(drag.x, drag.y);
    if (target) {
      performDrop(target, drag);
    } else if (drag.source === "primary") {
      unequipSlot(drag.index);
    } else if (drag.source === "secondary") {
      clearSecondarySlot(drag.index);
    }
  }

  clearDrag();
  if (drag.backpackWasOpen) {
    drag.backpackWasOpen = false;
    if (!state.backpackOpen) toggleBackpack(true);
  }
}

function clearDrag() {
  const drag = state.drag;
  if (!drag) return;
  if (drag.touchTimer) window.clearTimeout(drag.touchTimer);
  drag.touchTimer = 0;
  drag.element.classList.remove("dragging");
  if (drag.ghost) drag.ghost.remove();
  highlightDropTarget(null);
  state.drag = null;
}

function makeDragGhost(item) {
  const ghost = document.createElement("div");
  ghost.className = "drag-ghost";
  const stack = makePetalStack(item.petalType, item.rarity, 1);
  ghost.appendChild(stack);
  return ghost;
}

function positionDragGhost(drag) {
  if (!drag.ghost) return;
  const baseSize = 58;
  const target = drag.targetElement;
  const size = target
    ? Math.max(30, Math.min(72, target.clientWidth || baseSize))
    : Math.round(baseSize * 1.1);
  drag.ghost.style.width = `${size}px`;
  drag.ghost.style.height = `${size}px`;
  drag.ghost.style.transform = `translate(${drag.x + 8}px, ${drag.y + 8}px)`;
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
  if (loadoutEditingBlocked()) return;
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

function hurtFlashAmount(entity) {
  const age = Number.isFinite(entity?.hurtFlashAge)
    ? entity.hurtFlashAge
    : hurtFlashDuration;
  if (age >= hurtFlashDuration) return 0;
  const progress = clamp(age / hurtFlashDuration, 0, 1);
  return (1 - progress) * (1 - progress);
}

// 普通生物离场使用粒子，掉落保留卡片消失，玩家尸体使用独立惯性表现。
function drawMobDeathEffect(pos, radius, progress, rarity) {
  const eased = progress * progress * (3 - progress * 2);
  const alpha = 1 - eased;
  ctx.save();
  ctx.filter = "none";
  ctx.globalCompositeOperation = "lighter";
  ctx.globalAlpha *= alpha;
  ctx.strokeStyle = rarityColor(rarity || 1, alpha * 0.26);
  ctx.lineWidth = Math.max(1.2, radius * (0.13 - eased * 0.07));
  ctx.beginPath();
  ctx.arc(pos.x, pos.y, radius * (0.72 + eased * 1.36), 0, Math.PI * 2);
  ctx.stroke();
  ctx.fillStyle = rarityColor(rarity || 1, alpha * 0.42);
  for (let i = 0; i < 7; i++) {
    const angle = i / 7 * Math.PI * 2 + progress * 0.9;
    const spread = radius * (0.34 + eased * 1.12);
    const size = Math.max(1.1, radius * (0.08 + (1 - progress) * 0.05));
    ctx.beginPath();
    ctx.arc(pos.x + Math.cos(angle) * spread, pos.y + Math.sin(angle) * spread, size, 0, Math.PI * 2);
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

function emitTerrainDust(center, tileSize, count = 14) {
  if (
    !center ||
    !Number.isFinite(center.x) ||
    !Number.isFinite(center.y)
  )
    return;
  const edge = Math.max(0.5, (Number(tileSize) || 512) * 0.5);
  const particleCount = Math.max(1, Math.floor(count));
  for (let i = 0; i < particleCount; i += 1) {
    const angle = Math.random() * Math.PI * 2;
    const speed = edge * (3.2 + Math.random() * 4.5);
    state.particles.push({
      pos: {
        x: center.x + Math.cos(angle) * edge,
        y: center.y + Math.sin(angle) * edge,
      },
      velocity: {
        x: Math.cos(angle) * speed,
        y: Math.sin(angle) * speed,
      },
      color: "#6b4a2b",
      compositeOperation: "source-over",
      age: 0,
      lifetime: 0.12 + Math.random() * 0.13,
      size: 6 + Math.random() * 9,
      alphaScale: 0.75 + Math.random() * 0.25,
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
      const last = state.particles.pop();
      if (i < state.particles.length) state.particles[i] = last;
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

// 受击色缓存按身体深度立即绘制，避免全场叠加穿透前景。
function drawHurtFlashPass() {
  if (!currentHurtFlashSprites.length) return;
  setSpriteHitTint(true);
  for (const entry of currentHurtFlashSprites) {
    ctx.save();
    ctx.globalAlpha = clamp(entry.flash, 0, 1);
    drawEntity(entry.entity, { visualOnly: true });
    ctx.restore();
  }
  setSpriteHitTint(false);
  currentHurtFlashSprites.length = 0;
}

function drawHurtFlashOverlays() {
  if (!currentHurtFlashOverlays.length) return;
  ctx.save();
  ctx.filter = "none";
  ctx.globalCompositeOperation = "source-over";
  ctx.fillStyle = "#ff7954";
  for (const overlay of currentHurtFlashOverlays) {
    ctx.globalAlpha = overlay.a * 0.9;
    ctx.beginPath();
    ctx.arc(overlay.x, overlay.y, overlay.r, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
  currentHurtFlashOverlays.length = 0;
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
    if (getPetalBaseCopy(visualPetalType, slot.rarity) !== 0) continue;
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
  visualOnly = false,
) {
  const phase = bloodSacrificePhase(progress);
  if (phase.alpha <= 0) return;

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

  if (visualOnly) return;
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

function frameEntityAppearance(entity) {
  if (entity.renderAppearanceFrame !== currentRenderFrameId) {
    entity.renderAppearanceFrame = currentRenderFrameId;
    entity.renderAppearance = getEntityAppearance(entity);
  }
  return entity.renderAppearance;
}

function getFlowerMobVisualScale(entityType) {
  if (entityType === titanType)
    return getClientConfigValue(state.clientConfig, "titan_sprite_scale");
  // 木桩显示轮廓即碰撞轮廓，不使用旧装饰缩放覆盖权威半径。
  if (entityType === dummyType) return 1;
  return 1;
}

function getFlowerMobCullScale(entityType) {
  if (entityType !== titanType && entityType !== dummyType) return 1;
  return Math.max(1, getFlowerMobVisualScale(entityType) * 1.35);
}

function activeOwnerHallucinationEntity() {
  const owner = state.entities.get(state.ownerEntityId);
  const snap = owner?.snapshot;
  if (
    !owner ||
    !snap ||
    !owner.renderPos ||
    owner.dying ||
    (snap.flags & flagDead) !== 0 ||
    !entityHasState(snap, stateHallucinogenic) ||
    !Array.isArray(snap.hallucinations) ||
    snap.hallucinations.length === 0
  )
    return null;
  return owner;
}

function ownerHallucinationGhostOffset(owner) {
  const snap = owner?.snapshot;
  if (!snap) return null;

  let direction = state.localMoveInput || { x: 0, y: 0 };
  let directionLength = Math.hypot(direction.x || 0, direction.y || 0);
  let directionAlreadyTransformed = false;
  if (directionLength <= 0.0001) {
    direction = owner.snapshotVelocity || {
      x: 0,
      y: 0,
    };
    directionLength = Math.hypot(direction.x || 0, direction.y || 0);
    directionAlreadyTransformed = directionLength > 0.0001;
  }
  if (directionLength <= 0.0001) {
    const angle = Number.isFinite(owner.renderAngle)
      ? owner.renderAngle
      : Number(snap.angle) || 0;
    direction = { x: Math.cos(angle), y: Math.sin(angle) };
    directionLength = 1;
    directionAlreadyTransformed = true;
  }

  const distance = Math.max(1, Number(snap.radius) || 0) * 3;
  let transformed = {
    x: (direction.x / directionLength) * distance,
    y: (direction.y / directionLength) * distance,
  };
  if (!directionAlreadyTransformed)
    transformed = transformOwnerHallucinationVector(snap, transformed);
  return transformed;
}

function entityHallucinationGhostPositions(entity, hallucinationOwner) {
  const snap = entity?.snapshot;
  const actual = entity?.renderPos;
  const ownerSnap = hallucinationOwner?.snapshot;
  const ownerPos = hallucinationOwner?.renderPos;
  if (
    !snap ||
    !actual ||
    !ownerSnap ||
    !ownerPos
  )
    return [];

  let ghost = null;
  if (snap.entityId === ownerSnap.entityId) {
    const offset = ownerHallucinationGhostOffset(hallucinationOwner);
    if (!offset) return [];
    ghost = { x: actual.x + offset.x, y: actual.y + offset.y };
  } else {
    const relative = { x: actual.x - ownerPos.x, y: actual.y - ownerPos.y };
    const transformed = transformOwnerHallucinationVector(ownerSnap, relative);
    ghost = { x: ownerPos.x + transformed.x, y: ownerPos.y + transformed.y };
  }

  return [
    { x: (actual.x + ghost.x) * 0.5, y: (actual.y + ghost.y) * 0.5 },
    ghost,
  ];
}

function drawEntityHallucinationGhosts(entity, hallucinationOwner) {
  const positions = entityHallucinationGhostPositions(
    entity,
    hallucinationOwner,
  );
  if (positions.length === 0) return;

  for (let i = positions.length - 1; i >= 0; i -= 1)
    drawEntity(entity, {
      renderPos: positions[i],
      alpha: 0.5,
      visualOnly: true,
    });
}

function drawEntity(entity, options = {}) {
  const visualOnly = options.visualOnly === true;
  if (!visualOnly) {
    drawEntityVisual(entity, options);
    // 在身体所属深度绘制受击色，避免覆盖前景花朵和花瓣。
    if (currentHurtFlashSprites.length) drawHurtFlashPass();
    if (currentHurtFlashOverlays.length) drawHurtFlashOverlays();
    return;
  }

  entityVisualOnlyRenderDepth += 1;
  try {
    drawEntityVisual(entity, options);
  } finally {
    entityVisualOnlyRenderDepth -= 1;
  }
}

function drawEntityVisual(entity, options = {}) {
  const snap = entity.snapshot;
  const pos = options.screenPosition || worldToScreen(options.renderPos || entity.renderPos);
  const radius = Math.max(0, Number.isFinite(options.screenRadius) ? options.screenRadius : worldLengthToScreen(snap.radius));
  const isOwner =
    (snap.flags & flagOwner) !== 0 || snap.entityId === state.ownerEntityId;
  const isPetal = isPetalEntity(snap.entityType);
  const isDrop = isDropEntity(snap.entityType);
  const dead = (snap.flags & flagDead) !== 0;
  const appearance = frameEntityAppearance(entity);
  const deathScale = appearance.scale;
  const deathAlpha = appearance.alpha;
  const animationTime = entity.dying && Number.isFinite(entity.retirementTime)
    ? entity.retirementTime : currentRenderTimeSeconds;
  const hurtFlash = entity.dying ? 0 : hurtFlashAmount(entity);
  const spriteScale = getFlowerMobVisualScale(snap.entityType);

  if (!options.visualOnly) {
    if (isPetal) emitPetalParticleBurst(entity, snap);
    else if (snap.entityType === playerFlowerType)
      emitZeroCopyFlowerParticleBurst(entity, snap);
  }

  ctx.save();
  const requestedAlpha = Number(options.alpha);
  const renderAlpha = Number.isFinite(requestedAlpha)
    ? clamp(requestedAlpha, 0, 1)
    : 1;
  ctx.globalAlpha *= deathAlpha * renderAlpha;

  if (snap.entityType === chainEntityType) {
    const chain = parseChainSnapshot(snap.name);
    const first = chain ? state.entities.get(chain.aId) : null;
    const second = chain ? state.entities.get(chain.bId) : null;
    if (chain && first?.snapshot?.entityGeneration === chain.aGenerationKey &&
        second?.snapshot?.entityGeneration === chain.bGenerationKey) {
      drawChain(
        ctx,
        { pos: worldToScreen(first.renderPos || first.snapshot.pos) },
        { pos: worldToScreen(second.renderPos || second.snapshot.pos) },
        chain.tension,
      );
    }
    ctx.restore();
    return;
  }

  const visibleRadius = radius * spriteScale * deathScale;
  const nativeFlower = snap.entityType === playerFlowerType || snap.entityType === titanType || snap.entityType === dummyType;
  if (!options.visualOnly && hurtFlash > 0 && !nativeFlower) {
    if (snap.entityType === bloodSacrificeEffectType) {
      currentHurtFlashOverlays.push({
        x: pos.x,
        y: pos.y,
        r: Math.max(2, visibleRadius),
        a: hurtFlash,
      });
    } else {
      currentHurtFlashSprites.push({ entity, flash: hurtFlash });
    }
  }

  if (!isPetal && !isDrop && snap.entityType === bloodSacrificeEffectType) {
    drawBloodSacrificeEffect(
      pos,
      visibleRadius,
      snap.hpPercent,
      entity.bloodSacrificeAngle ?? Math.random() * Math.PI * 2,
      entity,
      options.visualOnly === true,
    );
    ctx.restore();
    return;
  }

  if (!options.visualOnly && entity.dying && entity.visualMotion?.mob) {
    const progress = clamp(entity.visualMotion.age / entity.visualMotion.duration, 0, 1);
    drawMobDeathEffect(pos, visibleRadius, progress, snap.rarity);
  }

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
      animationTime,
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

  if (!isPetal && snap.entityType === bubbleType) {
    drawBubble(ctx, pos, radius * deathScale, snap.entityId);
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
      animationTime,
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
      animationTime,
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
      animationTime,
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
      animationTime,
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
      animationTime,
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
      animationTime,
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
      animationTime,
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
      animationTime,
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
      animationTime,
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
      animationTime,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === crabType) {
    drawCrab(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      animationTime,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === jellyfishType) {
    drawJellyfish(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      animationTime,
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
      animationTime,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (
    !isPetal &&
    (snap.entityType === fireflyType || snap.entityType === magicFireflyType)
  ) {
    drawFirefly(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      animationTime,
      snap.entityType === magicFireflyType,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === fireflyLarvaType) {
    const visualVariant = entityVisualVariantFromFlags(snap.flags);
    drawFireflyLarva(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      animationTime,
      visualVariant,
      visualVariant === FireflyLarvaVisualRole.Head && (snap.flags & flagAttacking) !== 0,
    );
    if (!entity.dying && visualVariant === FireflyLarvaVisualRole.Head)
      drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && !isDrop && snap.entityType === portalType) {
    if (!options.visualOnly) emitPortalParticleBurst(entity, snap);
    drawPortal(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      animationTime,
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
        animationTime,
      );
    } else if (snap.entityType === soldierTermiteType) {
      drawSoldierTermite(
        ctx,
        pos,
        radius * deathScale,
        snap.entityId,
        entity.renderAngle ?? snap.angle,
        entity.motionBlend || 0,
        animationTime,
      );
    } else if (snap.entityType === leafcutterSoldierType) {
      drawLeafcutterSoldier(
        ctx,
        pos,
        radius * deathScale,
        snap.entityId,
        entity.renderAngle ?? snap.angle,
        entity.motionBlend || 0,
        animationTime,
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
        animationTime,
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
      animationTime,
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
      animationTime,
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
      animationTime,
    );
    if (!entity.dying) drawMobFrame(snap, pos, radius);
    ctx.restore();
    return;
  }

  if (!isPetal && snap.entityType === hornetType) {
    const windupId = skillWindupIdFromFlags(snap.flags);
    const antennaWobble =
      windupId === 2
        ? Math.sin((animationTime || 0) * Math.PI * 4) * 0.34
        : 0;
    drawHornet(
      ctx,
      pos,
      radius * deathScale,
      snap.entityId,
      entity.renderAngle ?? snap.angle,
      entity.motionBlend || 0,
      animationTime,
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
      animationTime,
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
      animationTime,
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
      animationTime,
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
    drawTitanFlower(
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
    const idleTilt = getDropCardTilt(snap.entityId, animationTime);
    ctx.translate(pos.x, pos.y);
    ctx.rotate(idleTilt + appearance.rotation);
    ctx.translate(-pos.x, -pos.y);
    drawDropPetalCard(
      petalTypeFromSnap(snap),
      snap.rarity,
      pos,
      radius * deathScale,
      dropStackCountFromSnap(snap),
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
  else if (isPetal || isDrop) ctx.strokeStyle = "#4f6680";
  else ctx.strokeStyle = "rgba(48, 62, 82, 0.52)";
  ctx.stroke();

  ctx.restore();
}

// 闪电接收时一次生成稳定世界坐标折线，按透明度分桶绘制。
// 保留全部服务器线段，仅按寿命和屏幕包围盒剔除。
const LIGHTNING_LIFETIME_MS = 100;
const LIGHTNING_ALPHA_BUCKETS = 6;

function lightningHash(value) {
  const hashed = Math.sin(value * 127.1 + 311.7) * 43758.5453;
  return hashed - Math.floor(hashed);
}

function lightningBoltGeometry(a, b) {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const length = Math.hypot(dx, dy) || 1;
  // 相机不旋转且等比缩放，世界与屏幕的垂线方向一致。
  const px = -dy / length;
  const py = dx / length;
  const spread = Math.min(20, length * 0.16);
  const hash = a.x * 12.9898 + a.y * 78.233 + b.x * 37.719 + b.y * 37.719;
  const midpoints = [];
  const steps = 3;
  for (let s = 1; s <= steps; s += 1) {
    const t = s / (steps + 1);
    const jitter = (lightningHash(hash + s) - 0.5) * spread;
    midpoints.push({
      x: a.x + dx * t + px * jitter,
      y: a.y + dy * t + py * jitter,
    });
  }
  return midpoints;
}

function pushLightningChain(points, style = 0) {
  const segments = [];
  let minX = Infinity;
  let minY = Infinity;
  let maxX = -Infinity;
  let maxY = -Infinity;
  for (let n = 0; n + 1 < points.length; n += 1) {
    const a = points[n];
    const b = points[n + 1];
    segments.push({
      ax: a.x,
      ay: a.y,
      bx: b.x,
      by: b.y,
      midpoints: lightningBoltGeometry(a, b),
    });
    if (a.x < minX) minX = a.x;
    if (b.x < minX) minX = b.x;
    if (a.x > maxX) maxX = a.x;
    if (b.x > maxX) maxX = b.x;
    if (a.y < minY) minY = a.y;
    if (b.y < minY) minY = b.y;
    if (a.y > maxY) maxY = a.y;
    if (b.y > maxY) maxY = b.y;
  }
  if (segments.length === 0) return;

  state.lightningChains.push({
    segments,
    minX,
    minY,
    maxX,
    maxY,
    style: style === 1 ? 1 : 0,
    age: 0,
    spawnedAt: performance.now(),
  });
}

function drawLightningChains(now = performance.now()) {
  const chains = state.lightningChains;
  if (!chains.length) return;

  let write = 0;
  for (let i = 0; i < chains.length; i += 1) {
    const chain = chains[i];
    chain.age = (now - (chain.spawnedAt || now)) / LIGHTNING_LIFETIME_MS;
    if (chain.age >= 1) continue;
    chains[write] = chain;
    write += 1;
  }
  chains.length = write;
  if (!write) return;

  ctx.save();
  ctx.filter = "none";
  ctx.globalCompositeOperation = "lighter";
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.lineWidth = 2.4;

  const canvasW = state.canvasWidth;
  const canvasH = state.canvasHeight;
  const marginPx = 60;
  let currentBucket = -1;
  let currentStyle = -1;
  let pathOpen = false;

  const flushPath = () => {
    if (pathOpen) {
      ctx.stroke();
      pathOpen = false;
    }
  };
  const beginBucket = (bucket, style) => {
    flushPath();
    ctx.globalAlpha = (bucket + 0.5) / LIGHTNING_ALPHA_BUCKETS;
    ctx.strokeStyle = style === 1 ? "#63cfff" : "#ffffff";
    ctx.beginPath();
    pathOpen = true;
  };

  for (let i = chains.length - 1; i >= 0; i -= 1) {
    const chain = chains[i];
    const cornerA = worldToScreen({ x: chain.minX, y: chain.minY });
    const cornerB = worldToScreen({ x: chain.maxX, y: chain.maxY });
    if (
      cornerB.x < -marginPx ||
      cornerA.x > canvasW + marginPx ||
      cornerB.y < -marginPx ||
      cornerA.y > canvasH + marginPx
    )
      continue;

    const bucket = Math.min(
      LIGHTNING_ALPHA_BUCKETS - 1,
      Math.floor((1 - chain.age) * LIGHTNING_ALPHA_BUCKETS),
    );
    for (const segment of chain.segments) {
      if (bucket !== currentBucket || chain.style !== currentStyle) {
        beginBucket(bucket, chain.style);
        currentBucket = bucket;
        currentStyle = chain.style;
      }
      const a = worldToScreen({ x: segment.ax, y: segment.ay });
      const b = worldToScreen({ x: segment.bx, y: segment.by });
      ctx.moveTo(a.x, a.y);
      for (const mid of segment.midpoints) {
        const m = worldToScreen(mid);
        ctx.lineTo(m.x, m.y);
      }
      ctx.lineTo(b.x, b.y);
    }
  }
  flushPath();
  ctx.restore();
}


function drawPlayerFlower(snap, pos, radius, angle, isOwner, portrait = false) {
  drawFlower(snap, pos, radius, angle, isOwner, "player", portrait);
}

function drawMechaFlower(snap, pos, radius, angle) {
  drawFlower(snap, pos, radius, angle, false, "mecha");
}

function drawTitanFlower(snap, pos, radius, angle) {
  drawFlower(snap, pos, radius, angle, false, "titan");
}

function drawFlower(snap, pos, radius, angle, isOwner, bodyKind, portrait = false) {
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
    hasPlayerSnapPetal(snap, petalAntennaeType) ||
    (isOwner && hasOwnerPetal(petalAntennaeType));
  const hasDouli =
    hasPlayerSnapPetal(snap, petalDouliType) ||
    (isOwner && hasOwnerPetal(petalDouliType));
  const hasThirdEye =
    hasPlayerSnapPetal(snap, petalThirdEyeType) ||
    (isOwner && hasOwnerPetal(petalThirdEyeType));
  const hasMagicEye =
    hasPlayerSnapPetal(snap, petalMagicEyeType) ||
    (isOwner && hasOwnerPetal(petalMagicEyeType));
  const hasBandage =
    hasPlayerSnapPetal(snap, petalBandageType) ||
    (isOwner && hasOwnerPetal(petalBandageType));
  const hasDisc =
    hasPlayerSnapPetal(snap, petalDiscType) ||
    (isOwner && hasOwnerPetal(petalDiscType));
  const nullified = entityHasState(snap, stateNullification);
  const dead = (flags & flagDead) !== 0;
  const entity = state.entities.get(snap.entityId);
  const face = entity?.flowerFace || stepFlowerFace(null, {
    attacking: !!(flags & flagAttacking), defending: !!(flags & flagDefending), dead,
    lookX: Math.cos(angle || 0), lookY: Math.sin(angle || 0),
  }, 0);
  const palette = getFlowerPalette({
    poisoned: entityHasState(snap, statePoison), undead: entityHasState(snap, stateUndead),
    corrupted: entityHasState(snap, stateCorruption), psionic: entityHasState(snap, statePsionicConnection),
    antiHeal: entityHasState(snap, stateAntiHeal), dummy: snap.entityType === dummyType,
    titan: bodyKind === "titan", portrait, hit: entity ? hurtFlashAmount(entity) : 0,
  });
  const featureRadius = bodyKind === "titan" ? getTitanCoreRadius(radius) : radius;
  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (face.tilt) ctx.rotate(face.tilt);
  if (nullified) ctx.globalAlpha *= 0.58;
  if (bodyKind === "mecha") drawFlowerBody(bodyKind, radius, snap);
  else if (bodyKind === "titan") drawTitanBody(ctx, radius, palette, hasDisc);
  else drawFlowerDisc(ctx, radius, palette, hasDisc);
  if (hasBandage) drawFlowerBandage(ctx, featureRadius, bandageOverlayImage(), drawSpriteImage);
  drawFlowerFace(ctx, featureRadius, face, palette, {
    dummy: snap.entityType === dummyType,
    magicEye: hasMagicEye,
  });
  if (hasAntennae) drawFlowerAntennae(ctx, featureRadius, antennaeOverlayImage(), drawSpriteImage);
  if (hasThirdEye) drawThirdEyeOverlay(featureRadius, face.dead);
  if (hasDouli) drawDouliOverlay(featureRadius);
  ctx.restore();
}

function drawFlowerBody(bodyKind, radius, snap) {
  const image = assetImage(getFlowerBodyTexturePath(bodyKind));
  // 玩家花朵按 50 单位直径；机甲贴图保留其 110 单位素材比例。
  const size = Math.max(0.5, radius * (bodyKind === "mecha" ? 2.36 : 2));

  ctx.save();
  ctx.filter = getFlowerBodyFilter(snap, bodyKind === "mecha", ctx.filter);
  if (imageReady(image)) {
    drawSpriteImage(ctx, image, -size * 0.5, -size * 0.5, size, size);
  } else if (bodyKind === "mecha") {
    drawMechaFlowerFallback(radius);
  } else {
    drawPlayerFlowerFallback(radius);
  }
  ctx.restore();
}

function getFlowerBodyFilter(snap, mechanical, inheritedFilter) {
  const filters = [];
  const corrupted = entityHasState(snap, stateCorruption);
  const undead = entityHasState(snap, stateUndead);
  const poisoned = entityHasState(snap, statePoison);
  const psionic = entityHasState(snap, statePsionicConnection);

  if (mechanical && (corrupted || undead || poisoned || psionic))
    filters.push("sepia(0.55)");
  if (psionic)
    filters.push("saturate(0.7) brightness(0.45) contrast(1.15)");
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
    drawSpriteImage(ctx, image, -size * 0.5, -size * 0.5, size, size);
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

function getFlowerBodyTexturePath(bodyKind) {
  const textureFile = bodyKind === "mecha" ? "mecha_flower" : "player_flower";
  return `./assets/${textureFile}.svg?v=${flowerTextureVersion}`;
}

function drawDouliOverlay(radius) {
  const icon = douliOverlayImage();
  const layout = flowerAttachmentLayout.douli;
  const size = Math.max(0.5, radius * layout.size);
  ctx.save();
  ctx.translate(0, radius * layout.y);
  ctx.rotate(layout.angle);
  if (imageReady(icon)) {
    drawSpriteImage(ctx, icon, -size * 0.5, -size * 0.5, size, size);
  }
  ctx.restore();
}

function drawThirdEyeOverlay(radius, dead = false) {
  const icon = thirdEyeOverlayImage();
  const layout = flowerAttachmentLayout.thirdEye;
  const size = Math.max(0.5, radius * layout.size);
  ctx.save();
  ctx.translate(0, radius * layout.y);
  if (dead) {
    ctx.strokeStyle = "#222";
    ctx.lineWidth = radius * 0.06;
    ctx.lineCap = "round";
    const arm = radius * 0.08;
    ctx.beginPath();
    ctx.moveTo(-arm, -arm); ctx.lineTo(arm, arm);
    ctx.moveTo(arm, -arm); ctx.lineTo(-arm, arm);
    ctx.stroke();
  } else if (imageReady(icon)) {
    drawSpriteImage(ctx, icon, -size * 0.5, -size * 0.5, size, size);
  } else {
    ctx.fillStyle = "#f4f0e9";
    ctx.strokeStyle = "#322a35";
    ctx.lineWidth = size * 0.06;
    ctx.beginPath();
    ctx.ellipse(0, 0, size * 0.21, size * 0.43, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = "#333127";
    ctx.beginPath();
    ctx.arc(0, 0, size * 0.13, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

function drawPlayerFlowerFallback(radius) {
  drawFlowerDisc(ctx, radius, getFlowerPalette());
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

function drawLivePetal(petalType, rarity, pos, radius, angle, entityId = 0) {
  const size = radius * worldPetalSizeScale;
  if (!(size > 0) || !Number.isFinite(size)) return;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  if (hasPetalMultiCopyVisual(petalType, rarity))
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

  if (petalType === petalBulbType) {
    drawBulbLivePetal(size, rarity, entityId);
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
  } else if (petalType === petalHallucinogenicMushroomType) {
    icon = livePetalIconImage(
      petalType,
      rarity,
      extractHallucinogenicMushroomLiveSvg,
      "hallucinogenic-mushroom-centered",
    );
  } else {
    icon = livePetalIconImage(petalType, rarity);
  }

  const visualScale =
    petalType === petalWaxType
      ? waxLivePetalVisualScale
      : petalType === petalHallucinogenicMushroomType
        ? hallucinogenicMushroomLiveVisualScale
        : 1;
  // 服务端木板半径扩大 1.5 倍，贴图保持原尺寸。
  const spriteSizeFactor = petalType === petalPlankType ? 1 / 1.5 : 1;
  if (imageReady(icon)) {
    drawCenteredImage(icon, size * spriteSizeFactor, visualScale);
  } else {
    drawPetalSilhouette(size * visualScale * spriteSizeFactor, rarity);
  }

  ctx.restore();
}

// 灯泡光环使用探测半径，花瓣本体仍为基本尺寸。
const BULB_GLOW_RADIUS_SCALE = 1.1;
const BULB_GLOW_AREA_PULSE = 0.125;
const BULB_GLOW_PULSE_HZ = 4.4;
function drawBulbLivePetal(size, rarity, entityId) {
  const iconSize = size;
  const pulse = Math.sqrt(
    Math.max(
      0,
      1 +
        BULB_GLOW_AREA_PULSE *
          Math.sin(
            currentRenderTimeSeconds * BULB_GLOW_PULSE_HZ + (entityId || 0) * 0.83,
          ),
    ),
  );
  const glowRadius = Math.max(1, iconSize * 0.5 * BULB_GLOW_RADIUS_SCALE) * pulse;
  const gradient = ctx.createRadialGradient(0, 0, iconSize * 0.1, 0, 0, glowRadius);
  gradient.addColorStop(0, "rgba(255, 240, 160, 0.42)");
  gradient.addColorStop(0.55, "rgba(255, 226, 110, 0.18)");
  gradient.addColorStop(1, "rgba(255, 226, 110, 0)");
  ctx.save();
  ctx.filter = "none";
  ctx.globalCompositeOperation = "lighter";
  ctx.fillStyle = gradient;
  ctx.beginPath();
  ctx.arc(0, 0, glowRadius, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();

  const icon = livePetalIconImage(petalBulbType, rarity);
  if (imageReady(icon)) {
    drawCenteredImage(icon, iconSize, 1);
  } else {
    drawPetalSilhouette(iconSize, rarity);
  }
}

function drawDandelionMissile(pos, radius, angle, rarity, entityId = 0) {  const safeRarity = rarity || 1;
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
    drawSpriteImage(
      ctx,
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
  drawMoonSurface(ctx, size * moonSvgGeometry.liveRadiusFactor, rarityLevel(rarity));
}

function drawDustParticlePetal(size, rarity) {
  // 尘土素材只占原画框的一小部分，程序绘制保持相同视觉比例。
  const drawSize = size * 0.42;
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

function drawDropPetalCard(petalType, rarity, pos, radius, stackCount = 1) {
  const size = radius * worldDropSizeScale;
  if (!(size > 0) || !Number.isFinite(size)) return;
  const base = petalBaseImage(rarity);
  const copy = effectivePetalCardCopy(petalType, rarity);
  const splitLabel = copy > 1;
  const icon = petalCardIconImage(petalType, rarity, { stripped: splitLabel });

  ctx.save();
  ctx.translate(pos.x, pos.y);
  drawPetalCardShadow(size);

  if (imageReady(base)) {
    drawCenteredImage(base, size, 1);
  } else {
    drawPetalCardFallback(size, rarity);
  }

  drawPetalCardCopies(icon, size, rarity, copy, petalType);
  drawPetalCardName(petalType, size);
  if (stackCount > 1) drawDropStackBadge(size, stackCount);

  ctx.restore();
}

function dropStackCountFromSnap(snap) {
  const state = (snap.states || []).find(
    (entityState) => entityState?.type === stateDropStack,
  );
  return state ? Math.max(1, state.value || 0) : 1;
}

// 画布卡片数量角标与界面一致，位于右上并随尺寸缩放。
const dropStackBadgeTilt = (12 * Math.PI) / 180;
function drawDropStackBadge(size, stackCount) {
  const text = `x${formatShortCount(stackCount)}`;
  const fontSize = Math.max(5, size * 0.2);
  ctx.save();
  ctx.translate(size * 0.56, -size * 0.44);
  ctx.rotate(dropStackBadgeTilt);
  drawOutlinedText(text, 0, 0, {
    font: `900 ${fontSize}px ${UI_FONT_FAMILY}`,
    fill: "#ffffff",
    stroke: "#000000",
    lineWidth: Math.max(1, fontSize * 0.3),
    align: "right",
    baseline: "bottom",
  });
  ctx.restore();
}

function drawPetalCardName(petalType, size) {
  if (!petalType) return;
  const name = getPetalDisplayName(petalType);
  if (!name) return;

  ctx.save();
  ctx.font = `900 100px ${UI_FONT_FAMILY}`;
  const widthAt100 = Math.max(1, ctx.measureText(name).width);
  const sizeFactor = size / 56;
  const maxFontPx = getClientConfigValue(state.clientConfig, "petal_label_max_font_px") ?? 14;
  const maxWidthPx = getClientConfigValue(state.clientConfig, "petal_label_max_width_px") ?? 45;
  const fontSize = Math.min(
    maxFontPx * sizeFactor,
    Math.max(1, (maxWidthPx * sizeFactor) / (widthAt100 / 100)),
  );
  ctx.font = `900 ${fontSize}px ${UI_FONT_FAMILY}`;
  ctx.textAlign = "center";
  ctx.textBaseline = "alphabetic";
  ctx.lineJoin = "round";
  // 卡片以原点居中，标签基线位于局部坐标的 0.34 倍尺寸处。
  const x = 0;
  const y = size * 0.34;
  ctx.lineWidth = Math.max(1.2, fontSize * 0.18);
  ctx.strokeStyle = "#000000";
  ctx.fillStyle = "#ffffff";
  ctx.strokeText(name, x, y);
  ctx.fillText(name, x, y);
  ctx.restore();
}

function drawPetalCardCopies(icon, size, rarity, copy, petalType = 0) {
  const layout = getPetalCopyLayout(copy, petalType);
  const cardAngle = petalType === petalLinkerType ? Math.PI * 0.25 : 0;
  for (const part of layout) {
    ctx.save();
    if (layout.length > 1) {
      ctx.translate((size * part.x) / 100, (size * part.y) / 100);
      ctx.rotate(part.angle);
    }
    if (cardAngle) ctx.rotate(cardAngle);
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
  return assetImage(getPetalIconPath(petalType, rarity));
}

function petalCardIconImage(petalType, rarity = 0, options = {}) {
  const src = getPetalIconPath(petalType, rarity);
  if (!src) return null;
  if (options.stripped)
    return transformedPetalImage(src, stripLivePetalLabel, "card-stripped");
  return assetImage(src);
}

function petalLabelImage(petalType, rarity = 0) {
  const src = getPetalIconPath(petalType, rarity);
  if (!src) return null;
  return transformedPetalImage(src, extractPetalLabelSvg, "card-label");
}

function transformedPetalImage(src, transformSvg, cachePrefix) {
  const cacheKey = `${cachePrefix}:${src}`;
  let entry = transformedPetalImages.get(cacheKey);
  if (entry) return entry.image;

  const image = prepareSvgImage(src, cachePrefix, transformSvg);
  entry = { image };
  transformedPetalImages.set(cacheKey, entry);

  return image;
}

function livePetalIconImage(
  petalType,
  rarity = 0,
  transformSvg = stripLivePetalLabel,
  cacheSuffix = "live",
) {
  const src = getPetalIconPath(petalType, rarity, { live: true });
  if (!src) return null;

  const cacheKey = `${cacheSuffix}:${src}`;
  let entry = livePetalImages.get(cacheKey);
  if (entry) return entry.image;

  const image = prepareSvgImage(src, cacheSuffix, transformSvg);
  entry = { image };
  livePetalImages.set(cacheKey, entry);

  return image;
}

// 活体花瓣按实际素材边界居中裁剪；边长不小于 minSide，以保持调整过的视觉尺寸。
function measuredLiveViewBox(svgMarkup, minSide) {
  const box = measureSvgContentBox(svgMarkup);
  if (!box) return null;
  const side = Math.max(minSide, Math.max(box.w, box.h) * 1.02);
  const centerX = box.x + box.w / 2;
  const centerY = box.y + box.h / 2;
  return `${(centerX - side / 2).toFixed(2)} ${(centerY - side / 2).toFixed(2)} ${side.toFixed(2)} ${side.toFixed(2)}`;
}

function extractSingleAntEggLiveSvg(svgText) {
  return extractFirstLivePetalPathsSvg(svgText, 2, 74);
}

function extractWaxLiveSvg(svgText) {
  return extractFirstLivePetalPathsSvg(svgText, 2, 52);
}

function extractDandelionLiveSvg(svgText) {
  return extractFirstLivePetalPathsSvg(svgText, 3, 68);
}

function extractTrapperLiveSvg(svgText) {
  return cropLivePetalSvg(svgText, 110);
}

function extractHallucinogenicMushroomLiveSvg(svgText) {
  return cropLivePetalSvg(svgText, 52);
}

function cropLivePetalSvg(svgText, minSide) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined")
    return stripLivePetalLabel(svgText);

  const stripped = stripLivePetalLabel(svgText);
  const doc = new DOMParser().parseFromString(stripped, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return stripped;

  const viewBox = measuredLiveViewBox(stripped, minSide);
  if (!viewBox) return stripped;
  root.setAttribute("viewBox", viewBox);
  return new XMLSerializer().serializeToString(root);
}

function extractFirstLivePetalPathsSvg(svgText, pathCount, minSide) {
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
  const contentSvg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 110 110">${body}</svg>`;
  const viewBox = measuredLiveViewBox(contentSvg, minSide);
  if (!viewBox) return stripLivePetalLabel(svgText);
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}">${body}</svg>`;
}

function bandageOverlayImage() {
  const src = getPetalIconPath(petalBandageType);
  if (!src) return null;

  let entry = bandageOverlayImages.get(src);
  if (entry) return entry.image;

  const image = prepareSvgImage(src, "bandage-overlay", text => centerPetalArtSvg(extractBandageOverlaySvg(text)));
  entry = { image };
  bandageOverlayImages.set(src, entry);

  return image;
}

function antennaeOverlayImage() {
  const src = getPetalIconPath(petalAntennaeType);
  if (!src) return null;
  return transformedPetalImage(src, svg => centerPetalArtSvg(stripLivePetalLabel(svg)), "antennae-overlay-centered");
}

function douliOverlayImage() {
  const src = getPetalIconPath(petalDouliType);
  if (!src) return null;
  return transformedPetalImage(src, svg => centerPetalArtSvg(stripLivePetalLabel(svg)), "douli-overlay-centered");
}

function thirdEyeOverlayImage() {
  const src = getPetalIconPath(petalThirdEyeType);
  if (!src) return null;
  return transformedPetalImage(src, svg => centerPetalArtSvg(stripLivePetalLabel(svg)), "third-eye-overlay-centered");
}


function drawCenteredImage(image, size, scale) {
  const drawSize = size * scale;
  if (!(drawSize > 0) || !Number.isFinite(drawSize)) return;
  drawSpriteImage(ctx, image, -drawSize * 0.5, -drawSize * 0.5, drawSize, drawSize);
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

function drawTrackedHealthBar(snap, x, y, width, height, progress, fill, border) {
  const identity = state.entities.get(snap.entityId);
  let record = healthTrails.get(snap.entityId);
  if (!record || record.identity !== identity) record = { identity, motion: null };
  record.motion = stepHealthTrail(record.motion, snap.hpPercent, currentRenderTimeSeconds * 1000);
  healthTrails.set(snap.entityId, record);
  if (healthTrails.size > 512) healthTrails.delete(healthTrails.keys().next().value);
  drawCapsuleBar(x, y, width, height, Math.max(progress, record.motion.trail), "#d56565", border);
  const inset = Math.min(border, width * 0.35, height * 0.35);
  drawSolidProgressBar(ctx, { x: x + inset, y: y + inset, maxLength: Math.max(0, width - inset * 2),
    width: Math.max(0, height - inset * 2), progress, color: fill });
}

function drawShieldBar(x, y, width, height, shieldPercent) {
  const shield = clamp(shieldPercent || 0, 0, 1);
  if (shield <= 0) return;

  const shieldLength = width * 0.82;
  const shieldWidth = height * 0.48;
  drawSolidProgressBar(ctx, {
    x: x + (width - shieldLength) * 0.5,
    y: y + (height - shieldWidth) * 0.5,
    maxLength: shieldLength,
    width: shieldWidth,
    progress: shield,
    color: "#ffffff",
  });
}

function normalizedLayeredBarMembers(members) {
  return (members || [])
    .map((member) => ({
      hp: clamp(member?.hp || 0, 0, 1),
      count: Math.max(0, Number(member?.count) || 0),
    }))
    .filter((member) => Number.isFinite(member.hp) && member.count > 0);
}

function drawLayeredSolidProgressBar(x, y, width, height, members, fill) {
  const entries = normalizedLayeredBarMembers(members);
  const totalCount = entries.reduce((sum, member) => sum + member.count, 0);
  if (totalCount <= 0) return;

  for (const member of entries) {
    ctx.save();
    const geometry = traceSolidProgressBarPath(ctx, {
      x,
      y,
      maxLength: width,
      width: height,
      progress: member.hp,
    });
    if (!geometry) {
      ctx.restore();
      continue;
    }

    // 短胶囊每个逻辑副本贡献 (m * p) / (d * copy_num)。
    const perCopyAlpha = geometry.alpha / totalCount;
    const bucketAlpha = 1 - Math.pow(1 - perCopyAlpha, member.count);
    ctx.globalAlpha *= bucketAlpha;
    ctx.fillStyle = fill;
    ctx.fill();
    ctx.restore();
  }
}

function drawLayeredCapsuleBar(x, y, width, height, members, fill, border = 5) {
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

  drawLayeredSolidProgressBar(innerX, innerY, innerW, innerH, members, fill);
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
  if (!(radius > 0) || !Number.isFinite(radius)) return;

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

function drawFlowerFrame(snap, pos, radius) {
  const isOwner = snap.entityId === state.ownerEntityId || (snap.flags & flagOwner) !== 0;
  const hp = clamp(snap.hpPercent || 0, 0, 1);
  const shield = clamp(snap.shieldPercent || 0, 0, 1);
  const maxMana = Math.max(0, Number(snap.maxMana) || 0);
  const hasMana = maxMana > 0;
  const mana = hasMana
    ? clamp((Number(snap.mana) || 0) / maxMana, 0, 1)
    : 0;
  const dead = (snap.flags & flagDead) !== 0;
  // 自身名字和等级已在界面显示，世界内只显示必要资源条。
  if (isOwner && dead) return;
  const frame = getFlowerFrameLayout(radius, mobileUiScale(), isOwner);
  const frameScale = mobileUiScale();
  const manaHeight = hasMana
    ? Math.max(2.25 * frameScale, frame.barHeight * 0.58)
    : 0;
  const manaGap = hasMana ? Math.max(1, 1.25 * frameScale) : 0;
  const x = pos.x - frame.width / 2;
  const y = pos.y + frame.barY;
  if (!isOwner && snap.name) {
    let nameSize = frame.nameSize;
    ctx.font = `800 ${nameSize}px ${UI_FONT_FAMILY}`;
    const labelGap = Math.max(1, frameScale * 2.5);
    // 名字与其他生物一样左对齐，保持与血条的关联。
    const maxWidth = Math.max(frame.width, frame.nameSize * 7);
    const measured = ctx.measureText(snap.name).width;
    if (measured > maxWidth) nameSize *= maxWidth / measured;
    drawOutlinedText(snap.name, x, y - manaHeight - manaGap - labelGap, {
      font: `800 ${nameSize}px ${UI_FONT_FAMILY}`,
      fill: playerNameColor(snap, "#f7f7f7"), lineWidth: Math.max(1, nameSize * 0.25),
      align: "left", baseline: "bottom",
    });
  }
  if (hasMana) {
    const manaMotion = playerHud.smooth(
      `flower-frame:${snap.entityId}`,
      { mana },
      state.entities.get(snap.entityId) || snap,
      currentRenderTimeSeconds * 1000,
    );
    drawCapsuleBar(
      x,
      y - manaHeight - manaGap,
      frame.width,
      manaHeight,
      manaMotion.mana,
      "#55d9ee",
      Math.max(0.65, manaHeight * 0.22),
    );
  }
  drawTrackedHealthBar(snap, x, y, frame.width, frame.barHeight, hp, healthBarColor(snap),
    Math.max(0.8, frame.barHeight * 0.26));
  drawShieldBar(x, y, frame.width, frame.barHeight, shield);
  if (!isOwner) drawOutlinedText(rarityOrLevelLabel(snap), pos.x, pos.y + frame.levelY, {
    font: `800 ${frame.levelSize}px ${UI_FONT_FAMILY}`,
    fill: rarityColor(getPlayerLevelRarity(snap), 1), lineWidth: Math.max(1, frame.levelSize * 0.25),
    align: "center", baseline: "top",
  });
}

function drawPulseArcs(snap, pos, radius) {
  const pulse = entityState(snap, statePulse);
  if (!pulse || !(radius > 0)) return;

  // 稳定时间窗口内使用确定形状，避免逐帧随机闪烁和分配。
  const shiftedTime = currentRenderTimeSeconds + (snap.entityId % 97) * 0.013;
  const windowDuration = 0.68;
  const window = Math.floor(shiftedTime / windowDuration);
  const localTime = shiftedTime - window * windowDuration;
  const eventSeed = snap.entityId * 131 + window * 17;
  if (localTime > 0.2 || lightningHash(eventSeed) > 0.48) return;

  const fade = Math.sin((localTime / 0.2) * Math.PI);
  const arcCount = lightningHash(eventSeed + 3) > 0.58 ? 2 : 1;
  ctx.save();
  ctx.globalCompositeOperation = "lighter";
  ctx.globalAlpha *= 0.35 + fade * 0.65;
  ctx.strokeStyle = "#ffe14a";
  ctx.lineWidth = Math.max(1.1, Math.min(3.2, radius * 0.055));
  ctx.lineCap = "round";
  ctx.lineJoin = "round";

  for (let arc = 0; arc < arcCount; arc += 1) {
    const seed = eventSeed + arc * 29;
    const angle = lightningHash(seed + 5) * Math.PI * 2;
    const tangentX = -Math.sin(angle);
    const tangentY = Math.cos(angle);
    const radialX = Math.cos(angle);
    const radialY = Math.sin(angle);
    const span = radius * (0.52 + lightningHash(seed + 7) * 0.28);
    const baseRadius = radius * (0.62 + lightningHash(seed + 11) * 0.25);
    ctx.beginPath();
    for (let point = 0; point < 5; point += 1) {
      const t = point / 4 - 0.5;
      const jag = point === 0 || point === 4
        ? 0
        : (lightningHash(seed + point * 13) - 0.5) * radius * 0.26;
      const x = pos.x + radialX * (baseRadius + jag) + tangentX * span * t;
      const y = pos.y + radialY * (baseRadius + jag) + tangentY * span * t;
      if (point === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }
  ctx.restore();
}

function drawMobFrame(snap, pos, radius) {
  if (entityVisualOnlyRenderDepth > 0) return;
  drawPulseArcs(snap, pos, radius);
  if (radius < entityNameMinScreenRadius) return;
  if (shouldSkipMobFrame(snap, radius)) return;

  if (snap.entityType === playerFlowerType) {
    drawFlowerFrame(snap, pos, radius);
    return;
  }

  if (!isMobHitboxSnap(snap)) {
    drawEntityLabel(snap, pos, radius);
    return;
  }

  // 名字可在较小尺寸保留，完整血条/稀有度使用较高阈值，避免远处文字堆叠。
  if (radius < entityFrameMinScreenRadius) {
    drawEntityLabel(snap, pos, radius);
    return;
  }

  // 血条宽度跟随身体，文字和间距保持紧凑，避免巨型生物产生巨型标签。
  const frame = getMobFrameLayout(radius, mobileUiScale());
  const { width, barHeight, textSize, labelGap } = frame;
  const y = pos.y + frame.y;
  const x = pos.x - width * 0.5;
  const hp = clamp(snap.hpPercent, 0, 1);
  const labelColor = rarityColor(snap.rarity, 1);
  const strokeColor = "#050506";

  drawOutlinedText(mobDisplayName(snap), x, y - labelGap, {
    font: `800 ${textSize}px ${UI_FONT_FAMILY}`,
    fill: playerNameColor(snap, "#f7f7f7"),
    stroke: strokeColor,
    lineWidth: Math.max(1.1, textSize * 0.42),
    align: "left",
    baseline: "bottom",
  });

  drawTrackedHealthBar(
    snap,
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
    y + barHeight + labelGap,
    {
      font: `800 ${textSize}px ${UI_FONT_FAMILY}`,
      fill: labelColor,
      stroke: strokeColor,
      lineWidth: Math.max(1.1, textSize * 0.42),
      align: "right",
      baseline: "top",
    },
  );
}

function drawEntityLabel(snap, pos, radius) {
  if (!snap.name || radius < entityNameMinScreenRadius) return;
  if (shouldSkipMobFrame(snap, radius)) return;
  ctx.font = `12px ${UI_FONT_FAMILY}`;
  ctx.textAlign = "center";
  ctx.textBaseline = "bottom";
  ctx.fillStyle = playerNameColor(snap, "rgba(39, 50, 65, 0.86)");
  ctx.fillText(snap.name, pos.x, pos.y - radius - 10);
}

function playerNameColor(snap, fallback) {
  if (snap?.entityId !== state.ownerEntityId && partyMemberForEntity(state, snap)) return "#f17cab";
  return fallback;
}

function currentSquadMemberEntities() {
  return partyRoster(state).filter(entry => entry.entity).map(entry => entry.entity);
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
      ? entityNameMinScreenRadius * 2.5
      : entityNameMinScreenRadius * 1.5;
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

function motionFactor(now, previous, rate) {
  if (!Number.isFinite(previous)) return 1;
  const dt = clamp((now - previous) / 1000, 0, 0.12);
  return dt <= 0 ? 0 : 1 - Math.exp(-Math.max(0, rate) * dt);
}

function approachMotion(current, target, factor) {
  if (!Number.isFinite(current)) return target;
  return current + (target - current) * clamp(factor, 0, 1);
}

function cloneLayeredBarMembers(members) {
  return normalizedLayeredBarMembers(members).map((member) => ({
    hp: member.hp,
    count: member.count,
  }));
}

// 血量桶按百分比排序后线性配对，不把快照遍历次序当身份，也避免平方搜索。
function smoothLayeredBarMembers(previous, target, factor) {
  const oldMembers = normalizedLayeredBarMembers(previous).sort(
    (a, b) => a.hp - b.hp,
  );
  const nextMembers = normalizedLayeredBarMembers(target).sort(
    (a, b) => a.hp - b.hp,
  );
  const result = [];
  const blend = clamp(factor, 0, 1);

  const pairedCount = Math.min(oldMembers.length, nextMembers.length);
  for (let index = 0; index < nextMembers.length; index += 1) {
    const next = nextMembers[index];
    if (index < pairedCount) {
      const previousMember = oldMembers[index];
      result.push({
        hp: approachMotion(previousMember.hp, next.hp, blend),
        count: approachMotion(previousMember.count, next.count, blend),
      });
    } else {
      // 新增层按副本数渐显，避免首帧突然出现不透明条带。
      result.push({
        hp: next.hp,
        count: next.count * blend,
      });
    }
  }

  for (let index = pairedCount; index < oldMembers.length; index += 1) {
    const previousMember = oldMembers[index];
    const count = previousMember.count * (1 - blend);
    if (count > 0.001)
      result.push({ hp: previousMember.hp, count });
  }
  return result;
}

function bossBarGeometry(groupCount) {
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
  const slotHeight = (bandBottom - bandTop) / Math.max(1, groupCount);
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
  return {
    width,
    bandTop,
    slotHeight,
    outlinePadding,
    scale,
    height: bossBarHeightPx * scale,
    titleFontSize: bossBarTitleFontPx * scale,
    rarityFontSize: bossBarRarityFontPx * scale,
    labelGap: bossBarLabelGapPx * scale,
    blockHeight: naturalBlockHeight * scale,
    x: (state.canvasWidth - width) * 0.5,
  };
}

function bossBarTargetY(index, geometry) {
  const slotTop = geometry.bandTop + index * geometry.slotHeight;
  return (
    slotTop +
    (geometry.slotHeight - geometry.blockHeight) * 0.5 +
    geometry.outlinePadding * geometry.scale +
    geometry.titleFontSize +
    geometry.labelGap
  );
}

function updateBossBarMotion(group, targetY, now) {
  let motion = bossBarMotions.get(group.key);
  if (!motion) {
    motion = {
      key: group.key,
      alpha: 0,
      y: targetY - bossBarExitLiftPx,
      targetY,
      count: Math.max(1, group.count || group.members.length),
      members: cloneLayeredBarMembers(group.members),
      shieldMembers: cloneLayeredBarMembers(group.shieldMembers),
      shieldAlpha: group.hasShield ? 1 : 0,
      exitTargetY: targetY - bossBarExitLiftPx,
      lastUpdatedAt: now,
      group,
    };
    bossBarMotions.set(group.key, motion);
    return motion;
  }

  const factor = motionFactor(now, motion.lastUpdatedAt, bossBarMotionRate);
  motion.lastUpdatedAt = now;
  motion.targetY = targetY;
  motion.y = approachMotion(motion.y, targetY, factor);
  motion.alpha = approachMotion(motion.alpha, 1, factor);
  motion.count = approachMotion(
    motion.count,
    Math.max(1, group.count || group.members.length),
    factor,
  );
  motion.members = smoothLayeredBarMembers(motion.members, group.members, factor);
  motion.shieldMembers = smoothLayeredBarMembers(
    motion.shieldMembers,
    group.shieldMembers,
    factor,
  );
  motion.shieldAlpha = approachMotion(
    motion.shieldAlpha,
    group.hasShield ? 1 : 0,
    factor,
  );
  motion.group = group;
  motion.exitTargetY = motion.y - bossBarExitLiftPx;
  return motion;
}

function updateExitingBossBarMotion(motion, now) {
  const factor = motionFactor(now, motion.lastUpdatedAt, bossBarMotionRate);
  motion.lastUpdatedAt = now;
  motion.alpha = approachMotion(motion.alpha, 0, factor);
  motion.y = approachMotion(motion.y, motion.exitTargetY, factor);
  motion.count = approachMotion(motion.count, 0, factor);
  motion.members = smoothLayeredBarMembers(motion.members, [], factor);
  motion.shieldMembers = smoothLayeredBarMembers(
    motion.shieldMembers,
    [],
    factor,
  );
  motion.shieldAlpha = approachMotion(motion.shieldAlpha, 0, factor);
}

function drawBossBars(entities, now = performance.now()) {
  const groups = bossBarCandidates(entities);
  // 布局计入正在退出的血条，避免淡出未完时其余几何突然扩张。
  const geometry = bossBarGeometry(
    Math.max(groups.length, bossBarMotions.size),
  );
  const currentKeys = new Set();

  groups.forEach((group, index) => {
    currentKeys.add(group.key);
    updateBossBarMotion(group, bossBarTargetY(index, geometry), now);
  });

  for (const [key, motion] of bossBarMotions) {
    if (currentKeys.has(key)) continue;
    updateExitingBossBarMotion(motion, now);
    if (motion.alpha <= 0.01 && motion.count <= 0.01)
      bossBarMotions.delete(key);
  }

  if (bossBarMotions.size === 0) return;
  const motions = [...bossBarMotions.values()]
    .filter((motion) => motion.alpha > 0.001)
    .sort((a, b) => a.y - b.y);
  for (const motion of motions) {
    const group = motion.group;
    if (!group) continue;
    const rarityFill = rarityColor(group.rarity, 1);
    const count = Math.max(1, Math.round(motion.count));
    const title = count > 1 ? `${group.name} x${count}` : group.name;

    ctx.save();
    ctx.globalAlpha *= clamp(motion.alpha, 0, 1);
    drawOutlinedText(title, geometry.x + geometry.width * 0.5, motion.y - geometry.labelGap, {
      font: `900 ${geometry.titleFontSize}px ${UI_FONT_FAMILY}`,
      fill: "#f7f7f7",
      lineWidth: bossBarTitleOutlinePx * geometry.scale,
      baseline: "bottom",
    });
    drawLayeredCapsuleBar(
      geometry.x,
      motion.y,
      geometry.width,
      geometry.height,
      motion.members,
      group.invincible ? "#e8d64b" : "#69d33e",
      bossBarRadiusPx * geometry.scale,
    );
    if (motion.shieldAlpha > 0.001) {
      const shieldLength = geometry.width * 0.82;
      const shieldHeight = geometry.height * 0.48;
      ctx.save();
      ctx.globalAlpha *= clamp(motion.shieldAlpha, 0, 1);
      drawLayeredSolidProgressBar(
        geometry.x + (geometry.width - shieldLength) * 0.5,
        motion.y + (geometry.height - shieldHeight) * 0.5,
        shieldLength,
        shieldHeight,
        motion.shieldMembers,
        "#ffffff",
      );
      ctx.restore();
    }
    drawOutlinedText(
      rarityName(group.rarity),
      geometry.x + geometry.width * 0.5,
      motion.y + geometry.height + geometry.labelGap,
      {
        font: `900 ${geometry.rarityFontSize}px ${UI_FONT_FAMILY}`,
        fill: rarityFill,
        lineWidth: bossBarRarityOutlinePx * geometry.scale,
        baseline: "top",
      },
    );
    ctx.restore();
  }
}

function drawActorPortrait(entity, position, radius, owner = false) {
  const snap = entity.snapshot;
  if (snap.entityType === playerFlowerType) {
    drawPlayerFlower(snap, position, radius,
      (snap.flags & flagDead) !== 0 ? (entity.deathAngle ?? entity.renderAngle ?? snap.angle) : (entity.renderAngle ?? snap.angle),
      owner, true);
    return;
  }
  ctx.save();
  ctx.beginPath(); ctx.arc(position.x, position.y, radius * 1.2, 0, Math.PI * 2); ctx.clip();
  drawEntity(entity, { visualOnly: true, screenPosition: position, screenRadius: radius * 0.85 });
  ctx.restore();
}

function mobileUiScale() {
  // 只调整画布装饰，触摸目标尺寸独立。
  if (!document.body.classList.contains("mobile-ui")) return 1;
  const vw = state.canvasWidth || window.innerWidth;
  return clamp(vw / 678, 0.566, 0.848);
}

function minimapLayout(mode = state.minimapMode) {
  const layout = getGameUiLayout();
  return mode === "full" ? layout.fullMinimap : layout.minimap;
}

// 小地图下显示本地化地图名；没有译名时格式化资源名。
const minimapCardRadiusPx = 5;
const minimapCardBorderPx = 3;
const minimapLabelFontPx = 13;
const minimapLabelGapPx = 3;

function minimapAreaLabel() {
  const raw = String(state.mapDisplayName || state.mapName || "");
  if (!raw) return "";
  const slug = raw
    .split(/[\\/]/)
    .pop()
    .replace(/\.[^.]+$/, "")
    .toLowerCase();
  if (!slug) return "";
  const pretty = slug
    .split(/[_\-\s]+/)
    .filter(Boolean)
    .map((word) => word.charAt(0).toUpperCase() + word.slice(1))
    .join(" ");
  return t(`maps.${slug}`, {}, { defaultValue: pretty });
}

function drawMinimapCard(layout, scale) {
  const radius = minimapCardRadiusPx * scale;
  const border = minimapCardBorderPx * scale;
  ctx.fillStyle = "rgba(0, 0, 0, 0.42)";
  ctx.beginPath();
  ctx.roundRect(
    layout.x - border,
    layout.y - border,
    layout.size + border * 2,
    layout.size + border * 2,
    radius + border,
  );
  ctx.fill();
}

function drawMinimapLabel(layout, scale) {
  const label = minimapAreaLabel();
  if (!label) return;
  const fontSize = minimapLabelFontPx * scale;
  drawOutlinedText(
    label,
    layout.x + layout.size * 0.5,
    layout.y + layout.size + minimapCardBorderPx * scale + minimapLabelGapPx * scale,
    {
      font: `900 ${fontSize}px ${UI_FONT_FAMILY}`,
      fill: "#ffffff",
      stroke: "#050506",
      lineWidth: Math.max(1.5, fontSize * 0.28),
      align: "center",
      baseline: "top",
    },
  );
}

function minimapWorldBounds(map, mode = state.minimapMode) {
  const mapWidth = Math.max(
    1,
    (map.width || map.layers?.[0]?.width || 1) * map.tileWidth,
  );
  const mapHeight = Math.max(
    1,
    (map.height || map.layers?.[0]?.height || 1) * map.tileHeight,
  );
  if (mode === "full") {
    const side = Math.max(mapWidth, mapHeight);
    return { left: 0, top: 0, width: side, height: side };
  }

  const tileSize = Math.max(map.tileWidth || 1, map.tileHeight || 1);
  const side = Math.max(tileSize * 14, effectiveViewRadius() * 2.35, tileSize);
  return {
    left: state.camera.x - side * 0.5,
    top: state.camera.y - side * 0.5,
    width: side,
    height: side,
  };
}

function drawMinimap(now = performance.now()) {
  const map = state.map;
  if (!map || !map.tileWidth || !map.tileHeight) {
    minimapHitRect = null;
    minimapTransition = null;
    return;
  }

  const view = currentMinimapView(map, now);
  minimapHitRect = view.layout;
  drawMinimapLayer(map, view);
}

function currentMinimapView(map, now) {
  if (minimapTransition?.map !== map) minimapTransition = null;
  const target = { layout: minimapLayout(), bounds: minimapWorldBounds(map), detail: state.minimapMode === "full" ? 1 : 0 };
  const { view, complete } = sampleMinimapTransition(minimapTransition, target, now, minimapTransitionDurationMs);
  if (complete) minimapTransition = null;
  return view;
}

function drawMinimapLayer(map, { layout, bounds, detail }) {
  const scale = layout.size / Math.max(bounds.width, bounds.height, 1);
  const s = getGameUiLayout().scale;
  const cache = getMinimapCache(map);

  ctx.save();
  drawMinimapCard(layout, s);
  drawMinimapLabel(layout, s);
  ctx.beginPath();
  ctx.roundRect(
    layout.x,
    layout.y,
    layout.size,
    layout.size,
    minimapCardRadiusPx * s,
  );
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
    ctx.lineWidth = (1.8 - 0.3 * detail) * s;
    ctx.beginPath();
    ctx.arc(
      teammateX,
      teammateY,
      (4 - 0.5 * detail) * s,
      0,
      Math.PI * 2,
    );
    ctx.fill();
    ctx.stroke();

    if (detail > 0) {
      ctx.save(); ctx.globalAlpha *= detail;
      drawOutlinedText(
        partyMemberForEntity(state, teammate.snapshot)?.name || t("party.player"),
        teammateX,
        teammateY - 6,
        {
          font: `800 10px ${UI_FONT_FAMILY}`,
          fill: "#f17cab",
          stroke: "rgba(26, 21, 27, 0.94)",
          lineWidth: 3,
          baseline: "bottom",
        },
      );
      ctx.restore();
    }
  }

  for (const entity of state.entities.values()) {
    const snap = entity?.snapshot;
    if (!snap || snap.entityType !== portalType) continue;
    const portalPos = entity.renderPos || snap.pos;
    if (!portalPos) continue;
    const portalX = layout.x + (portalPos.x - bounds.left) * scale;
    const portalY = layout.y + (portalPos.y - bounds.top) * scale;
    if (
      portalX < layout.x ||
      portalX > layout.x + layout.size ||
      portalY < layout.y ||
      portalY > layout.y + layout.size
    )
      continue;

    ctx.fillStyle = "#ffd84f";
    ctx.strokeStyle = "rgba(45, 24, 38, 0.88)";
    ctx.lineWidth = (1.8 - 0.3 * detail) * s;
    ctx.beginPath();
    ctx.arc(
      portalX,
      portalY,
      (4 - 0.5 * detail) * s,
      0,
      Math.PI * 2,
    );
    ctx.fill();
    ctx.stroke();
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
    ctx.arc(px, py, (4 - detail) * s, 0, Math.PI * 2);
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
    map.terrainRevision || 0,
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
  // Tiled 图层从下往上排列，后面的图层覆盖前面的图层。
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
  const now = performance.now();
  const map = state.map;
  const from = map ? currentMinimapView(map, now) : null;
  const to = state.minimapMode === "full" ? "local" : "full";
  state.minimapMode = to;
  minimapTransition = from ? { from, map, startedAt: now } : null;
  requestRedraw();
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

  const radius =
    Math.max(0, snap.radius || 0) *
    Math.max(3, getFlowerMobCullScale(snap.entityType)) *
    Math.max(1, frameEntityAppearance(entity).scale);
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

function drawEntityWithHallucinationGhosts(entity, hallucinationOwner) {
  if (hallucinationOwner)
    drawEntityHallucinationGhosts(entity, hallucinationOwner);
  drawEntity(entity);
}

function drawEntityPass(entities, hallucinationOwner = null) {
  if (benchPhaseTimes) {
    for (const entity of entities) {
      const entityStart = performance.now();
      drawEntityWithHallucinationGhosts(entity, hallucinationOwner);
      const key = `t${entity.snapshot?.entityType ?? -1}`;
      benchPhaseTimes.set(
        key,
        (benchPhaseTimes.get(key) || 0) + (performance.now() - entityStart),
      );
    }
    return;
  }
  for (const entity of entities)
    drawEntityWithHallucinationGhosts(entity, hallucinationOwner);
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
    if (!(radius > 0) || !Number.isFinite(radius)) continue;
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

function estimatedServerTick(
  now = performance.now(),
  fixedDtSeconds = state.serverTickInterval || serverFixedDt,
) {
  if (!Number.isSafeInteger(state.serverTick) || state.serverTick < 0)
    return null;
  const interval = Math.max(0.000001, fixedDtSeconds);
  const elapsed = Math.max(0, (now - state.serverTickReceivedAt) / 1000);
  return state.serverTick + elapsed / interval;
}

function applyWeatherSmoothing(dt) {
  if (!state.environment?.received) return;
  const intensities = state.environment.weatherIntensity || [];
  const targetRain = Math.pow(clamp(intensities[2] || 0, 0, 1), 1.12);
  const targetFog = Math.pow(clamp(intensities[3] || 0, 0, 1), 1.4);
  const targetCloudy = Math.pow(clamp(intensities[1] || 0, 0, 1), 1.2);
  if (dt <= 0) return;
  const k = 1 - Math.exp(-2.4 * dt);
  actualRain += (targetRain - actualRain) * k;
  actualFog += (targetFog - actualFog) * k;
  actualCloudyTint += (targetCloudy - actualCloudyTint) * k;
}

function currentTimeOfDay(now = performance.now()) {
  const environment = state.environment;
  if (!environment?.received) {
    // 尚未收到服务端时间时按白天 08:00 显示。
    return 8 / 24;
  }
  const dayDuration = Math.max(0.001, environment.dayDurationSeconds || 21600);
  const fixedDt = Math.max(
    0.000001,
    environment.fixedDtSeconds || serverFixedDt,
  );
  const tick = estimatedServerTick(now, fixedDt);
  const elapsed = Number.isFinite(tick)
    ? (tick - environment.anchorServerTick) *
      fixedDt
    : Math.max(0, (now - environment.receivedAt) / 1000);
  const time = environment.timeOfDay + elapsed / dayDuration;
  return ((time % 1) + 1) % 1;
}

function darknessAt(timeOfDay) {
  const t = ((timeOfDay % 1) + 1) % 1;
  const dawnStart = 6 / 24;
  const dayStart = 8 / 24;
  const duskStart = 18 / 24;
  const nightStart = 20 / 24;
  if (t >= dayStart && t < duskStart) return 0;
  if (t >= dawnStart && t < dayStart)
    return 0.6 * (1 - (t - dawnStart) / (dayStart - dawnStart));
  if (t >= duskStart && t < nightStart)
    return 0.6 * ((t - duskStart) / (nightStart - duskStart));
  return 0.6;
}

function duskFilterStrength(timeOfDay) {
  const t = ((timeOfDay % 1) + 1) % 1;
  const start = 17 / 24;
  const peak = 19 / 24;
  const end = 21 / 24;
  if (t < start || t >= end) return 0;
  if (t < peak) return (t - start) / (peak - start);
  return 1 - (t - peak) / (end - peak);
}

function isLightFlower(entity) {
  return entity?.snapshot?.entityType === playerFlowerType;
}

function ensureDiurnalMask() {
  const width = Math.max(1, Math.round(state.canvasWidth * state.dpr));
  const height = Math.max(1, Math.round(state.canvasHeight * state.dpr));
  if (!diurnalMaskCanvas) {
    diurnalMaskCanvas = document.createElement("canvas");
    diurnalMaskContext = diurnalMaskCanvas.getContext("2d");
  }
  if (!diurnalLightCanvas) {
    diurnalLightCanvas = document.createElement("canvas");
    diurnalLightContext = diurnalLightCanvas.getContext("2d");
  }
  if (!diurnalMaskContext || !diurnalLightContext) return null;
  if (diurnalMaskCanvas.width !== width) diurnalMaskCanvas.width = width;
  if (diurnalMaskCanvas.height !== height) diurnalMaskCanvas.height = height;
  if (diurnalLightCanvas.width !== width) diurnalLightCanvas.width = width;
  if (diurnalLightCanvas.height !== height) diurnalLightCanvas.height = height;
  return {
    canvas: diurnalMaskCanvas,
    context: diurnalMaskContext,
    lightCanvas: diurnalLightCanvas,
    lightContext: diurnalLightContext,
  };
}

const LIGHT_RING_FADE_MS = 200;
const lightRings = new Map();

function drawMobLightMask(lightContext, darknessAlpha, now) {
  if (darknessAlpha <= 0 || !state.map) return;
  const tileRadius = Math.max(
    1,
    worldLengthToScreen(
      Math.max(state.map.tileWidth || 512, state.map.tileHeight || 512),
    ),
  );
  const tileWorld = Math.max(
    state.map.tileWidth || 512,
    state.map.tileHeight || 512,
  );
  // 光环按实体半径缩放；普通萤火虫的范围为玩家光环的一半。
  const fireflyBaseRadius = 12;
  const flowerInnerRatio = 8 / 9;
  const flowerEraseAlpha = 0.125;
  const fireflyInnerRatio = 0.5;
  const fireflyEraseAlpha = 0.18;
  // 灯泡探测半径按稀有度增加，世界单位转原始坐标乘二，光环需匹配权威探测范围。
  const bulbDetectRadiusWorldUnits = 300;
  const bulbDetectRadiusStepWorldUnits = 300;

  // 光环按实体登记并跟随位置，出现/退出分别用 0.2 秒渐变。
  const expected = [];
  for (const entity of state.entities.values()) {
    if (entity.dying) continue;
    const worldPos = entity.renderPos || entity.snapshot?.pos;
    if (!worldPos) continue;
    const isFlower = isLightFlower(entity);
    const isFireflyLarvaTail =
      !isFlower &&
      entity.snapshot?.entityType === fireflyLarvaType &&
      entityVisualVariantFromFlags(entity.snapshot.flags) === FireflyLarvaVisualRole.Tail;
    const isFirefly =
      !isFlower &&
      (entity.snapshot?.entityType === fireflyType ||
        entity.snapshot?.entityType === magicFireflyType ||
        isFireflyLarvaTail);
    const isBulb =
      !isFlower &&
      !isFirefly &&
      entity.snapshot &&
      petalTypeFromSnap(entity.snapshot) === petalBulbType;
    if (!isFlower && !isFirefly && !isBulb) continue;
    const worldRadius = Math.max(0, Number(entity.snapshot?.radius) || 0);
    if (worldRadius <= 0) continue;
    const bulbDetectRaw =
      (bulbDetectRadiusWorldUnits +
        bulbDetectRadiusStepWorldUnits * (rarityLevel(entity.snapshot.rarity) - 1)) *
      2;
    expected.push({
      id: entity.snapshot?.entityId,
      x: worldPos.x,
      y: worldPos.y,
      // 灯泡按探测范围缩放，花朵和萤火虫按瓦片单位缩放。
      targetRadius: isBulb
        ? bulbDetectRaw
        : isFlower
          ? defaultPlayerLightRadius(state.map)
          : tileWorld * (worldRadius / fireflyBaseRadius) * 0.5,
      innerRatio: isBulb ? flowerInnerRatio : isFlower ? flowerInnerRatio : fireflyInnerRatio,
      eraseAlpha: isBulb ? flowerEraseAlpha : isFlower ? flowerEraseAlpha : fireflyEraseAlpha,
    });
  }

  const expectedIds = new Set();
  for (const ring of expected) {
    const existing = lightRings.get(ring.id);
    if (existing) {
      existing.x = ring.x;
      existing.y = ring.y;
      existing.targetRadius = ring.targetRadius;
      existing.innerRatio = ring.innerRatio;
      existing.eraseAlpha = ring.eraseAlpha;
      if (existing.state === "stable") existing.radius = ring.targetRadius;
    } else {
      lightRings.set(ring.id, {
        x: ring.x,
        y: ring.y,
        targetRadius: ring.targetRadius,
        innerRatio: ring.innerRatio,
        eraseAlpha: ring.eraseAlpha,
        radius: 0,
        state: "in",
        phaseStart: now,
      });
    }
    expectedIds.add(ring.id);
  }
  for (const [id, ring] of lightRings) {
    if (expectedIds.has(id)) continue;
    if (ring.state === "out") {
      if ((now - ring.phaseStart) / LIGHT_RING_FADE_MS >= 1)
        lightRings.delete(id);
    } else {
      ring.state = "out";
      ring.phaseStart = now;
      ring.fromRadius = ring.radius;
    }
  }

  lightContext.save();
  lightContext.filter = "none";
  lightContext.globalCompositeOperation = "lighter";
  for (const ring of lightRings.values()) {
    if (ring.state === "in") {
      const progress = Math.min(1, (now - ring.phaseStart) / LIGHT_RING_FADE_MS);
      ring.radius = ring.targetRadius * progress;
    } else if (ring.state === "out") {
      const progress = Math.min(1, (now - ring.phaseStart) / LIGHT_RING_FADE_MS);
      ring.radius = ring.fromRadius * (1 - progress);
    }
    const pos = worldToScreen(ring);
    const outerRadius = worldLengthToScreen(ring.radius);
    const innerRadius = outerRadius * ring.innerRatio;
    if (outerRadius <= 0) continue;
    if (
      pos.x + outerRadius < 0 ||
      pos.y + outerRadius < 0 ||
      pos.x - outerRadius > state.canvasWidth ||
      pos.y - outerRadius > state.canvasHeight
    )
      continue;
    lightContext.fillStyle = `rgba(0, 0, 0, ${ring.eraseAlpha})`;
    lightContext.beginPath();
    lightContext.arc(pos.x, pos.y, innerRadius, 0, Math.PI * 2);
    lightContext.fill();
    lightContext.beginPath();
    lightContext.arc(pos.x, pos.y, outerRadius, 0, Math.PI * 2);
    lightContext.fill();
  }
  lightContext.restore();
}

function drawDiurnalLighting(now = performance.now()) {
  const timeOfDay = currentTimeOfDay(now);
  // 阴天使用独立色罩，不复用昼夜黑暗遮罩。
  const darknessAlpha = darknessAt(timeOfDay);
  const duskAlpha = duskFilterStrength(timeOfDay) * 0.08;
  if (darknessAlpha > 0.001) {
    const mask = ensureDiurnalMask();
    if (!mask) return;
    const maskContext = mask.context;
    maskContext.save();
    maskContext.setTransform(1, 0, 0, 1, 0, 0);
    maskContext.clearRect(0, 0, mask.canvas.width, mask.canvas.height);
    maskContext.restore();
    const lightContext = mask.lightContext;
    lightContext.save();
    lightContext.setTransform(1, 0, 0, 1, 0, 0);
    lightContext.clearRect(0, 0, mask.lightCanvas.width, mask.lightCanvas.height);
    lightContext.restore();

    maskContext.save();
    maskContext.setTransform(state.dpr, 0, 0, state.dpr, 0, 0);
    maskContext.filter = "none";
    maskContext.globalAlpha = 1;
    maskContext.globalCompositeOperation = "source-over";
    // 雾直接改变黑暗色，不再单独叠加雾层。
    const fogStrength = clamp(actualFog, 0, 1);
    maskContext.fillStyle = `rgba(${Math.round(208 * fogStrength)}, ${Math.round(216 * fogStrength)}, ${Math.round(226 * fogStrength)}, ${darknessAlpha})`;
    maskContext.fillRect(0, 0, state.canvasWidth, state.canvasHeight);
    maskContext.restore();

    lightContext.save();
    lightContext.setTransform(state.dpr, 0, 0, state.dpr, 0, 0);
    drawMobLightMask(lightContext, darknessAlpha, now);
    lightContext.restore();

    maskContext.save();
    maskContext.setTransform(1, 0, 0, 1, 0, 0);
    maskContext.globalCompositeOperation = "destination-out";
    maskContext.drawImage(mask.lightCanvas, 0, 0);
    maskContext.restore();

    ctx.save();
    ctx.filter = "none";
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = "source-over";
    ctx.drawImage(
      mask.canvas,
      0,
      0,
      mask.canvas.width,
      mask.canvas.height,
      0,
      0,
      state.canvasWidth,
      state.canvasHeight,
    );
    ctx.restore();
  }

  if (duskAlpha > 0.001) {
    ctx.save();
    ctx.filter = "none";
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = "source-over";
    ctx.fillStyle = `rgba(255, 138, 70, ${duskAlpha})`;
    ctx.fillRect(0, 0, state.canvasWidth, state.canvasHeight);
    ctx.restore();
  }
}

// 雨使用稳定的平行细线和轻微氛围色，避免高刷新率下逐帧生成/剔除粒子。
function drawWeatherEffects(now = performance.now()) {
  if (!state.environment?.received) return;
  const dt = Math.min(0.1, Math.max(0, (now - lastWeatherEffectsAt) / 1000));
  lastWeatherEffectsAt = now;
  if (dt <= 0) return;
  applyWeatherSmoothing(dt);
  const env = state.environment;
  rainField.step(dt);

  ctx.save();
  ctx.filter = "none";
  ctx.globalAlpha = 1;
  ctx.globalCompositeOperation = "source-over";

  const atmosphereAlpha = clamp(
    actualCloudyTint * 0.045 + actualRain * 0.07,
    0,
    0.12,
  );
  if (atmosphereAlpha > 0.001) {
    ctx.fillStyle = `rgba(61, 71, 72, ${atmosphereAlpha})`;
    ctx.fillRect(0, 0, state.canvasWidth, state.canvasHeight);
  }

  const owner = state.entities.get(state.ownerEntityId);
  const ownerPosition = owner?.renderPos || owner?.snapshot?.pos;
  const fogCenter = ownerPosition
    ? worldToScreen(ownerPosition)
    : { x: state.canvasWidth * 0.5, y: state.canvasHeight * 0.5 };
  blockFogField.draw(
    ctx,
    state.canvasWidth,
    state.canvasHeight,
    actualFog,
    fogCenter,
  );

  rainField.draw(
    ctx,
    state.canvasWidth,
    state.canvasHeight,
    actualRain,
    env.windDirection,
    env.windSpeed,
  );

  ctx.restore();

  const tintAlpha = clamp(actualRain * 0.028, 0, 0.04);
  if (tintAlpha > 0.005) {
    ctx.save();
    ctx.filter = "none";
    ctx.globalCompositeOperation = "source-over";
    ctx.globalAlpha = 1;
    ctx.fillStyle = `rgba(170, 184, 184, ${tintAlpha})`;
    ctx.fillRect(0, 0, state.canvasWidth, state.canvasHeight);
    ctx.restore();
  }
}

const menuBackdrop = createMenuBackdrop({
  getPetalImage: (type, rarity) => livePetalIconImage(type, rarity),
  isEnabled: () => getClientConfigValue(state.clientConfig, 'menu_petals') >= .5,
});

export function drawScene(now = performance.now()) {
  noteFrameDrawn(now);
  currentRenderFrameId = (currentRenderFrameId + 1) >>> 0;
  setSpriteRasterFrameTime(now);
  if (devDebugUi?.isOpen()) {
    if (!state.connected || !state.authenticated) devDebugUi.close();
    else {
      try { devDebugUi.render(now); }
      catch (error) { devDebugUi.close(); addConsoleLine(`Dev Debug render: ${error.message}`, "error"); }
      return;
    }
  }
  if (!state.authenticated && !benchRun) {
    menuBackdrop.draw(ctx, { width: state.canvasWidth, height: state.canvasHeight, dpr: state.dpr,
      scale: getGameUiLayout().scale, now });
    return;
  }
  currentRenderTimeSeconds = now / 1000;
  let phaseStart = benchPhaseTimes ? performance.now() : 0;
  const mark = benchPhaseTimes
    ? (name) => {
        const phaseEnd = performance.now();
        benchPhaseTimes.set(
          name,
          (benchPhaseTimes.get(name) || 0) + (phaseEnd - phaseStart),
        );
        phaseStart = phaseEnd;
      }
    : null;

  updateBloodSacrificeScreenShake(now);
  if (getClientConfigValue(state.clientConfig, "screen_shake") < 0.5) state.screenShake = { x: 0, y: 0 };
  drawGrid(now);
  mark?.("grid");
  const scale = worldScale();
  const passes = collectSceneRenderPasses({
    scale,
    isEntityInRenderView,
    isBoss: (snap) => raritySortRank(snap.rarity) >= raritySortRank(bossRarity),
    passes: gameRenderPasses,
  });
  currentVisibleEntityCount = countScenePassEntities(passes);
  currentRenderLoad = renderLoadForVisibleCount(currentVisibleEntityCount);
  currentHurtFlashSprites.length = 0;
  currentHurtFlashOverlays.length = 0;
  prepareFrameRenderCaches(passes);
  const hallucinationOwner = activeOwnerHallucinationEntity();
  mark?.("collect");

  if (state.authenticated && !ui.mobileControls?.isMobileMode() && state.movementMode === "mouse" && state.mouse.seen && !state.mouse.inUi &&
      !isMainPanelOpen() && !state.consoleOpen && !isOwnerDead() &&
      getClientConfigValue(state.clientConfig, "movement_arrow") >= 0.5) {
    const owner = state.entities.get(state.ownerEntityId);
    if (owner?.renderPos) drawMovementGuide(ctx, worldToScreen(owner.renderPos), state.mouse, worldLengthToScreen(owner.snapshot.radius));
  }

  drawEntityPass(passes.ground, hallucinationOwner);
  drawEntityPass(passes.underlay, hallucinationOwner);
  drawEntityPass(passes.world, hallucinationOwner);
  mark?.("world");
  drawEntityPass(passes.petals, hallucinationOwner);
  drawEntityPass(passes.overlays, hallucinationOwner);
  mark?.("petals");
  // 密道覆盖其他生物，本地玩家保持在密道表面之上；cover 仍按揭示范围遮盖玩家。
  ui.mapRenderer?.drawShortcutSurface(now);
  mark?.("shortcut");
  if (passes.owner)
    drawEntityWithHallucinationGhosts(passes.owner, hallucinationOwner);
  mark?.("owner");
  mark?.("hurtFlash");
  drawParticlePass();
  burrowParticles.draw(ctx, scale, worldToScreen({ x: 0, y: 0 }));
  ui.mapRenderer?.drawShortcutCover(now);
  if (state.weatherEnabled) {
    drawDiurnalLighting(now);
    drawWeatherEffects(now);
  }
  drawLightningChains(now);
  drawBloodSacrificeVisionDarkness();
  drawCheckpointZoneOutlines();
  mark?.("weather");
  drawSceneHitboxes(passes);
  combatFeedback.draw(ctx, now, worldToScreen, drawOutlinedText,
    getClientConfigValue(state.clientConfig, "damage_numbers") >= 0.5);
  drawBossBars(passes.bosses, now);
  mark?.("bossBars");
  playerHud.drawSelf(now);
  playerHud.drawParty(now);
  mark?.("hud");
  if (state.authenticated) drawMinimap(now);
  updateDeathOverlay();
  if (isOwnerDead() && !state.deathOverlayClosed) drawDeathPortrait(deathPortrait, ownerDeathSummary, now, {
    getImage: (type, rarity) => livePetalIconImage(type, rarity),
    getCopyCount: (type, rarity) => effectivePetalCardCopy(type, rarity),
    getAttachmentImage: kind => {
      if (kind === "bandage") return bandageOverlayImage();
      if (kind === "antennae") return antennaeOverlayImage();
      if (kind === "douli") return douliOverlayImage();
      return null;
    },
  });
  mark?.("minimap");
}

// 渲染基准在登录地图生成确定场景，测 drawScene 的 CPU 耗时。
// 控制台使用 bench；自动化可设置 bench/benchFrames 参数并等待标题 BENCH_DONE。
let benchRun = null;
let benchPhaseTimes = null;

function benchRng(seed) {
  let a = seed >>> 0;
  return () => {
    a += 0x6d2b79f5;
    let t0 = a;
    t0 = Math.imul(t0 ^ (t0 >>> 15), t0 | 1);
    t0 ^= t0 + Math.imul(t0 ^ (t0 >>> 7), t0 | 61);
    return ((t0 ^ (t0 >>> 14)) >>> 0) / 4294967296;
  };
}

const benchMobTypes = [
  beetleType,
  beeType,
  hornetType,
  bumbleBeeType,
  spiderType,
  normalLadybugType,
  soldierAntType,
  babyAntType,
  workerAntType,
  queenAntType,
  rockType,
  bubbleType,
  sandstormType,
  dandelionType,
  fireflyType,
  magicFireflyType,
  dummyType,
];

const benchPetalTypes = [
  petalBasicType,
  petalStingerType,
  petalRoseType,
  petalCactusType,
  petalWingType,
  petalIrisType,
  petalMissileType,
  petalLeafType,
  petalSawbladeType,
  petalLightType,
];

function makeBenchEntity(rng, entityId, center, spread) {
  const roll = rng();
  const anchorX = center.x + (rng() * 2 - 1) * spread.x;
  const anchorY = center.y + (rng() * 2 - 1) * spread.y;
  const snap = {
    entityId,
    team: rng() < 0.5 ? 1 : 2,
    pos: { x: anchorX, y: anchorY },
    hpPercent: 0.35 + rng() * 0.65,
    shieldPercent: rng() < 0.25 ? rng() * 0.6 : 0,
    flags: rng() < 0.2 ? flagAttacking : rng() < 0.2 ? flagDefending : 0,
    angle: rng() * Math.PI * 2,
    rarity: 1 + Math.floor(rng() * 8),
    name: "",
    primarySlots: [],
    states: [],
    hallucinations: [],
  };

  if (roll < 0.12) {
    snap.entityType = playerFlowerType;
    snap.radius = 18 + rng() * 8;
    snap.name = `Bench${String(entityId % 97).padStart(2, "0")}`;
    for (let slot = 0; slot < 5; slot += 1) {
      snap.primarySlots.push({
        petalType: benchPetalTypes[Math.floor(rng() * benchPetalTypes.length)],
        rarity: 1 + Math.floor(rng() * 8),
        visualType: PetalSlotVisualType.None,
        copies: [],
      });
    }
  } else if (roll < 0.55) {
    snap.entityType = benchMobTypes[Math.floor(rng() * benchMobTypes.length)];
    snap.radius =
      snap.entityType === rockType || snap.entityType === sandstormType
        ? 26 + rng() * 34
        : snap.entityType === queenAntType
          ? 40 + rng() * 20
          : 14 + rng() * 18;
  } else if (roll < 0.85) {
    snap.entityType =
      NETWORK_PETAL_TYPE_OFFSET +
      benchPetalTypes[Math.floor(rng() * benchPetalTypes.length)];
    snap.radius = 6 + rng() * 6;
  } else {
    const petalType =
      benchPetalTypes[Math.floor(rng() * benchPetalTypes.length)];
    snap.entityType = NETWORK_DROP_ENTITY_TYPE;
    snap.team = petalType;
    snap.dropPetalType = petalType;
    snap.radius = 10 + rng() * 5;
  }

  return {
    snapshot: snap,
    anchor: { x: anchorX, y: anchorY },
    orbitRadius: 20 + rng() * 120,
    orbitSpeed: (0.2 + rng() * 1.1) * (rng() < 0.5 ? -1 : 1),
    orbitPhase: rng() * Math.PI * 2,
    spin: (rng() * 2 - 1) * 2.4,
  };
}

function startRenderBenchmark(options = {}) {
  if (benchRun) {
    addConsoleLine(t("commands.bench.alreadyRunning"), "error");
    return;
  }
  if (state.authenticated) {
    addConsoleLine(t("commands.bench.loggedOutOnly"), "error");
    return;
  }
  if (state.mapLoadPromise) {
    addConsoleLine(t("commands.bench.mapLoading"), "error");
    return;
  }

  const entityCount = clamp(
    Math.floor(Number(options.entities) || 240),
    10,
    2000,
  );
  const frames = clamp(Math.floor(Number(options.frames) || 600), 60, 100000);
  const rng = benchRng(0x5eed + entityCount);
  const center = { x: state.camera.x, y: state.camera.y };
  const spreadBase = Math.max(600, state.viewRadius || 1200);
  const spread = { x: spreadBase * 0.95, y: spreadBase * 0.7 };

  const run = {
    ids: [],
    entities: [],
    frames,
    warmupLeft: 60,
    samples: [],
    startedAt: performance.now(),
    lastFlashIndex: -1,
  };
  for (let index = 0; index < entityCount; index += 1) {
    const record = makeBenchEntity(rng, 60001 + index, center, spread);
    const now = performance.now() / 1000;
    state.entities.set(record.snapshot.entityId, {
      snapshot: record.snapshot,
      renderPos: { x: record.snapshot.pos.x, y: record.snapshot.pos.y },
      renderAngle: record.snapshot.angle,
      snapshotMove: 0,
      snapshotVelocity: { x: 0, y: 0 },
      snapshotAt: now,
      lastSnapshotAt: now,
      missingSnapshotCount: 0,
      playerPrimarySlotRarity:
        record.snapshot.entityType === playerFlowerType
          ? highestSlotRarity(record.snapshot.primarySlots)
          : 0,
      motionBlend: 0,
      dying: false,
      deathAge: 0,
      hurtFlashAge: hurtFlashDuration,
      hurtFlashReadyAt: 0,
      deathAngle: null,
      particleBurstNextAt: null,
      bloodSacrificeAngle: null,
      bloodSacrificeSweepNextAt: null,
      bloodSacrificeSweeps: [],
    });
    run.ids.push(record.snapshot.entityId);
    run.entities.push(record);
  }

  benchRun = run;
  benchPhaseTimes = new Map();
  document.title = `BENCH_RUNNING ${entityCount}`;
  addConsoleLine(
    t("commands.bench.started", { entities: entityCount, frames }),
  );
}

function benchFrameUpdate(now) {
  const run = benchRun;
  if (!run) return;
  const elapsed = (now - run.startedAt) / 1000;
  const snapshotNow = now / 1000;
  for (const record of run.entities) {
    const entity = state.entities.get(record.snapshot.entityId);
    if (!entity) continue;
    const angle = record.orbitPhase + elapsed * record.orbitSpeed;
    const snap = entity.snapshot;
    snap.pos.x = record.anchor.x + Math.cos(angle) * record.orbitRadius;
    snap.pos.y = record.anchor.y + Math.sin(angle) * record.orbitRadius;
    snap.angle = normalizeAngle(record.snapshot.angle + elapsed * record.spin);
    entity.renderPos.x = snap.pos.x;
    entity.renderPos.y = snap.pos.y;
    entity.renderAngle = snap.angle;
    entity.snapshotMove = 1.2;
    entity.snapshotAt = snapshotNow;
  }
  const flashIndex = Math.floor(elapsed * 10) % run.entities.length;
  if (flashIndex !== run.lastFlashIndex) {
    run.lastFlashIndex = flashIndex;
    const entity = state.entities.get(run.entities[flashIndex].snapshot.entityId);
    if (entity) entity.hurtFlashAge = 0;
  }
}

function benchRecordFrame(costMs) {
  const run = benchRun;
  if (!run) return;
  if (run.warmupLeft > 0) {
    run.warmupLeft -= 1;
    if (run.warmupLeft === 0) benchPhaseTimes = new Map();
    return;
  }
  run.samples.push(costMs);
  if (run.samples.length >= run.frames) finishRenderBenchmark();
}

function finishRenderBenchmark() {
  const run = benchRun;
  if (!run) return;
  benchRun = null;
  for (const id of run.ids) state.entities.delete(id);

  const samples = run.samples.slice().sort((a, b) => a - b);
  const pick = (q) =>
    samples[Math.min(samples.length - 1, Math.floor(samples.length * q))] || 0;
  const avg = samples.reduce((sum, value) => sum + value, 0) /
    Math.max(1, samples.length);
  const stats = {
    n: samples.length,
    avg: avg.toFixed(2),
    p50: pick(0.5).toFixed(2),
    p95: pick(0.95).toFixed(2),
    max: (samples[samples.length - 1] || 0).toFixed(2),
    fps: state.fps.toFixed(1),
    dpr: state.dpr.toFixed(2),
    px: `${Math.round(state.canvasWidth * state.dpr)}x${Math.round(state.canvasHeight * state.dpr)}`,
    visible: currentVisibleEntityCount,
  };
  const phases = Array.from(benchPhaseTimes || [])
    .map(([name, totalMs]) => [name, totalMs / Math.max(1, samples.length)])
    .sort((a, b) => b[1] - a[1])
    .map(([name, ms]) => `${name}:${ms.toFixed(2)}`)
    .join(",");
  benchPhaseTimes = null;
  const summary =
    `BENCH_DONE n=${stats.n} avg=${stats.avg} p50=${stats.p50} p95=${stats.p95} ` +
    `max=${stats.max} fps=${stats.fps} dpr=${stats.dpr} px=${stats.px} ` +
    `visible=${stats.visible} phases=${phases}`;
  document.title = summary;
  addConsoleLine(t("commands.bench.doneDraw", stats));
  addConsoleLine(t("commands.bench.doneFrame", stats));
  if (phases) addConsoleLine(`phases(ms/frame): ${phases}`);
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
  const held = heldInputSources();
  const inputMode =
    state.movementMode +
    (held.length ? `[${held.map((hold) => hold.key).join("+")}]` : "");
  const socketState = state.ws
    ? ["connecting", "open", "closing", "closed"][state.ws.readyState] ||
      state.ws.readyState
    : "none";

  debugInfo.textContent = [
    `pos (${debugNumber(ownerPos?.x)}, ${debugNumber(ownerPos?.y)})  id ${state.ownerEntityId || "-"}  hp ${debugNumber(ownerSnap?.hpPercent * 100, 0)}%`,
    `cam (${debugNumber(state.camera.x)}, ${debugNumber(state.camera.y)})  mouse ${mouseWorld ? `(${debugNumber(mouseWorld.x)}, ${debugNumber(mouseWorld.y)})` : "(-, -)"}`,
    `view ${debugNumber(state.viewRadius, 0)} (${viewZoomPercent()}%)  scale ${debugNumber(worldScale(), 4)}  fps ${debugNumber(state.fps, 1)}  dpr ${debugNumber(state.dpr, 2)}`,
    `entities ${state.entities.size}/${currentVisibleEntityCount}  render load ${currentRenderLoad}  map ${map?.path || "-"}  chunks ${chunkCache}/${visibleChunks} q${chunkQueue} ${chunkProfile}  raster ${(sceneRasterBudget.bytes / 1048576).toFixed(1)}/${sceneRasterBudget.limitBytes / 1048576}MiB  input ${inputMode}  ws ${socketState}`,
  ].join("\n");
}

function tick(now) {
  const frameStart = performance.now();
  const elapsed = Math.max(0, (now - state.lastFrameTime) / 1000);
  const timing = getRenderFrameTiming(elapsed);
  const dt = timing.dt;
  state.lastFrameTime = now;
  if (dt > 0) {
    const instantFps = 1 / Math.max(elapsed, 0.001);
    state.fps =
      state.fps <= 0 ? instantFps : state.fps + (instantFps - state.fps) * 0.08;
  }
  flushPendingSnapshot();
  if (benchRun) benchFrameUpdate(now);
  updateViewRadius(now);
  updateViewZoom(dt);
  ui.mobileControls?.setBlocked(state.devDebugOpen || isMainPanelOpen() || state.chatOpen ||
    state.consoleOpen || isEmailBindingPanelOpen() || isTyping() || isOwnerDead());
  flushInput(dt);
  updateParticles(dt);
  burrowParticles.step(dt);
  updateRenderPositions(dt);
  ui.mysteryAudio.updateProximity({
    enabled: state.authenticated,
    owner: state.entities.get(state.ownerEntityId),
    entities: state.entities.values(),
    viewRadius: state.viewRadius,
  });
  if (!state.authenticated || state.devDebugOpen || !state.mapLoadPromise) {
    const drawStart = performance.now();
    drawScene(now);
    const drawCostMs = performance.now() - drawStart;
    noteSceneDrawCost(drawCostMs);
    if (benchRun) benchRecordFrame(drawCostMs);
  }
  updateChatVisibility(now);
  updateDebugInfo();
  noteRenderFrameCost(performance.now() - frameStart + takeRenderPreparationCost());
  if (!state.devDebugOpen) updateAdaptiveRenderScale(!!benchRun);

  requestAnimationFrame(tick);
}

function isUiTarget(target) {
  return target && target.closest && target.closest(".ui");
}

// 输入接口接收 bindings.js 计算的绝对移动/攻防状态。
const inputCtx = {
  setMoveVector(vector) {
    state.moveVector = { x: vector.x, y: vector.y };
  },
  setAttacking(active) {
    state.attacking = !!active;
  },
  setDefending(active) {
    state.defending = !!active;
  },
  toggleBackpack: () => toggleBackpack(),
  toggleCraft: () => toggleCraft(),
  toggleTalent: () => toggleTalent(),
  toggleCodex: () => toggleCodex(),
  toggleSettings: () => toggleSettings(),
  toggleConsole: () => toggleConsole(),
  toggleFullscreen: () => void toggleFullscreen(),
  toggleMinimap: () => toggleMinimapMode(),
  zoomIn: () => adjustViewZoomPercent(1),
  zoomOut: () => adjustViewZoomPercent(-1),
  closeTopPanel: () => closeTopPanel(),
  openChat: (prefix) => openChat(prefix),
  quickSwapAllSlots: () => quickSwapAllSlots(),
  saveActiveLoadoutPreset: () => saveActiveLoadoutPreset(),
  toggleAlt: () => {
    state.altFeaturesEnabled = !state.altFeaturesEnabled;
  },
  selectSlot: (index) => {
    state.selectedSlot = index;
    quickSwapSlot(index);
  },
  selectLoadoutPreset: (index) => selectLoadoutPreset(index),
};
initBindingContext(inputCtx);

// 关闭最上层窗口；无窗口可关时打开设置。
function closeTopPanel() {
  if (state.chatOpen) return closeChat();
  if (state.backpackOpen) return toggleBackpack(false);
  if (state.craftOpen) return toggleCraft(false);
  if (state.talentOpen) return toggleTalent(false);
  if (state.codexOpen) return toggleCodex(false);
  if (state.settingsOpen) return toggleSettings(false);
  return toggleSettings(true);
}

function setupEvents() {
  window.addEventListener("keydown", () => ui.mysteryAudio.unlock(), {
    capture: true,
  });
  window.addEventListener("resize", () => {
    resizeCanvas(); slotSwapMotion.cancelAll({ notify: true }); syncUiLayout();
  });
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) slotSwapMotion.cancelAll({ notify: true });
  });
  window.addEventListener("scroll", () => slotSwapMotion.cancelAll({ notify: true }), { capture: true, passive: true });
  document.addEventListener("fullscreenchange", () => {
    state.fullscreen = !!document.fullscreenElement;
    saveSettings();
    if (state.settingsOpen) ui.settingsUi?.render();
  });
  bindMobileViewport({ onTyping: () => {
    releaseInputHolds(); ui.mobileControls?.reset();
    sendNeutralGameplayState({ includeInput: true });
  } });
  document.getElementById("mobileConsoleCloseBtn")?.addEventListener("click", () => toggleConsole(false));
  const slotCollapseBtn = document.getElementById("slotCollapseBtn");
  document.body.classList.toggle("slots-collapsed", state.slotPanelCollapsed);
  slotCollapseBtn?.setAttribute("aria-pressed", String(state.slotPanelCollapsed));
  slotCollapseBtn?.addEventListener("click", () => {
    state.slotPanelCollapsed = !state.slotPanelCollapsed;
    document.body.classList.toggle("slots-collapsed", state.slotPanelCollapsed);
    slotCollapseBtn.setAttribute(
      "aria-pressed",
      state.slotPanelCollapsed ? "true" : "false",
    );
    syncUiLayout();
  });
  connectBtn.addEventListener("click", connectAndAuth);
  sendCodeBtn.addEventListener("click", requestVerificationCode);
  bindEmailBtn.addEventListener("click", confirmEmailBinding);
  cancelEmailBindingBtn.addEventListener("click", cancelEmailBinding);
  openEmailBindingBtn.addEventListener("click", openEmailBinding);
  registerModeInput.addEventListener("change", () => {
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
  settingsCloseBtn.addEventListener("click", () => toggleSettings(false));
  craftOnceBtn.addEventListener("click", () => submitCraft());
  topSettingsBtn?.addEventListener("click", () => toggleSettings());
  codexBtn?.addEventListener("click", () => toggleCodex());
  codexCloseBtn?.addEventListener("click", () => toggleCodex(false));
  bindCodexDetailEvents();
  chatHint?.addEventListener("click", () => openChat(""));
  chatChannels?.addEventListener("change", ui.chatUi.handleFilterChange);
  window.addEventListener(
    "pointerdown",
    (event) => {
      if (!state.chatOpen) return;
      if (ui.chatUi.contains(event.target)) return;
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

  ui.mobileControls = createMobileControls({
    onLayoutChange: () => { slotSwapMotion.cancelAll({ notify: true }); syncUiLayout(); },
    isUiTarget,
    movementSurface: canvas,
    onAction: (action, phase) => executeBinding(mobileActionBindings, "touch", action, phase, {
      typing: isTyping(), now: performance.now(),
    }),
    onSurfaceTap: event => {
      if (!isMinimapPoint(eventCanvasPoint(event))) return false;
      toggleMinimapMode(); return true;
    },
  });
  ui.mobileControls.setMode(state.mobileControlMode);
  ui.mobileControls.setEnabled(state.authenticated);

  // 第二指接管缩放，不创建新摇杆或玩法点击；张开缩小视野百分比，合拢恢复。
  bindPinchGesture(canvas, {
    onStart: () => {
      ui.mobileControls?.reset();
      state.suppressClickUntil = performance.now() + 500;
    },
    onChange: ({ deltaScale }) => {
      if (!(deltaScale > 0) || !Number.isFinite(deltaScale)) return;
      setViewZoomPercent(
        normalizeViewZoomPercent(state.viewZoomPercent) / deltaScale,
        false,
      );
      state.suppressClickUntil = performance.now() + 300;
    },
    onEnd: () => {
      saveSettings();
      state.suppressClickUntil = performance.now() + 300;
    },
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
    if (event.key === "Escape" && state.accountOpen) {
      event.preventDefault(); if (!accountUi.back()) accountUi.panel.setOpen(false); return;
    }
    if (state.devDebugOpen) {
      if (event.key === "F1") { event.preventDefault(); devDebugUi?.focusConsole(); }
      return;
    }
    if (isEmailBindingPanelOpen()) {
      if (event.key === "Escape" && !state.emailBindingRequired) {
        event.preventDefault();
        cancelEmailBinding();
      }
      return;
    }
    const key = inputKeyFromEvent(event);
    // 文本输入期间仅允许 whileTyping 命令。
    const executed = executeBinding(bindings, "keyboard", inputKeyCandidates(event), "down", {
      repeat: event.repeat,
      typing: isTyping(),
      now: event.timeStamp,
    });
    if (executed > 0) {
      // 阻止已绑定按键触发浏览器滚动和菜单。
      if (
        key === "space" ||
        key === "arrowup" ||
        key === "arrowdown" ||
        key === "arrowleft" ||
        key === "arrowright" ||
        key === "shift" ||
        key === "alt" ||
        key === "/" ||
        key === "f1" ||
        /^[0-9]$/.test(key)
      ) {
        event.preventDefault();
      }
    }
  });

  window.addEventListener("keyup", (event) => {
    if (state.devDebugOpen) return;
    const key = inputKeyFromEvent(event);
    if (key === "alt") event.preventDefault();
    // 松键始终处理，避免打字期间吞掉释放而残留动作。
    executeBinding(bindings, "keyboard", inputKeyCandidates(event), "up", {
      now: event.timeStamp,
    });
  });

  window.addEventListener(
    "pointermove",
    (event) => {
      if (state.devDebugOpen) return;
      updateMousePointer(event);
      syncPetalInfoTooltipHover();
      updateDrag(event);
    },
    { passive: false },
  );
  window.addEventListener("pointerup", finishDrag);
  window.addEventListener("pointercancel", event => {
    if (state.drag?.pointerId === event.pointerId) clearDrag();
  });

  // 失焦和标签页隐藏时显式撤销全部按住来源。
  const releaseAllInput = () => {
    releaseInputHolds();
    state.digging = false;
    ui.mobileControls?.reset();
    sendNeutralGameplayState({ includeInput: true });
  };
  window.addEventListener("blur", releaseAllInput);
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden") { releaseAllInput(); burrowParticles.clear(); }
  });

  window.addEventListener("mousedown", (event) => {
    if (state.devDebugOpen) return;
    if (event.sourceCapabilities?.firesTouchEvents || ui.mobileControls?.isMobileMode()) return;
    updateMousePointer(event);
    if (shouldSuppressClick()) return;
    if (event.button === 0 && isMinimapPoint(eventCanvasPoint(event))) {
      toggleMinimapMode();
      event.preventDefault();
      return;
    }
    if (isUiTarget(event.target)) return;
    const buttonKey = mouseKeyFromButton(event.button);
    if (buttonKey)
      executeBinding(bindings, "mouse", buttonKey, "down", {
        typing: isTyping(),
        now: event.timeStamp,
      });
  });

  window.addEventListener("mouseup", (event) => {
    if (state.devDebugOpen) return;
    const buttonKey = mouseKeyFromButton(event.button);
    if (buttonKey)
      executeBinding(bindings, "mouse", buttonKey, "up", { now: event.timeStamp });
  });

  // 滚轮每格生成一次按下/释放对，以复用命令链。
  window.addEventListener(
    "wheel",
    (event) => {
      if (state.devDebugOpen) return;
      updateMousePointer(event);
      if (isUiTarget(event.target) || state.mouse.inUi) return;
      const wheelKey = event.deltaY < 0 ? "wheelup" : event.deltaY > 0 ? "wheeldown" : null;
      if (!wheelKey) return;
      const typing = isTyping();
      if (executeBinding(bindings, "mouse", wheelKey, "down", { typing }) > 0)
        event.preventDefault();
      executeBinding(bindings, "mouse", wheelKey, "up", {});
    },
    { passive: false },
  );

  window.addEventListener("contextmenu", (event) => {
    if (!isUiTarget(event.target)) event.preventDefault();
  });

  window.addEventListener("beforeunload", () => {
    pageUnloading = true;
    closeSocket(false);
  });
  window.addEventListener("storage", event => {
    if (event.key !== null && !event.key.startsWith("florrbt.auth-recovery.") && !event.key.startsWith("florrbt.account.")) return;
    const endpoint = state.ws?.url || wsUrlField.getValue({ trim: true });
    if ((state.authenticated || state.accountSession) && !authRecovery.token(endpoint, state.accountName)) logoutAccount();
    else accountUi.render();
  });
  wsUrlInput.addEventListener("change", () => accountUi.reset());
}

function slotIndexFromKey(key) {
  if (/^[1-9]$/.test(key)) return Number(key) - 1;
  if (key === "0") return 9;
  return null;
}

document.title = appDisplayName;
const appVersionElement = document.getElementById("appVersion");
if (appVersionElement) appVersionElement.textContent = `v${appVersion}`;

function warmPetalImage(image) {
  return whenVisualReady(image);
}

// 只预热共用/专用花瓣视觉，普通变体按需创建。
function warmPetalCardImages() {
  const jobs = [];
  // Moon 世界渲染与卡片共用源图，提前解码以避免帧内降级。
  jobs.push(warmPetalImage(assetImage(moonSvgGeometry.source)), prepareMoonSurface());
  for (const src of [getFlowerBodyTexturePath("mecha"), getFlowerBodyTexturePath("normal"), "./assets/trap.svg?v=2"])
    jobs.push(warmPetalImage(assetImage(src)));
  // 加载时开始共用装备图变换，避免游戏中首次装备才测量 SVG。
  jobs.push(...[bandageOverlayImage(), antennaeOverlayImage(), douliOverlayImage(), thirdEyeOverlayImage()]
    .map(warmPetalImage));
  jobs.push(warmPetalImage(transformedPetalImage(getPetalIconPath(petalMimicType), extractMimicLabelSvg, "mimic-label")));
  for (const [type, crop, key] of [
    [petalAntEggType, extractSingleAntEggLiveSvg, "single-ant-egg"],
    [petalWaxType, extractWaxLiveSvg, "wax-petal"],
    [petalDandelionType, extractDandelionLiveSvg, "dandelion-petal"],
    [petalDandelionType, extractDandelionLiveSvg, "dandelion-missile"],
    [petalTrapperType, extractTrapperLiveSvg, "trapper-petal-centered"],
    [petalHallucinogenicMushroomType, extractHallucinogenicMushroomLiveSvg, "hallucinogenic-mushroom-centered"],
  ]) jobs.push(warmPetalImage(livePetalIconImage(type, 0, crop, key)));
  // 普通图标变体按需生成；清单只在后台预热源文件，避免启动时数百次解码。
  return Promise.allSettled(jobs);
}

installGameUiActions({ getAuthUiMode: () => authUiMode, closeSliderPanel, drawScene, isOwnerDead, renderCraftPanel, renderDeathOverlayContent, renderEmailSettingsRow, renderInventoryPanel, renderSliderPanel, renderTalentPanel, setAuthUiMode, toggleCodex });
loadSettings();
{
  const endpoint = wsUrlField.getValue({ trim: true });
  if (!authRecovery.account(endpoint)) authRecovery.select(endpoint, accountField.getValue({ trim: true }));
  const remembered = authRecovery.account(endpoint);
  if (remembered) accountField.setValue(remembered);
}
// 关键路径只等待小型界面字体，大型中日韩字体可回退系统字体。
const fontPreparation = document.fonts?.load
  ? Promise.allSettled([
      document.fonts.load('900 16px "Florr UI"', "FlorrBt"),
    ])
  : Promise.resolve();
criticalAssetsPromise = Promise.race([
  fontPreparation,
  new Promise((resolve) => window.setTimeout(resolve, 1500)),
]);

function scheduleBackgroundAssetWarmup() {
  if (backgroundAssetsScheduled) return assetsPreloadPromise;
  backgroundAssetsScheduled = true;
  assetsPreloadPromise = new Promise((resolve) => {
    const start = () => {
      Promise.allSettled([
        preloadAssets({ concurrency: 8 }),
        warmPetalCardImages(),
        prepareBeeSprites(),
        prepareBeetleSprites(),
        prepareSoldierAntSprites(),
        prepareSpiderSprites(),
        prepareCrabSprites(),
        prepareJellyfishSprites(),
        prepareFireflyLarvaSprites(),
        prepareGardenSprites(),
        prepareLadybugSprites(),
      ])
        .then(() => primeSpriteRasters(getPreparedVisualImages()))
        .catch((error) => {
          console.warn("Background visual preparation failed", error);
        })
        .finally(resolve);
    };
    if (typeof requestIdleCallback === "function")
      requestIdleCallback(start, { timeout: 1800 });
    else window.setTimeout(start, 900);
  });
  return assetsPreloadPromise;
}
void ensurePetalCopyDistanceData();
await initI18n(state.locale);
state.locale = getLocale();
applyDocumentTranslations();
setAuthUiMode(AuthUiMode.Login);
setupEvents();
updateQuickActionButtons();
updatePlayUiVisibility();
resizeCanvas();
renderInventoryPanel();
initCustomScrollbars();
setStatus(t("status.disconnected"));
accountUi.reset();
addConsoleLine(t("status.clientReady", { app: appDisplayName }));
initRenderScale({ addConsoleLine, saveSettings });
// 连接结束时通过钩子重置各模块的会话状态。
registerConnectionReset(noteConnectionReset);
// 断线后合成不会再收到本轮结果，停止轨道并归还暂存卡片。
registerConnectionReset(() => {
  craftVisualGeneration += 1;
  talentTreeUi?.reset();
  craftResultDismissing = false;
  if (!craftSpin.isActive()) return;
  craftSpin.cancel();
  if (state.craftPhase === "spinning") {
    state.craftPhase = "staged";
    renderCraftPanel();
  }
});
// 注入命令接口，避免命令模块反向导入游戏模块。
initClientCommands({
  requestDevDebug,
  addConsoleLine,
  saveSettings,
  refreshLocalizedUi,
  setMovementMode,
  setMobileControlMode,
  sendChat,
  startRenderBenchmark,
  loadMap,
  applyLoginMapView,
  renderSliderPanel,
  requestRedraw,
  bindings,
  onBindingsChanged: () => scheduleInventoryUiRender(),
  setViewZoomPercent,
  requestSkill: requestControlledSkill,
  clearConsole: () => ui.consoleUi?.clear(),
  onClientConfigChanged: (name) => {
    if (name === "all" || name === "petal_label_max_font_px" || name === "petal_label_max_width_px")
      refreshPetalCardLabels();
  },
});
initFreezeWatchdog({
  addConsoleLine,
  sampleContext: () => ({
    entities: state.entities.size,
    chunkQueue: state.map?.chunkBuildQueue?.length || 0,
    dpr: Number((state.dpr || 1).toFixed(2)),
    drawEma: Number(sceneDrawCostEma().toFixed(1)),
    ws: state.ws ? state.ws.readyState : -1,
  }),
});
const loginMapPreparation = loadLoginMap();
criticalAssetsPromise.catch((error) => {
  console.warn("Critical startup preparation failed", error);
}).then(() => setLoadingScreenVisible(false));
Promise.allSettled([criticalAssetsPromise, loginMapPreparation]).then(() => {
  scheduleBackgroundAssetWarmup();
});

const startupParams = new URLSearchParams(window.location.search);
if (startupParams.has("bench")) {
  const benchScale = startupParams.get("benchScale");
  if (benchScale) {
    // 自动化覆盖仅本次会话有效，不写入设置。
    state.renderScaleMode = normalizeRenderScaleMode(benchScale);
    resizeCanvas();
  }
  const benchEntities = Number(startupParams.get("bench")) || undefined;
  const benchFrames = Number(startupParams.get("benchFrames")) || undefined;
  const waitForBenchReady = () => {
    if (state.mapLoadPromise) {
      window.setTimeout(waitForBenchReady, 200);
      return;
    }
    scheduleBackgroundAssetWarmup().then(() =>
      window.setTimeout(
        () =>
          startRenderBenchmark({
            entities: benchEntities,
            frames: benchFrames,
          }),
        800,
      ),
    );
  };
  waitForBenchReady();
}

requestAnimationFrame(tick);
document.documentElement.dataset.clientReady = "true";
if (!startupParams.has('bench')) ensureAccountSession({ openPanel: false });

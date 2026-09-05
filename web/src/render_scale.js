import { state } from "./app_context.js";
import { normalizeRenderScaleMode } from "./client_settings.js";
import { t } from "./i18n.js";
import { minRenderScale, nativeDpr, resizeCanvas } from "./viewport.js";

// Adaptive render resolution ("auto" mode): a frame-budget controller keyed
// off the measured drawScene CPU cost, so it works the same on 60Hz and
// 120Hz+ displays. The budget derives from the observed peak framerate
// (~display refresh): the draw must fit in ~3/4 of a refresh interval.
// Sustained overruns step the scale down; sustained headroom steps it back
// up toward native DPI, so a temporary fight no longer degrades resolution
// permanently.
let lastDrawCostMs = 0;
let drawCostEmaMs = 0;
let adaptiveOverFrames = 0;
let adaptiveUnderFrames = 0;
let observedPeakFps = 0;
const adaptiveStep = 0.25;
const adaptiveDownFrames = 90;
const adaptiveUpFrames = 420;

// game.js injects the UI-layer callbacks (console line + settings
// persistence) so this module stays out of the game.js import cycle.
let notifyConsoleLine = null;
let persistSettings = null;

export function initRenderScale(options = {}) {
  notifyConsoleLine = options.addConsoleLine || null;
  persistSettings = options.saveSettings || null;
}

// Called once per frame with the measured drawScene CPU cost.
export function noteSceneDrawCost(ms) {
  lastDrawCostMs = ms;
}

export function sceneDrawCostEma() {
  return drawCostEmaMs;
}

export function setRenderScaleMode(value) {
  state.renderScaleMode = normalizeRenderScaleMode(value);
  state.adaptiveDpr = 0;
  drawCostEmaMs = 0;
  adaptiveOverFrames = 0;
  adaptiveUnderFrames = 0;
  resizeCanvas();
  persistSettings?.();
}

export function updateAdaptiveRenderScale(paused = false) {
  if (paused || state.renderScaleMode !== "auto" || state.mapLoadPromise)
    return;
  if (state.fps > 0)
    observedPeakFps = Math.max(
      observedPeakFps * 0.9995,
      Math.min(240, state.fps),
    );
  if (observedPeakFps <= 0 || lastDrawCostMs <= 0) return;
  drawCostEmaMs =
    drawCostEmaMs <= 0
      ? lastDrawCostMs
      : drawCostEmaMs + (lastDrawCostMs - drawCostEmaMs) * 0.05;

  // Draw work should fit in ~3/4 of one refresh interval; the rest of the
  // frame belongs to input handling, DOM updates and the compositor.
  const refreshMs = 1000 / Math.max(30, observedPeakFps);
  const budgetMs = Math.min(12, Math.max(4.5, refreshMs * 0.75));
  const current = state.adaptiveDpr > 0 ? state.adaptiveDpr : nativeDpr();

  if (drawCostEmaMs > budgetMs && current > minRenderScale) {
    adaptiveOverFrames += 1;
    adaptiveUnderFrames = 0;
  } else if (drawCostEmaMs < budgetMs * 0.45 && current < nativeDpr()) {
    adaptiveUnderFrames += 1;
    adaptiveOverFrames = 0;
  } else {
    adaptiveOverFrames = 0;
    adaptiveUnderFrames = 0;
  }

  if (adaptiveOverFrames >= adaptiveDownFrames) {
    applyAdaptiveRenderScale(
      Math.max(minRenderScale, current - adaptiveStep),
      "down",
    );
  } else if (adaptiveUnderFrames >= adaptiveUpFrames) {
    applyAdaptiveRenderScale(
      Math.min(nativeDpr(), current + adaptiveStep),
      "up",
    );
  }
}

function applyAdaptiveRenderScale(next, direction) {
  adaptiveOverFrames = 0;
  adaptiveUnderFrames = 0;
  if (Math.abs(next - state.dpr) < 0.01) return;
  state.adaptiveDpr = next;
  resizeCanvas();
  notifyConsoleLine?.(
    t(
      direction === "down"
        ? "status.renderScaleLowered"
        : "status.renderScaleRaised",
      { scale: String(next) },
    ),
  );
}

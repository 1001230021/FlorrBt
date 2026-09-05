import { dom, state } from "./app_context.js";

// Pure viewport/render-scale plumbing: canvas backing size, device pixel
// ratio resolution and the user's render-scale setting. No game imports —
// safe to use from any layer (camera.js re-exports these for existing call
// sites).
const { canvas, ctx } = dom;

// Highest allowed render scale: full native DPI on 4x displays, or up to 4x
// supersampling on 1x displays for users who want it.
export const maxRenderScale = 4;
export const minRenderScale = 0.75;

export function nativeDpr() {
  return Math.max(1, Math.min(maxRenderScale, window.devicePixelRatio || 1));
}

// The effective canvas scale honors the user's render-scale setting:
// - "auto": native DPI, stepped down/up by the frame-budget controller
//   (state.adaptiveDpr, 0 = untouched native).
// - "native": always full native DPI, no adaptation.
// - number: fixed scale, no adaptation.
export function resolveRenderScale() {
  const mode = state.renderScaleMode;
  if (typeof mode === "number" && Number.isFinite(mode) && mode > 0)
    return mode;
  if (mode === "native") return nativeDpr();
  return state.adaptiveDpr > 0 ? state.adaptiveDpr : nativeDpr();
}

export function resizeCanvas() {
  state.dpr = Math.max(
    minRenderScale,
    Math.min(maxRenderScale, resolveRenderScale()),
  );
  state.canvasWidth = window.innerWidth;
  state.canvasHeight = window.innerHeight;
  canvas.width = Math.round(state.canvasWidth * state.dpr);
  canvas.height = Math.round(state.canvasHeight * state.dpr);
  canvas.style.width = `${state.canvasWidth}px`;
  canvas.style.height = `${state.canvasHeight}px`;
  ctx.setTransform(state.dpr, 0, 0, state.dpr, 0, 0);
}

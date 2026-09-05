import { state, ui } from "./app_context.js";
import { clamp } from "./protocol.js";

// Pure camera/world-space math: view scaling, world<->screen transforms and
// small easing helpers. No game imports — entity simulation lives in
// sim_entities.js, the canvas backing store in viewport.js (re-exported here
// so existing call sites keep working until they migrate).
export {
  maxRenderScale,
  minRenderScale,
  nativeDpr,
  resolveRenderScale,
  resizeCanvas,
} from "./viewport.js";

export function smoothFactor(rate, dt) {
  return 1 - Math.exp(-rate * dt);
}

export function normalizeAngle(angle) {
  const full = Math.PI * 2;
  let value = angle % full;
  if (value < 0) value += full;
  return value;
}

export function lerpAngle(start, end, amount) {
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

export function screenViewRadius() {
  // The view circle (radius = viewRadius) circumscribes the canvas rectangle:
  // screen diagonal == circle diameter, so the server's snapshot circle covers
  // every pixel on screen (no corner pop-in).
  return Math.max(96, Math.hypot(state.canvasWidth, state.canvasHeight) / 2);
}

export function scaleForViewRadius(viewRadius) {
  if (!viewRadius || viewRadius <= 0) return 1;
  return screenViewRadius() / viewRadius;
}

export function worldScale() {
  return scaleForViewRadius(state.viewRadius);
}

export function worldLengthToScreen(value) {
  return value * worldScale();
}

export function currentScreenShake() {
  const shake = state.screenShake || { x: 0, y: 0 };
  return {
    x: Number.isFinite(shake.x) ? shake.x : 0,
    y: Number.isFinite(shake.y) ? shake.y : 0,
  };
}

export function worldToScreen(pos) {
  const scale = worldScale();
  const shake = currentScreenShake();
  return {
    x: (pos.x - state.camera.x) * scale + state.canvasWidth * 0.5 + shake.x,
    y: (pos.y - state.camera.y) * scale + state.canvasHeight * 0.5 + shake.y,
  };
}

export function screenToWorld(pos) {
  const scale = worldScale();
  const shake = currentScreenShake();
  return {
    x: (pos.x - state.canvasWidth * 0.5 - shake.x) / scale + state.camera.x,
    y: (pos.y - state.canvasHeight * 0.5 - shake.y) / scale + state.camera.y,
  };
}

export function debugNumber(value, digits = 1) {
  return Number.isFinite(value) ? value.toFixed(digits) : "-";
}

export function drawGrid() {
  ui.mapRenderer.drawGrid();
}

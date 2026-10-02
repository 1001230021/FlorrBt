import { state, ui } from "./app_context.js";
import { normalizeViewZoomPercent } from "./client_config.js";
import { clamp } from "./protocol.js";
import { stepRenderZoom } from "./render_timing.js";

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

export function viewZoomPercent() {
  return normalizeViewZoomPercent(state.viewZoomPercent);
}

export function updateViewZoom(dt) {
  state.renderViewZoomPercent = stepRenderZoom(state.renderViewZoomPercent, viewZoomPercent(), dt);
}

// The server radius describes the complete snapshot the client is allowed to
// see. A lower client percentage shrinks the camera field of view, making the
// same world units occupy more screen pixels. The authoritative server radius
// remains untouched for networking, audio and missing-entity grace handling.
export function effectiveViewRadius() {
  const serverRadius = Number(state.viewRadius);
  if (!Number.isFinite(serverRadius) || serverRadius <= 0) return 0;
  // 100% is the normal view. Lower percentages show a smaller world radius,
  // so flowers and other entities appear proportionally larger.
  const deathFocus = clamp(Number(state.deathViewFactor) || 1, 0.45, 1);
  const zoom = Number.isFinite(state.renderViewZoomPercent) ? state.renderViewZoomPercent : viewZoomPercent();
  return serverRadius * (zoom / 100) * deathFocus;
}

export function worldScale() {
  return scaleForViewRadius(effectiveViewRadius());
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

export function drawGrid(now) {
  ui.mapRenderer.drawGrid(now);
}

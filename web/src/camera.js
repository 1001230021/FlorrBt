import { state, ui } from "./app_context.js";
import { normalizeViewZoomPercent } from "./client_config.js";
import { clamp } from "./protocol.js";
import { stepRenderZoom } from "./render_timing.js";

// 相机纯数学：缩放、坐标转换和缓动；模拟及画布尺寸分别由其他模块管理。
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
  // 视野圆外接画布矩形，确保服务端快照覆盖屏幕四角。
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

// 客户端缩小视野只改变显示比例；网络、声音和实体离场宽限仍使用权威半径。
export function effectiveViewRadius() {
  const serverRadius = Number(state.viewRadius);
  if (!Number.isFinite(serverRadius) || serverRadius <= 0) return 0;
  const deathFocus = clamp(Number(state.deathViewFactor) || 1, 0.45, 1);
  const zoom = Number.isFinite(state.renderViewZoomPercent) ? state.renderViewZoomPercent : viewZoomPercent();
  return serverRadius * (zoom / 100) * deathFocus;
}

let cachedScale = 1, cachedWidth, cachedHeight, cachedRadius, cachedZoom, cachedZoomTarget, cachedDeath;
export function worldScale() {
  // 缓存键包含临时调试相机，避免回到游戏时复用观察者缩放。
  if (cachedWidth !== state.canvasWidth || cachedHeight !== state.canvasHeight ||
      cachedRadius !== state.viewRadius || cachedZoom !== state.renderViewZoomPercent ||
      cachedZoomTarget !== state.viewZoomPercent || cachedDeath !== state.deathViewFactor) {
    cachedWidth = state.canvasWidth; cachedHeight = state.canvasHeight;
    cachedRadius = state.viewRadius; cachedZoom = state.renderViewZoomPercent;
    cachedZoomTarget = state.viewZoomPercent; cachedDeath = state.deathViewFactor;
    cachedScale = scaleForViewRadius(effectiveViewRadius());
  }
  return cachedScale;
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
  const shake = state.screenShake;
  return {
    x: (pos.x - state.camera.x) * scale + state.canvasWidth * 0.5 + (Number.isFinite(shake?.x) ? shake.x : 0),
    y: (pos.y - state.camera.y) * scale + state.canvasHeight * 0.5 + (Number.isFinite(shake?.y) ? shake.y : 0),
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

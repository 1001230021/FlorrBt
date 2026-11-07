import { dom, state } from "./app_context.js";

// 独立管理画布尺寸、像素比与渲染比例，避免依赖游戏模块。
const { canvas, ctx } = dom;

// 最多四倍渲染比例，兼容高像素比及主动超采样。
export const maxRenderScale = 4;
export const minRenderScale = 0.75;

export function nativeDpr() {
  return Math.max(1, Math.min(maxRenderScale, window.devicePixelRatio || 1));
}

// auto 按帧预算调整原生像素比；native 固定原生比例；数值配置使用固定比例。
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

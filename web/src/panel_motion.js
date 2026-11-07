// 视频按实际 PTS 逐帧测得：下方工具面板竖直滑入，上方窗口从左滑入；退出沿原方向回退并淡出。
export const panelMotion = Object.freeze({ enterMs: 280, exitMs: 150, decayPerSecond: 30 });
const active = new WeakMap();

export function panelProgress(elapsedMs, durationMs) {
  if (!(durationMs > 0) || elapsedMs >= durationMs) return 1;
  const t = Math.max(0, elapsedMs) / 1000, end = durationMs / 1000;
  return -Math.expm1(-panelMotion.decayPerSecond * t) / -Math.expm1(-panelMotion.decayPerSecond * end);
}

export function closedPanelOffset(id, { left = 0, top = 0, width = 0, viewportHeight = 0 }) {
  return id === "settingsPanel" || id === "codexPanel" || id === "accountPanel"
    ? { x: -left - width - 8, y: 0 }
    : { x: 0, y: Math.max(0, viewportHeight - top) + 8 };
}

export function transitionPanel(element, visible, applyState = () => {
  element.classList.toggle("open", visible);
  element.setAttribute("aria-hidden", String(!visible));
}) {
  const previous = active.get(element);
  const wasVisible = element.classList.contains("open") || !!previous;
  const offset = closedPanelOffset(element.id, { left: element.offsetLeft, top: element.offsetTop,
    width: element.offsetWidth, viewportHeight: globalThis.innerHeight || 0 });
  let from = { ...offset, opacity: 0 };
  if (wasVisible && globalThis.getComputedStyle) {
    const style = getComputedStyle(element);
    const matrix = style.transform.match(/^matrix\(([^)]+)\)$/)?.[1].split(",").map(Number);
    from = { x: matrix?.[4] || 0, y: matrix?.[5] || 0, opacity: Number(style.opacity) || 0 };
  }
  if (previous) { previous.cancel(); active.delete(element); }
  element.classList.remove("ui-panel-entering", "ui-panel-exiting");
  applyState();
  element.inert = !visible;
  if (!element.animate || globalThis.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches) return;
  const phase = visible ? "ui-panel-entering" : "ui-panel-exiting";
  const to = visible ? { x: 0, y: 0, opacity: 1 } : { ...offset, opacity: 0 };
  const duration = visible ? panelMotion.enterMs : panelMotion.exitMs;
  element.classList.add(phase);
  const keyframes = Array.from({ length: 21 }, (_, i) => {
    const k = panelProgress(duration * i / 20, duration);
    return { offset: i / 20, transform: `translate(${from.x + (to.x - from.x) * k}px, ${from.y + (to.y - from.y) * k}px)`,
      opacity: from.opacity + (to.opacity - from.opacity) * k };
  });
  const animation = element.animate(keyframes, { duration, easing: "linear" });
  active.set(element, animation);
  animation.finished.then(() => {
    if (active.get(element) !== animation) return;
    active.delete(element); element.classList.remove(phase);
  }).catch(() => {});
}

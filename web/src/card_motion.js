// 世界掉落和界面卡片共用消失姿态：旋转一周、淡出并缩小。
// 世界掉落由实体离场时间线驱动，界面由下方动画入口驱动。

import { clientRuntimeConfig } from "./client_config.js";

/* 统一消失姿态：进度 0..1，旋转一周，径向和角度端点平滑。 */
export function petalCardVanishPose(progress) {
  const p = Number.isFinite(progress) ? Math.max(0, Math.min(1, progress)) : 0;
  const eased = p * p * (3 - 2 * p);
  return {
    rotation: eased * Math.PI * 2,
    scale: 1 - eased,
    alpha: 1 - p * p,
  };
}

const activeVanishes = new WeakMap();

function prefersReducedMotion() {
  return !!globalThis.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches;
}

/* 对界面卡片应用消失姿态，保留 baseTransform 中原有居中布局。 */
export function animatePetalCardVanish(
  element,
  {
    durationMs = clientRuntimeConfig.petalCardVanishDurationMs ?? 700,
    baseTransform = "translate(-50%, -50%)",
    onFinish,
  } = {},
) {
  if (!element) {
    onFinish?.();
    return;
  }
  const previous = activeVanishes.get(element);
  if (previous) previous.cancel();

  const applyPose = (pose) => {
    element.style.transform =
      `${baseTransform} rotate(${pose.rotation}rad) ` +
      `scale(${Math.max(0, pose.scale)})`;
    element.style.opacity = String(Math.max(0, pose.alpha));
  };

  if (prefersReducedMotion() || !(durationMs > 0)) {
    applyPose(petalCardVanishPose(1));
    activeVanishes.delete(element);
    onFinish?.();
    return { cancel() {} };
  }

  const startTime = performance.now();
  const record = { frame: 0, cancelled: false, cancel() {
    this.cancelled = true;
    cancelAnimationFrame(this.frame);
    if (activeVanishes.get(element) === this) activeVanishes.delete(element);
  } };
  activeVanishes.set(element, record);
  const tick = (now) => {
    if (record.cancelled) return;
    const progress = (now - startTime) / durationMs;
    applyPose(petalCardVanishPose(progress));
    if (progress < 1) {
      record.frame = requestAnimationFrame(tick);
      return;
    }
    activeVanishes.delete(element);
    onFinish?.();
  };
  record.frame = requestAnimationFrame(tick);
  return record;
}

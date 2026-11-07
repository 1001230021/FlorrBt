// 节点淡出后再隐藏，重复调用可安全逆转过渡，避免快速开关与延迟清理竞争。

const activeVisibilityMotions = new WeakMap();
const activeMotionPhases = new WeakMap();

function prefersReducedMotion() {
  return !!globalThis.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches;
}

function normalizedDuration(value, fallback = 220) {
  const number = Number(value);
  if (prefersReducedMotion()) return 0;
  return Number.isFinite(number) ? Math.max(0, number) : fallback;
}

function requestMotionFrame(callback) {
  if (typeof globalThis.requestAnimationFrame === "function")
    return { kind: "raf", id: globalThis.requestAnimationFrame(callback) };
  return { kind: "timeout", id: globalThis.setTimeout(callback, 0) };
}

function cancelMotionFrame(frame) {
  if (!frame) return;
  if (frame.kind === "raf" && typeof globalThis.cancelAnimationFrame === "function")
    globalThis.cancelAnimationFrame(frame.id);
  else globalThis.clearTimeout(frame.id);
}

function cancelVisibilityMotion(motion) {
  if (!motion) return;
  if (motion.timer !== null) globalThis.clearTimeout(motion.timer);
  cancelMotionFrame(motion.frame);
  motion.timer = null;
  motion.frame = null;
}

const DEFAULT_ENTER_MS = 200;
const DEFAULT_EXIT_MS = 90;

// 同时发布阶段时长和曲线，让进入减速、退出快速响应。
function applyMotionPhaseVars(element, milliseconds, entering) {
  element.style.setProperty("--ui-motion-duration", `${milliseconds}ms`);
  element.style.setProperty(
    "--ui-motion-ease",
    entering ? "var(--m-enter-ease)" : "var(--m-exit-ease)",
  );
}

/* 退出过渡结束后才应用隐藏类；可统一覆盖时长或分别设置进入/退出时长。 */
export function setAnimatedVisibility(
  element,
  visible,
  {
    hiddenClass = "hidden",
    duration = null,
    enterDuration = null,
    exitDuration = null,
  } = {},
) {
  if (!element) return false;

  const targetVisible = !!visible;
  let motion = activeVisibilityMotions.get(element);
  const currentlyHidden = element.classList.contains(hiddenClass);

  if (!motion) {
    motion = { targetVisible: null, timer: null, frame: null };
    activeVisibilityMotions.set(element, motion);
    // 初始状态不播放过渡。
    if ((targetVisible && !currentlyHidden) || (!targetVisible && currentlyHidden)) {
      motion.targetVisible = targetVisible;
      element.classList.add("ui-motion-target");
      element.setAttribute("aria-hidden", targetVisible ? "false" : "true");
      return false;
    }
  }

  if (motion.targetVisible === targetVisible) return false;
  cancelVisibilityMotion(motion);
  motion.targetVisible = targetVisible;

  const requested = targetVisible
    ? (enterDuration ?? duration ?? DEFAULT_ENTER_MS)
    : (exitDuration ?? duration ?? DEFAULT_EXIT_MS);
  const milliseconds = normalizedDuration(
    requested,
    targetVisible ? DEFAULT_ENTER_MS : DEFAULT_EXIT_MS,
  );
  element.classList.add("ui-motion-target");
  applyMotionPhaseVars(element, milliseconds, targetVisible);
  element.setAttribute("aria-hidden", targetVisible ? "false" : "true");

  if (targetVisible) {
    element.classList.remove(hiddenClass, "ui-motion-exiting");
    if (milliseconds <= 0) {
      element.classList.remove("ui-motion-entering");
      return true;
    }

    element.classList.add("ui-motion-entering");
    motion.frame = requestMotionFrame(() => {
      motion.frame = requestMotionFrame(() => {
        motion.frame = null;
        if (motion.targetVisible !== true) return;
        element.classList.remove("ui-motion-entering");
      });
    });
    return true;
  }

  element.classList.remove("ui-motion-entering");
  if (milliseconds <= 0) {
    element.classList.remove("ui-motion-exiting");
    element.classList.add(hiddenClass);
    return true;
  }

  element.classList.add("ui-motion-exiting");
  motion.timer = globalThis.setTimeout(() => {
    motion.timer = null;
    if (motion.targetVisible !== false) return;
    element.classList.add(hiddenClass);
    element.classList.remove("ui-motion-exiting");
  }, milliseconds);
  return true;
}

/* 为由其他可见性类控制的组件附加短暂进入/退出阶段。 */
export function runMotionPhase(
  element,
  phase,
  { duration = 240, classPrefix = "ui-panel" } = {},
) {
  if (!element) return;

  let motion = activeMotionPhases.get(element);
  if (!motion) {
    motion = { timer: null, frame: null };
    activeMotionPhases.set(element, motion);
  }
  if (motion.timer !== null) globalThis.clearTimeout(motion.timer);
  cancelMotionFrame(motion.frame);
  motion.timer = null;
  motion.frame = null;

  const classes = [
    `${classPrefix}-entering`,
    `${classPrefix}-exiting`,
  ];
  element.classList.remove(...classes);
  if (phase !== "entering" && phase !== "exiting") return;

  const milliseconds = normalizedDuration(duration, 240);
  applyMotionPhaseVars(element, milliseconds, phase === "entering");
  const phaseClass = `${classPrefix}-${phase}`;
  element.classList.add(phaseClass);
  if (milliseconds <= 0) {
    element.classList.remove(phaseClass);
    return;
  }
  if (phase === "entering") {
    // 先绘制一次初始姿态，下一帧开始正常过渡。
    motion.frame = requestMotionFrame(() => {
      motion.frame = requestMotionFrame(() => {
        motion.frame = null;
        if (element.classList.contains(phaseClass))
          element.classList.remove(phaseClass);
      });
    });
  } else {
    motion.timer = globalThis.setTimeout(() => {
      motion.timer = null;
      element.classList.remove(phaseClass);
    }, milliseconds);
  }
}

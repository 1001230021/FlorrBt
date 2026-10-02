// Small, dependency-free helpers for reversible UI transitions.
//
// A plain `display: none` toggle cannot be animated and also makes a fast
// open/close sequence race with delayed cleanup. These helpers keep the node
// mounted while it fades, then apply the existing hidden class after the
// transition. Calling the same helper again reverses the transition safely.

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

// Entrances and exits deliberately run at different speeds; see the
// --m-enter-dur / --m-exit-dur tokens in styles.css for the rationale.
const DEFAULT_ENTER_MS = 200;
const DEFAULT_EXIT_MS = 90;

// Publish the phase's duration *and* curve so a component can declare a single
// transition against these vars and still decelerate on the way in while
// snapping out on the way back.
function applyMotionPhaseVars(element, milliseconds, entering) {
  element.style.setProperty("--ui-motion-duration", `${milliseconds}ms`);
  element.style.setProperty(
    "--ui-motion-ease",
    entering ? "var(--m-enter-ease)" : "var(--m-exit-ease)",
  );
}

/**
 * Animate an element between its normal state and an existing hidden-class
 * state. The hidden class is applied only after the exit animation completes.
 *
 * `duration` still works as a symmetric override; pass `enterDuration` /
 * `exitDuration` to keep the asymmetric rhythm while retuning one side.
 */
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
    // Do not animate the initial state. Animate only an actual transition.
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

/**
 * Add a short enter/exit phase to a component whose visibility is controlled
 * by another class (for example UiPanel's `open` class).
 */
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
    // Let the browser paint the initial phase once, then the normal state
    // transition starts on the following frame instead of waiting for the
    // whole animation duration.
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

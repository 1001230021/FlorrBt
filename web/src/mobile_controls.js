import { t } from "./i18n.js";
import { setAnimatedVisibility } from "./ui_motion.js";

// A/B/X are touch labels: attack/defend/swap. They dispatch the same builtins
// as the keyboard without sharing (or changing) physical keyboard bindings.
export const mobileActionBindings = Object.freeze({ touch: Object.freeze(Object.fromEntries(
  ["attack", "defend", "swap"]
    .map(action => [action, [action]]),
)) });

export function getJoystickVector(dx, dy, radius, deadzone = 6) {
  const length = Math.hypot(dx, dy);
  const travel = Math.max(deadzone + 1, radius);
  if (!Number.isFinite(length) || length <= deadzone) return { x: 0, y: 0 };
  const strength = Math.min(1, (length - deadzone) / (travel - deadzone));
  return { x: dx / length * strength, y: dy / length * strength };
}

function isMobileInputMode() {
  return !!window.matchMedia?.("(pointer: coarse)")?.matches || window.innerWidth <= 720;
}

function setPressed(button, pressed) {
  button.classList.toggle("pressed", pressed);
  button.setAttribute("aria-pressed", String(pressed));
}

function createButton(labelKey, className, titleKey = labelKey) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = className;
  button.draggable = false;
  button.dataset.i18nLabel = labelKey;
  button.dataset.i18nTitle = titleKey;
  translateButton(button);
  return button;
}

function translateButton(button) {
  button.textContent = t(button.dataset.i18nLabel);
  const description = t(button.dataset.i18nTitle);
  button.title = button.classList.contains("mobile-control-button") ? `${button.textContent} · ${description}` : description;
  button.setAttribute("aria-label", button.title);
}

export function createMobileControls({ isUiTarget, movementSurface, onAction, onSurfaceTap, onLayoutChange } = {}) {
  const root = document.createElement("section");
  root.className = "mobile-controls ui hidden";
  root.setAttribute("aria-label", t("ui.mobile.ariaLabel"));
  const slotMenu = document.createElement("div");
  slotMenu.className = "mobile-slot-menu hidden";
  slotMenu.setAttribute("role", "dialog");
  slotMenu.setAttribute("aria-label", t("ui.mobile.slotActions"));
  const joystick = document.createElement("div");
  joystick.className = "mobile-joystick hidden";
  joystick.setAttribute("aria-hidden", "true");
  const base = document.createElement("div"); base.className = "mobile-joystick-base";
  const knob = document.createElement("div"); knob.className = "mobile-joystick-knob";
  joystick.append(base, knob);

  const cluster = document.createElement("div");
  cluster.className = "mobile-action-cluster";
  const attackButton = createButton("ui.mobile.attackLabel", "mobile-control-button mobile-attack-button", "ui.mobile.attack");
  const defendButton = createButton("ui.mobile.defendLabel", "mobile-control-button mobile-defend-button", "ui.mobile.defend");
  const swapButton = createButton("ui.mobile.swapLabel", "mobile-control-button mobile-swap-button", "ui.mobile.swap");
  cluster.append(attackButton, defendButton, swapButton);
  root.append(slotMenu, joystick, cluster);
  document.body.appendChild(root);

  let enabled = false, blocked = false, mode = "auto", slotMenuOpen = false;
  let safeInsets = { top: 10, left: 10, right: 10 };
  let movementPointerId = null, movementRadius = 38;
  let origin = { x: 0, y: 0 }, current = { x: 0, y: 0 };
  const heldPointers = new Map([["attack", new Set()], ["defend", new Set()]]);
  const holdButtons = new Map([["attack", attackButton], ["defend", defendButton]]);
  const usesMobileLayout = () => mode !== "off" && (mode === "on" || isMobileInputMode());
  const isActive = () => enabled && usesMobileLayout();
  const canPlay = () => isActive() && !blocked && !slotMenuOpen;

  function closeSlotMenu() {
    slotMenuOpen = false; slotMenu.classList.add("hidden");
    root.classList.remove("menu-open");
  }

  function runAction(action) {
    closeSlotMenu();
    onAction?.(action, "down");
    onAction?.(action, "up");
  }

  function resetMovement() {
    const pointer = movementPointerId;
    movementPointerId = null;
    if (pointer !== null && movementSurface?.hasPointerCapture?.(pointer)) movementSurface.releasePointerCapture(pointer);
    origin = { x: 0, y: 0 }; current = { ...origin };
    joystick.classList.remove("active");
    joystick.classList.add("hidden");
    joystick.style.removeProperty("left"); joystick.style.removeProperty("top");
    knob.style.transform = "translate(-50%, -50%)";
  }

  function releaseHold(action, pointerId) {
    const pointers = heldPointers.get(action);
    if (!pointers.delete(pointerId)) return;
    const button = holdButtons.get(action);
    if (!pointers.size) { setPressed(button, false); onAction?.(action, "up"); }
    if (button.hasPointerCapture?.(pointerId)) button.releasePointerCapture(pointerId);
  }

  function reset() {
    resetMovement();
    for (const [action, pointers] of heldPointers)
      for (const pointerId of [...pointers]) releaseHold(action, pointerId);
  }

  function updateVisibility() {
    const layout = usesMobileLayout(), active = enabled && layout;
    document.body.style.setProperty("--mob-scale", String(Math.max(0.566, Math.min(0.848, window.innerWidth / 678))));
    root.classList.toggle("force-mobile", mode === "on");
    document.body.classList.toggle("mobile-mode-forced", active && mode === "on");
    document.body.classList.toggle("mobile-ui", layout);
    setAnimatedVisibility(root, active, { duration: 120 });
    if (active) {
      const corner = document.getElementById?.("topLeftActions")?.getBoundingClientRect();
      const actionRect = cluster.getBoundingClientRect();
      safeInsets = { top: Math.max(10, corner?.top || 0), left: Math.max(10, corner?.left || 0), right: Math.max(10, window.innerWidth - actionRect.right) };
    }
    if (!active) { closeSlotMenu(); reset(); }
    onLayoutChange?.();
  }

  function updateKnob() {
    const dx = current.x - origin.x, dy = current.y - origin.y;
    const scale = Math.min(1, movementRadius / (Math.hypot(dx, dy) || 1));
    knob.style.transform = `translate(calc(-50% + ${dx * scale}px), calc(-50% + ${dy * scale}px))`;
  }

  function beginMovement(event) {
    if (!isActive()) return;
    if (slotMenuOpen && !slotMenu.contains(event.target)) {
      closeSlotMenu();
      if (!isUiTarget?.(event.target)) { event.preventDefault(); event.stopImmediatePropagation(); }
      return;
    }
    if (!canPlay() || movementPointerId !== null || (event.button !== undefined && event.button !== 0)) return;
    if (movementSurface && event.target !== movementSurface) return;
    if (isUiTarget?.(event.target)) return;
    if (onSurfaceTap?.(event)) { event.preventDefault(); return; }
    // The right side belongs to actions/camera taps; it must not steal a
    // second finger or create an invisible joystick under a battle button.
    if (event.clientX > window.innerWidth * 0.55) return;
    movementPointerId = event.pointerId;
    joystick.classList.remove("hidden");
    movementRadius = Math.max(16, ((joystick.clientWidth || 120) - (knob.clientWidth || 44)) / 2);
    origin = { x: event.clientX, y: event.clientY }; current = { ...origin };
    joystick.style.left = `${origin.x}px`; joystick.style.top = `${origin.y}px`;
    joystick.classList.add("active"); updateKnob();
    movementSurface?.setPointerCapture?.(event.pointerId);
    event.preventDefault();
  }

  function updateMovement(event) {
    if (event.pointerId !== movementPointerId) return;
    if (!canPlay()) { resetMovement(); return; }
    current = { x: event.clientX, y: event.clientY }; updateKnob(); event.preventDefault();
  }

  function endPointer(event) {
    if (event.pointerId === movementPointerId) { resetMovement(); event.preventDefault(); }
    for (const action of heldPointers.keys()) releaseHold(action, event.pointerId);
  }

  for (const [action, button] of holdButtons) {
    setPressed(button, false);
    button.dataset.mobileAction = action;
    button.addEventListener("pointerdown", event => {
      if (!canPlay() || (event.button !== undefined && event.button !== 0)) return;
      const pointers = heldPointers.get(action), wasHeld = pointers.size > 0;
      pointers.add(event.pointerId); setPressed(button, true);
      button.setPointerCapture?.(event.pointerId);
      if (!wasHeld) onAction?.(action, "down");
      event.preventDefault(); event.stopPropagation();
    });
    button.addEventListener("lostpointercapture", event => releaseHold(action, event.pointerId));
    button.addEventListener("click", event => { event.preventDefault(); event.stopPropagation(); });
  }
  swapButton.dataset.mobileAction = "swap";
  swapButton.addEventListener("click", event => {
    event.preventDefault(); event.stopPropagation();
    if (canPlay()) runAction("swap");
  });

  window.addEventListener("pointerdown", beginMovement, { capture: true, passive: false });
  window.addEventListener("pointermove", updateMovement, { capture: true, passive: false });
  window.addEventListener("pointerup", endPointer, { capture: true, passive: false });
  window.addEventListener("pointercancel", endPointer, { capture: true, passive: false });
  movementSurface?.addEventListener("lostpointercapture", event => {
    if (event.pointerId === movementPointerId) resetMovement();
  });
  window.addEventListener("resize", () => { reset(); updateVisibility(); });
  window.matchMedia?.("(pointer: coarse)")?.addEventListener?.("change", () => { reset(); updateVisibility(); });
  window.addEventListener("blur", reset);
  document.addEventListener("visibilitychange", () => { if (document.hidden) reset(); });
  root.addEventListener("contextmenu", event => event.preventDefault());
  root.addEventListener("dragstart", event => event.preventDefault());
  window.addEventListener("keydown", event => {
    if (slotMenuOpen && event.key === "Escape") { closeSlotMenu(); event.preventDefault(); event.stopImmediatePropagation(); }
  }, { capture: true });

  return {
    refreshTranslations() {
      root.setAttribute("aria-label", t("ui.mobile.ariaLabel"));
      slotMenu.setAttribute("aria-label", t("ui.mobile.slotActions"));
      for (const button of root.querySelectorAll("[data-i18n-label]")) translateButton(button);
    },
    setEnabled(value) { enabled = !!value; updateVisibility(); },
    setMode(value) { mode = value === "on" || value === "off" ? value : "auto"; updateVisibility(); },
    setBlocked(value) {
      if (blocked === !!value) return;
      blocked = !!value; root.classList.toggle("gameplay-blocked", blocked);
      if (blocked) { closeSlotMenu(); reset(); }
    },
    showSlotActions({ title, onSwap, onRemove }) {
      if (!canPlay()) return;
      closeSlotMenu(); reset(); slotMenuOpen = true;
      slotMenu.replaceChildren();
      const heading = document.createElement("strong"); heading.textContent = title; slotMenu.appendChild(heading);
      for (const [label, callback] of [["slotSwap", onSwap], ["unequip", onRemove], ["cancel", null]]) {
        const button = createButton(`ui.mobile.${label}`, "mobile-menu-button");
        button.addEventListener("click", () => { closeSlotMenu(); callback?.(); });
        slotMenu.appendChild(button);
      }
      slotMenu.classList.remove("hidden"); root.classList.add("menu-open");
    },
    getSafeInsets: () => safeInsets,
    hasMovement() { const move = this.getMove(); return move.x !== 0 || move.y !== 0; },
    isMobileMode: isActive,
    getMove() {
      return canPlay() && movementPointerId !== null
        ? getJoystickVector(current.x - origin.x, current.y - origin.y, movementRadius) : { x: 0, y: 0 };
    },
    isAttacking: () => canPlay() && heldPointers.get("attack").size > 0,
    isDefending: () => canPlay() && heldPointers.get("defend").size > 0,
    reset,
  };
}

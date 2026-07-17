const movementRadius = 62;
const movementDeadzone = 7;

function isMobileInputMode() {
  return window.matchMedia?.("(pointer: coarse)")?.matches || window.innerWidth <= 720;
}

function setPressed(button, pressed) {
  button.classList.toggle("pressed", pressed);
  button.setAttribute("aria-pressed", pressed ? "true" : "false");
}

function createButton(label, className, title) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = `mobile-control-button ${className}`;
  button.textContent = label;
  button.title = title || label;
  button.setAttribute("aria-label", title || label);
  button.setAttribute("aria-pressed", "false");
  return button;
}

export function createMobileControls({
  isUiTarget,
  onBackpack,
  onTalent,
  onCraft,
  onChat,
} = {}) {
  const root = document.createElement("section");
  root.className = "mobile-controls ui hidden";
  root.setAttribute("aria-label", "Mobile controls");

  const joystick = document.createElement("div");
  joystick.className = "mobile-joystick hidden";
  joystick.innerHTML = '<div class="mobile-joystick-base"></div><div class="mobile-joystick-knob"></div>';
  const knob = joystick.querySelector(".mobile-joystick-knob");

  const actionGroup = document.createElement("div");
  actionGroup.className = "mobile-panel-actions";
  const backpackButton = createButton("Bag", "mobile-panel-button", "Inventory");
  const talentButton = createButton("Tal", "mobile-panel-button", "Talents");
  const craftButton = createButton("Craft", "mobile-panel-button", "Craft");
  const chatButton = createButton("Chat", "mobile-panel-button", "Chat");
  actionGroup.append(backpackButton, talentButton, craftButton, chatButton);

  const attackButton = createButton("A", "mobile-attack-button", "Attack");
  const defendButton = createButton("B", "mobile-defend-button", "Defend");

  root.append(joystick, actionGroup, attackButton, defendButton);
  document.body.appendChild(root);

  let enabled = false;
  let movementPointerId = null;
  let origin = { x: 0, y: 0 };
  let current = { x: 0, y: 0 };
  let attacking = false;
  let defending = false;
  let mode = "auto";

  function shouldUseMobileMode() {
    return enabled && mode !== "off" && (mode === "on" || isMobileInputMode());
  }

  function updateRootVisibility() {
    const active = shouldUseMobileMode();
    root.classList.toggle("force-mobile", mode === "on");
    root.classList.toggle("hidden", !active);
    document.body.classList.toggle("mobile-mode-forced", active && mode === "on");
    if (!active) resetMovement();
  }

  function moveJoystickToOrigin() {
    joystick.style.left = `${origin.x}px`;
    joystick.style.top = `${origin.y}px`;
  }

  function updateKnob() {
    const dx = current.x - origin.x;
    const dy = current.y - origin.y;
    const length = Math.hypot(dx, dy);
    const scale = length > movementRadius ? movementRadius / length : 1;
    knob.style.transform = `translate(calc(-50% + ${dx * scale}px), calc(-50% + ${dy * scale}px))`;
  }

  function resetMovement() {
    movementPointerId = null;
    origin = { x: 0, y: 0 };
    current = { x: 0, y: 0 };
    joystick.classList.add("hidden");
    knob.style.transform = "translate(-50%, -50%)";
  }

  function beginMovement(event) {
    if (!shouldUseMobileMode()) return;
    if (movementPointerId !== null) return;
    if (event.button !== undefined && event.button !== 0) return;
    if (isUiTarget?.(event.target)) return;
    movementPointerId = event.pointerId;
    origin = { x: event.clientX, y: event.clientY };
    current = { ...origin };
    moveJoystickToOrigin();
    updateKnob();
    joystick.classList.remove("hidden");
    event.preventDefault();
  }

  function updateMovement(event) {
    if (event.pointerId !== movementPointerId) return;
    current = { x: event.clientX, y: event.clientY };
    updateKnob();
    event.preventDefault();
  }

  function endMovement(event) {
    if (event.pointerId !== movementPointerId) return;
    resetMovement();
    event.preventDefault();
  }

  function bindHoldButton(button, setter) {
    button.addEventListener("pointerdown", (event) => {
      if (!shouldUseMobileMode()) return;
      setter(true);
      setPressed(button, true);
      button.setPointerCapture?.(event.pointerId);
      event.preventDefault();
      event.stopPropagation();
    });
    const release = (event) => {
      setter(false);
      setPressed(button, false);
      event.preventDefault();
      event.stopPropagation();
    };
    button.addEventListener("pointerup", release);
    button.addEventListener("pointercancel", release);
    button.addEventListener("lostpointercapture", () => {
      setter(false);
      setPressed(button, false);
    });
  }

  function bindTapButton(button, handler) {
    button.addEventListener("click", (event) => {
      if (!shouldUseMobileMode()) return;
      handler?.();
      event.preventDefault();
      event.stopPropagation();
    });
  }

  bindHoldButton(attackButton, (value) => { attacking = value; });
  bindHoldButton(defendButton, (value) => { defending = value; });
  bindTapButton(backpackButton, onBackpack);
  bindTapButton(talentButton, onTalent);
  bindTapButton(craftButton, onCraft);
  bindTapButton(chatButton, onChat);

  window.addEventListener("pointerdown", beginMovement, { capture: true, passive: false });
  window.addEventListener("pointermove", updateMovement, { capture: true, passive: false });
  window.addEventListener("pointerup", endMovement, { capture: true, passive: false });
  window.addEventListener("pointercancel", endMovement, { capture: true, passive: false });

  return {
    setEnabled(value) {
      enabled = !!value;
      updateRootVisibility();
      if (!enabled) this.reset();
    },
    setMode(value) {
      mode = value === "on" || value === "off" ? value : "auto";
      updateRootVisibility();
      if (!shouldUseMobileMode()) this.reset();
    },
    hasMovement() {
      if (!shouldUseMobileMode() || movementPointerId === null) return false;
      return Math.hypot(current.x - origin.x, current.y - origin.y) > movementDeadzone;
    },
    isMobileMode() {
      return shouldUseMobileMode();
    },
    getMove() {
      if (!this.hasMovement()) return { x: 0, y: 0 };
      const dx = current.x - origin.x;
      const dy = current.y - origin.y;
      const length = Math.hypot(dx, dy);
      const scale = Math.min(1, length / movementRadius) / length;
      return { x: dx * scale, y: dy * scale };
    },
    isAttacking() {
      return shouldUseMobileMode() && attacking;
    },
    isDefending() {
      return shouldUseMobileMode() && defending;
    },
    reset() {
      resetMovement();
      attacking = false;
      defending = false;
      setPressed(attackButton, false);
      setPressed(defendButton, false);
    },
  };
}

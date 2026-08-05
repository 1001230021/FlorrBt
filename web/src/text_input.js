const supportedElements = new Set(["INPUT", "TEXTAREA"]);

export function isTextInputActive() {
  return supportedElements.has(document.activeElement?.tagName);
}

export function createTextInput(
  element,
  {
    variant = "",
    onSubmit = null,
    onCancel = null,
    onKeyDown = null,
  } = {},
) {
  if (!element || !supportedElements.has(element.tagName)) {
    throw new TypeError("createTextInput requires an input or textarea element");
  }

  element.classList.add("ui-text-input");
  if (variant) element.classList.add(`ui-text-input--${variant}`);

  const handleKeyDown = (event) => {
    if (event.isComposing || event.keyCode === 229) return;
    if (onKeyDown?.(event) === true || event.defaultPrevented) return;

    if (event.key === "Enter" && onSubmit) {
      event.preventDefault();
      onSubmit(element.value, event);
    } else if (event.key === "Escape" && onCancel) {
      event.preventDefault();
      onCancel(event);
    }
  };

  element.addEventListener("keydown", handleKeyDown);

  return Object.freeze({
    element,
    getValue({ trim = false } = {}) {
      return trim ? element.value.trim() : element.value;
    },
    setValue(value = "") {
      element.value = String(value ?? "");
    },
    clear() {
      element.value = "";
    },
    focus({ cursor = "preserve" } = {}) {
      element.focus();
      if (cursor !== "start" && cursor !== "end") return;
      const offset = cursor === "end" ? element.value.length : 0;
      element.setSelectionRange?.(offset, offset);
    },
    blur() {
      element.blur();
    },
    setAriaLabel(label = "") {
      if (label) element.setAttribute("aria-label", label);
      else element.removeAttribute("aria-label");
    },
    setPlaceholder(placeholder = "") {
      element.placeholder = String(placeholder);
    },
    setDisabled(disabled) {
      element.disabled = !!disabled;
    },
    destroy() {
      element.removeEventListener("keydown", handleKeyDown);
    },
  });
}

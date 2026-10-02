// Resize only mobile chrome for the software keyboard. Translating <body>
// also moves the canvas/fixed joystick and breaks client-coordinate hit tests.
export function bindMobileViewport({ onTyping = () => {} } = {}) {
  let frame = 0, wasTyping = false;
  const visualViewport = window.visualViewport;
  const sync = () => {
    frame = 0;
    const input = document.activeElement;
    const typing = !!input?.matches?.("textarea, input:not([type=checkbox]):not([type=radio]):not([type=range]):not([type=button]):not([type=submit]), [contenteditable=true]");
    const height = visualViewport?.height || window.innerHeight;
    const top = visualViewport?.offsetTop || 0;
    const keyboard = document.body.classList.contains("mobile-ui") && typing && window.innerHeight - height > 100;
    document.body.style.setProperty("--mobile-vv-height", `${height}px`);
    document.body.style.setProperty("--mobile-vv-top", `${top}px`);
    document.body.style.setProperty("--mobile-keyboard-inset", `${Math.max(0, window.innerHeight - height - top)}px`);
    document.body.classList.toggle("mobile-keyboard-open", keyboard);
    if (typing && !wasTyping) onTyping();
    wasTyping = typing;
    if (keyboard) {
      const rect = input.getBoundingClientRect();
      if (rect.bottom > top + height - 12 || rect.top < top + 12)
        input.scrollIntoView?.({ block: "nearest", inline: "nearest" });
    }
  };
  const schedule = () => { if (!frame) frame = requestAnimationFrame(sync); };
  window.addEventListener("resize", schedule);
  visualViewport?.addEventListener("resize", schedule);
  visualViewport?.addEventListener("scroll", schedule);
  document.addEventListener("focusin", schedule);
  document.addEventListener("focusout", schedule);
  sync();
  return sync;
}

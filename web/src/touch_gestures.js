// Stationary long press opens explicit actions; moving remains native scroll.
export function bindTouchLongPress(element, onHold, delay = 450) {
  let pointer = null, origin = null, timer = 0, suppressUntil = 0;
  const cancel = () => { clearTimeout(timer); timer = 0; pointer = null; };
  element.addEventListener("pointerdown", event => {
    if (event.pointerType !== "touch" || pointer !== null) return;
    pointer = event.pointerId; origin = { x: event.clientX, y: event.clientY };
    timer = setTimeout(() => {
      timer = 0;
      if (pointer === null || !element.isConnected || document.hidden || document.hasFocus?.() === false) return;
      suppressUntil = performance.now() + 1000;
      onHold();
    }, delay);
  });
  element.addEventListener("pointermove", event => {
    if (event.pointerId === pointer && Math.hypot(event.clientX - origin.x, event.clientY - origin.y) > 8) cancel();
  });
  for (const name of ["pointerup", "pointercancel", "lostpointercapture"])
    element.addEventListener(name, event => { if (event.pointerId === pointer) cancel(); });
  element.addEventListener("click", event => {
    if (performance.now() < suppressUntil) { event.preventDefault(); event.stopImmediatePropagation(); }
  }, { capture: true });
  element.addEventListener("contextmenu", event => {
    if (pointer !== null || performance.now() < suppressUntil) { event.preventDefault(); event.stopImmediatePropagation(); }
  }, { capture: true });
}

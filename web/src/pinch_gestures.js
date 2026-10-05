// Pointer-based pinch recognizer shared by the game camera and talent tree.
// A single touch remains available to the surrounding joystick/pan handler;
// the recognizer claims the gesture only after a second touch appears.
export function bindPinchGesture(element, { onStart, onChange, onEnd } = {}) {
  if (!element?.addEventListener) return () => {};

  const pointers = new Map();
  let gesture = null;

  const centerOf = () => {
    const values = [...pointers.values()];
    return { x: (values[0].x + values[1].x) * 0.5, y: (values[0].y + values[1].y) * 0.5 };
  };
  const distanceOf = () => {
    const values = [...pointers.values()];
    return Math.max(0.001, Math.hypot(values[0].x - values[1].x, values[0].y - values[1].y));
  };
  const claim = event => {
    if (pointers.size !== 2 || gesture) return;
    const center = centerOf();
    const distance = distanceOf();
    gesture = { startCenter: center, lastDistance: distance, startDistance: distance };
    onStart?.({ center, distance, event });
    // onStart may release a one-finger joystick/pan capture. Re-capture both
    // pointers only after that cleanup has completed.
    for (const id of pointers.keys()) element.setPointerCapture?.(id);
    event.preventDefault();
  };
  const onPointerDown = event => {
    if (event.pointerType !== "touch") return;
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    if (pointers.size === 2) claim(event);
  };
  const onPointerMove = event => {
    if (!pointers.has(event.pointerId)) return;
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    if (!gesture && pointers.size === 2) claim(event);
    if (!gesture) return;
    const center = centerOf();
    const distance = distanceOf();
    const previousDistance = gesture.lastDistance;
    gesture.lastDistance = distance;
    onChange?.({ center, startCenter: gesture.startCenter, distance,
      startDistance: gesture.startDistance, scale: distance / gesture.startDistance,
      deltaScale: distance / Math.max(0.001, previousDistance), event });
    event.preventDefault();
  };
  const finishPointer = event => {
    if (!pointers.has(event.pointerId)) return;
    pointers.delete(event.pointerId);
    if (gesture && pointers.size < 2) {
      onEnd?.({ event });
      gesture = null;
    }
  };
  const cancel = () => {
    if (gesture) onEnd?.({});
    gesture = null;
    pointers.clear();
  };

  element.addEventListener("pointerdown", onPointerDown, { passive: false });
  element.addEventListener("pointermove", onPointerMove, { passive: false });
  element.addEventListener("pointerup", finishPointer, { passive: false });
  element.addEventListener("pointercancel", finishPointer, { passive: false });
  window.addEventListener("blur", cancel);
  document.addEventListener("visibilitychange", () => { if (document.hidden) cancel(); });

  return () => {
    cancel();
    element.removeEventListener("pointerdown", onPointerDown);
    element.removeEventListener("pointermove", onPointerMove);
    element.removeEventListener("pointerup", finishPointer);
    element.removeEventListener("pointercancel", finishPointer);
    window.removeEventListener("blur", cancel);
  };
}

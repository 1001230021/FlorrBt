// Ad-hoc redraw requests from outside the tick loop (map chunks finishing,
// petal icons decoding) coalesce here: at most one extra drawScene per frame,
// and none at all when tick() already drew that frame. game.js binds the
// actual draw function at startup so callers never import the god module.
let redrawHandler = null;
let drawFrameQueued = false;
let lastDrawnFrameTime = 0;

export function setRedrawHandler(handler) {
  redrawHandler = handler;
}

// tick() reports every frame it draws so coalesced requests can skip frames
// that were already painted (rAF timestamps match).
export function noteFrameDrawn(time) {
  lastDrawnFrameTime = time;
}

export function requestRedraw() {
  if (drawFrameQueued || !redrawHandler) return;
  drawFrameQueued = true;
  requestAnimationFrame((time) => {
    drawFrameQueued = false;
    if (time - lastDrawnFrameTime < 1) return;
    redrawHandler(time);
  });
}

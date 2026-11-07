// 额外重绘请求合并到每帧至多一次；主循环已绘制的帧不再重画。
// 绘制函数由游戏启动时注入。
let redrawHandler = null;
let drawFrameQueued = false;
let lastDrawnFrameTime = 0;

export function setRedrawHandler(handler) {
  redrawHandler = handler;
}

// 主循环报告已绘制时间戳，让合并请求跳过重复帧。
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

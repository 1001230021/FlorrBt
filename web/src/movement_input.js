// 摇杆轴长度表示满速比例，沿用有符号字节轴协议。
export function clampMoveIntent(x, y) {
  if (!Number.isFinite(x) || !Number.isFinite(y)) return { x: 0, y: 0 };
  const length = Math.hypot(x, y);
  const scale = length > 1 ? 1 / length : 1;
  return { x: x * scale, y: y * scale };
}

export function getPointerMoveIntent(dx, dy, fullSpeedRadius, deadzone = 0) {
  if (!Number.isFinite(fullSpeedRadius) || fullSpeedRadius <= 0) return { x: 0, y: 0 };
  const length = Math.hypot(dx, dy);
  const inner = Math.max(0, Math.min(deadzone, fullSpeedRadius * 0.5));
  if (!Number.isFinite(length) || length <= inner) return { x: 0, y: 0 };
  // 从死区边缘连续增长，避免突然跳到 10% 速度。
  const strength = Math.min(1, (length - inner) / (fullSpeedRadius - inner));
  return { x: dx / length * strength, y: dy / length * strength };
}

export function quantizeMoveIntent(x, y) {
  const intent = clampMoveIntent(x, y);
  // 发送前统一量化，按键和指针使用相同精度。
  return {
    x: Math.round(intent.x * 127) / 127,
    y: Math.round(intent.y * 127) / 127,
  };
}

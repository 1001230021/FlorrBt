// Predict contact only against known stationary circles. Dynamic mob pushes
// stay authoritative on the server (the client does not know their masses).
export function resolveStaticCircleMotion(start, end, center, reach) {
  if (!(reach > 0)) return null;
  const sx = start.x - center.x, sy = start.y - center.y;
  const dx = end.x - start.x, dy = end.y - start.y;
  const a = dx * dx + dy * dy;
  const c = sx * sx + sy * sy - reach * reach;
  let pos = { x: end.x, y: end.y };
  let normal;
  if (c > 0 && a > 1e-12) {
    const b = sx * dx + sy * dy;
    const discriminant = b * b - a * c;
    if (b >= 0 || discriminant < 0) return null;
    const t = (-b - Math.sqrt(discriminant)) / a;
    if (t < 0 || t > 1) return null;
    const contact = { x: start.x + dx * t, y: start.y + dy * t };
    normal = { x: (contact.x - center.x) / reach, y: (contact.y - center.y) / reach };
    const rx = end.x - contact.x, ry = end.y - contact.y;
    const inward = Math.min(0, rx * normal.x + ry * normal.y);
    pos = { x: end.x - normal.x * inward, y: end.y - normal.y * inward };
  } else {
    const ex = end.x - center.x, ey = end.y - center.y;
    const distance = Math.hypot(ex, ey);
    if (distance > reach) return null;
    if (distance > 1e-6) normal = { x: ex / distance, y: ey / distance };
    else {
      const previousDistance = Math.hypot(sx, sy);
      normal = previousDistance > 1e-6 ? { x: sx / previousDistance, y: sy / previousDistance } : { x: 1, y: 0 };
    }
    pos = { x: center.x + normal.x * reach, y: center.y + normal.y * reach };
  }
  return { pos, normal };
}

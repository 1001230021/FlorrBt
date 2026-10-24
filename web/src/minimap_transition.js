const mix = (a, b, t) => a + (b - a) * t;

export function interpolateMinimapView(from, to, progress) {
  const t = Math.max(0, Math.min(1, progress));
  if (t === 0) return from;
  if (t === 1) return to;
  const eased = t * t * (3 - 2 * t);
  const width = Math.exp(mix(Math.log(from.bounds.width), Math.log(to.bounds.width), eased));
  const height = Math.exp(mix(Math.log(from.bounds.height), Math.log(to.bounds.height), eased));
  const cx = mix(from.bounds.left + from.bounds.width / 2, to.bounds.left + to.bounds.width / 2, eased);
  const cy = mix(from.bounds.top + from.bounds.height / 2, to.bounds.top + to.bounds.height / 2, eased);
  return {
    layout: { x: mix(from.layout.x, to.layout.x, eased), y: mix(from.layout.y, to.layout.y, eased),
      size: mix(from.layout.size, to.layout.size, eased) },
    bounds: { left: cx - width / 2, top: cy - height / 2, width, height },
    detail: mix(from.detail, to.detail, eased),
  };
}

export function sampleMinimapTransition(transition, target, now, duration) {
  if (!transition) return { view: target, complete: true };
  const progress = duration > 0 ? Math.max(0, (now - transition.startedAt) / duration) : 1;
  return { view: interpolateMinimapView(transition.from, target, progress), complete: progress >= 1 };
}

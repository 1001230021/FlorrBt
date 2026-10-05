// Bounded, visual-only presentation of authoritative server damage events.
export function formatCombatAmount(value) {
  if (!Number.isFinite(value) || value <= 0) return "";
  if (value < 0.05) return "<0.1";
  if (value < 1) return value.toFixed(1);
  if (value < 1e7) return String(Math.round(value));
  return `${(value / (value >= 1e9 ? 1e9 : 1e6)).toFixed(1)}${value >= 1e9 ? "b" : "m"}`;
}

export function createCombatFeedback() {
  const settlementIntervalMs = 1000;
  const labelLifetimeMs = 650;
  const pending = new Map();
  const labels = [];
  let serial = 0;

  function settlementKey(hit) {
    return `${hit.targetId}:${hit.kind}`;
  }

  function publish(entry, now) {
    if (!(entry.amount > 0)) return;
    if (labels.length >= 96) labels.shift();
    labels.push({
      ...entry,
      pos: { ...entry.pos },
      started: now,
      serial: serial++,
      text: formatCombatAmount(entry.amount),
    });
  }

  function settleDue(now) {
    for (const [key, entry] of pending) {
      if (now < entry.settleAt) continue;
      pending.delete(key);
      publish(entry, now);
    }
  }

  return {
    add(hits, now) {
      settleDue(now);
      for (const hit of hits || []) {
        if (!Number.isFinite(hit.amount) || hit.amount <= 0 || !Number.isFinite(hit.pos?.x) || !Number.isFinite(hit.pos?.y)) continue;
        const key = settlementKey(hit);
        const entry = pending.get(key);
        if (entry) {
          entry.amount += hit.amount;
          entry.pos = { ...hit.pos };
          entry.radius = hit.radius;
          entry.sourceType = hit.sourceType;
          entry.sourceRarity = hit.sourceRarity;
          continue;
        }
        pending.set(key, {
          ...hit,
          pos: { ...hit.pos },
          settleAt: now + settlementIntervalMs,
        });
      }
    },
    draw(ctx, now, project, drawText, enabled = true) {
      if (!enabled) { pending.clear(); labels.length = 0; return; }
      settleDue(now);
      for (let i = labels.length - 1; i >= 0; i--)
        if (now - labels[i].started >= labelLifetimeMs) labels.splice(i, 1);
      for (const label of labels) {
        const p = Math.max(0, (now - label.started) / labelLifetimeMs);
        const anchor = project({ x: label.pos.x, y: label.pos.y - label.radius * 0.65 });
        const offset = ((label.serial * 7) % 5 - 2) * 6;
        const size = label.kind === 1 ? 15 : 22;
        ctx.save();
        ctx.globalAlpha *= p < 0.55 ? 1 : (1 - p) / 0.45;
        drawText(label.text, anchor.x + offset, anchor.y - 32 * p, {
          font: `900 ${size * (1 + Math.max(0, 1 - p * 7) * 0.16)}px Florr UI, Sarasa SC, sans-serif`,
          fill: label.kind === 1 ? "#d89bec" : label.kind === 2 ? "#f8eaa0" : "#ff7373",
          stroke: "#382525", lineWidth: label.kind === 1 ? 2.5 : 3.5,
        });
        ctx.restore();
      }
    },
    clear() { pending.clear(); labels.length = 0; },
    count: () => labels.length,
    pendingCount: () => pending.size,
  };
}

export function stepHealthTrail(previous, value, now) {
  const health = Math.max(0, Math.min(1, Number(value) || 0));
  if (!previous) return { health, trail: health, holdUntil: now, at: now };
  if (health < previous.health - 0.0001) previous.holdUntil = now + 160;
  const dt = Math.max(0, Math.min(0.12, (now - previous.at) / 1000));
  previous.health = health;
  previous.trail = Math.max(health, previous.trail);
  if (now >= previous.holdUntil) previous.trail += (health - previous.trail) * (1 - Math.exp(-9 * dt));
  previous.at = now;
  return previous;
}

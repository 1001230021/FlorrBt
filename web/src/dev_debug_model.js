import { DEBUG_SELECTION_LIMIT } from "./dev_debug_protocol.js";

export function getDebugEntityKey(entity) {
  return `${entity.entityId}:${entity.generation}`;
}
export function getDebugWorldKey(world, generation) {
  return `${world}:${generation}`;
}
export function pointToDebugWorld(point, camera, size) {
  return { x: (point.x - size.width / 2) / camera.scale + camera.x,
    y: (point.y - size.height / 2) / camera.scale + camera.y };
}
export function pointToDebugScreen(point, camera, size) {
  return { x: (point.x - camera.x) * camera.scale + size.width / 2,
    y: (point.y - camera.y) * camera.scale + size.height / 2 };
}
export function zoomDebugCamera(camera, point, size, factor) {
  const anchor = pointToDebugWorld(point, camera, size);
  const scale = Math.max(0.015, Math.min(8, camera.scale * factor));
  return { scale, x: anchor.x - (point.x - size.width / 2) / scale,
    y: anchor.y - (point.y - size.height / 2) / scale };
}
export function hitTestDebugEntities(entities, point, filter = () => true) {
  let hit = null, score = Infinity;
  for (const entity of entities) {
    if (!filter(entity) || !entity.pos || !(entity.radius >= 0)) continue;
    const radius = Math.max(1, entity.radius);
    const distance = Math.hypot(point.x - entity.pos.x, point.y - entity.pos.y);
    // Prefer the smallest visible body under the cursor, so a giant boss
    // cannot make the players/petals standing on it impossible to select.
    if (distance <= radius && radius + distance / radius < score) {
      hit = entity; score = radius + distance / radius;
    }
  }
  return hit;
}
export function boxSelectDebugEntities(entities, a, b, filter = () => true, limit = DEBUG_SELECTION_LIMIT) {
  const minX = Math.min(a.x, b.x), maxX = Math.max(a.x, b.x);
  const minY = Math.min(a.y, b.y), maxY = Math.max(a.y, b.y);
  const result = [];
  for (const entity of entities) {
    if (!filter(entity) || !entity.pos) continue;
    if (entity.pos.x >= minX && entity.pos.x <= maxX && entity.pos.y >= minY && entity.pos.y <= maxY) {
      result.push(entity); if (result.length >= limit) break;
    }
  }
  return result;
}
export function toggleDebugSelection(selection, entity, additive = false) {
  const next = additive ? new Map(selection) : new Map();
  if (!entity) return next;
  const key = getDebugEntityKey(entity);
  if (additive && next.has(key)) next.delete(key);
  else if (next.size < DEBUG_SELECTION_LIMIT) next.set(key, { id: entity.entityId, generation: entity.generation });
  return next;
}
export function sortDebugPlayers(players, world, generation) {
  const nearby = player => player.world === world && player.worldGeneration === generation;
  return [...players].sort((a, b) => Number(nearby(b)) - Number(nearby(a)) ||
    String(a.name).localeCompare(String(b.name), "en", { sensitivity: "base", numeric: true }) || a.id - b.id);
}

// A bounded synchronous bridge to legacy renderer globals. Rendering must
// not be async: all game state is restored before any event/callback runs.
export function withDebugRenderState(state, patch, draw) {
  const saved = Object.keys(patch).map(key => [key, Object.hasOwn(state, key), state[key]]);
  try { Object.assign(state, patch); return draw(); }
  finally {
    for (const [key, existed, value] of saved) {
      if (existed) state[key] = value; else delete state[key];
    }
  }
}

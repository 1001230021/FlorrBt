import { flagShortcut, flagDead } from "./game_ids.js";

export function mapLayerRole(name) {
  return name === "shortcut" || name === "shortcut_cover" ? name : "terrain";
}

// Shared by normal flower lighting and shortcut reveal; deliberately not the
// final horizon, equipped bulb radius, weather darkness, or camera zoom.
export function defaultPlayerLightRadius(map) {
  return Math.max(Number(map?.tileWidth) || 512, Number(map?.tileHeight) || 512);
}

export function shortcutReveal(state, map) {
  if (!map || map !== state.map || state.mapLoadPromise) return null;
  const owner = state.entities?.get(state.ownerEntityId);
  if (!owner || owner.dying || (owner.snapshot?.flags & flagDead) || !(owner.snapshot?.flags & flagShortcut)) return null;
  const center = owner.renderPos || owner.snapshot?.pos;
  if (!center || !Number.isFinite(center.x) || !Number.isFinite(center.y)) return null;
  return { center, radius: defaultPlayerLightRadius(map) };
}

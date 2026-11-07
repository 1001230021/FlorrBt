import { flagShortcut, flagDead } from "./game_ids.js";

export function mapLayerRole(name) {
  return name === "shortcut" || name === "shortcut_cover" ? name : "terrain";
}

// 普通花朵照明和密道揭示共用默认半径，不受最终视距、灯泡、天气或相机缩放影响。
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

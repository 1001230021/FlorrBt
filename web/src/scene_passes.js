import { isDropEntity, isPetalEntity } from "./protocol.js";
import { state } from "./app_context.js";

export function collectSceneRenderPasses({ scale, isEntityInRenderView, isBoss }) {
  const passes = {
    ground: [],
    underlay: [],
    world: [],
    petals: [],
    overlays: [],
    bosses: [],
    owner: state.entities.get(state.ownerEntityId) || null,
  };

  for (const entity of state.entities.values()) {
    const snap = entity.snapshot;
    if (!snap || !isEntityInRenderView(entity, scale)) continue;
    if (snap.entityId === state.ownerEntityId) continue;

    if (snap.name === "SpiderWeb") {
      passes.ground.push(entity);
    } else if (isHornetMissileLayerEntity(snap)) {
      passes.underlay.push(entity);
    } else if (isPetalEntity(snap.entityType)) {
      passes.petals.push(entity);
    } else {
      passes.world.push(entity);
      if (isBoss?.(snap)) passes.bosses.push(entity);
    }
  }

  return passes;
}

function isHornetMissileLayerEntity(snap) {
  return snap && !isPetalEntity(snap.entityType) && !isDropEntity(snap.entityType) && snap.name === "Missile";
}

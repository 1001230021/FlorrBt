import {
  isDropEntity,
  isPetalEntity,
  petalTypeFromEntity,
} from "./protocol.js";
import { state } from "./app_context.js";
import {
  bloodSacrificeEffectType,
  dandelionMissileType,
  entityHasState,
  hornetMissileType,
  petalTrapperType,
  spiderWebZoneType,
  stateDigging,
} from "./game_ids.js";

export function collectSceneRenderPasses({
  scale,
  isEntityInRenderView,
  isBoss,
}) {
  const passes = {
    ground: [],
    underlay: [],
    world: [],
    petals: [],
    overlays: [],
    bosses: [],
    owner: null,
  };
  const owner = state.entities.get(state.ownerEntityId) || null;

  for (const entity of state.entities.values()) {
    const snap = entity.snapshot;
    if (!snap || !isEntityInRenderView(entity, scale)) continue;
    if (snap.entityId === state.ownerEntityId) {
      if (entityHasState(snap, stateDigging)) passes.ground.push(entity);
      else passes.owner = entity;
      continue;
    }

    if (entityHasState(snap, stateDigging)) {
      passes.ground.push(entity);
    } else if (
      snap.entityType === spiderWebZoneType ||
      snap.entityType === bloodSacrificeEffectType
    ) {
      passes.ground.push(entity);
    } else if (isHornetMissileLayerEntity(snap)) {
      passes.underlay.push(entity);
    } else if (isMountedPetalLayerEntity(snap)) {
      passes.underlay.push(entity);
    } else if (isPetalEntity(snap.entityType)) {
      passes.petals.push(entity);
    } else {
      passes.world.push(entity);
      if (isBoss?.(snap)) passes.bosses.push(entity);
    }
  }

  if (
    owner &&
    !passes.owner &&
    !passes.ground.includes(owner) &&
    isEntityInRenderView(owner, scale)
  )
    passes.owner = owner;

  return passes;
}

function isMountedPetalLayerEntity(snap) {
  return (
    snap &&
    isPetalEntity(snap.entityType) &&
    petalTypeFromEntity(snap.entityType) === petalTrapperType
  );
}

function isHornetMissileLayerEntity(snap) {
  return (
    snap &&
    !isPetalEntity(snap.entityType) &&
    !isDropEntity(snap.entityType) &&
    (snap.entityType === hornetMissileType ||
      snap.entityType === dandelionMissileType)
  );
}

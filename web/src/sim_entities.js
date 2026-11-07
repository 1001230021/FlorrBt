import { stepSnapshotPose, syncEntityRenderToSnapshot } from "./snapshot_pose.js";
export { syncEntityRenderToSnapshot } from "./snapshot_pose.js";
import { slotHasItem, state } from "./app_context.js";
import { worldToScreen } from "./camera.js";
import { stepFlowerFace } from "./flower_renderer.js";
import { clientRuntimeConfig } from "./client_config.js";
import { stepEntityAppearance, stepEntityGait } from "./entity_motion.js";
import { clearSpriteMotion } from "./sprite_motion.js";
import { beginDeathMotion, stepDeathMotion, getDeathTilt, stepDeathViewFactor } from "./death_motion.js";
import {
  entityHasState,
  entityState,
  dummyType,
  titanType,
  normalFlowerType,
  mechaFlowerType,
  flagAttacking,
  flagDefending,
  flagDead,
  playerFlowerType,
  stateHallucinogenic,
} from "./game_ids.js";
import { clearLadybugPattern } from "./ladybug_sprite.js";
import {
  clamp,
} from "./protocol.js";

// 位置只向最新权威快照插值；没有输入预测、速度外推或延迟追帧。
const {
  entitySnapshotVelocityBlend,
  hurtFlashDuration,
  serverFixedDt,
} = clientRuntimeConfig;

const flowerFaceTypes = new Set([playerFlowerType, normalFlowerType, mechaFlowerType, dummyType, titanType]);

export function updateRenderPositions(dt) {
  const owner = state.entities.get(state.ownerEntityId);
  const ownerDead = !!(owner?.snapshot?.flags & flagDead);
  state.deathViewFactor = stepDeathViewFactor(state.deathViewFactor, state.authenticated && ownerDead, dt);
  if (owner && !owner.dying) {
    if (ownerDead) {
      if (!owner.deathMotion) beginDeathMotion(owner, performance.now() / 1000, { resting: true });
      stepDeathMotion(owner, dt);
    } else {
      stepSnapshotPose(owner, dt, true);
    }
    if (owner.renderPos) {
      state.camera.x = owner.renderPos.x;
      state.camera.y = owner.renderPos.y;
    }
  }

  const dead = [];
  const now = performance.now() / 1000;
  for (const [id, entity] of state.entities) {
    const deadFlower = flowerFaceTypes.has(entity.snapshot?.entityType) && !!(entity.snapshot.flags & flagDead);
    if (deadFlower && id !== state.ownerEntityId && !entity.dying) {
      if (!entity.deathMotion) beginDeathMotion(entity, now, { resting: true });
      stepDeathMotion(entity, dt);
    }
    if (flowerFaceTypes.has(entity.snapshot?.entityType)) {
      const snap = entity.snapshot;
      let lookX = Math.cos(entity.renderAngle ?? snap.angle ?? -Math.PI / 2);
      let lookY = Math.sin(entity.renderAngle ?? snap.angle ?? -Math.PI / 2);
      if (id === state.ownerEntityId && state.mouse?.seen && !state.mouse.inUi) {
        const center = worldToScreen(entity.renderPos || snap.pos);
        const dx = state.mouse.x - center.x, dy = state.mouse.y - center.y;
        const distance = Math.max(80, Math.hypot(dx, dy));
        lookX = dx / distance; lookY = dy / distance;
      }
      entity.flowerFace = stepFlowerFace(entity.flowerFace, {
        dead: !!(snap.flags & flagDead), attacking: !!(snap.flags & flagAttacking),
        deadTilt: getDeathTilt(entity),
        defending: !!(snap.flags & flagDefending), lookX, lookY,
      }, dt);
    }
    if (entity.hurtFlashAge < hurtFlashDuration)
      entity.hurtFlashAge = Math.min(
        hurtFlashDuration,
        entity.hurtFlashAge + dt,
      );

    if (stepEntityAppearance(entity, dt)) dead.push(id);
    if (entity.dying) continue;
    stepEntityGait(entity, dt, now, state.serverTickInterval || serverFixedDt);
    if (id === state.ownerEntityId || deadFlower) continue;
    stepSnapshotPose(entity, dt);
  }


  for (const id of dead) {
    state.entities.delete(id);
    clearLadybugPattern(id);
    clearSpriteMotion(id);
  }
}

export function hasPlayerSnapPetal(snap, petalType) {
  return (snap.primarySlots || []).some(
    (slot) => slotHasItem(slot) && slot.petalType === petalType,
  );
}

export function updateEntitySnapshotVelocity(entity, snap, previousSnapshot) {
  if (!entity || !snap?.pos || !previousSnapshot?.pos) return;
  const dt = Math.max(
    0.005,
    (entity.snapshotAt || 0) - (entity.lastSnapshotAt || 0),
  );
  const sample = {
    x: (snap.pos.x - previousSnapshot.pos.x) / dt,
    y: (snap.pos.y - previousSnapshot.pos.y) / dt,
  };
  const previous = entity.snapshotVelocity || { x: sample.x, y: sample.y };
  entity.snapshotVelocity = {
    x: previous.x + (sample.x - previous.x) * entitySnapshotVelocityBlend,
    y: previous.y + (sample.y - previous.y) * entitySnapshotVelocityBlend,
  };
}

function transformHallucinationVector(vector, parameters) {
  const vx = Number(vector?.x) || 0;
  const vy = Number(vector?.y) || 0;
  const originalLength = Math.hypot(vx, vy);
  if (originalLength <= 0.000001 || !Array.isArray(parameters)) return vector;

  const value = (index) => {
    const parameter = Number(parameters[index]);
    return Number.isFinite(parameter) ? parameter : 0;
  };
  const angle = value(0);
  const scaleX = Math.exp(clamp(value(1), -4, 4));
  const scaleY = Math.exp(clamp(value(2), -4, 4));
  const shear = clamp(value(3), -8, 8);
  const localX = scaleX * vx + shear * vy;
  const localY = scaleY * vy;
  const cosine = Math.cos(angle);
  const sine = Math.sin(angle);
  const transformedX =
    cosine * localX - sine * localY + clamp(value(4), -8, 8) * originalLength;
  const transformedY =
    sine * localX + cosine * localY + clamp(value(5), -8, 8) * originalLength;
  const transformedLength = Math.hypot(transformedX, transformedY);
  if (!Number.isFinite(transformedLength) || transformedLength <= 0.000001)
    return vector;

  const preserveLength = originalLength / transformedLength;
  return {
    x: transformedX * preserveLength,
    y: transformedY * preserveLength,
  };
}

export function transformOwnerHallucinationVector(snap, vector) {
  if (
    !entityHasState(snap, stateHallucinogenic) ||
    !Array.isArray(snap?.hallucinations)
  )
    return vector;

  let transformed = vector;
  for (const parameters of snap.hallucinations)
    transformed = transformHallucinationVector(transformed, parameters);
  return transformed;
}

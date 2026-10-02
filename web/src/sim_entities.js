import { slotHasItem, state, ui } from "./app_context.js";
import { lerpAngle, smoothFactor, worldToScreen } from "./camera.js";
import { stepFlowerFace } from "./flower_renderer.js";
import { clientRuntimeConfig } from "./client_config.js";
import { resolveStaticCircleMotion } from "./circle_contact.js";
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
  petalMoonType,
  petalTrapperType,
  petalYinYangType,
  playerFlowerType,
  rarityPrimordial,
  stateDigging,
  stateHallucinogenic,
  stateNullification,
} from "./game_ids.js";
import { clearLadybugPattern } from "./ladybug_sprite.js";
import { clamp, isPetalEntity, petalTypeFromEntity } from "./protocol.js";

// Client-side entity simulation: the per-frame step that turns server
// snapshots into smooth render positions — owner movement prediction (with
// wall resolution and error correction), remote-entity interpolation and
// extrapolation, per-snapshot velocity estimation, and the trapper mount
// lock. game.js applies snapshots and calls into this module; drawing reads
// entity.renderPos/renderAngle.
const {
  entityAngleLerpRate,
  entityExtrapolateMaxDistance,
  entityExtrapolateMaxSeconds,
  entityRenderLerpRate,
  entitySnapshotVelocityBlend,
  hurtFlashDuration,
  ownerAngleLerpRate,
  ownerBaseAcceleration,
  ownerBaseMaxVelocity,
  ownerDiggingSpeedMultiplier,
  ownerPredictionHardCorrectionDistance,
  ownerPredictionHardCorrectionRate,
  ownerPredictionMinTickScale,
  ownerPredictionSnapshotStaleMin,
  ownerPredictionSoftCorrectionRate,
  ownerPredictionSpeedSampleMinDt,
  ownerPredictionTimingSampleCount,
  ownerRenderLerpRate,
  ownerRenderSnapBaseDistance,
  ownerSlowToMaxVelocityTime,
  ownerStopDampingPerTick,
  ownerStopVelocityEpsilon,
  packetInterval,
  petalAngleLerpRate,
  petalExtrapolateMaxDistance,
  petalExtrapolateMaxSeconds,
  petalRenderLerpRate,
  renderSnapBaseDistance,
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
    } else stepOwnerPrediction(owner, dt);
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
    stepEntityGait(entity, dt, now, state.ownerPredictionTiming?.snapshotInterval || serverFixedDt);
    if (id === state.ownerEntityId || deadFlower) continue;
    stepEntityRenderToSnapshot(entity, dt, false);
  }

  lockTrapperRenderPositionsToMounts();

  for (const id of dead) {
    state.entities.delete(id);
    clearLadybugPattern(id);
    clearSpriteMotion(id);
  }
}

function lockTrapperRenderPositionsToMounts() {
  const trappers = [];
  const mounts = [];

  for (const entity of state.entities.values()) {
    const snap = entity?.snapshot;
    if (!snap?.pos || entity.dying) continue;

    if (
      isPetalEntity(snap.entityType) &&
      petalTypeFromEntity(snap.entityType) === petalTrapperType
    ) {
      trappers.push(entity);
    } else if (
      snap.entityType === playerFlowerType ||
      (isPetalEntity(snap.entityType) &&
        petalTypeFromEntity(snap.entityType) === petalMoonType)
    ) {
      mounts.push(entity);
    }
  }

  for (const trapper of trappers) {
    const mount = findTrapperMountEntity(trapper, mounts);
    if (!mount?.renderPos) {
      trapper.trapperMountEntityId = null;
      continue;
    }

    const trapperSnap = trapper.snapshot;
    const mountSnap = mount.snapshot;
    trapper.trapperMountEntityId = mountSnap.entityId;
    trapper.renderPos.x =
      mount.renderPos.x + (trapperSnap.pos.x - mountSnap.pos.x);
    trapper.renderPos.y =
      mount.renderPos.y + (trapperSnap.pos.y - mountSnap.pos.y);
  }
}

function findTrapperMountEntity(trapper, mounts) {
  let best = null;
  let bestScore = Number.POSITIVE_INFINITY;
  const previousMountId = trapper.trapperMountEntityId;

  for (const mount of mounts) {
    const score = trapperMountScore(trapper, mount);
    if (!Number.isFinite(score)) continue;

    const stickyScore =
      mount.snapshot.entityId === previousMountId ? score * 0.75 : score;
    if (stickyScore >= bestScore) continue;
    best = mount;
    bestScore = stickyScore;
  }

  return best;
}

function trapperMountScore(trapper, mount) {
  const trapperSnap = trapper?.snapshot;
  const mountSnap = mount?.snapshot;
  if (!trapperSnap?.pos || !mountSnap?.pos || trapper === mount)
    return Number.POSITIVE_INFINITY;
  if (trapperSnap.team !== mountSnap.team) return Number.POSITIVE_INFINITY;

  const distance = Math.hypot(
    trapperSnap.pos.x - mountSnap.pos.x,
    trapperSnap.pos.y - mountSnap.pos.y,
  );
  let score = Math.abs(distance - Math.max(0, mountSnap.radius || 0));

  if (
    mountSnap.entityType === playerFlowerType &&
    hasPlayerSnapPetal(mountSnap, petalTrapperType)
  )
    score *= 0.5;
  return score;
}

export function hasPlayerSnapPetal(snap, petalType) {
  return (snap.primarySlots || []).some(
    (slot) => slotHasItem(slot) && slot.petalType === petalType,
  );
}

export function syncEntityRenderToSnapshot(entity, snap) {
  if (!entity || !snap?.pos) return;
  if (!entity.renderPos) entity.renderPos = { x: snap.pos.x, y: snap.pos.y };
  else {
    entity.renderPos.x = snap.pos.x;
    entity.renderPos.y = snap.pos.y;
  }
  if (Number.isFinite(snap.angle)) entity.renderAngle = snap.angle;
}

export function updateEntitySnapshotVelocity(entity, snap, previousSnapshot) {
  if (!entity || !snap?.pos || !previousSnapshot?.pos) return;
  const dt = Math.max(
    ownerPredictionSpeedSampleMinDt,
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

export function resetOwnerPredictionTiming(tickInterval = serverFixedDt) {
  const safeTickInterval = clamp(tickInterval || serverFixedDt, 0.001, 0.25);
  const ticksPerSnapshot = Math.max(
    1,
    Math.ceil(packetInterval / safeTickInterval - 0.000001),
  );
  const expectedSnapshotInterval = ticksPerSnapshot * safeTickInterval;
  state.ownerPredictionTiming = {
    expectedSnapshotInterval,
    snapshotInterval: expectedSnapshotInterval,
    simulationScale: 1,
    samples: [],
    lastSnapshotAt: 0,
    lastSnapshotId: -1,
  };
}

export function updateOwnerPredictionTiming(snapshotId, snapshotNow) {
  const timing = state.ownerPredictionTiming;
  if (!timing || !Number.isFinite(snapshotNow)) return;

  const idDelta = snapshotId - timing.lastSnapshotId;
  const elapsed = snapshotNow - timing.lastSnapshotAt;
  if (
    timing.lastSnapshotId >= 0 &&
    idDelta > 0 &&
    idDelta <= 120 &&
    elapsed > 0
  ) {
    const sample = elapsed / idDelta;
    const expected = timing.expectedSnapshotInterval;
    if (sample >= expected * 0.5 && sample <= Math.max(2, expected * 12)) {
      timing.samples.push(sample);
      if (timing.samples.length > ownerPredictionTimingSampleCount)
        timing.samples.shift();

      const sorted = [...timing.samples].sort((a, b) => a - b);
      const middle = Math.floor(sorted.length / 2);
      timing.snapshotInterval =
        sorted.length % 2 === 0
          ? (sorted[middle - 1] + sorted[middle]) * 0.5
          : sorted[middle];

      if (timing.samples.length >= 3) {
        const targetScale = clamp(
          expected / timing.snapshotInterval,
          ownerPredictionMinTickScale,
          1,
        );
        const blend = targetScale < timing.simulationScale ? 0.32 : 0.08;
        timing.simulationScale +=
          (targetScale - timing.simulationScale) * blend;
      }
    }
  }

  timing.lastSnapshotAt = snapshotNow;
  timing.lastSnapshotId = snapshotId;
}

export function initOwnerPrediction(
  entity,
  snap = entity?.snapshot,
  forceSnap = false,
) {
  if (!entity || !snap?.pos) return null;
  const prediction = entity.ownerPrediction || {};
  const shouldSnap =
    forceSnap ||
    !prediction.pos ||
    !Number.isFinite(prediction.pos.x) ||
    !Number.isFinite(prediction.pos.y);

  if (shouldSnap) {
    prediction.pos = { x: snap.pos.x, y: snap.pos.y };
    prediction.vel = { x: 0, y: 0 };
    prediction.correction = { x: 0, y: 0 };
    prediction.lastServerPos = { x: snap.pos.x, y: snap.pos.y };
    prediction.lastSnapshotAt = entity.snapshotAt || performance.now() / 1000;
    prediction.staleTime = 0;
    prediction.snapped = true;
    entity.renderPos = { x: snap.pos.x, y: snap.pos.y };
    entity.serverRenderPos = { x: snap.pos.x, y: snap.pos.y };
  } else {
    if (!prediction.vel) prediction.vel = { x: 0, y: 0 };
    if (!prediction.correction) prediction.correction = { x: 0, y: 0 };
    if (!prediction.lastServerPos)
      prediction.lastServerPos = { x: snap.pos.x, y: snap.pos.y };
  }

  entity.ownerPrediction = prediction;
  if (!Number.isFinite(entity.renderAngle) && Number.isFinite(snap.angle))
    entity.renderAngle = snap.angle;
  return prediction;
}

function stepOwnerPrediction(entity, dt) {
  const snap = entity?.snapshot;
  if (!snap?.pos || dt <= 0) return;
  const prediction = initOwnerPrediction(entity, snap, false);
  if (!prediction) return;

  const staleNow = Math.max(
    0,
    performance.now() / 1000 -
      (prediction.lastSnapshotAt || entity.snapshotAt || 0),
  );
  prediction.staleTime = staleNow;
  const timing = state.ownerPredictionTiming;
  const snapshotInterval = Math.max(
    timing?.expectedSnapshotInterval || 0,
    timing?.snapshotInterval || 0,
  );
  const staleGrace = Math.max(
    ownerPredictionSnapshotStaleMin,
    snapshotInterval * 2.5,
  );
  const staleFadeDuration = Math.max(0.08, snapshotInterval);
  const staleMultiplier =
    staleNow <= staleGrace
      ? 1
      : clamp(1 - (staleNow - staleGrace) / staleFadeDuration, 0, 1);
  const simulationScale = timing?.simulationScale || 1;
  const simulationDt = dt * simulationScale;
  const diggingMultiplier =
    entityHasState(snap, stateDigging) ? ownerDiggingSpeedMultiplier : 1;
  const speedScale = state.ownerMovementMultiplier || 1;
  const maxVelocity =
    ownerBaseMaxVelocity * diggingMultiplier * speedScale * staleMultiplier;
  const acceleration =
    ownerBaseAcceleration * diggingMultiplier * speedScale * staleMultiplier;

  let vel = prediction.vel || { x: 0, y: 0 };
  const speed = Math.hypot(vel.x, vel.y);
  if (speed > maxVelocity && ownerSlowToMaxVelocityTime > 0) {
    const slowAmount =
      ((speed - maxVelocity) * simulationDt) / ownerSlowToMaxVelocityTime;
    const nextSpeed = Math.max(maxVelocity, speed - slowAmount);
    const scale = speed > 0 ? nextSpeed / speed : 0;
    vel = { x: vel.x * scale, y: vel.y * scale };
  }

  const move = transformOwnerHallucinationVector(
    snap,
    state.localMoveInput || { x: 0, y: 0 },
  );
  const moveLength = Math.hypot(move.x, move.y);
  if (moveLength > 0.0001 && maxVelocity > 0 && acceleration > 0) {
    const requestedSpeed = maxVelocity * Math.min(1, moveLength);
    const desired = {
      x: (move.x / moveLength) * requestedSpeed,
      y: (move.y / moveLength) * requestedSpeed,
    };
    const diff = { x: desired.x - vel.x, y: desired.y - vel.y };
    const diffLength = Math.hypot(diff.x, diff.y);
    const maxAccel = acceleration * simulationDt;
    if (diffLength <= maxAccel || diffLength <= 0.000001) {
      vel = desired;
    } else {
      vel = {
        x: vel.x + (diff.x / diffLength) * maxAccel,
        y: vel.y + (diff.y / diffLength) * maxAccel,
      };
    }
  } else {
    const tickInterval = state.serverTickInterval || serverFixedDt;
    const damping = Math.pow(
      ownerStopDampingPerTick,
      simulationDt / tickInterval,
    );
    vel = { x: vel.x * damping, y: vel.y * damping };
    if (vel.x * vel.x + vel.y * vel.y <= ownerStopVelocityEpsilon)
      vel = { x: 0, y: 0 };
  }

  prediction.vel = vel;
  const radius = Math.max(0, Number(snap.radius) || 0);
  const predictedStart = { x: prediction.pos.x, y: prediction.pos.y };
  const predictedEnd = {
    x: predictedStart.x + vel.x * simulationDt,
    y: predictedStart.y + vel.y * simulationDt,
  };
  const predictedMotion = resolveOwnerPredictedMotion(
    predictedStart,
    predictedEnd,
    radius,
  );
  prediction.pos = predictedMotion.pos;
  if (predictedMotion.normals?.length) {
    for (const normal of predictedMotion.normals) {
      const intoWall = vel.x * normal.x + vel.y * normal.y;
      if (intoWall < 0) {
        vel = {
          x: vel.x - normal.x * intoWall,
          y: vel.y - normal.y * intoWall,
        };
      }
    }
    prediction.vel = vel;
  }

  const correctionDistance = Math.hypot(
    prediction.correction?.x || 0,
    prediction.correction?.y || 0,
  );
  if (correctionDistance > 0) {
    const correctionRate =
      correctionDistance >= ownerPredictionHardCorrectionDistance
        ? ownerPredictionHardCorrectionRate
        : ownerPredictionSoftCorrectionRate;
    const amount = smoothFactor(correctionRate, dt);
    const correctionStart = { x: prediction.pos.x, y: prediction.pos.y };
    const correctionEnd = {
      x: correctionStart.x + prediction.correction.x * amount,
      y: correctionStart.y + prediction.correction.y * amount,
    };
    prediction.pos = resolveOwnerPredictedMotion(
      correctionStart,
      correctionEnd,
      radius,
    ).pos;
    prediction.correction.x *= 1 - amount;
    prediction.correction.y *= 1 - amount;
    if (Math.hypot(prediction.correction.x, prediction.correction.y) < 0.01)
      prediction.correction = { x: 0, y: 0 };
  }

  entity.renderPos = { x: prediction.pos.x, y: prediction.pos.y };
  entity.serverRenderPos = { x: snap.pos.x, y: snap.pos.y };
  if (Number.isFinite(snap.angle)) {
    if (!Number.isFinite(entity.renderAngle)) entity.renderAngle = snap.angle;
    else
      entity.renderAngle = lerpAngle(
        entity.renderAngle,
        snap.angle,
        smoothFactor(ownerAngleLerpRate, dt),
      );
  }

  state.predictionDebug = {
    errorDistance: Math.hypot(
      snap.pos.x - prediction.pos.x,
      snap.pos.y - prediction.pos.y,
    ),
    speedScale,
    tickScale: simulationScale,
    snapshotInterval,
    staleTime: prediction.staleTime || 0,
    snapped: !!prediction.snapped,
  };
  prediction.snapped = false;
}

function stepEntityRenderToSnapshot(entity, dt, isOwner = false) {
  const snap = entity?.snapshot;
  if (!snap?.pos) return;

  if (
    !entity.renderPos ||
    !Number.isFinite(entity.renderPos.x) ||
    !Number.isFinite(entity.renderPos.y)
  ) {
    syncEntityRenderToSnapshot(entity, snap);
    return;
  }

  const petalType = isPetalEntity(snap.entityType)
    ? petalTypeFromEntity(snap.entityType)
    : 0;
  const target = entityInterpolationTarget(entity, snap, isOwner, petalType);
  const dx = target.x - entity.renderPos.x;
  const dy = target.y - entity.renderPos.y;
  const dist = Math.hypot(dx, dy);
  const snapDistance =
    (isOwner ? ownerRenderSnapBaseDistance : renderSnapBaseDistance) +
    Math.max(0, snap.radius || 0) * 3;
  if (dist >= snapDistance) {
    syncEntityRenderToSnapshot(entity, snap);
  } else if (dist > 0) {
    const renderRate =
      petalType > 0
        ? petalRenderLerpRate
        : isOwner
          ? ownerRenderLerpRate
          : entityRenderLerpRate;
    const factor = smoothFactor(renderRate, dt);
    entity.renderPos.x += dx * factor;
    entity.renderPos.y += dy * factor;
  }

  if (Number.isFinite(snap.angle)) {
    if (!Number.isFinite(entity.renderAngle)) {
      entity.renderAngle = snap.angle;
    } else {
      const angleRate =
        petalType > 0
          ? petalAngleLerpRate
          : isOwner
            ? ownerAngleLerpRate
            : entityAngleLerpRate;
      entity.renderAngle = lerpAngle(
        entity.renderAngle,
        snap.angle,
        smoothFactor(angleRate, dt),
      );
    }
  }
}

function entityInterpolationTarget(entity, snap, isOwner, petalType = 0) {
  if (isOwner || !entity?.snapshotVelocity || !snap?.pos)
    return snap?.pos || { x: 0, y: 0 };

  const age = Math.max(0, performance.now() / 1000 - (entity.snapshotAt || 0));
  if (petalType === petalYinYangType) return snap.pos;
  const maxLead =
    petalType > 0 ? petalExtrapolateMaxSeconds : entityExtrapolateMaxSeconds;
  const lead = Math.min(maxLead, age + serverFixedDt);
  const vx = entity.snapshotVelocity.x || 0;
  const vy = entity.snapshotVelocity.y || 0;
  let ex = vx * lead;
  let ey = vy * lead;
  const distance = Math.hypot(ex, ey);
  const maxDistance =
    petalType > 0
      ? Math.max(
          petalExtrapolateMaxDistance,
          Math.max(0, snap.radius || 0) * 0.4,
        )
      : Math.max(
          entityExtrapolateMaxDistance,
          Math.max(0, snap.radius || 0) * 1.5,
        );
  if (distance > maxDistance && distance > 0) {
    const scale = maxDistance / distance;
    ex *= scale;
    ey *= scale;
  }
  return { x: snap.pos.x + ex, y: snap.pos.y + ey };
}

function ownerCollidesWithWalls() {
  // Driven by the nullification STATE the server reports, not by what petal
  // is equipped: the null petal only provides the state, and the client's
  // wall phasing follows primordial nullification from the snapshot.
  const owner = state.entities.get(state.ownerEntityId);
  const snap = owner?.snapshot;
  if (!snap) return true;
  const nullState = entityState(snap, stateNullification);
  return !(nullState && nullState.rarity >= rarityPrimordial);
}

function resolveOwnerPredictedMotion(start, end, radius) {
  const walls = ownerCollidesWithWalls();
  const result = walls
    ? ui.mapRenderer.resolvePredictedCircleMotion(start, end, radius)
    : { pos: { x: end.x, y: end.y }, normals: [] };
  const owner = state.entities.get(state.ownerEntityId);
  const snap = owner?.snapshot;
  // State-dependent phasing can be configured server-side. Without that
  // configuration, defer to authoritative snapshots instead of blocking it.
  if (!snap || entityHasState(snap, stateNullification) || entityHasState(snap, stateDigging)) return result;
  if (contactSnapshot !== state.snapshotId || contactEntities !== state.entities || contactOwner !== owner) {
    contactSnapshot = state.snapshotId;
    contactEntities = state.entities;
    contactOwner = owner;
    stationaryContacts = [];
    for (const entity of state.entities.values()) {
      if (entity.snapshot?.entityType === dummyType) stationaryContacts.push(entity);
    }
  }
  let circleCorrected = false;
  for (const entity of stationaryContacts) {
    const other = entity.snapshot;
    if (entity.dying || (other.flags & flagDead) || entityHasState(other, stateNullification) || entityHasState(other, stateDigging)) continue;
    const contact = resolveStaticCircleMotion(start, result.pos, other.pos, radius + Math.max(0, other.radius));
    if (!contact) continue;
    result.pos = contact.pos;
    result.normals.push(contact.normal);
    circleCorrected = true;
  }
  if (walls && circleCorrected) {
    // Circle correction must not put the player into an adjacent map wall.
    const bounded = ui.mapRenderer.resolvePredictedCircleMotion(start, result.pos, radius);
    result.pos = bounded.pos;
    result.normals.push(...bounded.normals);
  }
  return result;
}

let contactSnapshot = -1;
let contactEntities = null;
let contactOwner = null;
let stationaryContacts = [];

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

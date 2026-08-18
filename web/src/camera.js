import { dom, state, ui } from "./app_context.js";
import { clientRuntimeConfig } from "./client_config.js";
import { clearLadybugPattern } from "./ladybug_sprite.js";
import { petalMoonType, petalTrapperType, playerFlowerType } from "./game_ids.js";
import { clamp, isPetalEntity, petalTypeFromEntity } from "./protocol.js";
import {
  currentRenderLoad,
  currentVisibleEntityCount,
  playerSnapHasPetal,
  stepEntityRenderToSnapshot,
  stepOwnerPrediction,
  updateParticles,
} from "./game.js";

const { canvas, ctx, debugInfo } = dom;
const { deathFadeDuration, hurtFlashDuration } = clientRuntimeConfig;

export function resizeCanvas() {
  state.dpr = Math.max(1, Math.min(2, window.devicePixelRatio || 1));
  state.canvasWidth = window.innerWidth;
  state.canvasHeight = window.innerHeight;
  canvas.width = Math.round(state.canvasWidth * state.dpr);
  canvas.height = Math.round(state.canvasHeight * state.dpr);
  canvas.style.width = `${state.canvasWidth}px`;
  canvas.style.height = `${state.canvasHeight}px`;
  ctx.setTransform(state.dpr, 0, 0, state.dpr, 0, 0);
}

export function updateRenderPositions(dt) {
  updateParticles(dt);
  const owner = state.entities.get(state.ownerEntityId);
  if (owner && !owner.dying) {
    updateEntityMotion(owner, dt);
    stepOwnerPrediction(owner, dt);
    if (owner.renderPos) {
      state.camera.x = owner.renderPos.x;
      state.camera.y = owner.renderPos.y;
    }
  }

  const dead = [];
  for (const [id, entity] of state.entities) {
    if (entity.hurtFlashAge < hurtFlashDuration)
      entity.hurtFlashAge = Math.min(
        hurtFlashDuration,
        entity.hurtFlashAge + dt,
      );

    if (entity.dying) {
      entity.deathAge = (entity.deathAge || 0) + dt;
      if (entity.deathAge >= deathFadeDuration) dead.push(id);
      continue;
    }
    updateEntityMotion(entity, dt);
    if (id === state.ownerEntityId) continue;
    stepEntityRenderToSnapshot(entity, dt, false);
  }

  lockTrapperRenderPositionsToMounts();

  for (const id of dead) {
    state.entities.delete(id);
    clearLadybugPattern(id);
  }
}

export function lockTrapperRenderPositionsToMounts() {
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

export function findTrapperMountEntity(trapper, mounts) {
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

export function trapperMountScore(trapper, mount) {
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
    playerSnapHasPetal(mountSnap, petalTrapperType)
  )
    score *= 0.5;
  return score;
}

export function updateEntityMotion(entity, dt) {
  const rawMove = entity.snapshotMove || 0;
  const target = rawMove < 0.16 ? 0 : clamp((rawMove - 0.16) / 1.25, 0, 1);
  const current = entity.motionBlend || 0;
  const rate = target > current ? 18 : 14;
  entity.motionBlend = current + (target - current) * smoothFactor(rate, dt);
  if (entity.motionBlend < 0.04) entity.motionBlend = 0;
}

export function smoothFactor(rate, dt) {
  return 1 - Math.exp(-rate * dt);
}

export function normalizeAngle(angle) {
  const full = Math.PI * 2;
  let value = angle % full;
  if (value < 0) value += full;
  return value;
}

export function lerpAngle(start, end, amount) {
  const full = Math.PI * 2;
  const t = clamp(amount, 0, 1);
  let from = normalizeAngle(start);
  let to = normalizeAngle(end);
  if (Math.abs(to - from) > Math.PI) {
    if (to > from) from += full;
    else to += full;
  }
  return normalizeAngle(from + (to - from) * t);
}

export function screenViewRadius() {
  // The view circle (radius = viewRadius) circumscribes the canvas rectangle:
  // screen diagonal == circle diameter, so the server's snapshot circle covers
  // every pixel on screen (no corner pop-in).
  return Math.max(96, Math.hypot(state.canvasWidth, state.canvasHeight) / 2);
}

export function scaleForViewRadius(viewRadius) {
  if (!viewRadius || viewRadius <= 0) return 1;
  return screenViewRadius() / viewRadius;
}

export function worldScale() {
  return scaleForViewRadius(state.viewRadius);
}

export function worldLengthToScreen(value) {
  return value * worldScale();
}

export function currentScreenShake() {
  const shake = state.screenShake || { x: 0, y: 0 };
  return {
    x: Number.isFinite(shake.x) ? shake.x : 0,
    y: Number.isFinite(shake.y) ? shake.y : 0,
  };
}

export function worldToScreen(pos) {
  const scale = worldScale();
  const shake = currentScreenShake();
  return {
    x: (pos.x - state.camera.x) * scale + state.canvasWidth * 0.5 + shake.x,
    y: (pos.y - state.camera.y) * scale + state.canvasHeight * 0.5 + shake.y,
  };
}

export function screenToWorld(pos) {
  const scale = worldScale();
  const shake = currentScreenShake();
  return {
    x: (pos.x - state.canvasWidth * 0.5 - shake.x) / scale + state.camera.x,
    y: (pos.y - state.canvasHeight * 0.5 - shake.y) / scale + state.camera.y,
  };
}

export function debugNumber(value, digits = 1) {
  return Number.isFinite(value) ? value.toFixed(digits) : "-";
}

export function updateDebugInfo() {
  if (!debugInfo) return;
  if (!state.consoleOpen) {
    debugInfo.textContent = "";
    return;
  }

  const owner = state.entities.get(state.ownerEntityId);
  const ownerPos = owner?.renderPos || owner?.snapshot?.pos || null;
  const ownerSnap = owner?.snapshot || null;
  const mouseWorld = state.mouse.seen ? screenToWorld(state.mouse) : null;
  const map = state.map;
  const chunkCache = map?.chunkCache?.size || 0;
  const chunkQueue = map?.chunkBuildQueue?.length || 0;
  const visibleChunks = map?.visibleChunkKeys?.size || 0;
  const chunkProfile = map?.chunkProfileLabel || "-";
  const inputMode = state.movementMode;
  const socketState = state.ws
    ? ["connecting", "open", "closing", "closed"][state.ws.readyState] ||
      state.ws.readyState
    : "none";
  const prediction = state.predictionDebug || {};

  debugInfo.textContent = [
    `pos (${debugNumber(ownerPos?.x)}, ${debugNumber(ownerPos?.y)})  id ${state.ownerEntityId || "-"}  hp ${debugNumber(ownerSnap?.hpPercent * 100, 0)}%`,
    `cam (${debugNumber(state.camera.x)}, ${debugNumber(state.camera.y)})  mouse ${mouseWorld ? `(${debugNumber(mouseWorld.x)}, ${debugNumber(mouseWorld.y)})` : "(-, -)"}`,
    `view ${debugNumber(state.viewRadius, 0)}  scale ${debugNumber(worldScale(), 4)}  fps ${debugNumber(state.fps, 1)}  dpr ${debugNumber(state.dpr, 2)}  pred err ${debugNumber(prediction.errorDistance)} mmt ${debugNumber(prediction.speedScale, 2)} tick ${debugNumber(prediction.tickScale, 2)} pkt ${debugNumber((prediction.snapshotInterval || 0) * 1000, 0)}ms stale ${debugNumber(prediction.staleTime, 2)}${prediction.snapped ? " snap" : ""}`,
    `entities ${state.entities.size}/${currentVisibleEntityCount}  render load ${currentRenderLoad}  map ${map?.path || "-"}  chunks ${chunkCache}/${visibleChunks} q${chunkQueue} ${chunkProfile}  input ${inputMode}  ws ${socketState}`,
  ].join("\n");
}

export function drawGrid() {
  ui.mapRenderer.drawGrid();
}


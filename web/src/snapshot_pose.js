import { isPetalEntity } from "./protocol.js";
import { clientRuntimeConfig as config } from "./client_config.js";

export function syncEntityRenderToSnapshot(entity, snap) {
  if (!entity || !snap?.pos) return;
  entity.renderPos = { x: snap.pos.x, y: snap.pos.y };
  if (Number.isFinite(snap.angle)) entity.renderAngle = snap.angle;
}

// 所有目标均来自已确认快照；输入、速度和收包年龄不参与位置计算。
export function stepSnapshotPose(entity, dt, isOwner = false) {
  const snap = entity?.snapshot;
  if (!snap?.pos || !Number.isFinite(snap.pos.x) || !Number.isFinite(snap.pos.y)) return;
  if (!entity.renderPos || !Number.isFinite(entity.renderPos.x) || !Number.isFinite(entity.renderPos.y)) {
    syncEntityRenderToSnapshot(entity, snap); return;
  }
  const time = Number.isFinite(dt) ? Math.max(0, dt) : 0;
  const petal = isPetalEntity(snap.entityType);
  const dx = snap.pos.x - entity.renderPos.x, dy = snap.pos.y - entity.renderPos.y;
  const threshold = (isOwner ? config.ownerRenderSnapBaseDistance : config.renderSnapBaseDistance) + Math.max(0, snap.radius || 0) * 3;
  if (Math.hypot(dx, dy) >= threshold) {
    syncEntityRenderToSnapshot(entity, snap); return;
  }
  const rate = petal ? config.petalRenderLerpRate : isOwner ? config.ownerRenderLerpRate : config.entityRenderLerpRate;
  const k = -Math.expm1(-rate * time);
  entity.renderPos.x += dx * k; entity.renderPos.y += dy * k;
  if (!Number.isFinite(snap.angle)) return;
  if (!Number.isFinite(entity.renderAngle)) { entity.renderAngle = snap.angle; return; }
  const angleRate = petal ? config.petalAngleLerpRate : isOwner ? config.ownerAngleLerpRate : config.entityAngleLerpRate;
  const delta = Math.atan2(Math.sin(snap.angle - entity.renderAngle), Math.cos(snap.angle - entity.renderAngle));
  entity.renderAngle += delta * -Math.expm1(-angleRate * time);
}

import { UI_FONT_FAMILY } from "./ui_typography.js";
import { clamp, rarityColor } from "./protocol.js";
import { getHudPortraitLayout } from "./ui_layout.js";
import { partyRoster } from "./party_state.js";
import { flagDead } from "./game_ids.js";
import { t } from "./i18n.js";

export function createPlayerHud({ state, getContext, getGameUiLayout, getPlayerLevelRarity, motionFactor, approachMotion, drawCapsuleBar, drawTrackedHealthBar, drawShieldBar, healthBarColor, drawActorPortrait, drawOutlinedText }) {
  const hudValueMotions = new Map();
  const hudMotionRate = 13;
  function smoothHudStats(key, target, identity, now) {
    const normalized = {
      health: clamp(Number(target?.health) || 0, 0, 1),
      shield: clamp(Number(target?.shield) || 0, 0, 1),
      experience: clamp(Number(target?.experience) || 0, 0, 1),
      mana: clamp(Number(target?.mana) || 0, 0, 1),
    };
    let motion = hudValueMotions.get(key);
    if (!motion || motion.identity !== identity) {
      motion = {
        identity,
        ...normalized,
        lastUpdatedAt: now,
      };
      hudValueMotions.set(key, motion);
      return motion;
    }

    const factor = motionFactor(now, motion.lastUpdatedAt, hudMotionRate);
    motion.lastUpdatedAt = now;
    motion.health = approachMotion(motion.health, normalized.health, factor);
    motion.shield = approachMotion(motion.shield, normalized.shield, factor);
    motion.experience = approachMotion(
      motion.experience,
      normalized.experience,
      factor,
    );
    motion.mana = approachMotion(motion.mana, normalized.mana, factor);
    return motion;
  }

  function drawSelfHud(now = performance.now()) {
    const ctx = getContext();
    const owner = state.authenticated
      ? state.entities.get(state.ownerEntityId)
      : null;
    if (!owner) return;

    const ownerDead = (owner.snapshot.flags & flagDead) !== 0;
    const hudStats = smoothHudStats(
      "owner",
      {
        health: owner.snapshot.hpPercent,
        shield: owner.snapshot.shieldPercent,
        experience: state.ownerExpProgress,
      },
      `${owner.snapshot.entityId}:${ownerDead ? 1 : 0}`,
      now,
    );
    const health = hudStats.health;
    const expProgress = hudStats.experience;
    const name = owner.snapshot?.name || "Player";
    const level = Math.max(1, state.ownerLevel || owner?.snapshot?.rarity || 1);
    const levelColor = rarityColor(getPlayerLevelRarity(owner.snapshot), 1);
    const hud = getHudPortraitLayout(getGameUiLayout());
    const scale = hud.scale;
    const flowerPos = hud.flower;
    const flowerRadius = hud.radius;
    const { x: healthX, y: healthY, width: healthW, height: healthH } = hud.health;
    const healthBackX = hud.backX;
    const healthBackW = healthX + healthW - healthBackX;
    const { x: expX, y: expY, width: expW, height: expH } = hud.experience;

    ctx.save();
    drawCapsuleBar(
      healthBackX,
      healthY,
      healthBackW,
      healthH,
      0,
      "#69d348",
      6 * scale,
    );
    drawTrackedHealthBar(
      owner.snapshot,
      healthX,
      healthY,
      healthW,
      healthH,
      health,
      healthBarColor(owner.snapshot),
      6 * scale,
    );
    drawShieldBar(
      healthX,
      healthY,
      healthW,
      healthH,
      hudStats.shield,
    );
    drawCapsuleBar(expX, expY, expW, expH, expProgress, "#e8e95a", 4 * scale);
    drawActorPortrait(owner, flowerPos, flowerRadius, true);
    let nameSize = 22 * scale;
    ctx.font = `900 ${nameSize}px ${UI_FONT_FAMILY}`;
    nameSize *= Math.min(1, (healthW - 16 * scale) / Math.max(1, ctx.measureText(name).width));
    drawOutlinedText(name, healthX + healthW * 0.5, healthY + healthH * 0.5, {
      font: `900 ${nameSize}px ${UI_FONT_FAMILY}`,
      fill: "#f7f7f7",
      lineWidth: 3 * scale,
    });

    drawOutlinedText(`Lvl ${level}`, expX + expW * 0.5, expY + expH * 0.5, {
      font: `900 ${13 * scale}px ${UI_FONT_FAMILY}`,
      fill: "#f7f7f7",
      lineWidth: 3.8 * scale,
    });
    ctx.restore();
  }

  function drawSquadHud(now = performance.now()) {
    const ctx = getContext();
    const members = partyRoster(state).slice(0, 3);
    if (!members.length) return;

    const layout = getGameUiLayout();
    const scale = layout.scale;
    const memberHudScale = 0.78;
    const healthBorder = 6 * memberHudScale * scale;

    for (let index = 0; index < members.length; index += 1) {
      const { entity, member, status } = members[index];
      const hud = getHudPortraitLayout(layout, index);
      const { x: flowerX, y: centerY } = hud.flower;
      const flowerRadius = hud.radius;
      const { x: healthX, width: healthW, height: healthH } = hud.health;
      const healthBackX = hud.backX;
      const healthBackW = healthX + healthW - healthBackX;
      if (!entity) {
        drawCapsuleBar(healthBackX, centerY - healthH * 0.5, healthBackW, healthH, 0, "#888", healthBorder);
        ctx.save(); ctx.fillStyle = "#8b8b8b"; ctx.strokeStyle = "#333"; ctx.lineWidth = 3 * scale;
        ctx.beginPath(); ctx.arc(flowerX, centerY, flowerRadius, 0, Math.PI * 2); ctx.fill(); ctx.stroke(); ctx.restore();
        drawOutlinedText(Array.from(member.name || "?")[0], flowerX, centerY, {
          font: `900 ${18 * scale}px ${UI_FONT_FAMILY}`, fill: "#eee", align: "center", lineWidth: 2 * scale,
        });
        drawOutlinedText(member.name || t("party.player"), healthX + healthW / 2, centerY, {
          font: `900 ${12 * scale}px ${UI_FONT_FAMILY}`, fill: "#ddd", align: "center", lineWidth: 2 * scale,
        });
        drawOutlinedText(t(`party.${status}`), healthX + 3 * scale, hud.levelY, {
          font: `900 ${10 * scale}px ${UI_FONT_FAMILY}`, fill: "#bbb", baseline: "top", lineWidth: 2 * scale,
        });
        continue;
      }
      const snap = entity.snapshot;
      const memberDead = (snap.flags & flagDead) !== 0;
      const hudStats = smoothHudStats(
        `squad:${member.playerId}:${member.sessionGeneration}`,
        { health: snap.hpPercent, shield: snap.shieldPercent },
        `${snap.entityId}:${snap.entityGeneration}:${memberDead ? 1 : 0}`,
        now,
      );
      const health = hudStats.health;
      const level = Math.max(1, snap.rarity || 1);
      const levelText = member.connected ? `Lvl ${level}` : t("party.offline");
      const levelFont = `900 ${11.5 * scale}px ${UI_FONT_FAMILY}`;
      const levelX = healthX + 3 * scale;

      drawCapsuleBar(
        healthBackX,
        centerY - healthH * 0.5,
        healthBackW,
        healthH,
        0,
        "#69d348",
        healthBorder,
      );
      drawTrackedHealthBar(
        snap,
        healthX,
        centerY - healthH * 0.5,
        healthW,
        healthH,
        health,
        healthBarColor(snap),
        healthBorder,
      );
      drawShieldBar(
        healthX,
        centerY - healthH * 0.5,
        healthW,
        healthH,
        hudStats.shield,
      );
      drawActorPortrait(entity, { x: flowerX, y: centerY }, flowerRadius);

      const ownerPos = state.entities.get(state.ownerEntityId)?.renderPos;
      if (ownerPos && entity.renderPos && !memberDead) {
        const direction = Math.atan2(entity.renderPos.y - ownerPos.y, entity.renderPos.x - ownerPos.x);
        ctx.save();
        ctx.translate(flowerX, centerY); ctx.rotate(direction);
        const markerX = flowerRadius + 7 * scale;
        ctx.fillStyle = "#f7f4e8"; ctx.strokeStyle = "#333"; ctx.lineWidth = 1.5 * scale;
        ctx.beginPath(); ctx.moveTo(markerX + 6 * scale, 0);
        ctx.lineTo(markerX - 3 * scale, -4 * scale); ctx.lineTo(markerX - 3 * scale, 4 * scale);
        ctx.closePath(); ctx.fill(); ctx.stroke(); ctx.restore();
      }

      ctx.font = levelFont;
      const maxNameWidth = Math.max(15 * scale, healthW - 12 * scale);
      let nameSize = 18 * scale;
      ctx.font = `900 ${nameSize}px ${UI_FONT_FAMILY}`;
      const measuredNameWidth = ctx.measureText(member.name || t("party.player")).width;
      if (measuredNameWidth > maxNameWidth)
        nameSize = Math.max(9 * scale, nameSize * (maxNameWidth / measuredNameWidth));

      drawOutlinedText(member.name || t("party.player"), healthX + healthW / 2, centerY, {
        font: `900 ${nameSize}px ${UI_FONT_FAMILY}`,
        fill: "#f7f7f7",
        lineWidth: 3 * scale,
        align: "center",
      });
      drawOutlinedText(levelText, levelX, hud.levelY, {
        font: levelFont,
        fill: rarityColor(getPlayerLevelRarity(snap), 1),
        lineWidth: 2.8 * scale,
        align: "left", baseline: "top",
      });
    }
  }

  return { drawSelf: drawSelfHud, drawParty: drawSquadHud, smooth: smoothHudStats, clear: () => hudValueMotions.clear() };
}

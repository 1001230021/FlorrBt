// 合成卡片保持正立并绕中心旋转，等待动画轮数和服务器结果同时完成。
// 成功时快速汇聚并显示结果；失败时展开归位，消耗卡片按统一消失姿态退出。

import { animatePetalCardVanish } from "./card_motion.js";

const TWO_PI = Math.PI * 2;

const smoothStep = t => t * t * (3 - 2 * t);

// 每轮快速收拢、缓慢展开，端点径向速度为零，避免方向切换时突变。
export function getCraftPulseRadius(phase, inner = 0.4) {
  const p = Math.max(0, Math.min(1, phase));
  const low = Math.max(0.28, Math.min(0.72, inner));
  if (p < 0.38) return 1 - (1 - low) * smoothStep(p / 0.38);
  return low + (1 - low) * smoothStep((p - 0.38) / 0.62);
}

const requestFrame = (callback) =>
  typeof globalThis.requestAnimationFrame === "function"
    ? globalThis.requestAnimationFrame(callback)
    : globalThis.setTimeout(() => callback(performance.now()), 16);

const cancelFrame = (frame) => {
  if (typeof globalThis.cancelAnimationFrame === "function")
    globalThis.cancelAnimationFrame(frame);
  else globalThis.clearTimeout(frame);
};

function easeOutCubic(t) {
  return 1 - Math.pow(1 - t, 3);
}

export function createCraftSpinController({
  stage,
  particles,
  rarityColor,
  roundMs = 600,
  revolutionsPerRound = 2,
  minRounds = 0,
  maxRounds = 7,
  roundScale = 0.5,
  rampMs = 90,
  watchdogMs = 20000,
} = {}) {
  let active = false;
  let frame = 0;
  let startTime = 0;
  let cards = [];
  let plannedRounds = 2;
  let pullTargets = new Map();
  let outcome = null;
  let finalSeg = null;
  let watchdogTimer = 0;
  let onTimeout = null;
  let lastFactor = 1;

  const omega = (revolutionsPerRound * TWO_PI) / roundMs; // 单位为弧度/毫秒。

  // 起步使用缓动后的旋转时间，此后与实际时间等速推进。
  const spinTime = (t) =>
    rampMs <= 0 ? t : t <= rampMs ? (t * t) / (2 * rampMs) : t - rampMs / 2;

  const stageCenter = () => {
    return { x: stage.clientWidth / 2, y: stage.clientHeight / 2 };
  };

  const collectCards = (slotRarities) => {
    const buttons = stage.querySelectorAll("[data-craft-slot]");
    const result = [];
    buttons.forEach((button) => {
      const index = Number(button.dataset.craftSlot || 0);
      const stack = button.querySelector(".petal-stack");
      if (!stack) return;
      // 布局坐标不继承面板显示变换；按钮中心由 left/top 和居中平移确定。
      let hx = button.offsetLeft - stage.clientWidth / 2;
      let hy = button.offsetTop - stage.clientHeight / 2;
      const styles = globalThis.getComputedStyle?.(button);
      if (!Number.isFinite(hx)) hx = Number.parseFloat(styles?.getPropertyValue("--slot-x"));
      if (!Number.isFinite(hy)) hy = Number.parseFloat(styles?.getPropertyValue("--slot-y"));
      if (!Number.isFinite(hx) || !Number.isFinite(hy)) {
        const stageRect = stage.getBoundingClientRect();
        const buttonRect = button.getBoundingClientRect();
        hx = buttonRect.left + buttonRect.width / 2 -
          (stageRect.left + stageRect.width / 2);
        hy = buttonRect.top + buttonRect.height / 2 -
          (stageRect.top + stageRect.height / 2);
      }
      if (!Number.isFinite(hx)) hx = 0;
      if (!Number.isFinite(hy)) hy = 0;
      result.push({
        button,
        stack,
        index,
        rarity: slotRarities?.[index] ?? 1,
        homeX: hx,
        homeY: hy,
        homeAngle: Math.atan2(hy, hx),
        homeRadius: Math.hypot(hx, hy) || 1,
        vanished: false,
        lastX: hx,
        lastY: hy,
        vanish: null,
      });
    });
    return result;
  };

  const roundTarget = (k) => {
    let target = pullTargets.get(k);
    if (target === undefined) {
      // 对随机数变换，使较小半径更常见。
      target = 0.3 + 0.38 * (1 - Math.sqrt(Math.random()));
      pullTargets.set(k, target);
    }
    return target;
  };

  const radiusFactorAt = (k, phase) => {
    return getCraftPulseRadius(phase, roundTarget(k));
  };

  const placeCard = (card, spinAngle, factor) => {
    const x = Math.cos(card.homeAngle + spinAngle) * card.homeRadius * factor;
    const y = Math.sin(card.homeAngle + spinAngle) * card.homeRadius * factor;
    card.lastX = x;
    card.lastY = y;
    card.button.style.transform = `translate(-50%, -50%) translate(${x}px, ${y}px)`;
  };

  const restoreCard = (card) => {
    card.button.classList.remove("craft-orbiting");
    card.button.style.transform = "";
  };

  const restoreAll = () => {
    for (const card of cards) restoreCard(card);
  };

  const stop = () => {
    if (frame) cancelFrame(frame);
    frame = 0;
    if (watchdogTimer) clearTimeout(watchdogTimer);
    watchdogTimer = 0;
    active = false;
  };

  const finish = (callback) => {
    stop();
    const done = callback;
    cards = [];
    outcome = null;
    finalSeg = null;
    done?.();
  };

  const step = (now) => {
    frame = 0;
    const u = spinTime(now - startTime);
    const spinAngle = u * omega;
    const k = Math.floor(u / roundMs);
    const phase = (u % roundMs) / roundMs;

    if (!finalSeg) {
      if (u >= plannedRounds * roundMs && outcome) {
        const fromFactor = lastFactor;
        finalSeg = {
          kind: outcome.success ? "converge" : "return",
          startU: u,
          fromFactor,
          durationMs: 120,
          startPositions: cards.map(card => ({ x: card.lastX, y: card.lastY })),
        };
        // 卡片离开前显示对应尺寸的空底板，避免末段运动闪现。
        for (const card of cards) card.button.classList.add("craft-card-fading");
      } else {
        const factor = radiusFactorAt(k, phase);
        lastFactor = factor;
        for (const card of cards) placeCard(card, spinAngle, factor);
        frame = requestFrame(step);
        return;
      }
    }

    if (finalSeg.kind === "converge") {
      const p = Math.min(1, (u - finalSeg.startU) / finalSeg.durationMs);
      const factor = finalSeg.fromFactor * (1 - easeOutCubic(p));
      const cardAlpha = 1 - easeOutCubic(p);
      for (const card of cards) {
        placeCard(card, spinAngle, factor);
        card.stack.style.opacity = String(cardAlpha);
      }
      if (p < 1) {
        frame = requestFrame(step);
        return;
      }
      const center = stageCenter();
      for (const card of cards) {
        card.stack.style.opacity = "0";
        restoreCard(card);
      }
      particles.burst(center.x, center.y, rarityColor(outcome.resultRarity, 1), {
        count: 42,
        speed: 180,
        size: 4,
        life: 0.65,
      });
      finish(outcome.onFinished);
      return;
    }

    if (finalSeg.kind === "return") {
      // 失败时曲线归位并轻微回转，消耗卡片在途中淡出并产生稀有度粒子。
      const returnMs = 360;
      const p = Math.min(1, (u - finalSeg.startU) / returnMs);
      const eased = easeOutCubic(p);
      cards.forEach((card, i) => {
        const from = finalSeg.startPositions[i];
        const arc = Math.sin(p * Math.PI) * 12;
        const x = from.x + (card.homeX - from.x) * eased - Math.sin(card.homeAngle) * arc;
        const y = from.y + (card.homeY - from.y) * eased + Math.cos(card.homeAngle) * arc;
        const settleSpin = Math.sin(p * Math.PI) * 0.22;
        card.lastX = x;
        card.lastY = y;
        card.button.style.transform =
          `translate(-50%, -50%) translate(${x}px, ${y}px) rotate(${settleSpin}rad)`;

        if (!card.vanished && (outcome.slotWillBeEmpty?.[card.index] || outcome.slotLostCount?.[card.index] > 0) && p >= 0.12) {
          card.vanished = true;
          card.button.classList.add("craft-card-fading");
          const center = stageCenter();
          particles.burst(center.x + x, center.y + y, rarityColor(card.rarity, 1), {
            count: 24,
            speed: 135,
            size: 3.5,
            life: 0.6,
          });
          if (outcome.slotWillBeEmpty?.[card.index])
            card.vanish = animatePetalCardVanish(card.stack, { durationMs: 220 });
        }
      });
      if (p < 1) {
        frame = requestFrame(step);
        return;
      }
      restoreAll();
      finish(outcome.onFinished);
    }
  };

  return {
    isActive: () => active,

    /* 启动现有卡片的悬念旋转，slotRarities 用于失败粒子颜色。 */
    begin({ slotRarities, onWatchdogTimeout } = {}) {
      if (active) this.cancel();
      cards = collectCards(slotRarities);
      if (!cards.length) return false;

      active = true;
      outcome = null;
      finalSeg = null;
      pullTargets = new Map();
      lastFactor = 1;
      const span = Math.max(0, maxRounds - minRounds);
      plannedRounds =
        (minRounds + Math.floor(Math.random() * (span + 1))) * roundScale;
      startTime = performance.now();
      onTimeout = onWatchdogTimeout;

      for (const card of cards) {
        card.stack.classList.remove("craft-card-entering");
        card.button.classList.add("craft-orbiting");
        // 立即设置第一帧轨道姿态，避免服务器快速响应时动画尚未显示。
        placeCard(card, 0, 1);
      }
      frame = requestFrame(step);
      // 服务器不回应时也不能无限旋转。
      watchdogTimer = setTimeout(() => {
        if (!active || outcome) return;
        restoreAll();
        finish(null);
        onTimeout?.();
      }, watchdogMs);
      return true;
    },

    /* 接收服务端结果，结束动画后调用 onFinished；未旋转时立即调用。 */
    resolve({ success, resultRarity, slotWillBeEmpty, slotLostCount, onFinished }) {
      if (!active) {
        onFinished?.();
        return;
      }
      outcome = { success, resultRarity, slotWillBeEmpty, slotLostCount, onFinished };
    },

    cancel() {
      if (!active && !cards.length) return;
      restoreAll();
      for (const card of cards) {
        card.vanish?.cancel?.();
        card.stack.style.opacity = "";
        card.stack.style.transform = "";
      }
      particles.clear();
      finish(null);
    },
  };
}

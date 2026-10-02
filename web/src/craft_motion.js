// Craft-stage orbit animation (florr-style gamble spin).
//
// While a craft round-trips to the server the five staged cards orbit the
// stage center in "rounds": every round lasts craftSpinRoundMs and spins the
// formation craftSpinRevolutionsPerRound full turns at constant angular
// velocity (after a short spin-up ramp). The suspense count is uniformly
// chosen from 0..7, then halved to 0..3.5 effective rounds. Each round breathes inward then
// outward along a smooth curve; cards remain upright. The spin waits until BOTH
// the suspense round count has elapsed AND the server verdict arrived, then:
//   - success: a fast convergence into the center, a square-particle
//     burst in the crafted rarity's color, and the result card pops in;
//   - failure: the ring expands and settles at home; consumed cards
//     vanish in flight (unified card vanish) in a burst of their
//     own rarity color.
//
// Cards stay upright while orbiting (florr cards do not self-rotate); the
// only self-rotation happens on the return-home flight and the vanish pose.

import { animatePetalCardVanish } from "./card_motion.js";

const TWO_PI = Math.PI * 2;

const smoothStep = t => t * t * (3 - 2 * t);

// Each 600ms cycle has a fast intake and a longer release. Endpoints have
// zero radial velocity: unlike linear legs the radius no longer hits a wall
// whenever it changes direction. Exposed for deterministic offline checks.
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

  const omega = (revolutionsPerRound * TWO_PI) / roundMs; // rad per ms

  // Spin-up ramp: u(t) is the "spin time" that drives both the angle and the
  // round clock, easing in over rampMs then advancing 1:1 with wall time so
  // the angular velocity stays constant afterwards.
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
      // Layout coordinates do not inherit panel scale/entrance transforms.
      // The button's CSS center is its left/top (translate(-50%,-50%)).
      let hx = button.offsetLeft - stage.clientWidth / 2;
      let hy = button.offsetTop - stage.clientHeight / 2;
      const styles = globalThis.getComputedStyle?.(button);
      if (!Number.isFinite(hx)) hx = Number.parseFloat(styles?.getPropertyValue("--slot-x"));
      if (!Number.isFinite(hy)) hy = Number.parseFloat(styles?.getPropertyValue("--slot-y"));
      // Last-resort fallback for nonstandard embedded layouts.
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

  // Shallow draws happen occasionally, but deep contractions are more common.
  const roundTarget = (k) => {
    let target = pullTargets.get(k);
    if (target === undefined) {
      // Smaller radii are more common: x in [0, 1] is transformed so its
      // density decreases as the radius multiplier grows.
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
      // Regular suspense rounds until the verdict and the round budget land.
      if (u >= plannedRounds * roundMs && outcome) {
        const fromFactor = lastFactor;
        finalSeg = {
          kind: outcome.success ? "converge" : "return",
          startU: u,
          fromFactor,
          durationMs: 120,
          startPositions: cards.map(card => ({ x: card.lastX, y: card.lastY })),
        };
        // Reveal the exact-sized empty backing before the card leaves it.
        // This prevents the ring from popping out during the last movement.
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
      // Success: snap into the center quickly instead of spending another
      // full suspense round on the result reveal.
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
      // Failure, phase b: fly home with a settling rotation; consumed cards
      // vanish mid-flight in a burst of their own rarity color.
      const returnMs = 360;
      const p = Math.min(1, (u - finalSeg.startU) / returnMs);
      const eased = easeOutCubic(p);
      cards.forEach((card, i) => {
        const from = finalSeg.startPositions[i];
        // A curved return with a small settling roll, not an instantaneous
        // 270-degree tilt at the first frame of the old return phase.
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

    /**
     * Start the suspense spin over the currently rendered staged cards.
     * slotRarities: per-slot rarity (for failure particle colors).
     */
    begin({ slotRarities, onWatchdogTimeout } = {}) {
      if (active) this.cancel();
      cards = collectCards(slotRarities);
      if (!cards.length) return false;

      active = true;
      outcome = null;
      finalSeg = null;
      pullTargets = new Map();
      lastFactor = 1;
      // Every integer in the configured range has the same probability.
      const span = Math.max(0, maxRounds - minRounds);
      plannedRounds =
        (minRounds + Math.floor(Math.random() * (span + 1))) * roundScale;
      startTime = performance.now();
      onTimeout = onWatchdogTimeout;

      for (const card of cards) {
        card.stack.classList.remove("craft-card-entering");
        card.button.classList.add("craft-orbiting");
        // Apply the first orbit pose immediately so the animation is visible
        // even when a fast server response arrives before the next paint.
        placeCard(card, 0, 1);
      }
      frame = requestFrame(step);
      // A craft the server never answers must not spin forever.
      watchdogTimer = setTimeout(() => {
        if (!active || outcome) return;
        restoreAll();
        finish(null);
        onTimeout?.();
      }, watchdogMs);
      return true;
    },

    /**
     * Deliver the server verdict. onFinished fires once the closing animation
     * lands (immediately when the spin is not running).
     */
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

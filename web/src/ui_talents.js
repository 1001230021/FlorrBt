import { talentNodes, talentNodeByKey, talentKey } from "./talent_data.js";
import { buildTalentLayout, collectTalentChain, collectTalentRemoval } from "./talent_layout.js";
import { createTalentRotation, stopTalentRotation, talentDragAngle, releaseTalentRotation,
  stepTalentRotation, talentRotationConfig } from "./talent_rotation.js";
import { getTalentGlyph, getTalentRootGlyph } from "./talent_icons.js";
import { t } from "./i18n.js";
import { rarityColor, rarityName, translationSlug } from "./protocol.js";
import { setAnimatedVisibility } from "./ui_motion.js";
import { bindPinchGesture } from "./pinch_gestures.js";

const NS = "http://www.w3.org/2000/svg";
const HOLD_MS = 520;
const nameOf = node => t(`talents.names.${translationSlug(node.name)}`, {}, { defaultValue: node.name });
const sumCost = nodes => nodes.reduce((sum, node) => sum + node.cost, 0);

export function createTalentTree({ panel, viewport, points, getState, request }) {
  const layout = buildTalentLayout(talentNodes);
  const graph = document.createElement("div");
  graph.className = "talent-graph";
  const links = document.createElementNS(NS, "svg");
  links.classList.add("talent-links");
  links.setAttribute("aria-hidden", "true");
  graph.appendChild(links);
  const root = document.createElement("button");
  root.type = "button";
  root.className = "talent-root";
  root.innerHTML = getTalentRootGlyph();
  graph.appendChild(root);
  viewport.replaceChildren(graph);
  viewport.tabIndex = 0;

  const footer = document.createElement("div");
  footer.className = "talent-footer";
  const hint = document.createElement("span");
  hint.className = "talent-help";
  hint.setAttribute("role", "status");
  const home = document.createElement("button");
  home.type = "button";
  home.className = "talent-home";
  const reset = document.createElement("button");
  reset.type = "button";
  reset.className = "talent-reset";
  footer.append(hint, home, reset);
  for (const [symbol, factor, label] of [["−", 1 / 1.2, "zoomOut"], ["+", 1.2, "zoomIn"]]) {
    const button = document.createElement("button");
    button.type = "button"; button.className = "talent-zoom-control"; button.textContent = symbol;
    button.dataset.i18nAriaLabel = `ui.mobile.${label}`;
    button.setAttribute("aria-label", t(`ui.mobile.${label}`));
    button.addEventListener("click", () => {
      releasePointer(); hideTip();
      view.scale = Math.max(0.35, Math.min(1.65, view.scale * factor));
      queueView();
    });
    footer.appendChild(button);
  }
  panel.appendChild(footer);

  const tooltip = document.createElement("div");
  tooltip.id = "talentTooltip";
  tooltip.className = "talent-tooltip ui hidden";
  tooltip.setAttribute("role", "tooltip");
  const tipName = document.createElement("strong");
  const tipRarity = document.createElement("span");
  tipRarity.className = "talent-tooltip-rarity";
  const tipEffect = document.createElement("p");
  const tipAction = document.createElement("p");
  tipAction.className = "talent-tooltip-action";
  tooltip.append(tipName, tipRarity, tipEffect, tipAction);
  document.body.appendChild(tooltip);

  const buttons = new Map(), edgeEls = new Map();
  let owned = new Set(), previousOwned = null;
  const view = { x: 0, y: 0, scale: 0.8 };
  let initialized = false;
  const rotation = createTalentRotation();
  let previousFrameTime = 0;
  let hold = null, drag = null, frame = 0, pending = null, pendingTimer = 0;
  let hovered = null, resetArmed = false, resetTimer = 0;
  let suppressClickUntil = 0;

  function setHint(key = "ui.talents.panHint") { hint.textContent = t(key); }
  const reducedMotion = () => !!globalThis.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches;
  function applyView() {
    graph.style.transform = `translate(${view.x}px, ${view.y}px) scale(${view.scale}) rotate(${rotation.angle}rad)`;
    graph.style.setProperty("--talent-counter-rotation", `${-rotation.angle}rad`);
  }
  function drawView(now = performance.now()) {
    frame = 0;
    const dt = Math.min(0.1, Math.max(0, (now - previousFrameTime) / 1000));
    previousFrameTime = now;
    const moving = stepTalentRotation(rotation, dt, reducedMotion());
    applyView();
    if (moving && panel.classList.contains("open")) frame = requestAnimationFrame(drawView);
  }
  function queueView() {
    if (!frame) { previousFrameTime = performance.now(); frame = requestAnimationFrame(drawView); }
  }
  function stopMotion() {
    stopTalentRotation(rotation);
    if (frame) cancelAnimationFrame(frame);
    frame = 0;
    applyView();
  }
  function anchorView() {
    const mobile = document.body.classList.contains("mobile-ui");
    view.x = viewport.clientWidth * 0.5;
    view.y = viewport.clientHeight - Math.min(mobile ? 72 : viewport.clientWidth * 0.1, viewport.clientHeight * 0.15);
  }
  function centerView() {
    const first = !initialized;
    initialized = true;
    const mobile = document.body.classList.contains("mobile-ui");
    anchorView();
    view.scale = mobile ? Math.max(0.7, Math.min(1.15, viewport.clientWidth / 550))
      : Math.max(0.55, Math.min(1.65, viewport.clientWidth / 520));
    rotation.velocity = 0;
    rotation.target = first ? 0 : rotation.angle - Math.atan2(Math.sin(rotation.angle), Math.cos(rotation.angle));
    if (first) rotation.angle = 0;
    queueView();
  }
  function hideTip() {
    hovered = null;
    setAnimatedVisibility(tooltip, false, { exitDuration: 70 });
  }
  function showTip(node, button) {
    if (!panel.classList.contains("open") || drag?.moved || Math.abs(rotation.velocity) > 0.01 || Math.abs(rotation.target - rotation.angle) > 0.01) return;
    hovered = { node, button };
    const isOwned = owned.has(node.key);
    const chain = collectTalentChain(node, owned, talentNodeByKey);
    tipName.textContent = nameOf(node);
    tipRarity.textContent = rarityName(node.rarity);
    tipRarity.style.color = rarityColor(node.rarity);
    tipEffect.textContent = t(`talents.effects.${translationSlug(node.name)}`);
    tipAction.textContent = isOwned
      ? t("ui.talents.removeRefund", { cost: sumCost(collectTalentRemoval(node, owned, talentNodes)) })
      : t("ui.talents.learnCost", { cost: sumCost(chain), count: chain.length });
    setAnimatedVisibility(tooltip, true, { enterDuration: 100 });
    const rect = button.getBoundingClientRect();
    const width = tooltip.offsetWidth || 280, height = tooltip.offsetHeight || 140;
    tooltip.style.left = `${Math.max(8, Math.min(innerWidth - width - 8, rect.left + rect.width / 2 - width / 2))}px`;
    tooltip.style.top = `${Math.max(8, rect.top >= height + 12 ? rect.top - height - 10 : Math.min(innerHeight - height - 8, rect.bottom + 10))}px`;
  }
  function cancelHold() {
    if (!hold) return;
    clearTimeout(hold.timer);
    hold.button.classList.remove("holding");
    hold = null;
  }
  function sendRequest(action, nodes, key) {
    if (pending || !getState().authenticated || !nodes.length) return false;
    if (request(action, nodes) === false) { setHint("ui.talents.requestFailed"); return false; }
    pending = key;
    pendingTimer = setTimeout(() => {
      pending = null;
      render();
      setHint("ui.talents.requestFailed");
    }, 5000);
    render();
    setHint("ui.talents.pending");
    return true;
  }
  function beginHold(node, button) {
    if (getState().talentReadOnly) return;
    cancelHold();
    const state = getState();
    const isOwned = owned.has(node.key);
    const chain = collectTalentChain(node, owned, talentNodeByKey);
    if (pending || !state.authenticated) return;
    if (!isOwned && sumCost(chain) > state.talentPoints) {
      setHint("ui.talents.insufficient");
      return;
    }
    button.classList.add("holding");
    hold = { button, timer: setTimeout(() => {
      cancelHold();
      // 操作前重新检查权威拥有状态，不使用缓存界面结果。
      const nowOwned = owned.has(node.key);
      const nodes = nowOwned ? [node] : collectTalentChain(node, owned, talentNodeByKey);
      if (nowOwned || sumCost(nodes) <= getState().talentPoints)
        sendRequest(nowOwned ? "remove" : "add", nodes, node.key);
    }, HOLD_MS) };
  }

  for (const edge of layout.edges) {
    const a = layout.positions.get(edge.from) || { x: 0, y: 0 };
    const b = layout.positions.get(edge.to);
    const line = document.createElementNS(NS, "line");
    for (const [key, value] of Object.entries({ x1: a.x, y1: a.y, x2: b.x, y2: b.y })) line.setAttribute(key, value);
    links.appendChild(line);
    edgeEls.set(edge.to, line);
  }
  for (const node of talentNodes) {
    const position = layout.positions.get(node.key);
    if (!position) continue;
    const button = document.createElement("button");
    button.type = "button";
    button.className = "talent-node";
    button.dataset.talentKey = node.key;
    button.style.left = `${position.x}px`;
    button.style.top = `${position.y}px`;
    button.style.setProperty("--talent-color", rarityColor(node.rarity));
    button.style.setProperty("--talent-hold-duration", `${HOLD_MS}ms`);
    button.setAttribute("aria-describedby", tooltip.id);
    const icon = document.createElement("span");
    icon.className = "talent-icon";
    icon.innerHTML = getTalentGlyph(node.id);
    const cost = document.createElement("span");
    cost.className = "talent-cost";
    const ring = document.createElementNS(NS, "svg");
    ring.setAttribute("viewBox", "0 0 64 64");
    ring.setAttribute("aria-hidden", "true");
    ring.classList.add("talent-hold-ring");
    const circle = document.createElementNS(NS, "circle");
    for (const [key, value] of Object.entries({ cx: 32, cy: 32, r: 29, pathLength: 1 })) circle.setAttribute(key, value);
    ring.appendChild(circle);
    button.append(icon, cost, ring);
    button.addEventListener("pointerenter", () => showTip(node, button));
    button.addEventListener("pointerleave", () => { if (!hold) hideTip(); });
    button.addEventListener("focus", () => showTip(node, button));
    button.addEventListener("blur", () => { cancelHold(); hideTip(); });
    button.addEventListener("keydown", event => {
      if (event.key !== " " && event.key !== "Enter") return;
      event.preventDefault(); event.stopPropagation();
      if (!event.repeat) beginHold(node, button);
    });
    button.addEventListener("keyup", event => {
      // 释放仍交给全局输入清理，避免按键按住时焦点转移导致卡住。
      if (event.key === " " || event.key === "Enter") { event.preventDefault(); cancelHold(); }
    });
    button.addEventListener("click", event => { event.preventDefault(); event.stopPropagation(); });
    graph.appendChild(button);
    buttons.set(node.key, { button, cost });
  }

  viewport.addEventListener("pointerdown", event => {
    if (event.button !== 0 || drag || viewport._pinchStart) return;
    event.preventDefault(); event.stopPropagation();
    stopMotion();
    const button = event.target.closest("[data-talent-key]");
    drag = { id: event.pointerId, x: event.clientX, y: event.clientY, lastX: event.clientX, lastY: event.clientY,
      lastTime: performance.now(), velocity: 0, moved: false };
    viewport.setPointerCapture?.(event.pointerId);
    if (button) {
      const node = talentNodeByKey.get(button.dataset.talentKey);
      showTip(node, button);
      beginHold(node, button);
    } else { cancelHold(); hideTip(); }
  });
  viewport.addEventListener("pointermove", event => {
    if (!drag || drag.id !== event.pointerId) return;
    const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
    if (!drag.moved && Math.hypot(dx, dy) < 6) return;
    drag.moved = true;
    cancelHold(); hideTip();
    viewport.classList.add("panning");
    const rect = viewport.getBoundingClientRect();
    const delta = talentDragAngle({ x: rect.left + view.x, y: rect.top + view.y },
      { x: drag.lastX, y: drag.lastY }, { x: event.clientX, y: event.clientY });
    const now = performance.now(), elapsed = Math.max(1 / 120, (now - drag.lastTime) / 1000);
    const speed = Math.max(-talentRotationConfig.maxVelocity, Math.min(talentRotationConfig.maxVelocity, delta / elapsed));
    drag.velocity += (speed - drag.velocity) * .5;
    drag.lastX = event.clientX; drag.lastY = event.clientY; drag.lastTime = now;
    rotation.target += delta;
    queueView();
  });
  function releasePointer(event) {
    if (!drag && event?.pointerId !== undefined) return;
    if (drag && event?.pointerId !== undefined && drag.id !== event.pointerId) return;
    const id = drag?.id;
    if (drag?.moved) suppressClickUntil = performance.now() + 250;
    const coast = event?.type === "pointerup" && drag?.moved && performance.now() - drag.lastTime < 100 && !reducedMotion();
    if (coast) { releaseTalentRotation(rotation, drag.velocity); queueView(); }
    else stopMotion();
    drag = null;
    cancelHold();
    viewport.classList.remove("panning");
    if (id !== undefined && viewport.hasPointerCapture?.(id)) viewport.releasePointerCapture(id);
  }
  for (const event of ["pointerup", "pointercancel", "lostpointercapture"]) viewport.addEventListener(event, releasePointer);
  bindPinchGesture(viewport, {
    onStart: () => {
      releasePointer();
      cancelHold();
      hideTip();
      viewport._pinchStart = { scale: view.scale };
      suppressClickUntil = performance.now() + 500;
    },
    onChange: ({ scale }) => {
      const start = viewport._pinchStart;
      if (!start) return;
      view.scale = Math.max(0.35, Math.min(1.65, start.scale * scale));
      queueView();
      suppressClickUntil = performance.now() + 300;
    },
    onEnd: () => {
      delete viewport._pinchStart;
      suppressClickUntil = performance.now() + 300;
    },
  });
  viewport.addEventListener("wheel", event => {
    event.preventDefault(); event.stopPropagation();
    if (drag) releasePointer();
    rotation.velocity = 0; hideTip();
    const delta = (event.deltaY || event.deltaX || 0) * (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? viewport.clientHeight : 1);
    if (event.ctrlKey || event.metaKey)
      view.scale = Math.max(0.35, Math.min(1.65, view.scale * Math.exp(-Math.max(-200, Math.min(200, delta)) * 0.0015)));
    else rotation.target += Math.max(-240, Math.min(240, delta)) * talentRotationConfig.wheelScale;
    queueView();
  }, { passive: false });
  viewport.addEventListener("keydown", event => {
    if (event.target !== viewport) return;
    const angles = { ArrowLeft: -.2, ArrowRight: .2 };
    if (Object.hasOwn(angles, event.key)) {
      event.preventDefault(); event.stopPropagation();
      if (drag) releasePointer();
      rotation.velocity = 0; hideTip(); rotation.target += angles[event.key]; queueView();
    } else if (event.key === "ArrowUp" || event.key === "ArrowDown") {
      event.preventDefault(); event.stopPropagation();
      releasePointer(); hideTip(); view.scale = Math.max(.35, Math.min(1.65, view.scale * (event.key === "ArrowUp" ? 1.1 : 1 / 1.1))); queueView();
    } else if (event.key === "Home") { event.preventDefault(); event.stopPropagation(); centerView(); }
  });
  // 根节点是归正按钮，不进入视口的拖动捕获，否则浏览器会将 click 重定向到视口。
  root.addEventListener("pointerdown", event => { event.stopPropagation(); stopMotion(); });
  root.addEventListener("click", () => { if (performance.now() >= suppressClickUntil) centerView(); });
  home.addEventListener("click", centerView);
  reset.addEventListener("click", () => {
    if (pending || !owned.size) return;
    if (!resetArmed) {
      resetArmed = true;
      reset.textContent = t("ui.talents.resetConfirm");
      resetTimer = setTimeout(() => { resetArmed = false; reset.textContent = t("ui.talents.reset"); }, 3500);
      return;
    }
    resetArmed = false;
    clearTimeout(resetTimer);
    // 移除已拥有根节点时，服务端同时移除后代。
    const roots = talentNodes.filter(node => owned.has(node.key) && !owned.has(node.base));
    sendRequest("remove", roots, "reset");
  });
  window.addEventListener("blur", () => { releasePointer(); hideTip(); });
  document.addEventListener("visibilitychange", () => { if (document.hidden) { releasePointer(); hideTip(); } });
  if (typeof ResizeObserver === "function") new ResizeObserver(() => {
    if (initialized && panel.classList.contains("open")) {
      // 窗口变化只调整根锚点，保持正在查看的角度和缩放。
      anchorView();
      queueView();
    }
  }).observe(viewport);

  function render() {
    const state = getState();
    owned = new Set((state.talents || []).map(node => talentKey(node.id, node.rarity, node.rank)));
    points.textContent = t("ui.talents.points", { count: state.talentPoints || 0 });
    root.setAttribute("aria-label", t("ui.talents.center"));
    home.textContent = t("ui.talents.center");
    reset.textContent = t(resetArmed ? "ui.talents.resetConfirm" : "ui.talents.reset");
    reset.disabled = !owned.size || !!pending || !state.authenticated || !!state.talentReadOnly;
    viewport.setAttribute("aria-label", t("ui.talents.panHint"));
    for (const node of talentNodes) {
      const entry = buttons.get(node.key);
      if (!entry) continue;
      const isOwned = owned.has(node.key);
      const cost = sumCost(collectTalentChain(node, owned, talentNodeByKey));
      const ready = cost <= state.talentPoints && state.authenticated && !state.talentReadOnly;
      entry.button.classList.toggle("owned", isOwned);
      entry.button.classList.toggle("available", !isOwned && ready);
      entry.button.classList.toggle("blocked", !isOwned && !ready);
      entry.button.classList.toggle("pending", pending === node.key);
      entry.button.setAttribute("aria-disabled", String(!!pending || (!isOwned && !ready) || !state.authenticated || !!state.talentReadOnly));
      entry.button.setAttribute("aria-pressed", String(isOwned));
      entry.button.setAttribute("aria-label", `${nameOf(node)} · ${rarityName(node.rarity)} · ${t(isOwned ? "ui.talents.holdRemove" : "ui.talents.holdLearn", { name: nameOf(node), names: nameOf(node), cost })}`);
      entry.cost.textContent = isOwned ? "" : String(cost);
      edgeEls.get(node.key)?.classList.toggle("owned", isOwned);
      if (previousOwned && previousOwned.has(node.key) !== isOwned && !globalThis.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches) {
        entry.button.animate?.([{ transform: "scale(0.94)" }, { transform: "scale(1.06)" }, { transform: "scale(1)" }], { duration: 220, easing: "ease-out" });
      }
    }
    previousOwned = new Set(owned);
    if (!initialized && viewport.clientWidth && viewport.clientHeight) centerView();
    else if (initialized && viewport.clientWidth && viewport.clientHeight) {
      const x = view.x, y = view.y;
      anchorView();
      if (x !== view.x || y !== view.y) queueView();
    }
    if (hovered) showTip(hovered.node, hovered.button);
    setHint(pending ? "ui.talents.pending" : "ui.talents.panHint");
  }

  return {
    render,
    acknowledge() { clearTimeout(pendingTimer); pending = null; if (panel.classList.contains("open")) render(); },
    close() { releasePointer(); delete viewport._pinchStart; hideTip(); clearTimeout(resetTimer); resetArmed = false; },
    reset() {
      this.close(); clearTimeout(pendingTimer); pending = null;
      previousOwned = null; initialized = false;
    },
  };
}

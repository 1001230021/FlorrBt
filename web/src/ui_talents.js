import { talentNodes, talentNodeByKey, talentKey } from "./talent_data.js";
import { buildTalentLayout, collectTalentChain, collectTalentRemoval, zoomTalentView } from "./talent_layout.js";
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
      view = zoomTalentView(view, { x: viewport.clientWidth / 2, y: viewport.clientHeight / 2 }, view.scale * factor);
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
  let view = { x: 0, y: 0, scale: 0.8 }, initialized = false;
  let hold = null, drag = null, frame = 0, pending = null, pendingTimer = 0;
  let hovered = null, resetArmed = false, resetTimer = 0;
  let suppressClickUntil = 0;
  let viewportSize = { width: viewport.clientWidth, height: viewport.clientHeight };

  function setHint(key = "ui.talents.panHint") { hint.textContent = t(key); }
  function drawView() {
    frame = 0;
    graph.style.transform = `translate(${view.x}px, ${view.y}px) scale(${view.scale})`;
  }
  function queueView() { if (!frame) frame = requestAnimationFrame(drawView); }
  function centerView() {
    initialized = true;
    const mobile = document.body.classList.contains("mobile-ui");
    // Reference: ~100px flower root, ~76px nodes in the 654px panel.
    // Keep the tree rooted near the lower centre; panning reveals the rest.
    view = { x: viewport.clientWidth * 0.5, y: viewport.clientHeight - Math.min(mobile ? 72 : viewport.clientWidth * 0.1, viewport.clientHeight * 0.15),
      scale: mobile ? Math.max(0.7, Math.min(1.15, viewport.clientWidth / 550))
        : Math.max(0.55, Math.min(1.65, viewport.clientWidth / 530)) };
    queueView();
  }
  function hideTip() {
    hovered = null;
    setAnimatedVisibility(tooltip, false, { exitDuration: 70 });
  }
  function showTip(node, button) {
    if (!panel.classList.contains("open") || drag?.moved) return;
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
      // Re-check against current authoritative ownership, never cached UI.
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
      // Releases still reach the global hold-input cleanup, even when focus
      // moved here while a gameplay key was down.
      if (event.key === " " || event.key === "Enter") { event.preventDefault(); cancelHold(); }
    });
    button.addEventListener("click", event => { event.preventDefault(); event.stopPropagation(); });
    graph.appendChild(button);
    buttons.set(node.key, { button, cost });
  }

  viewport.addEventListener("pointerdown", event => {
    if (event.button !== 0 || drag) return;
    event.preventDefault(); event.stopPropagation();
    const button = event.target.closest("[data-talent-key]");
    drag = { id: event.pointerId, x: event.clientX, y: event.clientY, start: { ...view }, moved: false };
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
    // Bounds keep the tree recoverable even after a very long drag.
    view.x = Math.max(-2400, Math.min(2400, drag.start.x + dx));
    view.y = Math.max(-2400, Math.min(2400, drag.start.y + dy));
    queueView();
  });
  function releasePointer(event) {
    if (drag && event?.pointerId !== undefined && drag.id !== event.pointerId) return;
    const id = drag?.id;
    if (drag?.moved) suppressClickUntil = performance.now() + 250;
    drag = null;
    cancelHold();
    viewport.classList.remove("panning");
    if (id !== undefined && viewport.hasPointerCapture?.(id)) viewport.releasePointerCapture(id);
  }
  for (const event of ["pointerup", "pointercancel", "lostpointercapture"]) viewport.addEventListener(event, releasePointer);
  bindPinchGesture(viewport, {
    onStart: ({ center }) => {
      releasePointer();
      cancelHold();
      hideTip();
      const rect = viewport.getBoundingClientRect();
      viewport._pinchStart = {
        view: { ...view },
        center: { x: center.x - rect.left, y: center.y - rect.top },
      };
      suppressClickUntil = performance.now() + 500;
    },
    onChange: ({ center, scale }) => {
      const start = viewport._pinchStart;
      if (!start) return;
      const rect = viewport.getBoundingClientRect();
      const localCenter = { x: center.x - rect.left, y: center.y - rect.top };
      const next = zoomTalentView(start.view, start.center, start.view.scale * scale);
      view.x = next.x + localCenter.x - start.center.x;
      view.y = next.y + localCenter.y - start.center.y;
      view.scale = next.scale;
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
    releasePointer(); hideTip();
    const rect = viewport.getBoundingClientRect();
    const delta = event.deltaY * (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? viewport.clientHeight : 1);
    view = zoomTalentView(view, { x: event.clientX - rect.left, y: event.clientY - rect.top }, view.scale * Math.exp(-Math.max(-200, Math.min(200, delta)) * 0.0015));
    queueView();
  }, { passive: false });
  viewport.addEventListener("keydown", event => {
    if (event.target !== viewport) return;
    const moves = { ArrowLeft: [50, 0], ArrowRight: [-50, 0], ArrowUp: [0, 50], ArrowDown: [0, -50] };
    if (moves[event.key]) {
      event.preventDefault(); event.stopPropagation();
      view.x += moves[event.key][0]; view.y += moves[event.key][1]; queueView();
    } else if (event.key === "Home") { event.preventDefault(); event.stopPropagation(); centerView(); }
  });
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
    // Removing an owned root also removes its descendants on the server.
    const roots = talentNodes.filter(node => owned.has(node.key) && !owned.has(node.base));
    sendRequest("remove", roots, "reset");
  });
  window.addEventListener("blur", () => { releasePointer(); hideTip(); });
  document.addEventListener("visibilitychange", () => { if (document.hidden) { releasePointer(); hideTip(); } });
  if (typeof ResizeObserver === "function") new ResizeObserver(() => {
    const nextSize = { width: viewport.clientWidth, height: viewport.clientHeight };
    if (initialized && panel.classList.contains("open")) {
      // A points/status update can resize the viewport a few pixels. Keep the
      // inspected world point anchored; never clamp a panned graph back home.
      view.x += (nextSize.width - viewportSize.width) * 0.5;
      view.y += (nextSize.height - viewportSize.height) * 0.5;
      queueView();
    }
    viewportSize = nextSize;
  }).observe(viewport);

  function render() {
    const state = getState();
    owned = new Set((state.talents || []).map(node => talentKey(node.id, node.rarity, node.rank)));
    points.textContent = t("ui.talents.points", { count: state.talentPoints || 0 });
    root.setAttribute("aria-label", t("ui.talents.center"));
    home.textContent = t("ui.talents.center");
    reset.textContent = t(resetArmed ? "ui.talents.resetConfirm" : "ui.talents.reset");
    reset.disabled = !owned.size || !!pending || !state.authenticated;
    viewport.setAttribute("aria-label", t("ui.talents.panHint"));
    for (const node of talentNodes) {
      const entry = buttons.get(node.key);
      if (!entry) continue;
      const isOwned = owned.has(node.key);
      const cost = sumCost(collectTalentChain(node, owned, talentNodeByKey));
      const ready = cost <= state.talentPoints && state.authenticated;
      entry.button.classList.toggle("owned", isOwned);
      entry.button.classList.toggle("available", !isOwned && ready);
      entry.button.classList.toggle("blocked", !isOwned && !ready);
      entry.button.classList.toggle("pending", pending === node.key);
      entry.button.setAttribute("aria-disabled", String(!!pending || (!isOwned && !ready) || !state.authenticated));
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
    if (hovered) showTip(hovered.node, hovered.button);
    setHint(pending ? "ui.talents.pending" : "ui.talents.panHint");
  }

  return {
    render,
    acknowledge() { clearTimeout(pendingTimer); pending = null; if (panel.classList.contains("open")) render(); },
    close() { releasePointer(); hideTip(); clearTimeout(resetTimer); resetArmed = false; },
    reset() {
      this.close(); clearTimeout(pendingTimer); pending = null;
      previousOwned = null; initialized = false;
    },
  };
}

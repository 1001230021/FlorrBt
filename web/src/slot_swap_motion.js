import { getCardFlightPose } from "./petal_card_layout.js";

// 仅管理换槽动画；飞行期间独占隐藏槽，反复换槽从当前影像位置逆转。
export function createSlotSwapMotion({ onSettled = () => {}, durationMs = 260,
  document: doc = globalThis.document, clock = () => performance.now(),
  schedule = callback => requestAnimationFrame(callback), cancel = id => cancelAnimationFrame(id),
} = {}) {
  const pairs = new Map();
  let frame = null;
  const validRect = rect => rect && Number.isFinite(rect.width) && rect.width > 0 &&
    Number.isFinite(rect.height) && rect.height > 0;
  const poseFromRect = rect => ({ x: rect.left, y: rect.top, size: rect.width });
  const destination = button => {
    const rect = button.getBoundingClientRect();
    return { x: rect.left, y: rect.top + (rect.height - rect.width) / 2, size: rect.width };
  };
  function release(pair) {
    for (const flight of pair.flights) flight.ghost.remove();
    for (const button of pair.buttons) button.classList.remove("slot-swap-hidden");
  }
  function queue() { if (frame === null && pairs.size) frame = schedule(step); }
  function step(now) {
    frame = null;
    let settled = false;
    for (const [index, pair] of pairs) {
      if (pair.buttons.some(button => button.isConnected === false)) {
        release(pair); pairs.delete(index); settled = true; continue;
      }
      const progress = Math.max(0, Math.min(1, (now - pair.startedAt) / durationMs));
      for (const flight of pair.flights) {
        flight.pose = getCardFlightPose(flight.from, flight.to, progress);
        const { pose, from } = flight;
        // 整卡统一变换，包含文字，不逐帧改字号或中途替换栅格。
        flight.ghost.style.transform = `translate(${pose.x - from.x}px, ${pose.y - from.y}px) scale(${pose.size / from.size})`;
      }
      if (progress >= 1) {
        release(pair); pairs.delete(index); settled = true;
      }
    }
    // 浏览器下一次绘制前统一发布已完成的卡片对。
    if (settled) onSettled();
    queue();
  }

  function setOrigin(flight, pose) {
    flight.from = { ...pose }; flight.pose = { ...pose };
    Object.assign(flight.ghost.style, {
      left: `${pose.x}px`, top: `${pose.y}px`, width: `${pose.size}px`, height: `${pose.size}px`,
      transform: "none", transformOrigin: "top left", willChange: "transform",
    });
  }
  function copyCard(from, to) {
    const stack = from.querySelector(":scope > .petal-stack");
    if (!stack || !validRect(stack.getBoundingClientRect())) return null;
    const ghost = doc.createElement("div");
    ghost.className = "petal-flight";
    ghost.setAttribute("aria-hidden", "true");
    ghost.style.setProperty("--slot-rarity-color", from.style.getPropertyValue("--slot-rarity-color"));
    // 复制实际标签、数量、图像及活跃变换，保留罗盘/拟态等显示状态。
    ghost.appendChild(stack.cloneNode(true));
    for (const selector of [":scope > .slot-durability-overlays", ":scope > .slot-value"]) {
      const overlay = from.querySelector(selector);
      if (overlay) ghost.appendChild(overlay.cloneNode(true));
    }
    const flight = { ghost, target: to, to: destination(to) };
    setOrigin(flight, poseFromRect(stack.getBoundingClientRect()));
    doc.body.appendChild(ghost);
    return flight;
  }
  function swap(index, primary, secondary) {
    if (!primary || !secondary || !validRect(primary.getBoundingClientRect()) || !validRect(secondary.getBoundingClientRect())) return false;
    const pair = pairs.get(index);
    if (pair) {
      for (const flight of pair.flights) {
        setOrigin(flight, flight.pose);
        flight.target = flight.target === primary ? secondary : primary;
        flight.to = destination(flight.target);
      }
      pair.startedAt = clock(); queue(); return true;
    }
    const flights = [copyCard(primary, secondary), copyCard(secondary, primary)].filter(Boolean);
    if (!flights.length) return false;
    primary.classList.add("slot-swap-hidden"); secondary.classList.add("slot-swap-hidden");
    pairs.set(index, { buttons: [primary, secondary], flights, startedAt: clock() });
    queue(); return true;
  }
  function cancelAll({ notify = false } = {}) {
    if (frame !== null) cancel(frame);
    frame = null;
    const hadPairs = pairs.size > 0;
    for (const pair of pairs.values()) release(pair);
    pairs.clear();
    if (hadPairs && notify) onSettled();
  }
  return { swap, has: index => pairs.has(index), cancelAll };
}

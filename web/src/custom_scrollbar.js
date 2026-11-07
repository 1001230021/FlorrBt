const HOST_CLASS = "custom-scrollbar-host";
const MARKED_SELECTOR = "[data-scrollbar]";
const PANEL_SELECTOR = ".ui";

const registry = new Map();

function attach(element, options = {}) {
  if (!element || element.nodeType !== 1 || registry.has(element)) return null;
  element.dataset.customScrollbar = "1";

  const explicitThumb = options.thumbColor || element.dataset.scrollbarThumb;
  const explicitThumbHover =
    options.thumbHoverColor || element.dataset.scrollbarThumbHover;
  const trackPadding = options.trackPadding ?? 2;
  const thumbMinSize = options.thumbMinSize ?? 32;

  const computed = getComputedStyle(element);
  if (computed.position === "static") element.style.position = "relative";
  if (computed.overflowY === "visible") element.style.overflowY = "auto";
  element.classList.add(HOST_CLASS);

  const wrapper = document.createElement("div");
  wrapper.className = "custom-scrollbar";
  // 仅显式颜色覆盖样式表，保留各面板自定义颜色。
  if (explicitThumb) wrapper.style.setProperty("--scrollbar-thumb", explicitThumb);
  if (explicitThumbHover)
    wrapper.style.setProperty("--scrollbar-thumb-hover", explicitThumbHover);
  const track = document.createElement("div");
  track.className = "custom-scrollbar-track";
  const thumb = document.createElement("div");
  thumb.className = "custom-scrollbar-thumb";
  track.appendChild(thumb);
  wrapper.appendChild(track);
  element.appendChild(wrapper);

  let dragging = false;
  let pointerStartY = 0;
  let scrollStartTop = 0;
  let trackHeight = 0;
  let thumbHeight = 0;

  function update() {
    const scrollHeight = element.scrollHeight;
    const clientHeight = element.clientHeight;
    if (scrollHeight <= clientHeight + 1) {
      wrapper.style.display = "none";
      return;
    }
    wrapper.style.display = "";
    // 滚动条位于滚动容器内，通过反向平移抵消内容滚动，固定在可见框中。
    wrapper.style.transform = `translate(${element.scrollLeft}px, ${element.scrollTop}px)`;
    trackHeight = clientHeight - trackPadding * 2;
    const ratio = clientHeight / scrollHeight;
    thumbHeight = Math.max(thumbMinSize, trackHeight * ratio);
    const maxScroll = scrollHeight - clientHeight;
    const maxThumbTop = trackHeight - thumbHeight;
    const thumbTop =
      maxScroll > 0 ? (element.scrollTop / maxScroll) * maxThumbTop : 0;
    thumb.style.height = `${thumbHeight}px`;
    thumb.style.transform = `translateY(${thumbTop}px)`;
  }

  function onThumbPointerDown(event) {
    if (event.button !== 0 && event.pointerType === "mouse") return;
    dragging = true;
    pointerStartY = event.clientY;
    scrollStartTop = element.scrollTop;
    thumb.setPointerCapture?.(event.pointerId);
    thumb.classList.add("dragging");
    event.preventDefault();
    event.stopPropagation();
  }

  function onThumbPointerMove(event) {
    if (!dragging) return;
    const scrollHeight = element.scrollHeight;
    const clientHeight = element.clientHeight;
    const maxScroll = scrollHeight - clientHeight;
    if (maxScroll <= 0 || trackHeight <= thumbHeight) return;
    const trackUsable = trackHeight - thumbHeight;
    const ratio = maxScroll / trackUsable;
    element.scrollTop =
      scrollStartTop + (event.clientY - pointerStartY) * ratio;
    event.preventDefault();
  }

  function onThumbPointerEnd(event) {
    if (!dragging) return;
    dragging = false;
    thumb.classList.remove("dragging");
    thumb.releasePointerCapture?.(event.pointerId);
  }

  thumb.addEventListener("pointerdown", onThumbPointerDown);
  thumb.addEventListener("pointermove", onThumbPointerMove);
  thumb.addEventListener("pointerup", onThumbPointerEnd);
  thumb.addEventListener("pointercancel", onThumbPointerEnd);

  function onTrackPointerDown(event) {
    if (event.button !== 0 && event.pointerType === "mouse") return;
    pageScroll(event.clientY);
    event.preventDefault();
  }

  function pageScroll(clientY) {
    const rect = thumb.getBoundingClientRect();
    if (clientY < rect.top + rect.height / 2) {
      element.scrollTop -= element.clientHeight * 0.8;
    } else {
      element.scrollTop += element.clientHeight * 0.8;
    }
  }

  track.addEventListener("pointerdown", onTrackPointerDown);

  element.addEventListener("scroll", update, { passive: true });

  let resizeObserver = null;
  if (typeof ResizeObserver !== "undefined") {
    resizeObserver = new ResizeObserver(update);
    resizeObserver.observe(element);
  }

  let mutationObserver = null;
  if (typeof MutationObserver !== "undefined") {
    mutationObserver = new MutationObserver(() => update());
    mutationObserver.observe(element, { childList: true, subtree: true });
  }

  update();

  const handle = {
    update,
    destroy() {
      registry.delete(element);
      delete element.dataset.customScrollbar;
      element.classList.remove(HOST_CLASS);
      element.removeEventListener("scroll", update);
      resizeObserver?.disconnect();
      mutationObserver?.disconnect();
      thumb.removeEventListener("pointerdown", onThumbPointerDown);
      thumb.removeEventListener("pointermove", onThumbPointerMove);
      thumb.removeEventListener("pointerup", onThumbPointerEnd);
      thumb.removeEventListener("pointercancel", onThumbPointerEnd);
      track.removeEventListener("pointerdown", onTrackPointerDown);
      if (wrapper.parentElement === element) element.removeChild(wrapper);
    },
  };
  registry.set(element, handle);
  return handle;
}

function detachTree(node) {
  if (node.nodeType !== 1) return;
  if (registry.has(node)) registry.get(node).destroy();
  const children = node.querySelectorAll("*");
  for (const child of children) {
    if (registry.has(child)) registry.get(child).destroy();
  }
}

/* 标记 data-scrollbar 或已有纵向滚动规则的界面面板使用自定义滚动条。 */
function shouldAttach(element) {
  if (!element || element.nodeType !== 1 || registry.has(element)) return false;
  if (element.matches?.(MARKED_SELECTOR)) return true;
  if (!element.matches?.(PANEL_SELECTOR)) return false;
  return getComputedStyle(element).overflowY !== "visible";
}

function scanTree(node) {
  if (node.nodeType !== 1) return;
  if (shouldAttach(node)) attach(node);
  const candidates = node.querySelectorAll?.("*");
  if (candidates) for (const candidate of candidates) attachCandidate(candidate);
}

function attachCandidate(candidate) {
  if (shouldAttach(candidate)) attach(candidate);
}

export function attachCustomScrollbar(element, options = {}) {
  return attach(element, options);
}

/* 自动跟踪可滚动元素增删，仅内容溢出时显示滚动条。 */
export function initCustomScrollbars(root = document) {
  for (const candidate of root.querySelectorAll("*")) {
    if (shouldAttach(candidate)) attach(candidate);
  }

  const observer = new MutationObserver((mutations) => {
    for (const mutation of mutations) {
      for (const node of mutation.addedNodes) scanTree(node);
      for (const node of mutation.removedNodes) detachTree(node);
    }
  });
  observer.observe(root, { childList: true, subtree: true });

  // 滚动事件不冒泡，在捕获阶段统一监听以同步鼠标和触屏滚动。
  const onGlobalScroll = (event) => {
    registry.get(event.target)?.update();
  };
  document.addEventListener("scroll", onGlobalScroll, {
    capture: true,
    passive: true,
  });

  return {
    destroy() {
      observer.disconnect();
      document.removeEventListener("scroll", onGlobalScroll, {
        capture: true,
      });
      for (const handle of [...registry.values()]) handle.destroy();
    },
  };
}

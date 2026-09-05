// Freeze watchdog: pins down the "picture frozen for 1-20s" reports.
//
// Two independent failure modes look identical on screen and need different
// fixes, so the watchdog tells them apart and records evidence for both:
//   - CLIENT stall: the main thread blocked, rAF stopped firing. Detected as
//     a gap between consecutive animation frames; long tasks observed in the
//     same window point at the blocking script.
//   - SERVER/NETWORK stall: frames keep rendering but no snapshot arrives,
//     so the world stands still. Detected as snapshot silence while the
//     socket stays open and the tab is visible.
//
// Events are kept in a ring buffer on `window.__freezeEvents` (inspect from
// devtools) and echoed to the in-game console once they exceed the print
// threshold, with a context sample (entity count, chunk queue, heap, dpr)
// captured at detection time.

const stallThresholdMs = 300;
const printThresholdMs = 500;
const serverGapThresholdMs = 1500;
const serverGapArmWindowMs = 30000;
const maxEvents = 60;

const events = [];
let longTasks = [];
let lastFrameAt = 0;
let lastSnapshotAt = 0;
let serverGapOpenedAt = 0;
let noteConsoleLine = null;
let sampleContext = null;
let started = false;

function pushEvent(event) {
  events.push(event);
  if (events.length > maxEvents) events.shift();
}

function contextSummary() {
  let context = {};
  try {
    context = sampleContext ? sampleContext() || {} : {};
  } catch {
    context = {};
  }
  const memory = performance.memory;
  if (memory && Number.isFinite(memory.usedJSHeapSize))
    context.heapMb = Math.round(memory.usedJSHeapSize / (1024 * 1024));
  return context;
}

function formatContext(context) {
  return Object.entries(context)
    .map(([key, value]) => `${key}=${value}`)
    .join(" ");
}

function report(kind, durationMs, context, detail = "") {
  const event = {
    kind,
    at: new Date().toISOString(),
    durationMs: Math.round(durationMs),
    detail,
    ...context,
  };
  pushEvent(event);
  const line =
    `[freeze] ${kind} ${Math.round(durationMs)}ms` +
    (detail ? ` ${detail}` : "") +
    ` | ${formatContext(context)}`;
  // Browser console always; in-game console only for clearly visible stalls.
  console.warn(line, event);
  if (durationMs >= printThresholdMs && noteConsoleLine) {
    try {
      noteConsoleLine(line);
    } catch {
      /* console UI not ready yet */
    }
  }
}

function recentLongTaskSummary(sinceMs) {
  const now = performance.now();
  longTasks = longTasks.filter((task) => now - task.end < 30000);
  const inWindow = longTasks.filter((task) => task.end >= sinceMs);
  if (inWindow.length === 0) return "";
  const total = inWindow.reduce((sum, task) => sum + task.duration, 0);
  const longest = inWindow.reduce(
    (best, task) => (task.duration > best.duration ? task : best),
    inWindow[0],
  );
  return `longtasks=${inWindow.length}/${Math.round(total)}ms max=${Math.round(longest.duration)}ms(${longest.name})`;
}

function onAnimationFrame(now) {
  if (lastFrameAt > 0 && !document.hidden) {
    const gap = now - lastFrameAt;
    if (gap >= stallThresholdMs) {
      report(
        "client-stall",
        gap,
        contextSummary(),
        recentLongTaskSummary(lastFrameAt),
      );
    }

    // Snapshot silence while frames keep flowing = the world stalled on the
    // server/network side, not in this tab.
    if (lastSnapshotAt > 0 && now - lastSnapshotAt < serverGapArmWindowMs) {
      const silence = now - lastSnapshotAt;
      if (silence >= serverGapThresholdMs && serverGapOpenedAt === 0)
        serverGapOpenedAt = lastSnapshotAt;
    }
  }
  lastFrameAt = now;
  requestAnimationFrame(onAnimationFrame);
}

export function noteServerSnapshot() {
  const now = performance.now();
  if (serverGapOpenedAt > 0) {
    const silence = now - serverGapOpenedAt;
    serverGapOpenedAt = 0;
    if (silence >= serverGapThresholdMs)
      report("server-gap", silence, contextSummary());
  }
  lastSnapshotAt = now;
}

export function noteConnectionReset() {
  lastSnapshotAt = 0;
  serverGapOpenedAt = 0;
}

export function freezeEvents() {
  return events.slice();
}

export function initFreezeWatchdog(options = {}) {
  if (started) return;
  started = true;
  noteConsoleLine = options.addConsoleLine || null;
  sampleContext = options.sampleContext || null;

  if (typeof PerformanceObserver !== "undefined") {
    try {
      const observer = new PerformanceObserver((list) => {
        for (const entry of list.getEntries()) {
          longTasks.push({
            end: entry.startTime + entry.duration,
            duration: entry.duration,
            name: entry.name || "longtask",
          });
        }
        if (longTasks.length > 200) longTasks = longTasks.slice(-100);
      });
      observer.observe({ entryTypes: ["longtask"] });
    } catch {
      /* longtask observation unsupported: gap detection still works */
    }
  }

  document.addEventListener("visibilitychange", () => {
    // Tab switches legitimately pause rAF; do not count them as stalls.
    lastFrameAt = 0;
    if (lastSnapshotAt > 0) lastSnapshotAt = performance.now();
    serverGapOpenedAt = 0;
  });

  window.__freezeEvents = events;
  requestAnimationFrame(onAnimationFrame);
}

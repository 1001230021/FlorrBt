// 冻结检测区分主线程停顿与快照断流：前者观察动画帧间隔/长任务，后者要求仍在渲染且连接打开。
// 事件及检测上下文保存于 window.__freezeEvents，明显停顿同步到游戏控制台。

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
  console.warn(line, event);
  if (durationMs >= printThresholdMs && noteConsoleLine) {
    try {
      noteConsoleLine(line);
    } catch {
      /* 控制台界面尚未就绪。 */
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

    // 持续渲染但无快照到达时记录服务器/网络侧断流。
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
      /* 不支持长任务观测时仍检测帧间隔。 */
    }
  }

  document.addEventListener("visibilitychange", () => {
    // 后台标签页暂停动画属正常行为，不记作冻结。
    lastFrameAt = 0;
    if (lastSnapshotAt > 0) lastSnapshotAt = performance.now();
    serverGapOpenedAt = 0;
  });

  window.__freezeEvents = events;
  requestAnimationFrame(onAnimationFrame);
}

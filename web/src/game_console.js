import { clientCommandNames, findClientCommandSpec } from "./client_commands_spec.js";

const FONT_FAMILY = '"Cascadia Mono", "Consolas", monospace';
const FONT_SIZE = 13;
const LINE_HEIGHT = FONT_SIZE + 4;
const STATUS_HEIGHT = 20;
const PADDING = 10;
const INPUT_GAP = 8;
const MIN_INPUT_HEIGHT = 26;
const PROMPT_GAP = 6;
const SUGGESTION_GAP = 7;
const MAX_WRAP_LINES = 24;
const MAX_HISTORY = 128;

const CONSOLE_COLORS = Object.freeze({
  default: "#c9c9c9",
  command: "#7fb0ff",
  error: "#ff6b57",
  warning: "#ffb454",
  muted: "#8f95a3",
});

const SEARCH_HIGHLIGHT = "rgba(255, 45, 45, 0.35)";
const SEARCH_SELECTED = "rgba(255, 230, 60, 0.45)";
const SELECTION_COLOR = "rgba(255, 255, 255, 0.18)";
const SUGGESTION_SELECTED_BG = "rgba(0, 0, 0, 0.85)";

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function colorForKind(kind) {
  return CONSOLE_COLORS[kind] || CONSOLE_COLORS.default;
}

function copyTextToClipboard(text) {
  if (!text) return;
  if (navigator.clipboard?.writeText) {
    navigator.clipboard.writeText(text).catch(() => fallbackCopy(text));
    return;
  }
  fallbackCopy(text);
}

function fallbackCopy(text) {
  const holder = document.createElement("textarea");
  holder.value = text;
  holder.setAttribute("readonly", "");
  holder.style.position = "fixed";
  holder.style.left = "-9999px";
  document.body.appendChild(holder);
  holder.select();
  try {
    document.execCommand("copy");
  } catch {
    // ignore
  }
  holder.remove();
}

export function createGameConsole({
  panel,
  input,
  commands = [],
  completionSources = {},
  executeCommand = null,
  translate = (key, params = {}, options = {}) =>
    options.defaultValue ?? key,
  version = "",
  onCloseRequested = null,
  maxBacklogEntries = 512,
} = {}) {
  if (!panel) throw new TypeError("createGameConsole requires a panel");

  const canvas = document.createElement("canvas");
  canvas.className = "console-canvas";
  panel.insertBefore(canvas, input);

  const ctx = canvas.getContext("2d", { alpha: true });

  let open = false;
  let rafId = 0;
  let dirty = true;
  let lastFrameTime = performance.now();

  const backlog = [];
  let wrapDirty = true;
  let visualLines = [];
  let totalLines = 0;
  const entryStartLine = new Map();

  const history = [];
  let historyIndex = -1;
  let historyDraft = "";

  let scrollUp = 0;
  let maxScroll = 0;

  let completion = null;
  let completionIndex = -1;
  let completionOffset = 0;
  let completionTarget = 0;
  // Input snapshot the current candidate list was computed from. Tab-cycling
  // reuses the list as long as the input hasn't changed.
  let completionSnapshotValue = null;
  let completionSnapshotCursor = -1;

  let searching = false;
  let searchMatches = [];
  let currentMatchIndex = -1;

  let selection = null;
  let selecting = false;
  let inputSelecting = false;
  let inputSelectAnchor = 0;

  let scrollbarDragging = false;
  let scrollbarDragStartY = 0;
  let scrollbarDragStartScrollUp = 0;
  let scrollbarHovered = false;

  let touchLastY = null;

  // ------------------------------------------------------------------ helpers

  function t(key, params = {}, fallback = "") {
    return translate(key, params, { defaultValue: fallback });
  }

  function contentWidth() {
    return Math.max(0, canvas.clientWidth - PADDING * 2);
  }

  function setFont(size = FONT_SIZE) {
    ctx.font = `${size}px ${FONT_FAMILY}`;
  }

  function measure(text) {
    return ctx.measureText(String(text ?? "")).width;
  }

  function wrapText(text, maxWidth) {
    const source = String(text ?? "");
    const segments = [];
    let base = 0;
    for (const rawLine of source.split("\n")) {
      if (rawLine.length === 0) {
        segments.push({ text: "", offset: base });
        base += 1;
        continue;
      }
      let lineStart = 0;
      while (lineStart < rawLine.length && segments.length < MAX_WRAP_LINES) {
        let end = lineStart;
        while (
          end < rawLine.length &&
          measure(rawLine.slice(lineStart, end + 1)) <= maxWidth
        ) {
          end += 1;
        }
        if (end === lineStart) end = Math.min(rawLine.length, lineStart + 1);
        segments.push({
          text: rawLine.slice(lineStart, end),
          offset: base + lineStart,
        });
        lineStart = end;
      }
      base += rawLine.length + 1;
    }
    if (segments.length === 0) segments.push({ text: "", offset: 0 });
    return segments;
  }

  function wrapEntry(entry, maxWidth) {
    entry.segments = wrapText(entry.text, maxWidth);
    entry.lineCount = entry.segments.length;
    entry.wrapWidth = maxWidth;
  }

  function rebuildVisualLines() {
    const maxWidth = contentWidth();
    for (const entry of backlog) {
      if (entry.wrapWidth !== maxWidth) wrapEntry(entry, maxWidth);
    }
    visualLines.length = 0;
    entryStartLine.clear();
    for (const entry of backlog) {
      entryStartLine.set(entry, visualLines.length);
      for (const segment of entry.segments) {
        visualLines.push({ entry, segment });
      }
    }
    totalLines = visualLines.length;
    wrapDirty = false;
  }

  function commandNames() {
    const names = clientCommandNames();
    for (const spec of commands) {
      names.push(spec.name);
      if (spec.aliases) names.push(...spec.aliases);
    }
    return [...new Set(names)];
  }

  function sortedCandidates(candidates, search) {
    const query = String(search || "").toLowerCase();
    const scored = candidates.map((name, index) => {
      const text = String(name);
      const lower = text.toLowerCase();
      const pos = query ? lower.indexOf(query) : 0;
      return { text, index, pos, length: text.length };
    });
    scored.sort((a, b) => {
      if (a.pos !== b.pos) return a.pos < 0 ? 1 : b.pos < 0 ? -1 : a.pos - b.pos;
      if (a.length !== b.length) return a.length - b.length;
      return a.text.localeCompare(b.text);
    });
    return scored.map((entry) => entry.text);
  }

  function wordRangeAt(value, cursor) {
    let start = cursor;
    while (start > 0 && !/\s/.test(value[start - 1])) start -= 1;
    let end = cursor;
    while (end < value.length && !/\s/.test(value[end])) end += 1;
    return { start, end };
  }

  function commandGroupOf(value, cursor) {
    const groupStart = value.lastIndexOf(";", cursor - 1) + 1;
    return value.slice(groupStart, cursor);
  }

  function tokenIndexAt(value, replaceBegin) {
    let index = 0;
    let inToken = false;
    for (let i = 0; i < replaceBegin; i += 1) {
      if (/\s/.test(value[i])) {
        if (inToken) {
          index += 1;
          inToken = false;
        }
      } else {
        inToken = true;
      }
    }
    return index;
  }

  function argKindsFor(spec) {
    return String(spec?.args || "")
      .split(/\s+/)
      .filter(Boolean);
  }

  function completionSource(kind, prefix) {
    const provider = completionSources[kind];
    if (typeof provider !== "function") return [];
    try {
      return (provider(prefix) || []).map(String);
    } catch {
      return [];
    }
  }

  function findSpecByName(name) {
    return (
      findClientCommandSpec(name) ||
      commands.find(
        (spec) =>
          spec.name === name ||
          spec.aliases?.some((alias) => alias.toLowerCase() === name),
      ) ||
      null
    );
  }

  // ------------------------------------------------------------------ completion

  function updateCompletion(resetChosen = true) {
    const value = input.value;
    const cursor = input.selectionStart ?? value.length;
    // Keep the current candidate list while the input hasn't changed, so
    // tabbing through candidates never recomputes/reorders them.
    if (completion && completionSnapshotValue === value && completionSnapshotCursor === cursor)
      return;
    completion = null;
    if (resetChosen) completionIndex = -1;
    if (searching) return;
    completionSnapshotValue = value;
    completionSnapshotCursor = cursor;
    if (!value) return;

    const { start, end } = wordRangeAt(value, cursor);
    const word = value.slice(start, end);
    const groupWords = commandGroupOf(value, cursor).trim().split(/\s+/).filter(Boolean);
    const commandWord = (groupWords[0] || "").toLowerCase();
    const spec = findSpecByName(commandWord);
    const tokenIndex = tokenIndexAt(value, start);

    if (tokenIndex === 0 || !spec) {
      const candidates = sortedCandidates(commandNames(), commandWord);
      if (candidates.length > 0) {
        completion = { kind: "command", candidates, range: { start, end }, spec: null };
      } else if (spec) {
        completion = { kind: "help", candidates: [], range: { start, end }, spec };
      }
      return;
    }

    const kinds = argKindsFor(spec);
    const argIndex = tokenIndex - 1;
    const kind = kinds[Math.min(argIndex, kinds.length - 1)];
    if (kind && kind !== "number") {
      const candidates = sortedCandidates(completionSource(kind, word), word);
      if (candidates.length > 0) {
        completion = { kind: "argument", candidates, range: { start, end }, spec };
        return;
      }
    }
    completion = { kind: "help", candidates: [], range: { start, end }, spec };
  }

  function applyCompletion(direction) {
    if (searching) {
      selectNextSearchMatch(direction);
      return;
    }
    updateCompletion(false);
    if (!completion || completion.kind === "help" || completion.candidates.length === 0)
      return;

    if (completionIndex < 0 && direction < 0) completionIndex = 0;
    const count = completion.candidates.length;
    completionIndex = (completionIndex + direction + count) % count;
    const candidate = completion.candidates[completionIndex];
    const { start, end } = completion.range;
    const value = input.value;
    input.value = value.slice(0, start) + candidate + value.slice(end);
    const cursorPos = start + candidate.length;
    input.setSelectionRange(cursorPos, cursorPos);
    updateCompletion(false);
    dirty = true;
  }

  // ------------------------------------------------------------------ search

  function setSearching(value) {
    if (searching === value) return;
    searching = value;
    searchMatches = [];
    currentMatchIndex = -1;
    input.value = "";
    input.setSelectionRange(0, 0);
    selection = null;
    completion = null;
    completionIndex = -1;
    if (value) {
      input.setAttribute("placeholder", t("ui.console.searchPlaceholder", {}, "Searching..."));
    } else {
      input.setAttribute("placeholder", t("ui.console.placeholder", {}, "Console"));
    }
    dirty = true;
  }

  function updateSearch() {
    if (!searching) return;
    const query = input.value;
    searchMatches = [];
    currentMatchIndex = -1;
    if (!query) {
      dirty = true;
      return;
    }
    const lower = query.toLowerCase();
    for (const entry of backlog) {
      const text = entry.text.toLowerCase();
      let pos = text.indexOf(lower);
      while (pos !== -1) {
        searchMatches.push({ entry, pos, len: query.length });
        pos = text.indexOf(lower, pos + 1);
      }
    }
    if (searchMatches.length > 0) {
      currentMatchIndex = 0;
      scrollToMatch(0);
    }
    dirty = true;
  }

  function scrollToMatch(index) {
    if (index < 0 || index >= searchMatches.length) return;
    const match = searchMatches[index];
    const segments = match.entry.segments || [];
    let segmentIndex = segments.length - 1;
    for (let i = 0; i < segments.length; i += 1) {
      if (match.pos < segments[i].offset + segments[i].text.length) {
        segmentIndex = i;
        break;
      }
    }
    const line = (entryStartLine.get(match.entry) ?? 0) + segmentIndex;
    const capacity = backlogCapacity();
    scrollUp = clamp(Math.round(line - capacity / 2), 0, maxScrollFor(capacity));
    dirty = true;
  }

  function selectNextSearchMatch(direction) {
    if (searchMatches.length === 0) return;
    currentMatchIndex =
      (currentMatchIndex + direction + searchMatches.length) %
      searchMatches.length;
    scrollToMatch(currentMatchIndex);
  }

  // ------------------------------------------------------------------ scrolling

  // Vertical space reserved above the input row for the suggestion strip
// (candidate row / search status / help block). The backlog bottom moves up
// by this amount so console messages never overlap the suggestions.
function suggestionReservedHeight() {
  if (searching) return LINE_HEIGHT + PROMPT_GAP + 4;
  if (!completion) return 0;
  if (completion.kind === "help" && completion.spec) return LINE_HEIGHT * 2 + PROMPT_GAP + 4;
  if (completion.candidates.length > 0) return LINE_HEIGHT + PROMPT_GAP + 4;
  return 0;
}

function backlogCapacity() {
    const bottom =
      canvas.clientHeight - PADDING - inputRowHeight() - INPUT_GAP - suggestionReservedHeight();
    const top = STATUS_HEIGHT + PADDING;
    return Math.max(1, Math.floor((bottom - top) / LINE_HEIGHT));
  }

  function maxScrollFor(capacity) {
    return Math.max(0, totalLines - capacity);
  }

  function updateMaxScroll() {
    maxScroll = maxScrollFor(backlogCapacity());
    scrollUp = clamp(scrollUp, 0, maxScroll);
  }

  function promptText() {
    if (searching) return t("ui.console.searchPrompt", {}, "Searching: ");
    return t("ui.console.prompt", {}, "> ");
  }

  function inputRowHeight() {
    if (!input.value) return MIN_INPUT_HEIGHT;
    const promptWidth = measure(promptText());
    const maxWidth = Math.max(40, contentWidth() - promptWidth - 8);
    const segments = wrapText(input.value, maxWidth);
    return Math.max(MIN_INPUT_HEIGHT, segments.length * LINE_HEIGHT + 6);
  }

  // ------------------------------------------------------------------ backlog

  function addLine(text, kind = "") {
    backlog.push({
      text: String(text ?? ""),
      kind,
      color: colorForKind(kind),
      segments: null,
      lineCount: 0,
      wrapWidth: -1,
    });
    if (backlog.length > maxBacklogEntries) {
      backlog.splice(0, backlog.length - maxBacklogEntries);
    }
    wrapDirty = true;
    // Chat-like behavior: new messages always insert at the bottom and push
    // older content up, so the view always follows the newest lines.
    scrollUp = 0;
    selection = null;
    dirty = true;
  }

  function executeLine(line) {
    const trimmed = String(line || "").trim();
    input.value = "";
    input.setSelectionRange(0, 0);
    completion = null;
    completionIndex = -1;
    history.push(line);
    if (history.length > MAX_HISTORY) history.shift();
    historyIndex = -1;
    historyDraft = "";
    if (trimmed) {
      addLine(`> ${trimmed}`, "command");
      executeCommand?.(trimmed);
    }
    dirty = true;
  }

  function submit() {
    executeLine(input.value);
  }

  // ------------------------------------------------------------------ selection

  function lineFromY(y) {
    const bottom =
      canvas.clientHeight - PADDING - inputRowHeight() - INPUT_GAP - suggestionReservedHeight();
    if (y > bottom || y < STATUS_HEIGHT + PADDING) return -1;
    const capacity = backlogCapacity();
    const firstLine = Math.max(0, totalLines - capacity - scrollUp);
    const lastLine = Math.min(totalLines, firstLine + capacity);
    if (lastLine <= firstLine) return -1;
    // Inverse of the bottom-anchored render formula: the row closest to the
    // input row is the newest rendered line (lastLine - 1).
    const offset = Math.floor((bottom - y) / LINE_HEIGHT);
    const index = lastLine - 1 - offset;
    if (index < firstLine || index >= totalLines) return -1;
    return index;
  }

  function charFromX(line, x) {
    if (line < 0 || line >= visualLines.length) return 0;
    const text = visualLines[line].segment.text;
    if (!text) return 0;
    let best = 0;
    let bestDistance = Infinity;
    let acc = 0;
    for (let i = 0; i <= text.length; i += 1) {
      const distance = Math.abs(acc - x);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = i;
      }
      if (i < text.length) acc += measure(text[i]);
    }
    return best;
  }

  function normalizedSelection() {
    if (!selection) return null;
    const { aLine, aChar, cLine, cChar } = selection;
    if (aLine === cLine) {
      if (aChar === cChar) return null;
      return aChar < cChar
        ? { aLine, aChar, cLine, cChar }
        : { aLine, aChar: cChar, cLine, cChar: aChar };
    }
    return aLine < cLine
      ? { aLine, aChar, cLine, cChar }
      : { aLine: cLine, aChar: cChar, cLine: aLine, cChar: aChar };
  }

  function selectedRects(line) {
    const range = normalizedSelection();
    if (!range || line < range.aLine || line > range.cLine) return [];
    const { segment } = visualLines[line];
    const startChar = line === range.aLine ? range.aChar : 0;
    const endChar = line === range.cLine ? range.cChar : segment.text.length;
    if (startChar >= endChar) return [];
    const rects = [];
    let x = 0;
    let rectStart = null;
    for (let i = 0; i <= segment.text.length; i += 1) {
      const inside = i >= startChar && i < endChar;
      if (inside && rectStart === null) rectStart = x;
      if (!inside && rectStart !== null) {
        rects.push({ x: rectStart, w: x - rectStart });
        rectStart = null;
      }
      if (i < segment.text.length) x += measure(segment.text[i]);
    }
    if (rectStart !== null) rects.push({ x: rectStart, w: x - rectStart });
    return rects;
  }

  function copySelection() {
    const range = normalizedSelection();
    if (!range) return;
    const parts = [];
    for (let line = range.aLine; line <= range.cLine; line += 1) {
      const { entry, segment } = visualLines[line];
      const startChar = line === range.aLine ? range.aChar : 0;
      const endChar = line === range.cLine ? range.cChar : segment.text.length;
      parts.push(entry.text.slice(segment.offset + startChar, segment.offset + endChar));
    }
    copyTextToClipboard(parts.join("\n"));
    selection = null;
    dirty = true;
  }

  // ------------------------------------------------------------------ input

  function inputSegments() {
    if (!input.value) return [{ text: "", offset: 0 }];
    const promptWidth = measure(promptText());
    const maxWidth = Math.max(40, contentWidth() - promptWidth - 8);
    return wrapText(input.value, maxWidth);
  }

  function inputSelectionXRange() {
    const start = input.selectionStart ?? input.value.length;
    const end = input.selectionEnd ?? start;
    return start === end ? null : { start: Math.min(start, end), end: Math.max(start, end) };
  }

  function inputCaretPosition() {
    const segments = inputSegments();
    const caret = input.selectionStart ?? input.value.length;
    let acc = 0;
    for (let i = 0; i < segments.length; i += 1) {
      const segment = segments[i];
      const segmentEnd = acc + segment.text.length;
      if (caret <= segmentEnd) {
        return { x: measure(segment.text.slice(0, caret - acc)), line: i };
      }
      acc = segmentEnd;
    }
    const last = segments[segments.length - 1];
    return { x: measure(last.text), line: segments.length - 1 };
  }

  function cursorFromX(x, y = 0) {
    const promptWidth = measure(promptText());
    const localX = Math.max(0, x - PADDING - promptWidth);
    const segments = inputSegments();
    const inputHeight = Math.max(MIN_INPUT_HEIGHT, segments.length * LINE_HEIGHT + 6);
    const inputTop = canvas.clientHeight - PADDING - inputHeight;
    const lineIndex = clamp(
      Math.floor((y - inputTop - 3) / LINE_HEIGHT),
      0,
      segments.length - 1,
    );
    let acc = 0;
    for (let i = 0; i < segments.length; i += 1) {
      const segment = segments[i];
      if (i !== lineIndex) {
        acc += segment.text.length;
        continue;
      }
      let best = segment.text.length;
      let bestDistance = Math.abs(measure(segment.text) - localX);
      for (let j = 0; j < segment.text.length; j += 1) {
        const width = measure(segment.text.slice(0, j + 1));
        const distance = Math.abs(width - localX);
        if (distance < bestDistance) {
          bestDistance = distance;
          best = j + 1;
        }
      }
      return acc + best;
    }
    return input.value.length;
  }

  function moveCaretFromPointer(event) {
    const rect = canvas.getBoundingClientRect();
    input.focus({ preventScroll: true });
    input.setSelectionRange(
      inputSelectAnchor,
      cursorFromX(event.clientX - rect.left, event.clientY - rect.top),
    );
    dirty = true;
  }

  // ------------------------------------------------------------------ render

  function backlogBottomY() {
    return (
      canvas.clientHeight - PADDING - inputRowHeight() - INPUT_GAP - suggestionReservedHeight()
    );
  }

  function scrollbarTrackRect() {
    const cssWidth = canvas.clientWidth || 1;
    const bottom = backlogBottomY();
    return {
      x: cssWidth - PADDING - 6,
      y: STATUS_HEIGHT + 2,
      w: 6,
      h: Math.max(0, bottom - STATUS_HEIGHT - 4),
    };
  }

  function scrollbarThumbRect(track) {
    const capacity = backlogCapacity();
    const maxScroll = maxScrollFor(capacity);
    if (maxScroll <= 0) return null;
    const thumbH = Math.max(24, track.h * Math.min(1, capacity / totalLines));
    const movable = track.h - thumbH;
    // Standard mapping: scrollUp == 0 means the view is at the BOTTOM of the
    // backlog, so the thumb sits at the bottom of the track; scrollUp == max
    // (view at the top) puts the thumb at the top.
    const y = track.y + movable - (movable * scrollUp) / maxScroll;
    return { x: track.x, y, w: track.w, h: thumbH };
  }

  function scrollbarVisible() {
    return totalLines > backlogCapacity();
  }

  function drawScrollbar() {
    if (!scrollbarVisible()) return;
    const track = scrollbarTrackRect();
    const thumb = scrollbarThumbRect(track);
    ctx.fillStyle = "rgba(255, 255, 255, 0.10)";
    ctx.fillRect(track.x, track.y, track.w, track.h);
    if (!thumb) return;
    ctx.fillStyle =
      scrollbarDragging || scrollbarHovered
        ? "rgba(255, 255, 255, 0.55)"
        : "rgba(255, 255, 255, 0.35)";
    roundRect(ctx, thumb.x, thumb.y, thumb.w, thumb.h, 3);
    ctx.fill();
  }

  function statusText() {
    const capacity = backlogCapacity();
    const rendered = Math.min(capacity, totalLines);
    const first = Math.max(1, totalLines - scrollUp - rendered + 1);
    const last = first + rendered - 1;
    const state = scrollUp > 0
      ? t("ui.console.locked", {}, "Locked")
      : t("ui.console.following", {}, "Following");
    return t(
      "ui.console.lines",
      { first, last, state },
      `Lines ${first} - ${last} (${state})`,
    );
  }

  function render(now) {
    const cssWidth = canvas.clientWidth || 1;
    const cssHeight = canvas.clientHeight || 1;
    const scale = Math.max(1, Math.min(2, window.devicePixelRatio || 1));
    if (
      canvas.width !== Math.round(cssWidth * scale) ||
      canvas.height !== Math.round(cssHeight * scale)
    ) {
      canvas.width = Math.round(cssWidth * scale);
      canvas.height = Math.round(cssHeight * scale);
      wrapDirty = true;
    }
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
    ctx.clearRect(0, 0, cssWidth, cssHeight);
    ctx.textBaseline = "alphabetic";
    ctx.textAlign = "left";
    setFont();

    if (wrapDirty) rebuildVisualLines();
    updateMaxScroll();

    const inputHeight = inputRowHeight();
    const inputTop = cssHeight - PADDING - inputHeight;
    const backlogBottom = inputTop - INPUT_GAP - suggestionReservedHeight();

    setFont(FONT_SIZE - 2);
    ctx.fillStyle = CONSOLE_COLORS.muted;
    ctx.fillText(statusText(), PADDING, STATUS_HEIGHT - 6);
    if (version) {
      ctx.textAlign = "right";
      ctx.fillText(version, cssWidth - PADDING, STATUS_HEIGHT - 6);
      ctx.textAlign = "left";
    }
    setFont();

    const capacity = backlogCapacity();
    const firstLine = Math.max(0, totalLines - capacity - scrollUp);
    const lastLine = Math.min(totalLines, firstLine + capacity);
    ctx.save();
    ctx.beginPath();
    ctx.rect(0, STATUS_HEIGHT, cssWidth, Math.max(0, backlogBottom - STATUS_HEIGHT));
    ctx.clip();

    for (let line = firstLine; line < lastLine; line += 1) {
      const { entry, segment } = visualLines[line];
      // Bottom-anchored: the newest line sits just above the input row and
      // older lines stack above it (canvas y grows downward).
      const y = backlogBottom - (lastLine - 1 - line) * LINE_HEIGHT;

      for (const rect of selectedRects(line)) {
        ctx.fillStyle = SELECTION_COLOR;
        ctx.fillRect(PADDING + rect.x, y - FONT_SIZE - 2, rect.w, LINE_HEIGHT);
      }

      if (searching && currentMatchIndex !== -1) {
        const segStart = segment.offset;
        const segEnd = segStart + segment.text.length;
        for (let m = 0; m < searchMatches.length; m += 1) {
          const match = searchMatches[m];
          if (match.entry !== entry) continue;
          const matchEnd = match.pos + match.len;
          if (matchEnd <= segStart || match.pos >= segEnd) continue;
          const localStart = Math.max(0, match.pos - segStart);
          const localEnd = Math.min(segment.text.length, matchEnd - segStart);
          ctx.fillStyle =
            m === currentMatchIndex ? SEARCH_SELECTED : SEARCH_HIGHLIGHT;
          const x0 = measure(segment.text.slice(0, localStart));
          const x1 = measure(segment.text.slice(0, localEnd));
          ctx.fillRect(PADDING + x0, y - FONT_SIZE - 2, x1 - x0, LINE_HEIGHT);
        }
      }

      ctx.fillStyle = entry.color;
      ctx.fillText(segment.text, PADDING, y);
    }
    ctx.restore();

    drawScrollbar();

    const suggestionBase = inputTop - PROMPT_GAP - LINE_HEIGHT - 4;
    if (searching) {
      ctx.fillStyle = CONSOLE_COLORS.muted;
      if (searchMatches.length > 0) {
        ctx.fillText(
          t(
            "ui.console.match",
            { current: currentMatchIndex + 1, total: searchMatches.length },
            `Match ${currentMatchIndex + 1} of ${searchMatches.length}`,
          ),
          PADDING,
          suggestionBase + FONT_SIZE,
        );
      } else {
        ctx.fillText(
          t("ui.console.noResults", {}, "No results"),
          PADDING,
          suggestionBase + FONT_SIZE,
        );
      }
    } else {
      renderSuggestions(suggestionBase, inputTop);
    }

    renderInput(inputTop, now);
    dirty = false;
  }

  function renderSuggestions(yBase, inputTop) {
    if (!completion) return;

    if (completion.kind === "help" && completion.spec) {
      ctx.fillStyle = CONSOLE_COLORS.muted;
      ctx.fillText(
        t(
          "ui.console.helpPrefix",
          { text: t(completion.spec.help, {}, "") },
          `Help: ${t(completion.spec.help, {}, "")}`,
        ),
        PADDING,
        yBase + FONT_SIZE,
      );
      ctx.fillText(
        t(
          "ui.console.usage",
          { name: completion.spec.name, params: completion.spec.params || "" },
          `Usage: ${completion.spec.name} ${completion.spec.params || ""}`,
        ),
        PADDING,
        yBase + FONT_SIZE + LINE_HEIGHT,
      );
      return;
    }
    if (completion.candidates.length === 0) return;

    const word = input.value.slice(
      completion.range.start,
      completion.range.end,
    );
    const rowY = yBase + FONT_SIZE;

    ctx.save();
    ctx.beginPath();
    ctx.rect(0, 0, canvas.clientWidth, inputTop);
    ctx.clip();

    let x = PADDING - completionOffset;
    for (let i = 0; i < completion.candidates.length; i += 1) {
      const candidate = completion.candidates[i];
      const width = measure(candidate) + 4;
      if (i === completionIndex) {
        ctx.fillStyle = SUGGESTION_SELECTED_BG;
        roundRect(ctx, x - 2, yBase - 2, width, FONT_SIZE + 4, 2);
        ctx.fill();
        ctx.fillStyle = "#ffffff";
      } else {
        ctx.fillStyle = CONSOLE_COLORS.muted;
      }
      const matchPos = word
        ? candidate.toLowerCase().indexOf(word.toLowerCase())
        : -1;
      if (matchPos !== -1) {
        ctx.fillText(candidate.slice(0, matchPos), x, rowY);
        const beforeWidth = measure(candidate.slice(0, matchPos));
        ctx.fillStyle = "#ffbf4d";
        ctx.fillText(candidate.slice(matchPos, matchPos + word.length), x + beforeWidth, rowY);
        if (i !== completionIndex) ctx.fillStyle = CONSOLE_COLORS.muted;
        else ctx.fillStyle = "#ffffff";
        ctx.fillText(
          candidate.slice(matchPos + word.length),
          x + beforeWidth + measure(word),
          rowY,
        );
      } else {
        ctx.fillText(candidate, x, rowY);
      }
      x += width + SUGGESTION_GAP;
    }

    if (completionIndex >= 0 && completionIndex < completion.candidates.length) {
      let selectedX = PADDING - completionTarget;
      for (let i = 0; i < completionIndex; i += 1) {
        selectedX += measure(completion.candidates[i]) + 4 + SUGGESTION_GAP;
      }
      const selectedWidth = measure(completion.candidates[completionIndex]) + 4;
      if (selectedX < PADDING) {
        completionTarget += selectedX - PADDING - canvas.clientWidth * 0.25;
      } else if (selectedX + selectedWidth > canvas.clientWidth - PADDING) {
        completionTarget +=
          selectedX + selectedWidth - (canvas.clientWidth - PADDING) +
          canvas.clientWidth * 0.25;
      }
    }
    ctx.restore();
  }

  function renderInput(inputTop, now) {
    const prompt = promptText();
    const promptWidth = measure(prompt);
    const segments = inputSegments();
    const textY = inputTop + FONT_SIZE + 3;
    const caret = inputCaretPosition();

    ctx.fillStyle = "#ffffff";
    ctx.fillText(prompt, PADDING, textY);

    const selectionRange = inputSelectionXRange();
    const selStart = selectionRange ? selectionRange.start : 0;
    const selEnd = selectionRange ? selectionRange.end : 0;

    let acc = 0;
    for (let i = 0; i < segments.length; i += 1) {
      const segment = segments[i];
      const segmentEnd = acc + segment.text.length;
      const lineY = textY + i * LINE_HEIGHT;
      const segmentStartX = PADDING + promptWidth;

      if (selectionRange) {
        const localStart = clamp(selStart - acc, 0, segment.text.length);
        const localEnd = clamp(selEnd - acc, 0, segment.text.length);
        if (localStart < localEnd) {
          const x0 = measure(segment.text.slice(0, localStart));
          const x1 = measure(segment.text.slice(0, localEnd));
          ctx.fillStyle = "rgba(255, 255, 255, 0.22)";
          ctx.fillRect(
            segmentStartX + x0,
            lineY - FONT_SIZE - 2,
            x1 - x0,
            LINE_HEIGHT,
          );
        }
      }

      ctx.fillStyle = "#e8e8e8";
      ctx.fillText(segment.text, segmentStartX, lineY);
      acc = segmentEnd;
    }

    const blink = Math.floor(now / 530) % 2 === 0;
    if (blink && document.activeElement === input) {
      ctx.fillStyle = "#ffffff";
      ctx.fillRect(
        PADDING + promptWidth + caret.x,
        textY + caret.line * LINE_HEIGHT - FONT_SIZE - 2,
        1.5,
        LINE_HEIGHT,
      );
    }
  }

  function roundRect(context, x, y, w, h, r) {
    const radius = Math.min(r, w / 2, h / 2);
    context.beginPath();
    context.moveTo(x + radius, y);
    context.arcTo(x + w, y, x + w, y + h, radius);
    context.arcTo(x + w, y + h, x, y + h, radius);
    context.arcTo(x, y + h, x, y, radius);
    context.arcTo(x, y, x + w, y, radius);
    context.closePath();
  }

  // ------------------------------------------------------------------ loop

  function loop(now) {
    rafId = requestAnimationFrame(loop);
    const dt = Math.min(0.05, Math.max(0, (now - lastFrameTime) / 1000));
    lastFrameTime = now;
    completionOffset += (completionTarget - completionOffset) * Math.min(1, dt * 14);
    render(now);
  }

  function setOpen(value) {
    if (open === value) return;
    open = value;
    dirty = true;
    if (open) {
      lastFrameTime = performance.now();
      rafId = requestAnimationFrame(loop);
    } else {
      cancelAnimationFrame(rafId);
      rafId = 0;
      selection = null;
      if (searching) setSearching(false);
    }
  }

  // ------------------------------------------------------------------ events

  function handleInputEvent() {
    updateCompletion();
    if (searching) updateSearch();
    dirty = true;
  }

  function handleKeyDown(event) {
    if (event.isComposing || event.keyCode === 229) return;

    if (event.key === "Enter") {
      event.preventDefault();
      if (searching) selectNextSearchMatch(event.shiftKey ? -1 : 1);
      else executeLine(input.value);
      return;
    }
    if (event.key === "Tab") {
      event.preventDefault();
      applyCompletion(event.shiftKey ? -1 : 1);
      return;
    }
    if (event.key === "Escape") {
      event.preventDefault();
      if (searching) {
        setSearching(false);
        input.focus({ preventScroll: true });
      } else {
        onCloseRequested?.();
      }
      return;
    }
    if (event.key === "ArrowUp") {
      event.preventDefault();
      if (searching) {
        selectNextSearchMatch(-1);
        return;
      }
      if (historyIndex === -1) historyDraft = input.value;
      if (historyIndex < history.length - 1) {
        historyIndex += 1;
        input.value = history[history.length - 1 - historyIndex];
        input.setSelectionRange(input.value.length, input.value.length);
      }
      updateCompletion();
      dirty = true;
      return;
    }
    if (event.key === "ArrowDown") {
      event.preventDefault();
      if (searching) {
        selectNextSearchMatch(1);
        return;
      }
      if (historyIndex > 0) {
        historyIndex -= 1;
        input.value = history[history.length - 1 - historyIndex];
      } else if (historyIndex === 0) {
        historyIndex = -1;
        input.value = historyDraft;
      }
      input.setSelectionRange(input.value.length, input.value.length);
      updateCompletion();
      dirty = true;
      return;
    }
    if (event.key === "PageUp") {
      event.preventDefault();
      scrollUp = clamp(scrollUp + backlogCapacity(), 0, maxScroll);
      selection = null;
      dirty = true;
      return;
    }
    if (event.key === "PageDown") {
      event.preventDefault();
      scrollUp = clamp(scrollUp - backlogCapacity(), 0, maxScroll);
      selection = null;
      dirty = true;
      return;
    }
    if (event.key === "Home" && !input.value) {
      event.preventDefault();
      scrollUp = maxScroll;
      selection = null;
      dirty = true;
      return;
    }
    if (event.key === "End" && !input.value) {
      event.preventDefault();
      scrollUp = 0;
      selection = null;
      dirty = true;
      return;
    }
    if (event.ctrlKey && event.key.toLowerCase() === "c") {
      if (!inputSelectionXRange()) {
        event.preventDefault();
        copySelection();
      }
      return;
    }
    updateCompletion();
  }

  function handlePaste(event) {
    const text = event.clipboardData?.getData("text") ?? "";
    if (!text) return;
    const lines = text.split(/\r?\n/);
    if (lines.length <= 1) return;
    event.preventDefault();
    const last = lines.pop();
    const cursor = input.selectionStart ?? input.value.length;
    const end = input.selectionEnd ?? cursor;
    const before = input.value.slice(0, Math.min(cursor, end));
    const after = input.value.slice(Math.max(cursor, end));
    const commands = [before + lines.shift(), ...lines];
    for (const command of commands) executeLine(command);
    input.value = last + after;
    input.setSelectionRange(last.length, last.length);
    updateCompletion();
    dirty = true;
  }

  function handleWheel(event) {
    if (!open) return;
    event.preventDefault();
    const lines = Math.max(1, Math.round(Math.abs(event.deltaY) / 53));
    scrollUp = clamp(scrollUp + Math.sign(event.deltaY) * lines, 0, maxScroll);
    selection = null;
    dirty = true;
  }

  function canvasPointerPosition(event) {
    const rect = canvas.getBoundingClientRect();
    return { x: event.clientX - rect.left, y: event.clientY - rect.top };
  }

  function handlePointerDown(event) {
    if (!open || event.button !== 0) return;
    const { x, y } = canvasPointerPosition(event);

    // Scrollbar takes priority over backlog selection.
    if (scrollbarVisible()) {
      const track = scrollbarTrackRect();
      const hit =
        x >= track.x - 4 &&
        x <= track.x + track.w + 4 &&
        y >= track.y - 4 &&
        y <= track.y + track.h + 4;
      if (hit) {
        const thumb = scrollbarThumbRect(track);
        if (thumb && y >= thumb.y && y <= thumb.y + thumb.h) {
          scrollbarDragging = true;
          scrollbarDragStartY = y;
          scrollbarDragStartScrollUp = scrollUp;
        } else if (thumb && y < thumb.y) {
          scrollUp = clamp(scrollUp + backlogCapacity(), 0, maxScroll);
        } else if (thumb && y > thumb.y + thumb.h) {
          scrollUp = clamp(scrollUp - backlogCapacity(), 0, maxScroll);
        }
        selection = null;
        dirty = true;
        return;
      }
    }

    const inputTop = canvas.clientHeight - PADDING - inputRowHeight();
    if (y >= inputTop) {
      input.focus({ preventScroll: true });
      const rect = canvas.getBoundingClientRect();
      inputSelectAnchor = cursorFromX(event.clientX - rect.left, y);
      input.setSelectionRange(inputSelectAnchor, inputSelectAnchor);
      inputSelecting = true;
      selection = null;
      dirty = true;
      return;
    }
    const line = lineFromY(y);
    if (line < 0) return;
    const charIndex = charFromX(line, x - PADDING);
    selection = { aLine: line, aChar: charIndex, cLine: line, cChar: charIndex };
    selecting = true;
    dirty = true;
  }

  function handlePointerMove(event) {
    if (!open) return;

    const { x, y } = canvasPointerPosition(event);
    if (scrollbarVisible()) {
      const track = scrollbarTrackRect();
      scrollbarHovered =
        x >= track.x - 2 &&
        x <= track.x + track.w + 2 &&
        y >= track.y &&
        y <= track.y + track.h;
    } else {
      scrollbarHovered = false;
    }

    if (scrollbarDragging) {
      const track = scrollbarTrackRect();
      const thumb = scrollbarThumbRect(track);
      const movable = track.h - (thumb ? thumb.h : 0);
      if (movable > 0) {
        // Dragging the thumb UP moves the view up (toward older lines).
        const ratio = (scrollbarDragStartY - y) / movable;
        scrollUp = clamp(
          Math.round(scrollbarDragStartScrollUp + ratio * maxScroll),
          0,
          maxScroll,
        );
      }
      dirty = true;
      return;
    }
    if (inputSelecting) {
      moveCaretFromPointer(event);
      return;
    }
    if (!selecting) return;
    const line = lineFromY(y);
    if (line < 0) return;
    selection.cLine = line;
    selection.cChar = charFromX(line, x - PADDING);
    dirty = true;
  }

  function handlePointerUp() {
    if (scrollbarDragging) {
      scrollbarDragging = false;
      dirty = true;
      return;
    }
    if (!selecting && !inputSelecting) return;
    if (
      selecting &&
      selection &&
      selection.aLine === selection.cLine &&
      selection.aChar === selection.cChar
    ) {
      selection = null;
    }
    selecting = false;
    inputSelecting = false;
    dirty = true;
  }

  function handleTouchStart(event) {
    if (!open || event.touches.length !== 1) return;
    touchLastY = event.touches[0].clientY;
  }

  function handleTouchMove(event) {
    if (!open || touchLastY === null || event.touches.length !== 1) return;
    const y = event.touches[0].clientY;
    const delta = touchLastY - y;
    touchLastY = y;
    if (Math.abs(delta) < 2) return;
    scrollUp = clamp(scrollUp + Math.round(delta / LINE_HEIGHT), 0, maxScroll);
    dirty = true;
  }

  function handleTouchEnd() {
    touchLastY = null;
  }

  function handleWindowKeyDown(event) {
    if (!open) return;
    if (event.key === "Escape") {
      event.preventDefault();
      event.stopPropagation();
      if (searching) {
        setSearching(false);
        input.focus({ preventScroll: true });
      } else {
        onCloseRequested?.();
      }
      return;
    }
    if (event.ctrlKey && (event.key === "f" || event.key === "F")) {
      event.preventDefault();
      event.stopPropagation();
      if (!searching) {
        setSearching(true);
        input.focus({ preventScroll: true });
      }
    }
  }

  function handleWindowPointerDown(event) {
    if (!open) return;
    if (panel.contains(event.target)) return;
    onCloseRequested?.();
  }

  function handleResize() {
    wrapDirty = true;
    dirty = true;
  }

  // ------------------------------------------------------------------ setup

  input.addEventListener("input", handleInputEvent);
  input.addEventListener("keydown", handleKeyDown);
  input.addEventListener("paste", handlePaste);
  input.addEventListener("select", () => {
    dirty = true;
  });
  canvas.addEventListener("pointerdown", handlePointerDown);
  window.addEventListener("pointermove", handlePointerMove);
  window.addEventListener("pointerup", handlePointerUp);
  panel.addEventListener("wheel", handleWheel, { passive: false });
  canvas.addEventListener("touchstart", handleTouchStart, { passive: true });
  canvas.addEventListener("touchmove", handleTouchMove, { passive: true });
  canvas.addEventListener("touchend", handleTouchEnd, { passive: true });
  window.addEventListener("keydown", handleWindowKeyDown, { capture: true });
  window.addEventListener("pointerdown", handleWindowPointerDown, { capture: true });
  window.addEventListener("resize", handleResize);
  const resizeObserver = new ResizeObserver(handleResize);
  resizeObserver.observe(panel);

  input.setAttribute("placeholder", t("ui.console.placeholder", {}, "Console"));
  dirty = true;

  return Object.freeze({
    addLine,
    copySelection,
    focusInput() {
      input.focus({ preventScroll: true });
      input.setSelectionRange(input.value.length, input.value.length);
    },
    isOpen: () => open,
    setOpen,
    submit,
    toggle() {
      setOpen(!open);
      return open;
    },
    destroy() {
      setOpen(false);
      input.removeEventListener("input", handleInputEvent);
      input.removeEventListener("keydown", handleKeyDown);
      input.removeEventListener("paste", handlePaste);
      panel.removeEventListener("wheel", handleWheel);
      canvas.removeEventListener("pointerdown", handlePointerDown);
      window.removeEventListener("pointermove", handlePointerMove);
      window.removeEventListener("pointerup", handlePointerUp);
      window.removeEventListener("keydown", handleWindowKeyDown, { capture: true });
      window.removeEventListener("pointerdown", handleWindowPointerDown, { capture: true });
      window.removeEventListener("resize", handleResize);
      resizeObserver.disconnect();
      canvas.remove();
    },
  });
}

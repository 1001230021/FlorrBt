import { ChatFlag, clamp, rarityColor } from "./protocol.js";
import { dom, state } from "./app_context.js";
import { clientRuntimeConfig } from "./client_config.js";
import { rarityExotic, rarityPrimordial } from "./game_ids.js";
import { t } from "./i18n.js";
import { createTextInput } from "./text_input.js";

const {
  chatMaxHistory,
  chatClosedMaxLines,
  chatVisibleMs,
  chatFadeMs,
  chatClosedRenderIntervalMs,
} = clientRuntimeConfig;
const {
  canvas,
  chatPanel,
  chatChannels,
  chatLog,
  chatComposer,
  chatComposeChannel,
  chatInput,
} = dom;
const composeKinds = Object.freeze(["local", "squad", "global"]);

export function createChatUi({
  beforeOpen = null,
  executeCommand = null,
  focusTarget = canvas,
} = {}) {
  let composeKind = composeKinds[0];
  let textInput = null;

  const refreshComposer = () => {
    const channel = chatFlagLabel(composeKind);
    chatComposer.dataset.channel = composeKind;
    chatComposeChannel.textContent = `[${channel}]`;
    textInput.setPlaceholder(t("ui.chat.messagePlaceholder"));
    textInput.setAriaLabel(t("ui.chat.messageAriaLabel", { channel }));
  };

  const cycleChannel = (direction = 1) => {
    const current = composeKinds.indexOf(composeKind);
    const step = direction < 0 ? -1 : 1;
    composeKind =
      composeKinds[
        (current + step + composeKinds.length) % composeKinds.length
      ];
    refreshComposer();
    return composeKind;
  };

  const render = (now = performance.now()) => {
    chatLog.replaceChildren();
    const rows = [];
    for (const chat of state.chats) {
      const kind = chatKind(chat);
      if (!state.chatFilters[kind]) continue;
      const alpha = state.chatOpen ? 1 : chatClosedAlpha(chat, now);
      if (alpha <= 0) continue;
      rows.push({ chat, kind, alpha });
    }

    const visibleRows = state.chatOpen ? rows : rows.slice(-chatClosedMaxLines);
    for (const { chat, kind, alpha } of visibleRows) {
      const line = document.createElement("div");
      line.className = `chat-line ${kind}`;
      line.style.setProperty("--chat-alpha", alpha.toFixed(3));

      if (kind !== "server") {
        const prefix = document.createElement("span");
        prefix.className = "chat-prefix";
        prefix.textContent = `[${chatFlagLabel(kind)}] `;
        line.appendChild(prefix);

        const name = document.createElement("span");
        name.className = "chat-name";
        name.textContent = `${chat.playerName || chat.playerId}: `;
        line.appendChild(name);
      }

      const body = document.createElement("span");
      appendRarityMessageParts(body, chat.message || "");
      line.appendChild(body);
      chatLog.appendChild(line);
    }

    if (state.chatOpen) chatLog.scrollTop = chatLog.scrollHeight;
    state.lastChatRenderAt = now;
  };

  const close = ({ focus = true } = {}) => {
    state.chatOpen = false;
    textInput.clear();
    chatComposer.classList.add("hidden");
    chatChannels.classList.add("hidden");
    chatPanel.classList.remove("open");
    render();
    if (focus) focusTarget?.focus?.();
  };

  const open = (prefix = "") => {
    if (!state.authenticated) return;
    beforeOpen?.();
    state.chatOpen = true;
    chatPanel.classList.add("open");
    chatChannels.classList.remove("hidden");
    textInput.setValue(prefix);
    refreshComposer();
    chatComposer.classList.remove("hidden");
    textInput.focus({ cursor: "end" });
    render();
  };

  const submit = () => {
    const text = textInput.getValue({ trim: true });
    close();
    if (!text) return;
    executeCommand?.(
      text.startsWith("/") ? text.slice(1) : `say ${composeKind} ${text}`,
    );
  };

  textInput = createTextInput(chatInput, {
    variant: "embedded",
    onSubmit: submit,
    onCancel: () => close(),
    onKeyDown: (event) => {
      if (event.key !== "Tab") return false;
      event.preventDefault();
      cycleChannel(event.shiftKey ? -1 : 1);
      return true;
    },
  });

  const append = (chat, now = performance.now()) => {
    state.chats.push({ ...chat, receivedAt: now });
    if (state.chats.length > chatMaxHistory) {
      state.chats.splice(0, state.chats.length - chatMaxHistory);
    }
    render(now);
  };

  const updateVisibility = (now) => {
    if (state.chatOpen || state.chats.length === 0) return;
    if (now - state.lastChatRenderAt < chatClosedRenderIntervalMs) return;
    render(now);
  };

  const setVisible = (visible) => {
    chatPanel?.classList.toggle("hidden", !visible);
    if (!visible) close({ focus: false });
  };

  const handleFilterChange = (event) => {
    const input = event.target?.closest?.("input[data-chat-filter]");
    if (!input) return;
    state.chatFilters[input.dataset.chatFilter] = input.checked;
    render();
  };

  const refreshTranslations = () => {
    refreshComposer();
    render();
  };

  return {
    append,
    close,
    contains: (target) => !!chatPanel?.contains(target),
    cycleChannel,
    handleFilterChange,
    open,
    refreshTranslations,
    render,
    setVisible,
    submit,
    updateVisibility,
  };
}

function chatKind(chat) {
  if (!chat) return "local";
  if (chat.flag === ChatFlag.Global) return "global";
  if (chat.flag === ChatFlag.Whisper) return "whisper";
  if (chat.flag === ChatFlag.Squad) return "squad";
  if (chat.flag === ChatFlag.Server) return "server";
  return "local";
}

function chatFlagLabel(kind) {
  if (kind === "global") return t("ui.chat.channels.global");
  if (kind === "whisper") return t("ui.chat.channels.whisper");
  if (kind === "squad") return t("ui.chat.channels.squad");
  return t("ui.chat.channels.local");
}

function chatClosedAlpha(chat, now) {
  const receivedAt = Number.isFinite(chat.receivedAt) ? chat.receivedAt : now;
  const age = now - receivedAt;
  if (age <= chatVisibleMs) return 1;
  if (age >= chatVisibleMs + chatFadeMs) return 0;
  return clamp(1 - (age - chatVisibleMs) / chatFadeMs, 0, 1);
}

function appendRarityMessageParts(parent, message) {
  const text = String(message || "");
  let plainStart = 0;
  let searchStart = 0;

  while (searchStart < text.length) {
    const markStart = text.indexOf("<", searchStart);
    if (markStart < 0) break;

    const doubleMark = text[markStart + 1] === "<";
    const markEnd = doubleMark
      ? text.indexOf(">>", markStart + 2)
      : text.indexOf(">", markStart + 1);
    if (markEnd < 0) break;

    const bodyOpen = doubleMark ? markEnd + 2 : markEnd + 1;
    if (text[bodyOpen] !== "(") {
      searchStart = markStart + 1;
      continue;
    }

    const rarity = matchRarity(
      text.slice(markStart + (doubleMark ? 2 : 1), markEnd),
    );
    const close = findRarityMessageClose(text, bodyOpen);
    if (rarity <= 0 || close < 0) {
      searchStart = markStart + 1;
      continue;
    }

    appendChatText(parent, text.slice(plainStart, markStart));
    appendChatText(
      parent,
      text.slice(bodyOpen + 1, close),
      rarityColor(rarity, 1),
      rarity === rarityPrimordial ? "chat-rarity-primordial" : "",
    );
    plainStart = close + 1;
    searchStart = close + 1;
  }

  appendChatText(parent, text.slice(plainStart));
}

function findRarityMessageClose(text, openIndex) {
  let depth = 0;
  for (let i = openIndex; i < text.length; i += 1) {
    const ch = text[i];
    if (ch === "(") depth += 1;
    else if (ch === ")") {
      depth -= 1;
      if (depth === 0) return i;
    }
  }
  return -1;
}

function appendChatText(parent, text, color = "", className = "") {
  if (!text) return;
  const span = document.createElement("span");
  span.textContent = text;
  if (color) span.style.color = color;
  if (className) span.className = className;
  parent.appendChild(span);
}

function matchRarity(text) {
  const key = String(text).toLowerCase();
  const aliases = {
    c: 1,
    common: 1,
    un: 2,
    unusual: 2,
    r: 3,
    rare: 3,
    e: 4,
    epic: 4,
    l: 5,
    legendary: 5,
    leg: 5,
    led: 5,
    m: 6,
    mythic: 6,
    my: 6,
    u: 7,
    ultra: 7,
    ult: 7,
    s: 8,
    super: 8,
    et: 9,
    eternal: 9,
    q: 10,
    unique: 10,
    uni: 10,
    p: 11,
    primordial: 11,
    ex: rarityExotic,
    exotic: rarityExotic,
  };
  return aliases[key] || 0;
}

const DEFAULT_TARGET_PLAYER = "kghlkjo";
const DEFAULT_CHAT_TRIGGER = "your phone linging";
const DEFAULT_CHAT_DURATION_SECONDS = 16;
const DEFAULT_AUDIBLE_DISTANCE = 2048;

const defaultProximitySource = new URL(
  "../assets/audio/kghlkjo.mp3",
  import.meta.url,
).href;
const defaultChatSource = new URL(
  "../assets/audio/phone_linging_trigger.mp3",
  import.meta.url,
).href;
const defaultDouliSource = new URL(
  "../assets/audio/douli.mp3",
  import.meta.url,
).href;

export function createMysteryAudio(options = {}) {
  const targetPlayer = normalizeText(
    options.targetPlayer || DEFAULT_TARGET_PLAYER,
  );
  const chatTrigger = normalizeText(
    options.chatTrigger || DEFAULT_CHAT_TRIGGER,
  );
  const chatDurationSeconds = positiveNumber(
    options.chatDurationSeconds,
    DEFAULT_CHAT_DURATION_SECONDS,
  );
  const fallbackAudibleDistance = positiveNumber(
    options.fallbackAudibleDistance,
    DEFAULT_AUDIBLE_DISTANCE,
  );
  const playerEntityType = options.playerEntityType;
  const douliPetalType = options.douliPetalType;
  const audioFactory = options.audioFactory || ((source) => new Audio(source));
  const setTimer = options.setTimer || globalThis.setTimeout.bind(globalThis);
  const clearTimer =
    options.clearTimer || globalThis.clearTimeout.bind(globalThis);

  const proximityAudio = prepareAudio(
    audioFactory(options.proximitySource || defaultProximitySource),
    true,
    0,
  );
  const chatAudio = prepareAudio(
    audioFactory(options.chatSource || defaultChatSource),
    false,
    1,
  );
  const douliAudio = prepareAudio(
    audioFactory(options.douliSource || defaultDouliSource),
    true,
    0,
  );

  let proximityVolume = 0;
  let proximityPlayPending = false;
  let proximityPlayBlocked = false;
  let douliVolume = 0;
  let douliPlayPending = false;
  let douliPlayBlocked = false;
  let chatRequested = false;
  let chatPlayPending = false;
  let chatPlayBlocked = false;
  let chatStopTimer = null;
  let chatRequestId = 0;

  const stopChat = () => {
    chatRequestId += 1;
    chatRequested = false;
    chatPlayPending = false;
    chatPlayBlocked = false;
    if (chatStopTimer !== null) {
      clearTimer(chatStopTimer);
      chatStopTimer = null;
    }
    pauseAndReset(chatAudio);
    syncPlayback();
  };

  const startChatPlayback = () => {
    if (
      !chatRequested ||
      douliVolume > 0 ||
      chatPlayPending ||
      !chatAudio.paused
    )
      return;

    chatPlayPending = true;
    chatPlayBlocked = false;
    const requestId = chatRequestId;
    playAudio(chatAudio).then(
      () => {
        if (requestId !== chatRequestId || !chatRequested) return;
        chatPlayPending = false;
        if (douliVolume > 0) {
          chatAudio.pause();
          return;
        }
        if (chatStopTimer === null)
          chatStopTimer = setTimer(stopChat, chatDurationSeconds * 1000);
      },
      () => {
        if (requestId !== chatRequestId || !chatRequested) return;
        chatPlayPending = false;
        chatPlayBlocked = true;
      },
    );
  };

  function syncProximityPlayback() {
    proximityAudio.volume = clamp(proximityVolume, 0, 1);
    if (douliVolume > 0 || chatRequested || proximityVolume <= 0) {
      proximityAudio.pause();
      if (!chatRequested && proximityVolume <= 0)
        resetPlaybackPosition(proximityAudio);
      return;
    }
    if (
      !proximityAudio.paused ||
      proximityPlayPending ||
      proximityPlayBlocked
    )
      return;

    proximityPlayPending = true;
    proximityPlayBlocked = false;
    playAudio(proximityAudio).then(
      () => {
        proximityPlayPending = false;
        if (douliVolume > 0 || chatRequested || proximityVolume <= 0)
          proximityAudio.pause();
      },
      () => {
        proximityPlayPending = false;
        proximityPlayBlocked =
          douliVolume <= 0 && !chatRequested && proximityVolume > 0;
      },
    );
  }

  function syncDouliPlayback() {
    douliAudio.volume = clamp(douliVolume, 0, 1);
    if (douliVolume <= 0) {
      douliAudio.pause();
      resetPlaybackPosition(douliAudio);
      return;
    }
    if (!douliAudio.paused || douliPlayPending || douliPlayBlocked) return;

    douliPlayPending = true;
    douliPlayBlocked = false;
    playAudio(douliAudio).then(
      () => {
        douliPlayPending = false;
        if (douliVolume <= 0) douliAudio.pause();
      },
      () => {
        douliPlayPending = false;
        douliPlayBlocked = douliVolume > 0;
      },
    );
  }

  function syncPlayback() {
    if (douliVolume > 0) {
      chatAudio.pause();
      proximityAudio.pause();
      syncDouliPlayback();
      return;
    }

    douliAudio.pause();
    resetPlaybackPosition(douliAudio);
    if (chatRequested) {
      proximityAudio.pause();
      startChatPlayback();
      return;
    }
    syncProximityPlayback();
  }

  const updateProximity = ({ enabled, owner, entities, viewRadius } = {}) => {
    const entityList = Array.from(entities || []);
    proximityVolume = enabled
      ? findTargetVolume({
          owner,
          entities: entityList,
          viewRadius,
          fallbackAudibleDistance,
          playerEntityType,
          targetPlayer,
        })
      : 0;
    douliVolume = enabled
      ? findPetalCarrierVolume({
          owner,
          entities: entityList,
          viewRadius,
          fallbackAudibleDistance,
          playerEntityType,
          petalType: douliPetalType,
        })
      : 0;
    syncPlayback();
  };

  const handleChat = (chat) => {
    if (normalizeText(chat?.message) !== chatTrigger || chatRequested)
      return false;

    chatRequestId += 1;
    chatRequested = true;
    chatPlayBlocked = false;
    pauseAndReset(chatAudio);
    syncPlayback();
    return true;
  };

  const unlock = () => {
    if (douliVolume > 0) {
      douliPlayBlocked = false;
      syncPlayback();
      return;
    }
    if (chatRequested) {
      chatPlayBlocked = false;
      syncPlayback();
      return;
    }
    if (proximityVolume > 0) {
      proximityPlayBlocked = false;
      syncPlayback();
    }
  };

  const reset = () => {
    proximityVolume = 0;
    proximityPlayPending = false;
    proximityPlayBlocked = false;
    douliVolume = 0;
    douliPlayPending = false;
    douliPlayBlocked = false;
    pauseAndReset(proximityAudio);
    pauseAndReset(douliAudio);
    stopChat();
  };

  chatAudio.addEventListener?.("ended", stopChat);

  return {
    updateProximity,
    handleChat,
    unlock,
    reset,
  };
}

function findTargetVolume({
  owner,
  entities,
  viewRadius,
  fallbackAudibleDistance,
  playerEntityType,
  targetPlayer,
}) {
  return findMatchingPlayerVolume({
    owner,
    entities,
    viewRadius,
    fallbackAudibleDistance,
    playerEntityType,
    matches: (snap) => normalizeText(snap.name) === targetPlayer,
  });
}

function findPetalCarrierVolume({
  owner,
  entities,
  viewRadius,
  fallbackAudibleDistance,
  playerEntityType,
  petalType,
}) {
  if (!Number.isFinite(petalType)) return 0;
  return findMatchingPlayerVolume({
    owner,
    entities,
    viewRadius,
    fallbackAudibleDistance,
    playerEntityType,
    matches: (snap) =>
      (snap.primarySlots || []).some(
        (slot) => slot?.petalType === petalType && slot?.rarity > 0,
      ),
  });
}

function findMatchingPlayerVolume({
  owner,
  entities,
  viewRadius,
  fallbackAudibleDistance,
  playerEntityType,
  matches,
}) {
  const ownerSnap = owner?.snapshot;
  const ownerPos = entityPosition(owner);
  if (!ownerSnap || !ownerPos) return 0;
  if (matches(ownerSnap)) return 1;

  const audibleDistance = positiveNumber(viewRadius, fallbackAudibleDistance);
  let volume = 0;
  for (const entity of entities || []) {
    const snap = entity?.snapshot;
    if (
      !snap ||
      snap.entityId === ownerSnap.entityId ||
      snap.entityType !== playerEntityType ||
      !matches(snap)
    )
      continue;

    const pos = entityPosition(entity);
    if (!pos) continue;
    const distance = Math.hypot(pos.x - ownerPos.x, pos.y - ownerPos.y);
    volume = Math.max(volume, 1 - distance / audibleDistance);
  }
  return clamp(volume, 0, 1);
}

function entityPosition(entity) {
  const pos = entity?.renderPos || entity?.snapshot?.pos;
  if (!Number.isFinite(pos?.x) || !Number.isFinite(pos?.y)) return null;
  return pos;
}

function prepareAudio(audio, loop, volume) {
  audio.loop = loop;
  audio.preload = "auto";
  audio.volume = volume;
  return audio;
}

function pauseAndReset(audio) {
  audio.pause();
  resetPlaybackPosition(audio);
}

function resetPlaybackPosition(audio) {
  try {
    audio.currentTime = 0;
  } catch {
    // Metadata may not be available yet; playback will still begin at zero.
  }
}

function playAudio(audio) {
  try {
    return Promise.resolve(audio.play());
  } catch (error) {
    return Promise.reject(error);
  }
}

function normalizeText(value) {
  return String(value || "")
    .trim()
    .replace(/\s+/g, " ")
    .toLowerCase();
}

function positiveNumber(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) && number > 0 ? number : fallback;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

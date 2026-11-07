// 先等待配装回执及权威状态，避免复活更换身体后使尚未发送的换装失效。
export function createRespawnRequest({ send, hasPendingChanges, now = () => performance.now() }) {
  let queued = false;
  let sentAt = null;
  const pending = () => queued || (sentAt !== null && now() - sentAt < 3000);
  return {
    request() {
      if (pending()) return false;
      queued = true;
      return true;
    },
    tick() {
      if (!queued || hasPendingChanges()) return;
      if (send()) { queued = false; sentAt = now(); }
    },
    reset() { queued = false; sentAt = null; },
    get pending() { return pending(); },
  };
}

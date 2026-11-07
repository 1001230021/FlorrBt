// 背包身份和计数按精确不透明值处理；超时只重发原请求字节，不能重新随机结算。
export const EconomyOperation = Object.freeze({ Craft: 1, Forge: 2, Loadout: 3 });
export const EconomyOutcome = Object.freeze({
  Completed: 0, Invalid: 1, StaleRevision: 2, Forbidden: 3, InsufficientItems: 4,
  StorageBusy: 5, ConflictingRequest: 6, OutcomeExpired: 7, OutOfOrder: 8, WrongScope: 9, Pending: 10,
});
const encoder = new TextEncoder();
const counter = value => BigInt("0x" + value);
const key = value => value.toString(16).padStart(16, "0");

export function packEconomyRequest(request) {
  const scope = encoder.encode(request.inventoryScope), actor = encoder.encode(request.actorScope || "");
  const loadout = request.operation === EconomyOperation.Loadout;
  const primary = request.primary || [], secondary = request.secondary || [];
  if (!scope.length || scope.length > 255 || actor.length > 255 || primary.length > 64 || secondary.length > 64)
    throw new RangeError("Invalid inventory request shape");
  const bytes = new Uint8Array(20 + scope.length + actor.length + (loadout ? 2 + 2 * (primary.length + secondary.length) : 6));
  const view = new DataView(bytes.buffer);
  bytes.set([0xfa, request.operation, scope.length, actor.length]);
  view.setBigUint64(4, counter(request.requestId), true);
  view.setBigUint64(12, counter(request.expectedRevision), true);
  let offset = 20;
  bytes.set(scope, offset); offset += scope.length;
  bytes.set(actor, offset); offset += actor.length;
  if (loadout) {
    bytes[offset++] = primary.length; bytes[offset++] = secondary.length;
    for (const slot of [...primary, ...secondary]) {
      bytes[offset++] = slot?.petalType || 0; bytes[offset++] = slot?.rarity || 0;
    }
  } else {
    bytes[offset++] = request.petalType; bytes[offset++] = request.rarity;
    view.setUint32(offset, request.count, true);
  }
  return bytes;
}

export function createEconomyRequests({ send, onResult = () => {}, onScopeChanged = () => {}, now = () => Date.now() }) {
  let identity = "", baseline = null, ownerEpoch = 0, active = null;
  let queue = [];
  const pump = () => {
    if (!baseline || !ownerEpoch) return;
    if (active?.terminal) {
      if (active.terminal.inventoryScope === baseline.inventoryScope &&
          counter(baseline.inventoryRevision) >= counter(active.terminal.inventoryRevision) &&
          ownerEpoch >= active.requiredOwnerEpoch) {
        const finished = active;
        active = null;
        const retryLayout = finished.terminal.recorded && finished.terminal.outcome === EconomyOutcome.StaleRevision &&
          finished.intent.operation === EconomyOperation.Loadout && (finished.intent.staleRetries || 0) < 2 &&
          finished.intent.actorScope === baseline.actorScope &&
          queue.every(intent => intent.operation === EconomyOperation.Loadout && intent.actorScope === baseline.actorScope);
        if (retryLayout) {
          // 拾取等操作可以先改变背包版本。只有明确未执行的整套换装才用新版本重试，不重抽合成结果。
          const target = queue.at(-1) || finished.intent;
          queue = [{ ...target, staleRetries: (finished.intent.staleRetries || 0) + 1 }];
        } else {
          if (finished.terminal.outcome !== EconomyOutcome.Completed) queue = [];
          onResult(finished.terminal, finished.intent);
        }
      } else return;
    }
    if (!active && queue.length) {
      const intent = queue.shift();
      if (counter(baseline.inventoryLastRequest) === 0xffffffffffffffffn) return;
      const request = { ...intent, inventoryScope: baseline.inventoryScope,
        requestId: key(counter(baseline.inventoryLastRequest) + 1n), expectedRevision: baseline.inventoryRevision };
      active = { intent, request, bytes: packEconomyRequest(request), lastSent: -Infinity, terminal: null };
    }
    if (!active || now() - active.lastSent < 2000) return;
    // 发送失败也保留原请求，供重连重试。
    if (send(active.bytes)) active.lastSent = now();
  };
  return {
    connect(nextIdentity) {
      if (identity !== nextIdentity) { active = null; queue = []; }
      identity = nextIdentity; baseline = null; ownerEpoch = 0;
      if (active) { active.lastSent = -Infinity; active.terminal = null; }
    },
    disconnect() { baseline = null; ownerEpoch = 0; queue = []; },
    reset() { active = null; baseline = null; queue = []; ownerEpoch = 0; },
    inventory(message) {
      if (baseline && message.inventoryScope === baseline.inventoryScope &&
          counter(message.inventoryRevision) < counter(baseline.inventoryRevision)) return;
      const changed = baseline && baseline.inventoryScope !== message.inventoryScope;
      if (changed || (active && active.request.inventoryScope !== message.inventoryScope)) {
        active = null; queue = []; onScopeChanged();
      }
      baseline = message; pump();
    },
    owner() { ++ownerEpoch; pump(); },
    submit(intent) {
      if (!baseline || !ownerEpoch) return false;
      const next = { ...structuredClone(intent), actorScope: baseline.actorScope };
      const last = queue.at(-1);
      // 整套布局是最终目标；仅合并尚未发送的相邻换装，不能跨越合成/锻造或修改在途请求。
      if (next.operation === EconomyOperation.Loadout && last?.operation === EconomyOperation.Loadout && last.actorScope === next.actorScope)
        queue[queue.length - 1] = next;
      else {
        if (queue.length >= 16) return false;
        queue.push(next);
      }
      pump(); return true;
    },
    result(message) {
      if (!active || message.inventoryScope !== active.request.inventoryScope || message.requestId !== active.request.requestId) return false;
      if (message.outcome === EconomyOutcome.Pending || (!message.recorded && message.outcome === EconomyOutcome.StorageBusy)) {
        active.lastSent = now(); return true;
      }
      active.terminal = message; active.requiredOwnerEpoch = ownerEpoch + 1;
      // 结果后等待权威背包和归属更新，不用乐观余额发起下一事务。
      pump(); return true;
    },
    tick: pump,
    get pending() { return !!active || queue.length > 0; },
  };
}

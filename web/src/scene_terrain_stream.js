// Welcome 是有序连接上的场景屏障；地图路径不是场景身份，同资源房间也要新建场景代数和可变地形。
export function createSceneTerrainStream(apply, { limit = 131072, onOverflow = () => {} } = {}) {
  let epoch = 0;
  let ready = false;
  let failed = false;
  let pending = [];
  return {
    get epoch() { return epoch; },
    get pendingCount() { return pending.length; },
    begin() {
      epoch += 1;
      ready = false;
      failed = false;
      pending = [];
      return epoch;
    },
    accept(delta) {
      if (failed) return false;
      if (ready) { apply(delta); return true; }
      if (pending.length >= limit) {
        failed = true;
        pending = [];
        onOverflow();
        return false;
      }
      pending.push(delta);
      return true;
    },
    finish(loadedEpoch) {
      if (loadedEpoch !== epoch || failed) return false;
      ready = true;
      const queued = pending;
      pending = [];
      for (const delta of queued) apply(delta);
      return true;
    },
  };
}

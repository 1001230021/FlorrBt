// 地图合成与实体栅格共用按字节计量的 LRU；当帧可见项固定，空间不足时拒绝分配。
export class RasterBudget {
  constructor(limitBytes) {
    this.limitBytes = limitBytes;
    this.bytes = 0;
    this.frame = 0;
    this.time = null;
    this.entries = new Map();
  }
  beginFrame(time) {
    if (time !== this.time) { this.time = time; this.frame += 1; }
  }
  touch(key, pin = true) {
    const entry = this.entries.get(key);
    if (!entry) return false;
    // 同一贴图一帧可绘制数千次，已固定的项不重复调整 LRU。
    if (entry.frame === this.frame) return true;
    if (pin) entry.frame = this.frame;
    this.entries.delete(key);
    this.entries.set(key, entry);
    return true;
  }
  store(key, bytes, release) {
    if (!(bytes > 0) || bytes > this.limitBytes) return false;
    if (this.entries.has(key)) this.remove(key);
    for (const [candidate, entry] of this.entries) {
      if (this.bytes + bytes <= this.limitBytes) break;
      if (entry.frame !== this.frame) this.remove(candidate);
    }
    if (this.bytes + bytes > this.limitBytes) return false;
    this.entries.set(key, { bytes, release, frame: -1 });
    this.bytes += bytes;
    return true;
  }
  remove(key) {
    const entry = this.entries.get(key);
    if (!entry) return;
    this.entries.delete(key);
    this.bytes -= entry.bytes;
    entry.release();
  }
}

export const sceneRasterBudget = new RasterBudget(192 * 1024 * 1024);

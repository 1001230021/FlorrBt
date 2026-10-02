// Shared, byte-accounted LRU for map composites and sprite rasters. Visible
// entries are pinned for the current frame; allocation fails instead of
// evicting something still being drawn. No full-cache sort on a miss.
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

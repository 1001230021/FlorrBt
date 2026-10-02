// Independent observer protocol; never reuse player auth/input framing.
export const DEBUG_MAGIC = 0xd0;
export const DEBUG_VERSION = 1;
export const DEBUG_REQUEST_LIMIT = 32768;
export const DEBUG_RESPONSE_LIMIT = 2 * 1024 * 1024;
export const DEBUG_SELECTION_LIMIT = 128;
const HEADER = 6;
const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });

export function packDebugMessage(message) {
  if (!message || typeof message.op !== "string") throw new Error("Invalid debug request");
  const payload = encoder.encode(JSON.stringify(message));
  if (!payload.length || payload.length > DEBUG_REQUEST_LIMIT) throw new Error("Debug request too large");
  const out = new Uint8Array(HEADER + payload.length);
  out[0] = DEBUG_MAGIC; out[1] = DEBUG_VERSION;
  new DataView(out.buffer).setUint32(2, payload.length, true);
  out.set(payload, HEADER);
  return out;
}

export class DebugFrameDecoder {
  constructor(limit = DEBUG_RESPONSE_LIMIT) {
    this.limit = limit;
    this.buffer = new Uint8Array(65536);
    this.begin = 0; this.end = 0;
  }
  push(bytes) {
    const input = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    const length = this.end - this.begin + input.length;
    if (length > this.limit * 2 + HEADER * 2) throw new Error("Debug receive buffer exceeded");
    if (this.end + input.length > this.buffer.length) {
      if (length <= this.buffer.length) this.buffer.copyWithin(0, this.begin, this.end);
      else {
        const next = new Uint8Array(Math.min(this.limit * 2 + HEADER * 2, Math.max(length, this.buffer.length * 2)));
        next.set(this.buffer.subarray(this.begin, this.end)); this.buffer = next;
      }
      this.end -= this.begin; this.begin = 0;
    }
    this.buffer.set(input, this.end); this.end += input.length;
    const messages = [];
    while (this.begin < this.end) {
      const at = this.begin, available = this.end - at;
      if (this.buffer[at] !== DEBUG_MAGIC || (available > 1 && this.buffer[at + 1] !== DEBUG_VERSION))
        throw new Error("Unsupported debug protocol");
      if (available < HEADER) break;
      const size = new DataView(this.buffer.buffer).getUint32(at + 2, true);
      if (!size || size > this.limit) throw new Error("Invalid debug frame size");
      if (available < HEADER + size) break;
      const value = JSON.parse(decoder.decode(this.buffer.subarray(at + HEADER, at + HEADER + size)));
      if (!value || Array.isArray(value) || typeof value !== "object") throw new Error("Invalid debug message");
      messages.push(value); this.begin += HEADER + size;
    }
    if (this.begin === this.end) this.begin = this.end = 0;
    return messages;
  }
}

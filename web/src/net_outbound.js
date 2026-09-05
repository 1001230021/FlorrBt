import { state } from "./app_context.js";

// Outbound packet transport: the only place that touches the raw socket for
// sending. Callers build packets with the pack* helpers from protocol.js.

export function sendBytes(bytes) {
  if (!bytes || !state.ws || state.ws.readyState !== WebSocket.OPEN)
    return false;
  state.ws.send(bytes);
  return true;
}

// Concatenates packets into one socket write (frame boundaries stay intact:
// each packet already carries its own framing).
export function sendPacketBatch(...packets) {
  const parts = packets.filter((packet) => packet && packet.length > 0);
  if (parts.length === 0) return false;
  if (parts.length === 1) return sendBytes(parts[0]);

  const totalBytes = parts.reduce((sum, packet) => sum + packet.length, 0);
  const out = new Uint8Array(totalBytes);
  let offset = 0;
  for (const packet of parts) {
    out.set(packet, offset);
    offset += packet.length;
  }
  return sendBytes(out);
}

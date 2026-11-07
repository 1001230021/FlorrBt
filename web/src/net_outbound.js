import { state } from "./app_context.js";

// 发送仅通过此模块访问底层连接，调用方使用协议打包函数。

export function sendBytes(bytes) {
  if (!bytes || !state.ws || state.ws.readyState !== WebSocket.OPEN)
    return false;
  try {
    state.ws.send(bytes);
    return true;
  } catch (error) {
    console.warn("WebSocket send failed", error);
    return false;
  }
}

// 多个已封帧的数据拼为一次发送，保留各自帧边界。
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

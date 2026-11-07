import { appendBytes, popFrame, parseServerMessage, packVersion, packAuth, packChores, AuthMode, AuthResultCode, ServerType } from "./protocol.js";

// 菜单重开后可能尚未连接；撤销凭据只建立账户会话，不进入世界。
export function revokeSavedAccount({ endpoint, account, deviceId, token, Socket = globalThis.WebSocket, timeoutMs = 6000 }) {
  if (!endpoint || !account || !deviceId || !token || !Socket) return Promise.resolve(false);
  return new Promise(resolve => {
    let socket, timer, finished = false, requested = false, bytes = new Uint8Array();
    const finish = result => {
      if (finished) return;
      finished = true; clearTimeout(timer); socket?.close(); resolve(result);
    };
    try { socket = new Socket(endpoint); } catch { finish(false); return; }
    socket.binaryType = "arraybuffer";
    timer = setTimeout(() => finish(false), timeoutMs);
    socket.addEventListener("open", () => {
      socket.send(packVersion());
      socket.send(packAuth({ mode: AuthMode.AccountLogin, name: account, recoveryDeviceId: deviceId, recoveryToken: token }));
    });
    socket.addEventListener("message", event => {
      if (!(event.data instanceof ArrayBuffer)) { finish(false); return; }
      bytes = appendBytes(bytes, new Uint8Array(event.data));
      for (;;) {
        const frame = popFrame(bytes); if (!frame) break; bytes = frame.rest;
        const message = parseServerMessage(frame.payload);
        if (message.type !== ServerType.AuthResult) continue;
        if (message.resultCode !== AuthResultCode.Authenticated) { finish(false); return; }
        requested = true; socket.send(packChores(false, false, false, true));
      }
    });
    socket.addEventListener("error", () => finish(false));
    socket.addEventListener("close", () => finish(requested));
  });
}

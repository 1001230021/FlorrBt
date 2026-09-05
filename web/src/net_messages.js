// Server message dispatch registry. Modules register their own handlers
// (game.js registers the legacy ones today; extracted sim/ui modules bring
// their handlers along as they move out), so receiving a new message type
// never grows a central switch again.
//
// Connection-reset hooks fire whenever the socket is torn down (remote close
// or local disconnect) so every module can drop per-connection state without
// game.js having to know about it.

const messageHandlers = new Map(); // ServerType -> (msg) => void
const connectionResetHooks = [];

export function registerMessageHandler(type, handler) {
  if (messageHandlers.has(type))
    throw new Error(`duplicate handler for server message type ${type}`);
  messageHandlers.set(type, handler);
}

export function dispatchServerMessage(msg) {
  if (!msg || msg.type === 0xff) return;
  const handler = messageHandlers.get(msg.type);
  if (handler) handler(msg);
}

export function registerConnectionReset(hook) {
  connectionResetHooks.push(hook);
}

export function notifyConnectionReset() {
  for (const hook of connectionResetHooks) {
    try {
      hook();
    } catch (error) {
      console.error("connection reset hook failed", error);
    }
  }
}

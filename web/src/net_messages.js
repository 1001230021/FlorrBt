// 消息处理器按类型登记，模块自行维护逻辑；断线钩子统一撤销各模块会话状态。

const messageHandlers = new Map();
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

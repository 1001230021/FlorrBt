import assert from 'node:assert/strict';
import { ServerType, AuthMode, packAuth } from '../web/src/protocol.js';

export async function checkAccountLobbyReload(page) {
  await page.addInitScript(({ inventoryType, authType }) => {
    if (sessionStorage.getItem('qa-menu-gate') !== '1') return;
    sessionStorage.removeItem('qa-menu-gate');
    const add = WebSocket.prototype.addEventListener, send = WebSocket.prototype.send;
    const gate = window.__menuGate = {
      phase: 'all', pending: [], received: [], authModes: [],
      deliver(item) {
        if (this.phase === 'all' || (this.phase === 'inventory' && item.type === inventoryType)) this.pending.push(item);
        else item.callback();
      },
      release(phase) { this.phase = phase; for (const item of this.pending.splice(0)) this.deliver(item); },
    };
    WebSocket.prototype.send = function(data) {
      const bytes = data instanceof ArrayBuffer ? new Uint8Array(data) : data;
      if (bytes[0] === authType) gate.authModes.push(bytes[1]);
      return send.call(this, data);
    };
    WebSocket.prototype.addEventListener = function(type, listener, options) {
      if (type !== 'message' || typeof listener !== 'function') return add.call(this, type, listener, options);
      const socket = this; let pending = new Uint8Array();
      return add.call(this, type, event => {
        if (!(event.data instanceof ArrayBuffer)) return listener.call(socket, event);
        const incoming = new Uint8Array(event.data), merged = new Uint8Array(pending.length + incoming.length);
        merged.set(pending); merged.set(incoming, pending.length); pending = merged;
        while (pending.length >= 2) {
          const size = new DataView(pending.buffer, pending.byteOffset).getUint16(0, true) + 2;
          if (pending.length < size) break;
          const frame = pending.slice(0, size); pending = pending.slice(size);
          gate.received.push(frame[2]);
          gate.deliver({ type: frame[2], callback: () => listener.call(socket, new MessageEvent('message', { data: frame.buffer })) });
        }
      }, options);
    };
  }, { inventoryType: ServerType.Inventory, authType: packAuth({ mode: AuthMode.AccountLogin, name: 'fixture', password: 'x' })[0] });
  await page.evaluate(() => sessionStorage.setItem('qa-menu-gate', '1'));
  await page.reload();
  await page.waitForFunction(() => window.__menuGate?.pending.length > 0);
  assert(await page.locator('#startGameBtn').isHidden(), '持久凭据还未被服务端验证，不能显示开始按钮');
  await page.evaluate(() => window.__menuGate.release('inventory'));
  await page.waitForFunction(async () => (await import('/src/app_context.js')).state.accountSession);
  assert(await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js');
    return !state.inventoryLoaded && !state.authenticated && state.entities.size === 0;
  }));
  assert(await page.locator('#startGameBtn').isHidden(), '背包尚未到达时不能入场');
  await page.locator('#startGameBtn').evaluate(button => button.click());
  assert.deepEqual(await page.evaluate(() => window.__menuGate.authModes), [AuthMode.AccountLogin]);
  await page.evaluate(() => window.__menuGate.release('ready'));
  await page.locator('#startGameBtn').waitFor({ state: 'visible' });
  await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js');
    const { packChores } = await import('/src/protocol.js');
    state.ws.send(packChores(false, false, true, false));
  });
  await page.waitForTimeout(250);
  const result = await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js');
    return { connected: state.connected, account: state.accountSession, inventory: state.inventoryLoaded,
      playing: state.authenticated, entities: state.entities.size, world: state.worldGeneration,
      messages: window.__menuGate.received, authModes: window.__menuGate.authModes };
  });
  assert(result.connected && result.account && result.inventory && !result.playing && !result.world && !result.entities);
  assert(!result.messages.includes(ServerType.Welcome) && !result.messages.includes(ServerType.Snapshot));
  return { autoLoginOnly: true, authGate: true, inventoryGate: true, rawRespawnBlocked: true, welcomes: 0, snapshots: 0 };
}

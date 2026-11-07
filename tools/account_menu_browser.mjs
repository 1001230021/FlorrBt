import assert from 'node:assert/strict';
import { setTimeout as delay } from 'node:timers/promises';
import { resolve } from 'node:path';
import { checkAccountLobbyReload } from './account_lobby_browser.mjs';

export async function checkAccountMenu({ page: initialPage, browser, secret, output, emailEnabled = false }) {
  let page = initialPage;
  const url = page.url(), errors = [];
  const menuReady = async () => {
    await page.waitForFunction(() => document.documentElement.dataset.clientReady === 'true');
    await page.locator('#loadingScreen').waitFor({ state: 'hidden' });
    await page.locator('#startGameBtn').waitFor({ state: 'visible' });
    assert(await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      return state.connected && state.accountSession && state.inventoryLoaded && !state.authenticated && state.entities.size === 0;
    }), '菜单等待真实登录和背包同步，尚未生成玩家实体');
  };
  const savedCredential = () => page.evaluate(async () => {
    const { authRecovery } = await import('/src/auth_recovery.js');
    const endpoint = document.querySelector('#wsUrl').value, account = authRecovery.account(endpoint);
    return { endpoint, account, token: authRecovery.token(endpoint, account), deviceId: authRecovery.device(endpoint, account) };
  });
  const assertRevoked = async credential => {
    const result = await page.evaluate(async credential => {
      const p = await import('/src/protocol.js');
      return new Promise(resolve => {
        const ws = new WebSocket(credential.endpoint); ws.binaryType = 'arraybuffer'; let bytes = new Uint8Array();
        const timer = setTimeout(() => { ws.close(); resolve({ timeout: true }); }, 12000);
        ws.onopen = () => { ws.send(p.packVersion()); ws.send(p.packAuth({ mode: p.AuthMode.AccountLogin,
          name: credential.account, recoveryDeviceId: credential.deviceId, recoveryToken: credential.token })); };
        ws.onmessage = event => {
          if (!(event.data instanceof ArrayBuffer)) return;
          bytes = p.appendBytes(bytes, new Uint8Array(event.data));
          for (;;) { const frame = p.popFrame(bytes); if (!frame) break; bytes = frame.rest;
            const reply = p.parseServerMessage(frame.payload);
            if (reply.type === p.ServerType.AuthResult) { clearTimeout(timer); ws.close(); resolve({ code: reply.resultCode, message: reply.message }); return; }
          }
        };
      });
    }, credential);
    assert.equal(result.code, 0, JSON.stringify(result));
    assert.match(result.message, /recovery|credential|expired/i, 'server must reject the revoked token, not merely another online connection');
  };
  try {
    await menuReady();
    assert(await page.locator('#authFields').isHidden());
    assert(await page.locator('#appVersion').isHidden(), 'the remembered-account landing page keeps only the logo and Start');
    assert(await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      return state.accountSession && !state.authenticated && state.entities.size === 0 && !state.worldGeneration;
    }), 'registration establishes an account session without a world or character');
    await page.locator('#accountBtn').click(); await delay(350);
    assert.equal(await page.locator('#accountDisplayName').textContent(), 'ReferenceQA');
    assert(await page.locator('#accountBindings').isHidden());
    assert((await page.locator('#accountPanel').boundingBox()).height < 300);
    await page.screenshot({ path: resolve(output, 'account-overview.png') });
    await page.locator('#accountBindingsBtn').click();
    assert(await page.locator('#accountBindings').isVisible());
    await page.keyboard.press('Escape');
    assert(await page.locator('#accountPanel').isVisible());
    assert(await page.locator('#accountBindings').isHidden());
    await page.locator('#accountBindingsBtn').click();
    const providers = await page.locator('#accountBindingList button').evaluateAll(buttons => buttons.map(button => ({ provider: button.dataset.provider, disabled: button.disabled })));
    assert.deepEqual(providers.map(item => item.provider), ['discord', 'qq', 'email']);
    assert(providers.filter(item => item.provider !== 'email').every(item => item.disabled), 'unconfigured platforms cannot initiate authorization');
    assert.equal(providers.find(item => item.provider === 'email').disabled, !emailEnabled);
    if (emailEnabled) {
      await page.locator('#accountBindingList button[data-provider="email"]').click();
      await page.locator('#emailBindingPanel').waitFor({ state: 'visible' });
      assert(await page.evaluate(async () => { const { state } = await import('/src/app_context.js'); return state.accountSession && !state.authenticated && state.entities.size === 0; }));
      await page.locator('#cancelEmailBindingBtn').click();
      await page.locator('#accountPanel').waitFor({ state: 'visible' });
      assert(await page.locator('#accountBindings').isVisible(), '取消邮箱绑定应回到二级页面');
      await delay(350);
      assert(await page.locator('#startGameBtn').isVisible());
    }
    const panelStyle = await page.locator('#accountPanel').evaluate(element => {
      const css = getComputedStyle(element); return { background: css.backgroundColor, inert: element.inert, outline: css.outlineStyle };
    });
    assert.equal(panelStyle.background, 'rgb(152, 103, 178)'); assert(!panelStyle.inert);
    assert((await page.locator('#accountPanel').boundingBox()).height < 700, 'account bindings fit without stretching the popup to the screen bottom');
    await page.screenshot({ path: resolve(output, 'account-panel.png') });
    await page.locator('#accountBackBtn').click();
    assert(await page.locator('#logoutBtn').isVisible());
    await page.locator('#accountCloseBtn').click(); await delay(200);
    await page.screenshot({ path: resolve(output, 'account-start.png') });
    const lobby = await checkAccountLobbyReload(page); await menuReady();
    assert(await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      return state.ws?.readyState === WebSocket.OPEN && state.accountSession && !state.authenticated && !state.worldGeneration;
    }), '刷新自动登录，但继续等待显式入场');
    await page.evaluate(async () => { window.__oldMenuSocket = (await import('/src/app_context.js')).state.ws; window.__oldMenuSocket.close(); });
    await page.waitForFunction(async () => {
      const { state } = await import('/src/app_context.js');
      return state.ws && state.ws !== window.__oldMenuSocket && state.accountSession && state.inventoryLoaded;
    });
    await menuReady();
    const persisted = await page.context().storageState();
    assert(!JSON.stringify(persisted).includes(secret), 'password is never persisted');
    await page.context().close();
    const context = await browser.newContext({ storageState: persisted, viewport: { width: 1280, height: 720 } });
    page = await context.newPage(); page.on('pageerror', error => errors.push(String(error)));
    await page.goto(url); await menuReady();
    assert(await page.locator('#authFields').isHidden(), 'fresh browser session shows only logo and Start');
    await page.locator('#startGameBtn').click();
    await page.waitForFunction(async () => { const { state } = await import('/src/app_context.js'); return state.authenticated && state.ownerStateLoaded && state.inventoryLoaded; });
    await page.locator('#loadingScreen').waitFor({ state: 'hidden', timeout: 60000 });
    const credential = await savedCredential();
    await page.locator('#accountBtn').click(); await delay(350); await page.locator('#logoutBtn').click();
    await page.waitForFunction(async () => !(await import('/src/app_context.js')).state.connected);
    assert(await page.locator('#authFields').isVisible());
    await assertRevoked(credential);
    await page.locator('#password').fill('wrong-password'); await page.locator('#connectBtn').click();
    await page.waitForFunction(() => document.querySelector('#authStatus').textContent.includes('Wrong password'));
    assert(await page.locator('#authFields').isVisible());
    await page.locator('#password').fill(secret); await page.locator('#connectBtn').click();
    await page.waitForFunction(async () => (await import('/src/app_context.js')).state.accountSession);
    await menuReady();
    const menuCredential = await savedCredential();
    await page.reload(); await menuReady();
    await page.setViewportSize({ width: 390, height: 844 });
    await page.locator('#accountBtn').click(); await delay(350);
    const bounds = await page.locator('#accountPanel').boundingBox();
    assert(bounds.x >= 0 && bounds.x + bounds.width <= 391 && bounds.y > 0 && bounds.height < 700 && bounds.y + bounds.height <= 845);
    await page.screenshot({ path: resolve(output, 'account-mobile.png') });
    await page.locator('#logoutBtn').click();
    await delay(2000);
    await assertRevoked(menuCredential);
    await page.reload();
    await page.waitForFunction(() => document.documentElement.dataset.clientReady === 'true');
    assert(await page.locator('#authFields').isVisible());
    assert(await page.locator('#startGameBtn').isHidden());
    await page.locator('#accountBtn').click(); await delay(350);
    assert(await page.locator('#accountPanel #authFields').isVisible());
    await page.screenshot({ path: resolve(output, 'account-login-form.png') });
    assert.deepEqual(errors, []);
    return { accountOnlyRegistration: true, purplePanel: panelStyle.background, refreshPersists: true,
      browserRestartPersists: true, passwordStored: false, explicitStart: true, liveLogoutRevokes: true,
      lobby, menuAutoLogin: true, inventoryBeforeSpawn: true, menuReconnect: true,
      disconnectedMenuLogoutRevokes: true, wrongPasswordRetry: true, emailBeforePlay: emailEnabled, mobile: bounds, errors };
  } catch (error) {
    await page.screenshot({ path: resolve(output, 'account-failed.png') }).catch(() => {});
    console.error(await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      return { connected: state.connected, playing: state.authenticated, mapLoading: !!state.mapLoadPromise,
        mapPath: state.mapLoadPath, mapToken: state.mapLoadToken, entityCount: state.entities.size };
    }).catch(() => 'page closed'));
    console.error(await page.locator('#authStatus').textContent().catch(() => 'page closed'));
    throw error;
  }
}

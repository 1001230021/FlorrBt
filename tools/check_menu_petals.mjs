import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createServer } from 'node:net';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { chromium } from '../build/render-qa/node_modules/playwright-core/index.mjs';

// 仅验证界面；独立浏览器存储和模型数据，不连接游戏服务器或账户库。
const root = fileURLToPath(new URL('..', import.meta.url));
const output = resolve(root, 'artifacts/menu-petals-toggle');
await mkdir(output, { recursive: true });
const probe = createServer(); await new Promise(resolve => probe.listen(0, '127.0.0.1', resolve));
const port = probe.address().port; await new Promise(resolve => probe.close(resolve));
const bridge = spawn(process.execPath, [resolve(root, 'tools/web_client_server.js')], {
  cwd: root, windowsHide: true, env: { ...process.env, FLORRBT_ROOT: root, WEB_HOST: '127.0.0.1', WEB_PORT: String(port) },
  stdio: ['ignore', 'pipe', 'pipe'],
});
let browser;
try {
  await new Promise((resolve, reject) => { bridge.stdout.once('data', resolve); bridge.once('error', reject); });
  browser = await chromium.launch({ headless: true, executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  const context = await browser.newContext({ viewport: { width: 1280, height: 720 }, reducedMotion: 'reduce' });
  await context.addInitScript(() => {
    if (!localStorage.getItem('florrbt.web.settings'))
      localStorage.setItem('florrbt.web.settings', JSON.stringify({ version: 15, renderScaleMode: 'native' }));
  });
  const page = await context.newPage(), errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  const ready = async () => {
    await page.waitForFunction(() => document.documentElement.dataset.clientReady === 'true');
    await page.locator('#loadingScreen').waitFor({ state: 'hidden' });
    await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      const { dispatchServerMessage } = await import('/src/net_messages.js');
      const { ServerType } = await import('/src/protocol.js');
      const { PETAL_IDS, RARITY_IDS } = await import('/src/generated_ids.js');
      const { loadVisualImage, whenVisualReady } = await import('/src/visual_assets.js');
      state.accountSession = true; state.connected = true; state.accountName = 'MenuMotionQA';
      dispatchServerMessage({ type: ServerType.Inventory, inventoryScope: state.accountName, actorScope: '',
        inventory: [{ petalType: PETAL_IDS.leaf, rarity: RARITY_IDS.common, count: 100 }] });
      await Promise.all([0, 1, 2, 3].map(i => whenVisualReady(loadVisualImage(`/data/tiles/grass_c_${i}.svg`))));
    });
  };
  const pixels = async () => createHash('sha256').update(await page.locator('#game').screenshot()).digest('hex');
  const openGraphics = async () => {
    await page.locator('#topSettingsBtn').click();
    await page.locator('.settings-tabs [data-category="graphics"]').click();
  };
  await page.goto(`http://127.0.0.1:${port}/index.html`); await ready();
  assert(await page.evaluate(() => matchMedia('(prefers-reduced-motion: reduce)').matches));
  await page.waitForTimeout(2500);
  const animatedBefore = await pixels(); await page.waitForTimeout(900); const animatedAfter = await pixels();
  assert.notEqual(animatedBefore, animatedAfter, '系统减少动态效果时，默认菜单花瓣仍应移动');
  await openGraphics();
  assert.equal(await page.locator('#setting-menu_petals').getAttribute('aria-pressed'), 'true');
  await page.screenshot({ path: resolve(output, 'setting.png') });
  await page.locator('#setting-menu_petals').click();
  assert.equal(await page.locator('#setting-menu_petals').getAttribute('aria-pressed'), 'false');
  await page.locator('#settingsPanel .panel-close').click(); await page.waitForTimeout(250);
  const offBefore = await pixels(); await page.waitForTimeout(900); const offAfter = await pixels();
  assert.equal(offBefore, offAfter, '关闭后只剩草地');
  await page.screenshot({ path: resolve(output, 'off.png') });
  await page.reload(); await ready(); await page.waitForTimeout(250);
  assert.equal(await page.evaluate(async () => (await import('/src/app_context.js')).state.clientConfig.menuPetals), 0);
  assert.equal(await pixels(), offAfter, '刷新后仍不显示花瓣');
  await openGraphics(); await page.locator('#setting-menu_petals').click();
  await page.locator('#settingsPanel .panel-close').click(); await page.waitForTimeout(2000);
  const resumedBefore = await pixels(); await page.waitForTimeout(900); const resumedAfter = await pixels();
  assert.notEqual(resumedBefore, resumedAfter, '重新开启后持续运动');
  assert.notEqual(resumedAfter, offAfter);
  await page.screenshot({ path: resolve(output, 'on.png') });
  assert.deepEqual(errors, []);
  const result = { systemReducedMotion: true, defaultAnimates: true, offHidesAll: true,
    offPersistsOnReload: true, onResumes: true, errors };
  await writeFile(resolve(output, 'result.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result));
} finally { await browser?.close(); bridge.kill(); }

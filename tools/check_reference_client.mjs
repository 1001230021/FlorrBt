// 本地验收：独立数据目录、随机账号与端口，只启动本次创建的进程。
// node tools/check_reference_client.mjs --live
import assert from "node:assert/strict";
import { cp, mkdir, writeFile, readFile, access } from "node:fs/promises";
import { createWriteStream } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";
import { createServer } from "node:net";
import { randomBytes } from "node:crypto";
import { setTimeout as delay } from "node:timers/promises";
const sourceRoot = fileURLToPath(new URL("..", import.meta.url));
const root = process.env.FLORRBT_QA_ROOT ? resolve(process.env.FLORRBT_QA_ROOT) : sourceRoot;
const output = resolve(sourceRoot, "artifacts/video-reference");
if (!process.argv.includes("--live")) throw new Error("Use --live to run the isolated browser/server check.");
const { chromium } = await import("../build/render-qa/node_modules/playwright-core/index.mjs");
const temp = resolve(output, `session-${Date.now()}`);
await mkdir(resolve(temp, "data"), { recursive: true });
for (const folder of ["maps", "tiles", "decor", "spawn_rules"])
  await cp(resolve(root, "data", folder), resolve(temp, "data", folder), { recursive: true });
const freePort = () => new Promise((resolvePort, reject) => {
  const server = createServer(); server.on("error", reject);
  server.listen(0, "127.0.0.1", () => { const port = server.address().port; server.close(() => resolvePort(port)); });
});
const gamePort = await freePort(), webPort = await freePort();
const secret = randomBytes(20).toString("hex");
await writeFile(resolve(temp, "data/server.cfg"), [
  `set port ${gamePort}`, "set account_backend json", "set email_enabled 0", "set gui_console 0",
  `set rcon_password ${secret}`, "set garden_world_instances 1", "set ant_hel_world_instances 1",
  ...(process.argv.includes('--email-ui') ? ['set email_enabled 1', 'set email_username sender@example.test',
    'set email_password offline-mail-fixture', 'set email_smtp_host 127.0.0.1'] : []),
  "set player_spawn_invincible_duration 3", "set weather_diurnal_start_time 0.5",
  "set mob_player_flower_initial_petal_slots 10",
  "world_controller 0 deathmatch",
].join("\n"));
const env = { ...process.env };
for (const key of Object.keys(env)) if (key.startsWith("FLORRBT_")) delete env[key];
if (!process.env.FLORRBT_QA_SERVER)
  env.PATH = `D:\\库\\SFML-3.1.0\\bin;${resolve(root, "build/architecture-review/_deps/curl-build/lib")};${env.PATH || env.Path}`;
const server = spawn(process.env.FLORRBT_QA_SERVER || resolve(root, "build/architecture-review/FlorrBt_Server.exe"), [], {
  cwd: temp, env, windowsHide: true, stdio: ["ignore", "pipe", "pipe"],
});
const serverLog = createWriteStream(resolve(temp, "server-output.log"));
server.stdout.pipe(serverLog); server.stderr.pipe(serverLog);
let serverText = ""; server.stdout.on("data", data => { serverText = (serverText + data).slice(-20000); });
const bridge = spawn(process.execPath, [resolve(root, "tools/web_client_server.js")], {
  cwd: root, windowsHide: true, env: { ...env, WEB_PORT: String(webPort), WEB_HOST: "127.0.0.1", GAME_PORT: String(gamePort), GAME_HOST: "127.0.0.1" },
  stdio: ["ignore", "pipe", "pipe"],
});
bridge.stdout.pipe(createWriteStream(resolve(temp, "bridge.log"))); bridge.stderr.pipe(process.stderr);
let browser, page;
const errors = [];
const metrics = {};
try {
  for (let n = 0; n < 120 && !serverText.includes("Listening on"); n++) {
    if (server.exitCode !== null) throw new Error(`Isolated server exited ${server.exitCode}: ${serverText}`);
    await delay(500);
  }
  assert.match(serverText, /Listening on/);
  const executablePath = process.env.FLORRBT_BROWSER_EXECUTABLE || "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";
  await access(executablePath);
  browser = await chromium.launch({ headless: true, executablePath });
  page = await browser.newPage({ viewport: { width: 1910, height: 884 }, deviceScaleFactor: 1 });
  page.on("pageerror", error => { errors.push(String(error)); console.error(error); });
  page.on("console", message => { if (["error", "warning"].includes(message.type())) console.log(`BROWSER ${message.type()}: ${message.text()}`); });
  page.on("response", response => { if (response.status() >= 400) console.log(`HTTP ${response.status()} ${response.url()}`); });
  if (process.argv.includes('--rapid-swaps') || process.argv.includes('--death-build')) await page.addInitScript(() => {
    const add = WebSocket.prototype.addEventListener;
    window.__qaDelivery = { socket: null, pending: [],
      flush() { for (const deliver of this.pending.splice(0)) deliver(); },
      stop() { this.socket = null; this.flush(); } };
    WebSocket.prototype.addEventListener = function(type, listener, options) {
      if (type !== 'message' || typeof listener !== 'function' || options?.qaBypassHold) return add.call(this, type, listener, options);
      return add.call(this, type, event => {
        const deliver = () => listener.call(this, event);
        if (window.__qaDelivery.socket === this) window.__qaDelivery.pending.push(deliver);
        else deliver();
      }, options);
    };
  });
  const originalEntry = process.argv.includes("--original");
  await page.goto(`http://127.0.0.1:${webPort}/${originalEntry ? 'index.html' : 'reference.html'}`);
  await page.waitForFunction(() => document.documentElement.dataset.clientReady === "true");
  await page.locator("#account").fill("ReferenceQA");
  await page.locator("#password").fill(secret);
  await page.locator("#registerMode").check();
  await page.locator("#connectBtn").click();
  await page.waitForFunction(async () => (await import('/src/app_context.js')).state.accountSession);
  if (process.argv.includes('--accounts')) {
    const { checkAccountMenu } = await import('./account_menu_browser.mjs');
    const result = await checkAccountMenu({ page, browser, secret, output, emailEnabled: process.argv.includes('--email-ui') });
    await writeFile(resolve(output, 'accounts.json'), JSON.stringify(result, null, 2));
    console.log(JSON.stringify(result, null, 2));
    // finally 负责停止本次隔离服务，账户验收无需运行战斗压力流程。
  } else {
  await page.locator('#startGameBtn').click();
  let connected = false;
  for (let n = 0; n < 120 && !connected; n++) {
    connected = await page.evaluate(async () => {
      const { state } = await import("/src/app_context.js");
      return !!(state.authenticated && state.ws?.readyState === 1 && state.ownerStateLoaded && state.inventoryLoaded && state.entities.get(state.ownerEntityId) && !state.mapLoadPromise);
    });
    if (!connected) await delay(500);
  }
  assert.equal(connected, true, "real authenticated session, scene and inventory ready");
  console.log("Connected with the real server.");
  metrics.textRendering = await (await import('./text_render_browser.mjs')).checkTextRendering(page);
  metrics.entry = originalEntry ? "original" : "reference";
  metrics.panelMotion = await page.evaluate(async () => {
    const { ui } = await import('/src/app_context.js');
    const root = document.getElementById('backpackPanel');
    ui.backpackUiPanel.setOpen(true);
    const open = root.getAnimations()[0]; open.pause(); open.currentTime = 0;
    const start = root.getBoundingClientRect();
    open.currentTime = 70; const moving = root.getBoundingClientRect();
    const alpha = Number(getComputedStyle(root).opacity);
    ui.backpackUiPanel.setOpen(false);
    const close = root.getAnimations()[0]; close.pause(); close.currentTime = 0;
    const reversed = root.getBoundingClientRect();
    close.currentTime = 40;
    const closing = root.getBoundingClientRect();
    ui.backpackUiPanel.setOpen(true);
    const reopened = root.getAnimations()[0]; reopened.pause(); reopened.currentTime = 0;
    const again = root.getBoundingClientRect();
    reopened.finish(); await Promise.resolve();
    ui.backpackUiPanel.setOpen(false); root.getAnimations()[0]?.finish(); await Promise.resolve();
    return { startY:start.y, movingY:moving.y, alpha, widthChange:Math.abs(start.width-moving.width),
      closeJump:Math.abs(reversed.y-moving.y), reopenJump:Math.abs(again.y-closing.y) };
  });
  assert(metrics.panelMotion.startY > metrics.panelMotion.movingY);
  assert(metrics.panelMotion.alpha > 0 && metrics.panelMotion.alpha < 1);
  assert(metrics.panelMotion.widthChange < .1 && metrics.panelMotion.closeJump < 1 && metrics.panelMotion.reopenJump < 1);
  await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js');
    const { appendBytes, popFrame, parseServerMessage } = await import('/src/protocol.js');
    window.__qaWire = []; const send = state.ws.send.bind(state.ws); let receive = new Uint8Array();
    state.ws.send = bytes => { if (bytes[0] === 250) window.__qaWire.push({ sent: bytes[0], op: bytes[1], length: bytes.length,
      requestId: new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getBigUint64(4,true).toString(16).padStart(16,'0') }); send(bytes); };
    state.ws.addEventListener('message', event => {
      if (!(event.data instanceof ArrayBuffer)) return;
      receive = appendBytes(receive, new Uint8Array(event.data));
      for (;;) { const frame = popFrame(receive); if (!frame) break; receive = frame.rest;
        if (frame.payload[0] === 30) window.__qaWire.push({ result: parseServerMessage(frame.payload) });
      }
    }, { qaBypassHold: true });
  });
  const command = async text => {
    await page.evaluate(async text => {
      const { state } = await import("/src/app_context.js");
      const { packChat, ChatFlag } = await import("/src/protocol.js");
      const { sendBytes } = await import("/src/net_outbound.js");
      if (!sendBytes(packChat(ChatFlag.Local, text))) throw new Error("Test command send failed");
    }, text);
    await delay(100);
  };
  const until = async (fn, arg, message, count = 80) => {
    for (let i = 0; i < count; i++) { if (await page.evaluate(fn, arg)) return; await delay(100); }
    throw new Error(message);
  };
  await command(`/rcon ${secret}`);
  await command("/rcon level_up ReferenceQA 119");
  await command("/rcon tele ReferenceQA 0 35072,26368");
  if (process.argv.includes('--features')) {
    const { checkFeaturePolicies } = await import('./feature_policy_browser.mjs');
    const result = await checkFeaturePolicies({ page, command, until });
    await writeFile(resolve(output, 'features.json'), JSON.stringify(result, null, 2)); console.log(JSON.stringify(result, null, 2));
    await command('/rcon quit');
  } else if (process.argv.includes('--control') || process.argv.includes('--control-probe')) {
    const { checkControl } = await import('./control_browser.mjs');
    const result = await checkControl({ page, command, until, getServerText: () => serverText,
      probe: process.argv.includes('--control-probe'), larva: process.argv.includes('--larva') });
    await writeFile(resolve(output, 'control.json'), JSON.stringify(result, null, 2));
    console.log(JSON.stringify(result, null, 2));
    await command('/rcon quit');
  } else {
  for (const name of ["basic", "rose", "leaf", "light", "stinger", "rock", "antennae", "bubble", "pollen", "dahlia"])
    for (const rarity of ["common", "rare", "legendary", "ultra"])
      await command(`/rcon add ReferenceQA ${name} ${rarity} 30`);
  await page.evaluate(async () => {
    const { packStateRequest } = await import("/src/protocol.js");
    (await import("/src/net_outbound.js")).sendBytes(packStateRequest());
  });
  await page.evaluate(async () => {
    const { state } = await import("/src/app_context.js"); state.movementMode = "keyboard";
  });
  const owner = () => page.evaluate(async () => {
    const { state } = await import("/src/app_context.js"); return state.entities.get(state.ownerEntityId)?.snapshot.pos;
  });
  const before = await owner();
  await page.locator("#game").focus();
  await page.keyboard.down("d"); await delay(750); await page.keyboard.up("d"); await delay(200);
  const after = await owner();
  metrics.moveDistance = Math.hypot(after.x - before.x, after.y - before.y);
  assert(metrics.moveDistance > 5, "real movement input reaches the server");
  await delay(1300);
  await page.locator("#quickBackpackBtn").click(); await delay(350);
  for (const [slot, name] of ["Rose", "Dahlia", "Pollen", "Rose", "Basic", "Dahlia", "Light", "Rock", "Leaf", "Rose"].entries()) {
    const card = page.locator('.inventory-group').filter({ has: page.locator('.inventory-group-title', { hasText: "Ultra" }) })
      .locator('.inventory-item').filter({ has: page.locator('.petal-card-name', { hasText: new RegExp(`^${name}$`) }) }).first();
    await card.scrollIntoViewIfNeeded();
    const source = await card.boundingBox(), target = await page.locator('#primarySlots .slot').nth(slot).boundingBox();
    await page.mouse.move(source.x + source.width / 2, source.y + source.height / 2);
    await page.mouse.down(); await page.mouse.move(target.x + target.width / 2, target.y + target.height / 2, { steps: 12 }); await page.mouse.up();
    await until(async ({ slot, name }) => {
      const { state } = await import("/src/app_context.js");
      const { PETAL_IDS } = await import("/src/generated_ids.js");
      return state.ownerSlots[slot]?.petalType === PETAL_IDS[name.toLowerCase()] && state.ownerSlots[slot]?.rarity === 7;
    }, { slot, name }, `Inventory drag ${name} receives authoritative loadout`);
    await delay(350);
  }
  metrics.equip = true;
  await page.locator("#backpackCloseBtn").click(); await delay(180);
  if (process.argv.includes('--rapid-swaps')) {
    const {checkRapidLoadout}=await import('./rapid_loadout_browser.mjs');
    metrics.rapidSwaps=await checkRapidLoadout({page,until});
  }
  await page.locator("#quickCraftBtn").click(); await delay(400);
  const craftBefore = await page.evaluate(async () => {
    const { state } = await import("/src/app_context.js"); return state.inventory.find(i => i.petalType === 4 && i.rarity === 1)?.count || 0;
  });
  await page.locator('.craft-matrix-cell[data-craft-key="4:1"]').click();
  metrics.craftButtonColor = await page.locator('#craftOnceBtn').evaluate(button => getComputedStyle(button).backgroundColor);
  assert.equal(metrics.craftButtonColor, "rgb(255, 230, 93)", "Common input produces an Unusual-colored Craft button");
  await page.locator("#craftOnceBtn").click();
  await until(async count => {
    const { state } = await import("/src/app_context.js");
    return state.craftPhase !== "spinning" && (state.inventory.find(i => i.petalType === 4 && i.rarity === 1)?.count || 0) < count;
  }, craftBefore, "Craft consumes materials and receives a terminal result");
  metrics.craftTransaction = true;
  await page.screenshot({ path: resolve(output, "reference-craft-result.png") });
  await page.locator("#craftCloseBtn").click(); await delay(180);
  await page.locator("#quickTalentBtn").click(); await delay(300);
  const talentBefore = await page.evaluate(async () => (await import("/src/app_context.js")).state.talentPoints);
  const talent = page.locator('.talent-node[data-talent-key="1:1:0"]');
  await talent.focus(); await page.keyboard.down("Enter"); await delay(650); await page.keyboard.up("Enter");
  await until(async value => (await import("/src/app_context.js")).state.talentPoints < value, talentBefore, "Talent purchase reaches server");
  metrics.talentPurchase = true;
  await page.locator("#talentCloseBtn").click(); await delay(180);
  await page.keyboard.press("Enter");
  await page.locator("#chatInput").fill("Reference gameplay check"); await page.keyboard.press("Enter");
  await page.getByText("Reference gameplay check", { exact: false }).first().waitFor();
  metrics.chat = true;
  await page.keyboard.press("Escape");
  await command("/rcon world_controller 0 open");
  const combatAt = await owner();
  const codexBefore = await page.evaluate(async () => [...(await import("/src/app_context.js")).state.codex.values()].reduce((a, b) => a + b, 0));
  // 瓢虫平时随机游荡；放在花瓣可触及的位置，避免等待随机路线进入攻击范围。
  await command(`/rcon spawn 0 ladybug common ${Math.round(combatAt.x + 40)},${Math.round(combatAt.y)}`);
  await page.locator("#game").focus(); await page.keyboard.down("Space");
  await until(async count => [...(await import("/src/app_context.js")).state.codex.values()].reduce((a, b) => a + b, 0) > count, codexBefore, "Combat produces authoritative kill progression");
  await page.keyboard.up("Space");
  metrics.combat = true;
  await page.screenshot({ path: resolve(output, "reference-play.png") });
  for (const [button, panel, name] of [["quickBackpackBtn", "backpackPanel", "inventory"], ["quickCraftBtn", "craftPanel", "craft"],
    ["quickTalentBtn", "talentPanel", "talents"], ["codexBtn", "codexPanel", "gallery"], ["topSettingsBtn", "settingsPanel", "settings"]]) {
    await page.locator(`#${button}`).click(); await delay(450);
    assert(await page.locator(`#${panel}`).isVisible(), name);
    metrics[name] = await page.locator(`#${panel}`).boundingBox();
    await page.screenshot({ path: resolve(output, `reference-${name}.png`) });
    await page.locator(`#${panel} .panel-close`).click(); await delay(200);
  }
  metrics.serverPosition = after;
  metrics.entities = await page.evaluate(async () => (await import("/src/app_context.js")).state.entities.size);
  let profiler;
  if(process.argv.includes('--profile')){
    profiler=await page.context().newCDPSession(page);await profiler.send('Profiler.enable');await profiler.send('Profiler.start');
    await profiler.send('Performance.enable');
  }
  metrics.performance = await page.evaluate(async () => {
    const { state } = await import("/src/app_context.js");
    const times = []; let previous = performance.now();
    await new Promise(resolve => { const sample = now => { times.push(now - previous); previous = now; if (times.length < 180) requestAnimationFrame(sample); else resolve(); }; requestAnimationFrame(sample); });
    times.shift(); times.sort((a, b) => a - b);
    return { rafP50: times[Math.floor(times.length * .5)], rafP95: times[Math.floor(times.length * .95)],
      heapMB: performance.memory ? performance.memory.usedJSHeapSize / 1048576 : null,
      dpr: state.dpr, resources: performance.getEntriesByType("resource").length,
      stalls: window.__freezeEvents || [] };
  });
  if(profiler){
    const {profile}=await profiler.send('Profiler.stop');
    await writeFile(resolve(root,'artifacts/client-performance/cpu-profile.json'),JSON.stringify(profile));
    metrics.browserTasks=await profiler.send('Performance.getMetrics');
    await profiler.detach();
  }
  await page.setViewportSize({ width: 1280, height: 720 }); await delay(600);
  await page.screenshot({ path: resolve(output, "reference-1280.png") });
  const slotRect = await page.locator("#primarySlots").boundingBox();
  assert(slotRect.x >= 0 && slotRect.x + slotRect.width <= 1281, "all equipped slots remain in the viewport");
  metrics.responsive = true;
  await page.setViewportSize({ width: 390, height: 844 }); await delay(600);
  await page.locator("#quickBackpackBtn").click(); await delay(350);
  await page.screenshot({ path: resolve(output, "reference-mobile.png") });
  const mobilePanel = await page.locator("#backpackPanel").boundingBox();
  assert(mobilePanel.x >= -1 && mobilePanel.x + mobilePanel.width <= 391, "mobile inventory stays reachable");
  await page.locator("#backpackCloseBtn").click(); await delay(180);
  await page.setViewportSize({ width: 1910, height: 884 }); await delay(400);
  const deathCheck = process.argv.includes('--death-build') ? await import('./death_loadout_browser.mjs') : null;
  if (deathCheck) await deathCheck.beginDeathProbe(page);
  await command("/rcon kill ReferenceQA");
  await until(async () => {
    const { state } = await import('/src/app_context.js');
    return !!(state.entities.get(state.ownerEntityId)?.snapshot.flags & 4);
  }, null, "server confirms player death");
  await page.screenshot({ path: resolve(output, "reference-death.png") });
  if (deathCheck) metrics.deathBuild = await deathCheck.checkDeathLoadout({ page, until });
  else await page.locator("#reviveBtn").click();
  await until(async () => {
    const { state } = await import('/src/app_context.js');
    const owner = state.entities.get(state.ownerEntityId);
    return !!owner && !(owner.snapshot.flags & 4) && !state.wasOwnerDead;
  }, null, "respawn finishes");
  metrics.respawn = true;
  const oldSocket = await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js'); window.__qaOldSocket = state.ws; state.ws.close(); return true;
  });
  await until(async () => {
    const { state } = await import('/src/app_context.js');
    return state.ws !== window.__qaOldSocket && state.authenticated && state.inventoryLoaded && state.ownerStateLoaded;
  }, null, "recovery token reconnects without a stored password", 200);
  metrics.reconnect = oldSocket;
  if (deathCheck) await until(async expected => {
    const { state } = await import('/src/app_context.js');
    const same = (a, b) => a?.length === b.length && a.every((s, i) => s.petalType === b[i].petalType && s.rarity === b[i].rarity);
    return same(state.ownerSlots, expected.primary) && same(state.secondarySlots, expected.secondary);
  }, metrics.deathBuild.expected, 'build configured while dead survives reconnect');
  metrics.errors = errors;
  assert.deepEqual(errors, []);
  await writeFile(resolve(output, "playtest.json"), JSON.stringify(metrics, null, 2));
  console.log(JSON.stringify(metrics, null, 2));
  await command(`/rcon ${secret}`);
  await command("/rcon quit");
  }
  }
} catch (error) {
  if (page) {
    await page.screenshot({ path: resolve(output, "failed.png") }).catch(() => {});
    console.log(await page.locator("#authStatus").textContent().catch(() => "No auth status"));
    console.log(await page.evaluate(async () => {
      const { state } = await import('/src/app_context.js');
      return { slots: state.ownerSlots, secondary: state.secondarySlots, selected: state.selectedSlot, locked: state.ownerLoadoutLocked,
        craftPhase: state.craftPhase, talentPoints: state.talentPoints, wire: window.__qaWire, messages: document.querySelector('#chatLog')?.textContent };
    }).catch(() => ({})));
  }
  throw error;
} finally {
  if (browser) await browser.close();
  if (server.exitCode === null) { await delay(500); if (server.exitCode === null) server.kill(); }
  bridge.kill(); serverLog.end();
}

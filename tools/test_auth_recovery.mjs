import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { createAuthRecoveryStore } from "../web/src/auth_recovery.js";
import { packAuth, parseServerMessage, ServerType, AuthResultCode, AuthMode } from "../web/src/protocol.js";

const records = new Map();
const storage = { getItem: key => records.get(key), setItem: (key, value) => records.set(key, value),
  removeItem: key => records.delete(key) };
let now = 1000;
const create = () => createAuthRecoveryStore({ storage, crypto: webcrypto, now: () => now * 1000 });
const store = create(), endpoint = "wss://one.test/ws", account = "player";
const device = store.device(endpoint, account), token = "ab".repeat(32);
assert.match(device, /^[a-f0-9]{32}$/);
assert.equal(store.token(endpoint, account), "");
assert.equal(store.accept(endpoint, account, device, token, 1100), true);
assert.equal(store.token(endpoint, account), token);
assert.equal(create().token(endpoint, account), token, "a new browser context can read the persisted credential");
assert.equal(create().account(endpoint), account, "only a verified login selects the remembered account");
assert.equal(store.account("wss://two.test/ws"), "");
assert.equal(store.token("wss://two.test/ws", account), "");
assert.equal(store.token(endpoint, "someone-else"), "");
assert.notEqual(store.device(endpoint, "someone-else"), device);
assert.equal(store.accept(endpoint, account, "00".repeat(16), token, 1100), false, "another pending device cannot overwrite a credential");
now = 1100;
assert.equal(store.token(endpoint, account), "", "absolute expiry, including boundary");
assert.equal(store.accept(endpoint, account, device, token, 1100), false);
assert.equal(store.accept(endpoint, account, device, "invalid", 1200), false);
assert.equal(store.accept(endpoint, account, device, token, NaN), false);
const anotherTab = create(); anotherTab.device(endpoint, account);
store.forget(endpoint, account);
assert.equal(create().token(endpoint, account), "", "logout clears persistent browser state");
assert.equal(anotherTab.token(endpoint, account), "", "an existing tab cannot retain a deleted credential in memory");
assert.equal(create().account(endpoint), "");
const broken = createAuthRecoveryStore({ crypto: webcrypto, now: () => 0,
  storage: { getItem() { throw new Error(); }, setItem() { throw new Error(); }, removeItem() { throw new Error(); } } });
const temporaryDevice = broken.device(endpoint, account);
assert.ok(broken.accept(endpoint, account, temporaryDevice, token, 1200));
assert.equal(broken.token(endpoint, account), token);
broken.forget(endpoint, account); assert.equal(broken.token(endpoint, account), "");
const unavailable = createAuthRecoveryStore({ crypto: {}, now: () => 0 });
assert.equal(unavailable.device(endpoint, account), "", "no insecure random fallback");
const packet = packAuth({ name: "fixture", recoveryDeviceId: device, recoveryToken: token });
assert.equal(packet.length, 8 + 7 + 32 + 64);
assert.equal(packet[6], 32); assert.equal(packet[7], 64);
assert.equal(Buffer.from(packet.slice(8 + 7 + 32)).toString(), token);
assert.equal(packAuth({ name: "fixture", recoveryDeviceId: device, recoveryToken: token.slice(1) }), null);
const response = Buffer.alloc(15 + 32 + 64);
response[0] = ServerType.AuthResult; response[1] = AuthResultCode.Authenticated;
response[5] = 32; response[6] = 64; response.writeBigUInt64LE(2000000000n, 7);
response.write(device, 15); response.write(token, 47);
const reply = parseServerMessage(response);
assert.equal(reply.recoveryDeviceId, device); assert.equal(reply.recoveryToken, token);
assert.equal(reply.recoveryExpiresAt, 2000000000);
assert.equal(parseServerMessage(response.subarray(0, response.length - 1)).type, 255);

// Exercise the actual game login functions: cleared passwords must use the
// scoped credential, while expired credentials return to a visible login UI.
const source = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
function method(name) {
  const start = source.indexOf(`function ${name}(`), end = source.indexOf("\nfunction ", start + 1);
  assert.ok(start >= 0); return source.slice(start, end);
}
now = 1000;
const loginStore = create(), loginDevice = loginStore.device(endpoint, account);
assert.ok(loginStore.accept(endpoint, account, loginDevice, token, 1100));
let password = "", visible = false;
const packets = [];
const authFlow = vm.runInNewContext(`let reconnectArmed=true,resumingConnection=true;
  ${method("validateAuthFields")}
  ${method("sendAuth")}
  ;({sendAuth,connection:()=>({reconnectArmed,resumingConnection})})`, {
  state: { ws: { url: endpoint }, clientConfig: {}, accountName: account }, authRecovery: loginStore, AuthMode, packAuth,
  accountField: { getValue: () => account, focus() {} }, passwordField: { getValue: () => password, focus() {} },
  wsUrlField: { getValue: () => endpoint }, isEmailBindingMode: () => false, getClientConfigValue: () => 1,
  setStatus() {}, t: key => key, clearAuthRequestPending() {}, cancelReconnect() {}, updatePlayUiVisibility() {},
  setAuthPanelVisible: value => { visible = value; }, loadLoginMap() {}, addConsoleLine() {},
  accountUi: { requireLogin() { visible = true; } },
  sendBytes: bytes => { packets.push(bytes); return true; },
});
authFlow.sendAuth(AuthMode.Login, { allowRecovery: true });
assert.equal(packets.at(-1)[3], 0); assert.equal(packets.at(-1)[7], 64);
password = "explicit-password";
authFlow.sendAuth(AuthMode.Login, { allowRecovery: true });
assert.ok(packets.at(-1)[3] > 0); assert.equal(packets.at(-1)[7], 0, "explicit password never silently falls back to token");
password = ""; now = 1100; const beforeExpired = packets.length;
authFlow.sendAuth(AuthMode.Login, { allowRecovery: true });
assert.equal(packets.length, beforeExpired); assert.equal(visible, true);
assert.equal(authFlow.connection().reconnectArmed, false); assert.equal(authFlow.connection().resumingConnection, false);
console.log("PASS persistent browser/server/account recovery isolation, cross-tab logout, expiry, malformed data and storage fallback");

// 只保存服务器签发的凭据，按服务器和账户隔离；不接收密码。
export function createAuthRecoveryStore({ storage, legacyStorage, crypto = globalThis.crypto, now = () => Date.now() } = {}) {
  const memory = new Map(), volatile = new Set();
  const server = endpoint => { try { return new URL(String(endpoint)).href; } catch { return String(endpoint); } };
  const key = (endpoint, account) => `florrbt.auth-recovery.v1:${JSON.stringify([server(endpoint), String(account)])}`;
  const selectedKey = endpoint => `florrbt.account.v1:${server(endpoint)}`;
  const validDevice = value => typeof value === "string" && /^[a-f0-9]{32}$/.test(value);
  const validToken = value => typeof value === "string" && /^[a-f0-9]{64}$/.test(value);
  function load(name) {
    if (volatile.has(name) || !storage) return memory.get(name) ?? null;
    try { return JSON.parse(storage.getItem(name) || "null"); }
    catch { return memory.get(name) ?? null; }
  }
  function write(name, value) {
    memory.set(name, value);
    try {
      if (!storage) throw new Error("storage unavailable");
      if (value === null) storage.removeItem(name);
      else storage.setItem(name, JSON.stringify(value));
      volatile.delete(name);
    } catch { volatile.add(name); }
  }
  function read(endpoint, account) {
    const name = key(endpoint, account);
    let value = load(name);
    if (!value && !volatile.has(name)) {
      try {
        value = JSON.parse(legacyStorage?.getItem(name) || "null");
        if (value && validDevice(value.deviceId)) { write(name, value); legacyStorage.removeItem(name); }
      } catch { /* 旧标签页凭据迁移失败时仍可重新登录。 */ }
    }
    if (!value || !validDevice(value.deviceId)) return null;
    return { deviceId: value.deviceId, token: validToken(value.token) ? value.token : "",
      expiresAt: Number.isSafeInteger(value.expiresAt) && value.expiresAt > 0 ? value.expiresAt : 0 };
  }
  function device(endpoint, account) {
    if (!endpoint || !account) return "";
    const previous = read(endpoint, account);
    if (previous) return previous.deviceId;
    if (!crypto?.getRandomValues) return "";
    const deviceId = [...crypto.getRandomValues(new Uint8Array(16))].map(byte => byte.toString(16).padStart(2, "0")).join("");
    write(key(endpoint, account), { deviceId, token: "", expiresAt: 0 });
    return deviceId;
  }
  function token(endpoint, account) {
    const value = read(endpoint, account);
    return value && value.expiresAt > Math.floor(now() / 1000) ? value.token : "";
  }
  function select(endpoint, account) {
    if (!token(endpoint, account)) return false;
    write(selectedKey(endpoint), account); return true;
  }
  function account(endpoint) {
    const value = load(selectedKey(endpoint));
    return typeof value === "string" && token(endpoint, value) ? value : "";
  }
  function accept(endpoint, account, deviceId, tokenValue, expiresAt) {
    if (!endpoint || !account || !validDevice(deviceId) || !validToken(tokenValue) ||
        !Number.isSafeInteger(expiresAt) || expiresAt <= Math.floor(now() / 1000) ||
        read(endpoint, account)?.deviceId !== deviceId) return false;
    write(key(endpoint, account), { deviceId, token: tokenValue, expiresAt });
    select(endpoint, account); return true;
  }
  function forget(endpoint, account) {
    write(key(endpoint, account), null);
    try { legacyStorage?.removeItem(key(endpoint, account)); } catch { /* 主存储和内存已撤销。 */ }
    if (load(selectedKey(endpoint)) === account) write(selectedKey(endpoint), null);
  }
  return { device, token, accept, forget, select, account };
}

let storage, legacyStorage;
try { storage = globalThis.localStorage; } catch { /* 隐私模式使用内存。 */ }
try { legacyStorage = globalThis.sessionStorage; } catch { /* 不迁移无法访问的旧存储。 */ }
export const authRecovery = createAuthRecoveryStore({ storage, legacyStorage });

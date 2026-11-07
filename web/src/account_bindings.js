import { t } from './i18n.js';
import { fitUtf8 } from './protocol.js';

export function packAccountLinkRequest(value) {
  if (typeof value.password === 'string') value = { ...value, password: new TextDecoder().decode(fitUtf8(value.password, 64)) };
  const payload = new TextEncoder().encode(JSON.stringify(value));
  if (!payload.length || payload.length > 8192) return null;
  const bytes = new Uint8Array(payload.length + 3); bytes[0] = 0xfc;
  new DataView(bytes.buffer).setUint16(1, payload.length, true); bytes.set(payload, 3); return bytes;
}
export function createAccountBindings({ state, send, openEmail, ensureSession, onChange }) {
  const root = document.getElementById('accountBindings'), list = document.getElementById('accountBindingList');
  const password = document.getElementById('accountBindingPassword'), status = document.getElementById('accountBindingStatus');
  let providers = [], nextRequest = 0, pending = null, popup = null, timeout = null;
  const ready = () => state.connected && (state.accountSession || state.authenticated);
  const showError = error => { status.textContent = t('ui.accountLinks.' + error, {}, { defaultValue: t('ui.accountLinks.binding_failed') }); };
  function finish(error = '') { pending = null; clearTimeout(timeout); timeout = null; popup?.close(); popup = null; if (error) showError(error); render(); }
  function request(action, provider, extra = {}) {
    const id = ++nextRequest;
    if (!send(packAccountLinkRequest({ request: id, action, provider, ...extra }))) return 0;
    return id;
  }
  function operate(provider, action) {
    if (pending) return;
    if (!ready()) { ensureSession(); return; }
    if (provider === 'email' && action === 'begin') { openEmail(); return; }
    const proof = password.value;
    if (!proof) { showError('password_required'); password.focus(); return; }
    if (action === 'begin') {
      popup = window.open('about:blank', '_blank', 'popup,width=520,height=720');
      if (!popup) { showError('popup_blocked'); return; }
      popup.document.body.textContent = t('ui.accountLinks.verifying');
    }
    const id = request(action, provider, { password: proof }); password.value = '';
    if (!id) { finish('binding_failed'); return; }
    pending = { id, provider, action, state: '', callbackUsed: false };
    status.textContent = t('ui.accountLinks.verifying');
    timeout = setTimeout(() => finish('authorization_expired'), action === 'begin' ? 300000 : 20000); render();
  }
  function render() {
    if (!root) return;
    list.replaceChildren();
    const available = ready();
    for (const name of ['discord', 'qq', 'email']) {
      const item = providers.find(value => value.provider === name);
      const row = document.createElement('div'); row.className = 'account-binding-row';
      const title = document.createElement('span'); title.textContent = name === 'email' ? t('ui.auth.email') : name === 'qq' ? 'QQ' : 'Discord';
      const detail = document.createElement('small'); detail.textContent = item?.bound ? (item.label || t('ui.accountLinks.linked')) :
        !available ? t('ui.accountLinks.connect') : !item?.configured ? t('ui.accountLinks.not_configured') : t('ui.accountLinks.unlinked');
      const button = document.createElement('button'); button.type = 'button'; button.dataset.provider = name;
      button.textContent = t(item?.bound ? 'ui.accountLinks.unlink' : 'ui.accountLinks.link');
      button.disabled = !!pending || (available && (item?.bound ? !item.canUnlink : !item?.configured));
      button.addEventListener('click', () => operate(name, item?.bound ? 'unlink' : 'begin'));
      const label = document.createElement('div'); label.append(title, detail); row.append(label, button); list.append(row);
    }
    document.getElementById('accountBindingPasswordRow').classList.toggle('hidden', !available || !providers.some(item => item.canUnlink || (item.provider !== 'email' && item.configured)));
    password.disabled = !!pending;
  }
  function receive(data) {
    if (data.schema !== 1) return;
    if (data.kind === 'state') {
      if (data.account !== state.accountName || !Array.isArray(data.providers) || data.providers.length > 3 || !ready()) return;
      providers = data.providers.filter(item => ['discord', 'qq', 'email'].includes(item.provider) && typeof item.configured === 'boolean' && typeof item.bound === 'boolean');
      if (Number.isInteger(data.emailPolicy) && data.emailPolicy >= 0 && data.emailPolicy <= 2) state.emailPolicy = data.emailPolicy;
      state.emailBound = providers.some(item => item.provider === 'email' && item.bound); onChange(); render(); return;
    }
    if (!pending || data.request !== pending.id) return;
    if (data.kind === 'result') { finish(data.error || ''); if (!data.error) status.textContent = t('ui.accountLinks.saved'); return; }
    if (data.kind === 'authorize') {
      let url; try { url = new URL(data.url); } catch { finish('authorization_failed'); return; }
      if (data.provider !== pending.provider || !/^[a-f0-9]{32}$/.test(data.state) ||
          url.origin !== (pending.provider === 'discord' ? 'https://discord.com' : 'https://graph.qq.com')) { finish('authorization_failed'); return; }
      pending.state = data.state;
      if (!popup || popup.closed) { finish('popup_blocked'); return; }
      status.textContent = t('ui.accountLinks.authorizing'); popup.location.replace(url.href);
    }
  }
  function callback(message) {
    if (!pending?.state || pending.callbackUsed || message?.type !== 'florrbt.account-link' ||
        message.provider !== pending.provider || message.state !== pending.state) return;
    pending.callbackUsed = true;
    if (message.error || typeof message.code !== 'string' || !message.code.length || message.code.length > 4096) { finish('authorization_failed'); return; }
    pending.id = request('complete', pending.provider, { state: pending.state, code: message.code });
    popup?.close(); popup = null; clearTimeout(timeout); timeout = setTimeout(() => finish('authorization_expired'), 20000);
    status.textContent = t('ui.accountLinks.verifying'); if (!pending.id) finish('binding_failed');
  }
  window.addEventListener('message', event => { if (event.origin === location.origin && event.source === popup) callback(event.data); });
  const channel = globalThis.BroadcastChannel ? new BroadcastChannel('florrbt.account-links') : null;
  if (channel) channel.onmessage = event => callback(event.data);
  return { receive, render, refresh() { if (ready()) request('list', ''); else render(); },
    reset() { providers = []; password.value = ''; status.textContent = ''; finish(); } };
}

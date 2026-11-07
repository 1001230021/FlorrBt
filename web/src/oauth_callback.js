const url = new URL(location.href), provider = url.pathname.split('/').at(-1);
const message = { type: 'florrbt.account-link', provider, state: url.searchParams.get('state') || '',
  code: url.searchParams.get('code') || '', error: url.searchParams.get('error') || '' };
history.replaceState(null, '', url.pathname);
if (['discord', 'qq'].includes(provider) && /^[a-f0-9]{32}$/.test(message.state) && message.code.length <= 4096 && message.error.length <= 256) {
  if (window.opener) window.opener.postMessage(message, location.origin);
  if (globalThis.BroadcastChannel) {
    const channel = new BroadcastChannel('florrbt.account-links'); channel.postMessage(message);
    setTimeout(() => channel.close(), 500);
  }
  document.getElementById('status').textContent = '授权结果已返回，请回到游戏查看绑定结果。 Authorization returned; check the result in the game.';
  setTimeout(() => window.close(), 700);
} else document.getElementById('status').textContent = '授权回调无效，请返回游戏重试。 Invalid callback; return to the game and retry.';

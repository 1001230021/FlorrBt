"use strict";

// 回调只把授权结果交还同源页面，密钥与令牌交换均留在游戏服务端。
function handleOAuthCallback(req, res) {
  let url;
  try { url = new URL(req.url, "http://callback.invalid"); } catch { return false; }
  if (!/^\/oauth\/callback\/(discord|qq)$/.test(url.pathname)) return false;
  if (req.method !== "GET") { res.writeHead(405, { Allow: "GET" }); res.end(); return true; }
  res.writeHead(200, {
    "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store", "Referrer-Policy": "no-referrer",
    "Content-Security-Policy": "default-src 'none'; script-src 'self'; style-src 'self'; base-uri 'none'; frame-ancestors 'none'",
    "X-Content-Type-Options": "nosniff",
  });
  res.end('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"></head><body><p id="status">正在返回游戏账户… Returning to your game account…</p><script type="module" src="/src/oauth_callback.js"></script></body></html>');
  return true;
}
module.exports = { handleOAuthCallback };

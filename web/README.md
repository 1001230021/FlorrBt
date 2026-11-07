# FlorrBt Web Client

2026-11-01：视频参考 UI 的面板、卡片及动画已接入默认 `index.html`。
`reference.html`（也可使用 `refresh.html`）保留独立显示设置，用于比较；两者都已删除位置预测、外推和输入提前排帧。
详见 `docs/ui-motion-2026-11-01.md`。

视频参考版入口：`http://127.0.0.1:8080/reference.html`。
它与默认客户端共用协议 1.0.7、角色模拟和物品事务，调整视频中共有内容的布局、颜色、字号和默认视距。
参考版设置单独保存；登录页及设置页可返回原客户端。
服务器、账号、地图和花瓣内容仍来自 FBT。见 `docs/web-reference-2026-11-01.md`。

隔离联网验收：`node tools/check_reference_client.mjs --live`。需要已构建的 Debug 服务端及 Playwright/Edge，
脚本只复制静态地图素材，在 `artifacts/video-reference/session-*` 下创建测试账号与配置，退出时关闭本次进程。

Run the bridge and static server:

```powershell
node tools/web_client_server.js
```

Then open:

```text
http://127.0.0.1:8080
```

Environment overrides:

```powershell
$env:WEB_PORT = "8080"
$env:GAME_HOST = "127.0.0.1"
$env:GAME_PORT = "10012"
node tools/web_client_server.js
```

# FlorrBt Web Client

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

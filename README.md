# FlorrBt v1.0.1

FlorrBt is a C++ florr.io-inspired server with a browser-based HTML client.

## Quick Start

Build the server. From a Visual Studio Developer PowerShell, or after locating
MSBuild via vswhere:

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild .\FlorrBt.Server.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Run the server, then start the web client bridge:

```powershell
node tools\web_client_server.js
```

Open:

```text
http://127.0.0.1:8080
```

## Configuration & secrets

- `data/server.cfg` is committed as a template. Secret values (SMTP password,
  RCON password, DeepSeek API key) are intentionally empty; set them at deploy
  time via the `FLORRBT_EMAIL_PASSWORD`, `FLORRBT_RCON_PASSWORD`, and
  `FLORRBT_DEEPSEEK_API_KEY` environment variables, or edit an untracked local
  copy. Never commit real credentials.
- `data/accounts.json` (player accounts) and `data/unique_petals.json` are
  runtime data and are not tracked. The server creates them automatically on
  first run; `data/accounts.json.example` documents the format.

## Shared enums and the web client

The client's wire IDs (petals, mobs, rarities, states) are generated from the
C++ enums in `src/Shared`:

```powershell
node tools\generate_shared_ids.js
```

Run this whenever you change an enum in `src/Shared`, then commit the
regenerated `web/src/generated_ids.js`. CI verifies the two stay in sync.

## Layout

- `src/Server`: game server
- `src/Shared`: shared protocol and game data
- `src/Engine`: shared engine utilities
- `web`: HTML client
- `tools/web_client_server.js`: static web server and WebSocket-to-TCP bridge

Visual Studio users can open `FlorrBt.slnx` and start the `FlorrBt Server + Web` launch profile.

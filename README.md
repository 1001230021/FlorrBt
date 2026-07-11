# FlorrBt

FlorrBt is a C++ florr.io-inspired server with a browser-based HTML client.

## Quick Start

Build the server with Visual Studio:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe' .\FlorrBt.Server.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Run the server, then start the web client bridge:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Microsoft\VisualStudio\NodeJs\node.exe' tools\web_client_server.js
```

Open:

```text
http://127.0.0.1:8080
```

## Layout

- `src/Server`: game server
- `src/Shared`: shared protocol and game data
- `src/Engine`: shared engine utilities
- `web`: HTML client
- `tools/web_client_server.js`: static web server and WebSocket-to-TCP bridge

Visual Studio users can open `FlorrBt.slnx` and start the `FlorrBt Server + Web` launch profile.

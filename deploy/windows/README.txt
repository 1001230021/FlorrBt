FlorrBt v1.0.1 Windows deployment

Quick start:
1. Build FlorrBt.Server.exe first, usually x64/Release or x64/Debug.
2. Deployment packages include node.exe; source checkouts may use Node.js LTS or Visual Studio's bundled node.exe.
3. Run deploy/windows/deploy.cmd.
4. Open http://SERVER_IP:8080.

Useful commands:
- deploy/windows/deploy.cmd
  Starts server and web bridge in two PowerShell windows.

- deploy/windows/deploy.cmd -Mode start-web -GameHost 127.0.0.1 -GamePort 10012 -WebPort 8080
  Starts only the web client and WebSocket bridge.

- deploy/windows/deploy.cmd -Mode start-server -GamePort 10012
  Starts the C++ game server through FlorrBt.Launcher.exe so hot_reload can restart it.

- deploy/windows/deploy.cmd -Mode firewall
  Opens TCP 8080 and TCP 10012. Run as Administrator.

- deploy/windows/deploy.cmd -Mode install
  Installs auto-start scheduled tasks for server and web. Run as Administrator.

- deploy/windows/deploy.cmd -Mode stop
  Stops the scheduled tasks.

- deploy/windows/deploy.cmd -Mode uninstall
  Removes the scheduled tasks. Run as Administrator.

- deploy/windows/deploy.cmd -Mode status
  Shows scheduled task and port status.

Notes:
- data/server.cfg controls the real game server port. If it does not exist, deploy.ps1 creates it from server.cfg.example.
- Put a replacement binary at FlorrBt.Server.next.exe, then enter hot_reload in the server console. You can also use hot_reload <candidate-path>.
- To deploy only the static web client on another machine, run with -Mode start-web and point -GameHost to the game server IP.
- The browser should use http://SERVER_IP:8080, not 127.0.0.1, when connecting from another computer.

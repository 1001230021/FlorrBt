FlorrBt web client deployment

1. Install Node.js LTS on the Windows server.
2. Edit deploy/windows-web/web.env.ps1 if needed.
   - WEB_PORT is the public web port, default 8080.
   - GAME_HOST/GAME_PORT point to the C++ game server, default 127.0.0.1:10012.
3. Run deploy/windows-web/open_firewall.ps1 as Administrator.
4. Run deploy/windows-web/start_web.ps1 to test.
5. Optional: run deploy/windows-web/install_web_task.ps1 to auto-start at logon.

Open this in a browser:
http://SERVER_IP:8080

FlorrBt web client deployment

1. Put this folder inside the FlorrBt project/package folder.
   The script auto-detects the root by looking for web/index.html and tools/web_client_server.js.
2. Install Node.js LTS on the Windows server.
3. Edit deploy/windows-web/web.env.ps1 if needed.
   - WEB_HOST is the bind address. Keep 0.0.0.0 for public LAN/WAN testing.
   - WEB_PORT is the public web port, default 8080.
   - GAME_HOST/GAME_PORT point to the C++ game server, default 127.0.0.1:10012.
4. Run deploy/windows-web/open_firewall.ps1 as Administrator.
5. Double-click deploy/windows-web/start_web.cmd to test.
   You can also run:
   powershell -ExecutionPolicy Bypass -File deploy/windows-web/start_web.ps1 -Root D:\VSProject\FlorrBt
6. Optional: run deploy/windows-web/install_web_task.ps1 to auto-start at logon.

Open this in a browser:
http://SERVER_IP:8080

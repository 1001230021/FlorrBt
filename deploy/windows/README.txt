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

- deploy/windows/start_mysql.cmd -MySqlRoot C:\path\to\mysql
  Initializes (only when the data directory is empty), starts the local MySQL server, and creates the configured FlorrBt database/user. Its data and logs live under `mysql\` in the deployment package.

- deploy/windows/migrate_accounts_to_mysql.cmd
  Converts `data\accounts.json` and verifies the round trip through MySQL. Add `-StartMySql` to start the package-managed database first, or pass `-JsonPath <file>` for another account JSON file.

- deploy/windows/stop_mysql.cmd / deploy/windows/mysql_status.cmd
  Stops only the MySQL process started by this deployment, or reports its status.

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
- The MySQL build must ship `libmysql.dll`, `libssl-3-x64.dll` and `libcrypto-3-x64.dll` beside `FlorrBt.Server.exe`; the Visual Studio post-build step and package script copy all three automatically.
- The deployment package contains the management scripts, but not the large MySQL server distribution. Install/extract MySQL 8.x separately and pass its root directory, or set `FLORRBT_MYSQL_ROOT`.
- `START_ALL.cmd` starts MySQL before the game server. On a fresh MySQL initialization, root has no password; use `-RootPassword` when managing an existing secured MySQL installation. Set `FLORRBT_MYSQL_PASSWORD` for the application user's password.
- If `-MySqlRoot` is not available through `FLORRBT_MYSQL_ROOT` or a standard installation path, pass it to each command that starts or manages MySQL (for example, both `START_MYSQL.cmd` and `START_ALL.cmd`).
- When the MySQL installation has no `my.ini`, the launcher creates a marked temporary one for the package data directory and removes it after a clean `stop_mysql.cmd`. An existing unmarked `my.ini` is never overwritten.
- Recommended first deployment order: `START_MYSQL.cmd -MySqlRoot <mysql-directory>`, then `MIGRATE_ACCOUNTS_TO_MYSQL.cmd`, then `START_ALL.cmd`. Account migration is intentionally not automatic because it publishes the JSON snapshot into MySQL.
- Put a replacement binary at FlorrBt.Server.next.exe, then enter hot_reload in the server console. You can also use hot_reload <candidate-path>.
- To deploy only the static web client on another machine, run with -Mode start-web and point -GameHost to the game server IP.
- The browser should use http://SERVER_IP:8080, not 127.0.0.1, when connecting from another computer.

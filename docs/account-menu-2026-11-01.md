# 账户菜单与持久登录

左上角新增紫色账户按钮，使用白色人物矢量图标匹配提供的参考。账户面板沿用现有面板动画，桌面和手机都使用紧凑紫色弹窗。未登录时可输入账号密码并选择注册；登录后显示当前账户与登出按钮。

首次访问保留中央登录表单。2026-11-07 更新后，有持久凭据时会自动向服务端恢复账户会话；收到真实背包后才显示“开始游戏”。此时服务端只有无身体的 CPlayer，点击开始才进入世界。中央表单与账户面板共用输入控件，切换入口不会产生两份不同的账号或注册状态。详见 `account-lobby-2026-11-07.md`。

新增 `account_ui.js` 管理账户入口和表单位置，`account_logout.js` 处理未连接菜单的远端凭据撤销。凭据保存在按服务器、账户隔离的浏览器存储中，新凭据有效期三十天；密码不写入浏览器存储。刷新、关闭网页不再执行登出。其他标签页登出会同步清理当前会话。

协议更新为 **1.0.8**，新增 AccountLogin、AccountRegister、EnterGame，服务端与客户端需要同时更新。账户会话没有游戏身体，不会在等待点击开始时参与游戏。原有直接登录并进入游戏的模式保留给现有工具，版本握手继续严格校验。

验证入口：

```powershell
node tools/test_auth_recovery.mjs
node tools/check_reference_client.mjs --live --original --accounts
build/architecture-review/FlorrBt_RuntimeTests.exe AccountMenu
build/architecture-review/FlorrBt_AccountTests.exe
build/architecture-review/tests/FlorrBt_Tests.exe
```

浏览器验证使用独立账户目录和随机端口，覆盖注册但不进入、刷新、新浏览器上下文、开始游戏、错误密码重试、已连接和未连接菜单的登出及旧令牌失效、移动端布局。不连接正式数据库，不保存测试密码或原始令牌到验收结果。

最终结果：43 个客户端回归脚本、账户菜单运行时专项 1/1、账户事务和凭据测试 19/19、基础与协议测试 96/96 通过。原入口完成账户流程验收；参考入口完成快速换瓣、合成、天赋、聊天、战斗、死亡配装、复活和重连的联合验收。未重跑完整服务器运行时套件。

换瓣压力测试夹具改为等待观测到真实回执后再交付延迟消息，避免用固定 350ms 假设服务器已经完成持久化。生产换装代码未因此修改。

结果与截图：`artifacts/account-menu/`。主要日志：`build/account-menu-build.log`、`build/account-menu-runtime-final.log`、`build/account-menu-runtime.log`、`build/account-menu-js.log`、`build/account-menu-browser-final.log`、`build/account-menu-gameplay-final.log`。离线账户菜单夹具最初遗漏玩家索引，修正后专项通过；实际联网场景初始化通过浏览器验证。

详细凭据生命周期见 [auth-recovery.md](auth-recovery.md)。部署包与 GitHub 上传继续等待用户验收。

修改前备份：`D:\VSProject\FlorrBt-backups\pre-account-menu-20261101-123426.zip`。
SHA256：`83f3f86c90297956a3ab2c2f1491cb5c7a439289c77829e629a84334d284a976`。

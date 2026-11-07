# 死亡配装与同场景复活修复

## 原因

- 手动复活、附身生物死亡和缺失身体后的恢复都复用了 `NotifyPlayerWorldChanged → QueueWelcome`。客户端将 Welcome 视为完整场景屏障，清空场景并以 `freshInstance` 重新加载地图，因此同世界复活也会出现 Loading。
- 客户端 `loadoutEditingBlocked()` 要求身体存活，OwnerState 和死亡事件还会清掉未确认配装。服务端 `TryApplyLoadout()` 拒绝死亡身体，`InventoryActorScope()` 在死亡时返回空字符串。只放开界面仍会被服务端拒绝。
- 换装回执未到就复活，会更换身体代数；仍在客户端队列中的换装带着旧身体身份，后续自然失效。

## 当前行为

死亡期间允许调整主槽、副槽和配装预设。操作仍经过库存守恒、模式权限、封槽、作用域和持久化回执校验，成功后更新账户与保留的槽位。尸体不加载花瓣、不因新装备启用光环，也不重算死亡时的属性。原地复活和重新生成身体均使用更新后的配装。

`CPlayerLifecycleService::RespawnPlayer()` 统一处理三条复活入口，根据世界实例代数选择同步方式。同场景调用 `QueuePlayerState()`，继续使用有重试和快照屏障的原子玩家基线；真正换世界或无法确定原场景时仍发送完整 Welcome。客户端不通过地图路径猜测是否可以复用场景，避免重新引入同资源房间的地形串用。

新增 `web/src/respawn_request.js`。点击复活后暂时停止继续修改配装，等待经济队列中的回执、Inventory 和 OwnerState 全部确认，再发送复活请求。若换装被拒绝，取消等待中的复活并显示已有错误提示，保留纠正配置的机会。等待不以超时推断换装成功。

死亡不会取消同一身体的待确认配装预览。玩法锁定、身体/库存作用域变更等失效条件继续清理预览。死亡 NPC 和已标记移除的实体仍不能换装；死亡状态下仍不能锻造。

## 验证与验收

实际浏览器使用原入口 `index.html`，连接独立数据目录、随机端口的本地测试服务器：死亡后分别拖拽修改主副槽；暂缓 WebSocket 消息交付，连续切换三次后立即点击复活；确认回执未交付时没有发送复活，交付后只发送一次，最终主副槽、身体快照与目标方案一致。整个死亡及同世界复活过程保持同一个地图对象和加载代数，Loading 出现次数为 0；随后使用恢复令牌重连，配装仍保留。

最终构建通过。43 个纯 JS/源码回归脚本通过；新增 3 项死亡配装运行时测试通过，并验证经济事务、场景重试、复活、来源状态及尸体快照相关回归。最终浏览器运行同时包含此前 23 次连续 R 和快速 1/2 切换检查，无页面异常。未重新执行完整 C++ 套件，也未连接正式账户库。

结果保存在 `artifacts/client-death-build/live.json`。日志为 `build/death-loadout-build-final.log`、`build/death-loadout-runtime-related.log`、`build/death-loadout-runtime-final.log`、`build/death-loadout-js.log` 和 `build/death-loadout-browser-final.log`。

回归命令：

```powershell
node tools/test_respawn_request.mjs
node tools/test_architecture_obvious_fixes.mjs
node tools/test_loadout_reconciliation.mjs
node tools/check_reference_client.mjs --live --original --death-build --rapid-swaps
build/architecture-review/FlorrBt_RuntimeTests.exe DeathLoadout
```

本次包含服务端逻辑修改，验收须运行重新编译的服务端，并在浏览器 Ctrl+F5。首次登录、真正换世界和断线重连仍需初始化场景；本次不宣称解决上一轮记录的首次地图加载停顿。

备份：`D:\VSProject\FlorrBt-backups\pre-death-loadout-20261101-121836.zip`。SHA256：`5b273150766bf1468ab403e48714344c2788f91dcb048a82ecb8112803fb7a7c`。

部署打包和 GitHub 上传仍等待用户验收通过。

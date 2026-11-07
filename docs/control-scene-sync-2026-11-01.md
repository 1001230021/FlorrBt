# control 与场景同步

2026-11-07 补充：已复现并修复萤火虫幼虫单体节附身导致整条生物被清理、玩家自动重生的问题，见 [幼虫专项记录](firefly-larva-control-2026-11-07.md)。

`CWorldTransfer::Possess()` 在附身提交后，无论是否跨世界都调用 `NotifyPlayerWorldChanged()`。这会发送 Welcome，并让客户端清空实体视图、以 `freshInstance` 重新加载地图。同世界传送也存在同样的通知问题。此前复活路径已拆分，但附身和传送还未接入。

现在 `ISceneTransitions::NotifyPlayerStateChanged()` 用于同世界的附身和传送，复用原子玩家基线的队列、背压重试和快照屏障。控制对象、背包、模式限制和后续实体快照继续同步，地图和地形保持当前实例。跨世界仍使用完整 Welcome 和目标场景初始化，不以地图文件路径判断是否同一世界。

客户端检测控制对象的 ID 或实体代数变化后，会清除旧身体的拖拽、待确认配装预览和槽位运行时显示；不再依赖清空整个场景完成这些清理。NPC 独立背包、释放附身时恢复原 AI，以及无默认 AI 时非击杀移除的规则不变。

## 验证

修复前的真实指令测试复现了同世界 control 触发 Loading；该测试中的蜜蜂和机甲花仍保留了控制，未复现所有情形下必然回到出生点，不能据此声称所有失去控制的原因均已查尽。

修复后真实服务器及 Chromium 测试覆盖：

- 同世界先后控制蜜蜂、机甲花并持续移动；目标 ID 保持一致，地图对象、地图加载代数和 WebSocket 均不变，Loading 为 0。
- 创建另一个世界并跨世界控制其中的蜜蜂；目标场景加载完成后仍控制该实体，没有重连或被替换为玩家花。
- C++ 附身相关测试 7/7、NPC 背包测试 3/3、场景原子初始化测试 1/1 通过。
- 世界事务、配装确认、死亡表现、快照身份、客户端接口及地图加载相关 JS 回归通过；模块依赖无新增环。

未启动正式服务器或访问正式账户库。测试在独立数据目录和随机端口运行；未重复执行完整 C++ 套件。

```powershell
node tools/check_reference_client.mjs --live --original --control
build/architecture-review/FlorrBt_RuntimeTests.exe Possession
build/architecture-review/FlorrBt_RuntimeTests.exe NpcInventory
build/architecture-review/FlorrBt_RuntimeTests.exe SceneInitialization
```

结果：`artifacts/control-sync/live.json`。日志：`build/control-before.log`、`build/control-build.log`、`build/control-runtime.log`、`build/control-browser-final.log`。

本次未改变协议版本，仍为 1.0.8；需要重启更新后的服务端并 Ctrl+F5。部署打包和上传继续等待用户验收。

修改前备份：`D:\VSProject\FlorrBt-backups\pre-control-sync-20261101-132805.zip`。
SHA256：`950ae01fe2105c92efe6bcec992cae1bb007cf5df6bdcdbf8a9e7ebe005a5240`。

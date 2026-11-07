# 萤火虫幼虫 control 回出生点

## 已确认根因

幼虫的头、身体和尾部都是独立实体，客户端都以 FireflyLarva 类型展示，只用视觉变体区分部位。原附身入口允许单独接管体节，但体节的移动由头部约束，不能独立响应玩家移动。

随后从体节 A 切换到体节 B 时，A 的默认 AI 为空，`CMobBase::ReleasePlayerControl()` 按独立 NPC 的规则删除 A。`DetachMultiSegmentBody()` 发现整条硬约束链缺少部件，标记头部删除；头部移除再清理所有剩余体节。玩家控制的 B 随之消失，玩家生命周期服务自动生成玩家花，于是回到出生点。

这条故障在此前去掉同世界 Loading 后仍可复现，因此上一轮场景通知修复没有覆盖这个独立问题。修复前的实际记录为：头部 ID 4 → 控制体节 ID 5 → 请求尾部 ID 9 → 玩家变为 ID 6、类型 PlayerFlower，全部幼虫体节消失。

## 修复

- `CWorldTransfer::Possess()`：验证体节与根的双向关系，将任意部位的控制请求归一到头部；占用检查覆盖整条生物，不能通过另一体节抢占其他玩家的控制。
- 对同一条幼虫重复选择不同部位，保持现有控制器、技能运行时和临时背包作用域。
- `CMobBase::ReleasePlayerControl()`：仍依附于有效根的体节恢复跟随状态，即使没有独立 AI 也不删除；独立 NPC 的无默认 AI、默认 PlayerController 非击杀移除规则保持原样。
- 旧快照中的单体节附身可以安全切回整条生物，也可以直接退出会话，避免级联删除。
- `control` 成功日志报告实际接管的头部 ID，避免继续显示请求的身体节 ID。

## 验证

构建通过，实际 Chromium 与隔离服务器验证从身体节开始附身、移动、身体/尾部/头部重复切换，以及恢复凭据重连。所有请求最终控制头部，六个部件完整，同世界控制 Loading 为 0。结果位于 `artifacts/larva-control/live.json`。

16 项服务端运行时测试通过：幼虫专项 3、附身 7、NPC 背包 3、完整多节体迁移 1、复合迁移回滚/占用保护 2。幼虫专项包括旧体节控制快照恢复、退出、原 AI 恢复和跨玩家占用保护。未重跑完整 C++ 套件。

```powershell
node tools/check_reference_client.mjs --live --original --control --larva
build/architecture-review/FlorrBt_RuntimeTests.exe Larva
```

日志：`build/larva-control-before.log`、`build/larva-control-build.log`、`build/larva-control-runtime.log`、`build/larva-control-browser.log`。

## 另需审查的架构问题

复合生物的阵营仍由各实体独立保存。`AssembleMultiSegmentMob()` 只在装配时把根的队伍复制给体节；`CEntity::Team()` 读取各实体自身的来源值。玩法进入回调（如死亡竞赛、PVP）修改当前受控头部的队伍时，体节不会同步，碰撞和 AI 查询却仍使用各实体的 Team。这会让同一条生物的不同部位出现不同的敌友判断。

建议让复合生物提供统一的有效阵营与玩家归属接口，并明确腐化等来源状态作用于单个体节还是整条生物，再统一接入碰撞、目标选择、伤害归属和快照。该决策涉及战斗规则，本次未改变，单独列出供架构审查。

备份：`D:\VSProject\FlorrBt-backups\pre-larva-fix-20261107-134521.zip`。
SHA256：`e0ee156e74c13148fe6f9ddf72a8c6f8192a99620c1a3572563e62c938aa8c0e`。

协议仍为 1.0.8；验收需要重启更新后的服务端。未打包、提交或推送，继续等待用户验收。

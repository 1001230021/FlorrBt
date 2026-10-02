# 服务端生命周期审查（2026-09-25）

## 范围与结论边界

本轮配合 Shovel 入土/出土特效，重点检查伤害归属、掉落归属、花瓣销毁、跨世界转移及状态清理的调用链。
以下是当前工作区的静态审查结果，不是线上复现记录，也不代表服务端已经经过完整审计。
本轮未修改服务端代码，未启动游戏、服务器或数据库，未操作真实账号和世界。

优先级：P1 为可能访问已释放内存的问题；P2 为有条件触发的归属或玩法错误。

## 1. P1：伤害记录持有已离线玩家的裸指针，热重载保存直接解引用

触发链：玩家攻击尚未死亡的生物 → 玩家被彻底移除 → 生物仍保留贡献记录 → 热重载保存该生物 → 读取已释放的玩家对象。

证据：

- [mob.h](../src/Server/Game/entities/mob.h#L21)：`CDamageData::m_player` 是 `CPlayer*`。
- [mob.cpp](../src/Server/Game/entities/mob.cpp#L148)：`ApplyDamageDirect` 按指针地址累计贡献。
- [network_module.cpp](../src/Server/Module/network_module.cpp#L2942)：`DropPlayer` 最后删除 `m_players` 中的对象，没有清理其他存活生物的伤害记录。
- [hot_reload_snapshot.cpp](../src/Server/HotReload/hot_reload_snapshot.cpp#L521)：保存时只检查非空，随后调用 `record.m_player->GetId()`。

影响：热重载保存可能崩溃或写入错误身份。奖励端的 [IsActivePlayer](../src/Server/Game/gamecontrollers/open_reward_service.cpp#L30) 只检查指针地址是否在当前玩家集合中；如果分配器把同一地址交给新玩家，还可能把旧贡献归到新玩家名下。

相关设计问题：`m_reset_timer` 被设为 120 秒并被序列化，但当前 `src` 中没有递减或到期移除逻辑。不能依靠这个字段消除过期贡献或悬空引用。

建议：用稳定账号/会话句柄记录贡献，明确重连是否继承；需要连接身份时加入 generation。快照只保存可验证的身份数据，并在玩家退出时清理或转换历史记录。仅在保存前增加一次指针地址检查不能解决地址复用。

## 2. P2：私人掉落使用可复用的连接 ID，可能被另一账号继承

触发链：A 获得私人掉落但未拾取 → A 显式断开或被踢 → B 连接并得到释放的 ID → B 能看到并拾取尚未过期的 A 的掉落。

证据：

- [open_reward_service.cpp](../src/Server/Game/gamecontrollers/open_reward_service.cpp#L372)：构造 `CDrop` 时将 `player->GetId()` 用作所有者。
- [drop.cpp](../src/Server/Game/entities/drop.cpp#L114)：拾取权限只比较 `m_owner_id` 与当前玩家数字 ID。
- [snapshot_service.cpp](../src/Server/Game/systems/snapshot_service.cpp#L291)：私人掉落的可见性也只比较该 ID。
- [network_module.cpp](../src/Server/Module/network_module.cpp#L2962)：退出时回收 ID；`GetNewPlayerId` 优先取可用 ID。退出清理没有同步撤销这些掉落的所有权。

影响：账号之间的私人掉落隔离失效。必须在掉落到期前、正确世界内触发；不需要地址复用，也不是公共掉落的预期行为。

建议：归属绑定账号或带 generation 的会话，统一用于可见性、拾取、合堆和热重载。先确定断线重连是否应保留掉落，避免简单改为公共掉落而改变规则。

## 3. P1：调试移除花瓣绕过所属槽的生命周期，休眠生物留下悬空指针

触发链：普通稀有度机械花已经生成花瓣 → 玩家远离，机械花停止 Tick → 调试观察者移除其中一枚花瓣 → 世界 Cleanup 释放花瓣 → 机械花再次活动或刷新槽时访问旧指针。

证据：

- [dev_debug_service.cpp](../src/Server/DevTools/dev_debug_service.cpp#L600)：`despawn` 对所有实体直接 `MarkForDestroy`，没有为槽内花瓣执行解除挂接。
- [petal_slot.h](../src/Server/Game/entities/petals/petal_slot.h#L31)：所属槽保存 `std::vector<CPetal*>`。
- [gameworld.cpp](../src/Server/Game/gameworld.cpp#L2374)：普通生物可以不进入活动集合；调试观察不让生物自动恢复 Tick。ET/Q/P 的常驻 Tick 是另一分支，不是这里的触发条件。
- [gameworld.cpp](../src/Server/Game/gameworld.cpp#L3038)：Cleanup 消费销毁队列并释放实体，不要求所属生物正在 Tick；`CPetal` 没有销毁前解除槽引用的覆盖实现。
- [petal.cpp](../src/Server/Game/entities/petals/petal.cpp#L445)：`RefreshPetalState` 直接访问非空的槽内指针；槽 Tick 中也有同类访问。

影响：再次激活时可能访问已释放内存。活动中的所属花通常能在 Cleanup 前清掉指针，所以这个问题容易被常规调试漏掉。

边界：正常花瓣受伤并非这个链路；其 `TakeDamage` 主要降低血量，交给槽 Tick 处理生命周期。不能据此推断所有花瓣受击都会造成悬空指针。

建议：建立类型感知的管理端移除入口，槽内花瓣先从所属槽摘除，再安排回装/销毁；或者在统一销毁阶段保证双向解除引用。还应遵守 `OnPetalCleared` 等行为回调，不能只改成血量为零来替代“移除而不发奖励”的语义。

## 4. P2：Corruption 状态退出时恢复旧队伍，覆盖新世界的队伍分配

触发链：玩家处于 Corruption 状态 → 管理端直接传送到锁定配装的 PvP/EOU 世界 → 新 controller 分配新队伍、卸下 Corruption → 下一次状态刷新移除 Corruption → 析构函数把队伍写回旧世界的值。

证据：

- [states.cpp](../src/Server/Game/states/states.cpp#L379)：状态创建时缓存 `m_old_team`；析构时无条件恢复。
- [gameworld.cpp](../src/Server/Game/gameworld.cpp#L2177)：跨世界转移复用同一个玩家实体，保留状态，再调用目标 controller 的进入回调。
- [pvp_controller.cpp](../src/Server/Game/gamecontrollers/pvp_controller.cpp#L96)：进入世界先分配队伍，再清理配装；后续 Tick 只维护配装，没有重新纠正队伍。
- [flower.cpp](../src/Server/Game/entities/flower.cpp#L707)：卸下/禁用 Corruption 且不在扩散范围时移除状态。
- [dev_debug_service.cpp](../src/Server/DevTools/dev_debug_service.cpp#L640)：玩家传送入口确实可以直接调用跨世界转移。

影响：实体的敌我判定与 PvP/EOU 的队伍登记可能不一致。管理员在 Corruption 期间直接改队也有同类覆盖风险。

边界：普通房间入口可能先经过 waiting 世界，提前移除状态，因而不能宣称所有正常进房都会触发。

建议：把“基础队伍”与状态覆盖分离，或在世界/队伍切换时明确撤销或重建相关状态；不要让状态析构无条件恢复跨世界的历史值。

## 需要确认规则：图鉴统计的是产生掉落的击败次数，而非所有有效击杀

[open_reward_service.cpp](../src/Server/Game/gamecontrollers/open_reward_service.cpp#L341) 在 `drops.empty()` 时提前继续，`RecordCodexKill` 在此之后；没有掉落表时也提前返回。
因此即便玩家有有效贡献，空掉落也不增加图鉴计数。注释明确提到以掉落结果计数，这可能是刻意规则，本报告不把它认定为确定缺陷。

若界面希望显示“击杀次数”，应把计数与随机掉落分离；若希望显示“成功掉落次数”，应统一客户端文案与服务端命名。

## 后续修复与离线验证建议

- [ ] 优先修复伤害记录身份，以及管理端移除花瓣的悬空引用。
- [ ] 用稳定归属统一私人掉落的可见、拾取、合堆与快照行为。
- [ ] 明确 controller 与状态对队伍字段的写入责任。
- [ ] 覆盖“贡献者退出后保存快照”“ID 复用后拾取”“所属生物休眠时移除花瓣再唤醒”“带 Corruption 直接跨世界”四组回归场景。
- [ ] 确认伤害贡献超时与图鉴计数规则，再决定是否改变玩法。

## 本轮客户端交付与检查

入土/出土复用合成的旋转方块绘制：入土向内收拢、出土向外散开，土色渐隐；普通游戏和调试视图均接入。
粒子使用世界坐标、独立层和数量预算，并排除首次出现、重复状态、死亡、观测间断及明显传送的伪触发。

以下仅为离线检查，均通过；没有进行游戏内视觉验收或线上服务端复现：

- `node tools/test_burrow_particles.mjs`：状态边沿、世界坐标、30/60/120fps 运动一致性、粒子预算与减少动态效果。
- `node tools/test_dev_debug.mjs`：模拟 DOM/连接下的调试工作区回归。
- `node tools/test_craft_talent_ui.mjs`：共用绘制函数后的合成/天赋回归。
- `node tools/check_client_deps.mjs`：未新增依赖环或 `game.js` 导入者。
- 相关 JavaScript 语法检查与差异空白检查。

# 架构、玩法扩展与体验审查

基线：`073ae6f`。本轮先在本地修复确定问题，涉及产品规则或大范围迁移的改动留待审查，不自动提交、推送或部署。

## 范围与判断依据

审查了服务器装配、世界和会话、迁移与附身、扩展调度、玩法控制器、地形读取/修改、花瓣与状态、经济保存、快照可见性，以及客户端连接、合成、装备、队伍、小地图等调用链。大型生物/花瓣文件按相关入口追踪，没有声称逐行证明所有技能和数值逻辑。

下文区分代码已能确定的行为、扩展时会遇到的约束，以及需要用户测试验证的体验判断。拟合方向以本项目已经具备的花瓣轨道、攻击/防御、探索掉落和稀有度成长为基础；没有把对 florr.io 隐藏数值或当前版本规则的印象当作复刻规格。

当前架构不需要推倒重写。世界独占实体、会话不拥有实体、世界限定句柄、迁移提交边界、场景基线以及扩展能力接口，都是应当保留的基础。主要问题是几个边界仍不完整：会话与身体、世界事实与玩法规则、动作请求与完成结果、内存成功与持久化成功。

## 已直接处理的确定问题

| 编号 | 原问题与用户影响 | 修复位置和处理 |
|---|---|---|
| F01 | 注册/登录可以使用内部 `@sandbox:` 名称；这种记录又会被正式保存排除，还可能把临时进度键当成登录身份 | `account_store.cpp` 的 `PrepareAuthentication`、`CommitRegistrationInternal` 拒绝保留命名空间；内部克隆接口不受影响 |
| F02 | 最后一次账户保存失败后，如果没有新操作，就没有新的保存触发 | `AccountSaveWorkerMain` 保留失败快照，以 100ms 至 2s 退避重试；有新一代快照时让新快照取代旧快照；关闭时不无限重试 |
| F03 | 合成失败于校验/库存不足时没有终态响应，沙盒锻造也直接返回；客户端要等 20 秒 watchdog | 网络层始终回复拒绝，`consumed=0` 明确表示没有执行；重发权威库存，客户端取消动画并提示未消耗材料。保留真正随机失败的返还动画 |
| F04 | 装备、卸下、拖动、单槽换装及合成在 WebSocket 发送失败时仍提交乐观状态或启动动画 | `net_outbound.js` 返回发送异常；各操作检查返回值；没有有效受控实体时禁止操作活体装备 |
| F05 | 没有身体但还在房间会话中的玩家，被规则同步和房间人数漏掉；副槽限制也随身体消失 | `CGameWorld::Players/GetPlayerCount` 和副槽入口按 `CPlayer::GetWorld` 的会话归属判断；保留重生期间的房间与玩法作用域 |
| F06 | 手动重生在身体已删除时回退主世界，而自动重生会保留原世界 | 手动重生统一使用保存的世界上下文，再在确实无上下文时回退主世界 |
| F07 | 同世界传送不刷新环境样本，下一阶段会把起终点之间的密道当成实际经过的路线 | 同世界迁移提交后采样落点，重置环境检测起点；回滚不执行这一步 |
| F08 | EOU 胜负公告遍历全服玩家，其他房间也收到比赛消息 | 公告和蚁群消息改用世界限定玩家视图 |
| F09 | 短暂掉线或身体替换会使队伍成员失去资格并被刷新掉，重连后队伍关系已丢失 | 队伍资格跟随已认证且仍保留的世界会话；实际会话移除仍清理成员。不改变“跨世界队伍是否保留”的规则 |
| F10 | 地形改变但尺寸/碰撞数量不变时，小地图缓存不失效；越界列可折算成下一行 | 地形 revision 纳入小地图缓存键；地形入口分别检查行列范围和整数性 |
| F11 | `eou_small` 的内嵌图片集合 tileset 无法加载；地形控制器还直接解引用不存在的 `source` 字段；混用内嵌/外置 tileset 时会漏掉外置碰撞 | 修正 vendored libtmj 的可选字段解析；控制器支持两种 tileset；瓦片碰撞统一经一个 JSON 提取入口，移除“只有完全没有内嵌碰撞才回退”的分支 |

补充了 Windows 离线测试的错误输出设置，避免断言/访问异常弹窗使测试进程长期等待。它不屏蔽断言。

F11 的格式依据是 [Tiled 官方 JSON 格式说明](https://doc.mapeditor.org/en/stable/reference/json-map-format/#tileset)：tileset 可以外置，独立图片可定义在各 tile 上，独立 tileset 文件的元数据不能被当成所有内嵌 tileset 的必填字段。本次修复针对现有地图和上述两种形式，没有扩大宣称支持无限地图、全部图层变换或全部新版格式。

## 需要审查的架构问题

### A01 · P0：Unique 归属与背包没有共同的持久化事务

**位置**：[unique_petal_registry.cpp](../src/Server/Persistence/unique_petal_registry.cpp) 的 `GetUnique/SetOwner/ShutDown`；[account_store.cpp](../src/Server/Persistence/account_store.cpp) 的 `ExchangeUniquePetal/Save`；[server_data_store.cpp](../src/Server/Persistence/server_data_store.cpp) 的 `ShutDown`。

**根源**：Unique 归属在注册表内存中修改，正常落盘主要发生在 `ShutDown`；账户由另一条后台保存链发布。进程内失败已有回滚，但两份存储没有同一个提交点，不能把这种回滚当成崩溃一致性。

**后果**：锻造后的账户可能已保存，归属文件仍是旧主人。异常退出后，“谁拥有唯一物品”与实际背包发生分歧，后续转移、返还和唯一性检查失去共同依据。

**建议**：MySQL 后端把归属、扣除、授予及旧主人返还放在同一数据库事务；JSON 后端用同一代经济快照或可重放事务日志。操作完成通知由事务结果驱动。

**请决定**：以账户数据库中的唯一所有权账本为权威，还是必须继续兼容独立归属文件？后者需要日志/恢复协议，不能仅每次多调用一次保存。此项应在继续扩展高价值交易前解决。

### A02 · P1：IP 被当成免密码身份凭证

**位置**：[account_store.cpp](../src/Server/Persistence/account_store.cpp) `PrepareAuthentication/VerifyAuthentication`；[game.js](../web/src/game.js) 登录成功清空密码与自动重连链路；[client_config.js](../web/src/client_config.js) `auto_register`。

**根源**：省略密码且 IP 与账户保存的 IP 相同，可以跳过密码验证。客户端选项是便利开关，不能成为服务端身份验证边界。

**后果**：同一公网 NAT 下的不同设备可能共享这一条件。它表达的是网络位置相同，不是同一个人。直接删除此逻辑又会破坏当前无密码自动重连，所以没有静默更改登录规则。

**建议**：首次正常认证后发放可撤销、有期限、按设备/会话绑定的恢复令牌，服务端保存令牌摘要；IP 只参与限流或异常提示。原始密码继续不落浏览器存储。

**请决定**：公开服务器切换到设备恢复令牌，还是仅把 IP 免密保留为明确标注的内网开发选项？推荐前者。

### A03 · P1：经济请求缺少身份，组合换装不是服务端事务

**位置**：[network_msg.h](../src/Shared/network_msg.h) `ClientCraftRequest/CraftResult`；[game.js](../web/src/game.js) `beginCraftSpinMotion/handleCraftResult/quickSwapAllSlots/applyLoadoutPreset`；[net_outbound.js](../web/src/net_outbound.js) `sendPacketBatch`；[network_module.cpp](../src/Server/Module/network_module.cpp) 请求分发。

**根源**：TCP 有序只保证字节顺序，不表示业务动作具备 request ID、幂等性或整体回滚。批量发送仍是一串独立的卸下、装备、副槽操作。客户端以动画阶段和近似状态匹配结果。

**后果**：迟到的同配方结果可能落入下一次合成；超时重试不能表达“查询上一次是否成功”。套装换装中某个槽位被规则拒绝，其他槽位已经改动，玩家得到半套结果。

**已收窄**：本轮修复本地发送失败和明确拒绝的反馈，并忽略不同配方的迟到结果；这不等于解决同配方重试或组合事务。

**建议**：经济请求带 `request_id + progress_scope + expected_revision`，响应带终态原因和权威 revision；服务端缓存/持久化重复请求结果。换装提交“目标装备方案”，整体校验后原子应用。移动输入继续用轻量序列，不进入经济事务账本。

**请决定**：换装失败时整套拒绝，还是允许明确列出成功/失败项的部分应用？推荐默认整套拒绝。

### A04 · P1：地形事实仍分散在世界和玩法控制器

**位置**：[gameworld.h](../src/Server/Game/gameworld.h) 地图/墙网格；[mutable_terrain_controller.h](../src/Server/Game/gamecontrollers/mutable_terrain_controller.h) `m_tiles/m_dirty/m_removed_wall_rects`；其 `SetTile/WritePlayerScene`；[wall_board.h](../src/Server/Game/wall_board.h)；[map_renderer.js](../web/src/map_renderer.js)。

**根源**：地图资源、可变瓦片、物理墙、EOU 寻路和客户端基线不是由同一个世界级对象管理。F11 的多份 tileset 解析只是这个分裂的一次具体表现。

**后果**：带编辑历史的世界换控制器后，物理状态可能仍在，但新控制器没有旧瓦片历史可重放；绘制、碰撞、寻路也难以以同一 revision 发布。刷子画地表和“建造实体墙”现在不是同一操作，不能未经定义就把所有贴图变更变成碰撞。

**建议**：世界拥有 `CWorldTerrain`，统一资源引用、瓦片修改、碰撞分类、几何 revision 和基线/增量。玩法通过受限编辑接口提出命令；寻路只持有可失效的视图/版本。保留装饰编辑与物理编辑的显式区别。

**请决定**：切换玩法保留哪些地形与建筑，哪些属于本局临时内容？推荐地形默认随世界，玩法专属建筑由模式实例拥有明确清理策略。

### A05 · P1：迁移的事务边界没有覆盖玩法和进度切换

**位置**：[world_transition.cpp](../src/Server/Game/world_transition.cpp) `CWorldTransfer::Move/Possess` 提交后的清理、`SynchronizeGameplayScope` 和通知；[gameworld.cpp](../src/Server/Game/gameworld.cpp) `SetController`。

**根源**：实体索引搬迁已有检查点，但新控制器激活、进度作用域创建、旧玩法资源释放和回调在其他阶段修改状态。旧控制器被替换后再激活新控制器，失败时不能简单拿回旧对象。

**后果**：异常、初始化失败或未来插件失败时，可能形成“身体已经到新世界，规则/进度尚未完成”的状态。已有回调重入保护和句柄解析要保留，但它们不提供跨这些服务的事务。

**建议**：预检阶段准备目标进度作用域和模式资源；帧边界提交所有权；明确提交后回调不可否决，只能发送事件。失败恢复要么回到完整旧状态，要么进入明确的恢复/等待状态。

**请决定**：换玩法失败时保持原玩法，还是整个房间回到等待大厅？推荐优先保持旧玩法，无法安全回滚时显式进入恢复态。

### A06 · P1：限制状态缺少来源，析构直接撤销共享效果

**位置**：[states.cpp](../src/Server/Game/states/states.cpp) `CBanSlotState` 构造/析构、`CDiggingState` 析构、`CCorruptionState` 析构；[flower.cpp](../src/Server/Game/entities/flower.cpp) `SetBanned`；[petal_slot.h](../src/Server/Game/entities/petals/petal_slot.h) `m_banned`。

**根源**：多个来源会改同一个 bool 或阵营。`CBanSlotState` 构造设 true，析构设 false；状态容器本身并不把同槽位的所有来源合并成一个生命周期。

**后果**：两个封槽来源重叠时，先结束的来源可以撤销后一个；玩法禁用与技能禁用也难以区分。简单增加一个“在密道里”判断不能解决所有叠加关系。

**建议**：引入按来源标识的限制/修饰声明，效果由有效声明集合推导；来源释放只撤销自身。状态生命周期、stats 贡献和技能可用性分别表达，保持你要求的“密道不禁技能、states 正常运行”。

**请决定**：同类效果取最大、相加、按优先级覆盖，哪些有免疫规则？推荐按效果类型声明聚合规则，禁止一个通用 bool 承担所有情况。这涉及快照迁移，因此没有只补局部引用计数掩盖剩余来源问题。

### A07 · P1：控制权解绑没有统一的旧身体策略

**位置**：[world_transition.cpp](../src/Server/Game/world_transition.cpp) `Possess`；[player.cpp](../src/Server/Game/player.cpp) `ResetControlledMob/CommitOwnedEntity`；[mob.cpp](../src/Server/Game/entities/mob.cpp) `SetController`。

**根源**：会话改指向新实体，旧实体的输入被清空，但旧的玩家控制器不一定被换回 AI，也没有统一的停放、消失或归还控制协议。

**后果**：旧身体可能继续以玩家控制器类型参与规则判断，却已没有玩家给它输入；队伍显示、技能延续、仇恨和密道通行资格容易各自猜测。

**建议**：明确控制租约与 `OnControlAcquired/Released`，记录默认 AI/旧控制来源；由玩法策略选择还原 AI、停放身体、召回或删除，受控实体关联继续使用完整句柄。

**请决定**：附身离开原身体后，希望原身体留在原地、由 AI 接管，还是隐藏/消失？这是玩法规则，不能替你擅自选择。

### A08 · P2：玩法继承组合带入了不相关规则

**位置**：[ant_kingdom_controller.h](../src/Server/Game/gamecontrollers/ant_kingdom_controller.h) 多继承；[mutable_terrain_controller.h](../src/Server/Game/gamecontrollers/mutable_terrain_controller.h) 与 [pvp_controller.h](../src/Server/Game/gamecontrollers/pvp_controller.h) 对 `COpenController` 的继承；[gamecontroller.h](../src/Server/Game/gamecontroller.h) EOU 专用回调和槽位代理。

**根源**：复用代码与选择规则绑在继承关系中。地形编辑、开放世界刷怪、奖励、永久相位、两人阵营、EOU 命令被组合在一个类层级里。

**后果**：做波次生存、合作 Boss 或四人队战时，继承一个方便的基类可能顺带继承不想要的刷怪、进度或碰撞规则；通用接口也越来越了解某个具体玩法。

**建议**：保留 IGameController 作为模式编排，组合世界级 SpawnDirector、RewardPolicy、ProgressPolicy、Terrain、Visibility 等能力。CExtension 继续承担可选体验能力，不把碰撞和经济安全迁进去。

**请决定**：优先用哪个新玩法验证这个组合边界？推荐先做独立进度的波次生存房，覆盖刷怪、结束、结算、重连四条真实链路。

### A09 · P1/P2：观战、阵营、比赛阶段尚未分开

**位置**：[pvp_controller.cpp](../src/Server/Game/gamecontrollers/pvp_controller.cpp) `AssignTeam/HasMatchTeam`；[deathmatch_controller.h](../src/Server/Game/gamecontrollers/deathmatch_controller.h)；[pvp_controller_base.h](../src/Server/Game/gamecontrollers/pvp_controller_base.h)；[ant_kingdom_controller.cpp](../src/Server/Game/gamecontrollers/ant_kingdom_controller.cpp) `OnQueenSlain`。

**根源**：观战由一个 team 数字代表；`HasMatchTeam` 使用 `team >= start_team`，而可配置 spectator_team 仅按正整数校验。死亡竞赛已能设 team 0，但这不等于已有完整计分、胜负与重开流程；EOU 胜负公告也不能替代 MatchState。

**后果**：默认 2/3 阵营和 spectator=1 之外的参数容易让观战者被当成参赛者。不同模式很难一致表达准备、参赛、阵亡、观战、结算和重开。

**建议**：PlayerRole 与 CombatTeam 分离，规则按能力判定；参数校验同时考虑字段间关系及网络字段范围；增加 Waiting/Ready/Running/Settling/Finished 的明确模式状态。

**请决定**：死亡后是否观战到回合结束、能否中途加入、是否自动重开？配置范围修补只能作为过渡，不能继续用 team 大小推导权限。

### A10 · P1（用于迷雾/PvP 时）：快照优先级会绕过可见性

**位置**：[snapshot_service.cpp](../src/Server/Game/systems/snapshot_service.cpp) `mandatory_squad_entities/mandatory_titan_entity` 与可见性过滤；[gamecontroller.h](../src/Server/Game/gamecontroller.h) `SnapshotEntityVisibleToPlayer`。

**根源**：保底发送的队友/Titan 与普通候选走不同路径；接口注释也明确这些实体不走普通可见性过滤。

**后果**：这可以支持现在的队友 HUD，但将来做潜行、侦察、战争迷雾时，会把“不该知道的位置”发送给客户端。密道 cover 目前是表现层，不能被宣传为服务端防透视。

**建议**：先判断是否允许知道，再决定已允许信息的预算优先级。本人、观战者、队友定位分别由 ViewPolicy 明确授权。

**请决定**：队友和世界 Boss 是否永远可见？推荐作为模式规则，不让 mandatory 自动等于越权可见。

### A11 · P2：社交关系的协议还依赖聊天文字和显示名

**位置**：[squad_extension.cpp](../src/Server/Game/extensions/squad_extension.cpp) `Metadata/InWorld`；[game.js](../web/src/game.js) `consumeSquadMetadata/currentSquadMemberEntities`；[network_module.cpp](../src/Server/Module/network_module.cpp) `QueueChat`。

**根源**：队伍用 `<unseen>(...)` 文本传名单，客户端按显示名匹配且只识别玩家花朵；聊天 player ID 仍压到 uint16，而实体/会话其他链路已有更完整身份。

**后果**：改名、附身非花朵、扩展昵称和较大的玩家 ID 会带来显示歧义；跨房间关系与短暂在线状态也不能靠清空字符串集合来表达。本轮仅修复了现有保留会话的断线/重生关系丢失。

**建议**：专用 SquadState 消息传稳定会话/玩家标识、受控实体句柄、角色及在线状态；聊天只传聊天。ID 位宽和请求结果协议一起规划升级。

**请决定**：社交队伍是全服持久关系，还是每个世界独立的小队？推荐全服 party + 模式内 combat team 分开；是否采用仍由你决定。

### A12 · P2：扩展声明还没有完整的依赖与升级契约

**位置**：[extension_runtime.cpp](../src/Server/Game/extensions/extension_runtime.cpp) `RegisterFactory/Attach/Init/RestoreScope`；[extension.h](../src/Server/Game/extension.h)；队伍元数据的专用同步。

**根源**：能力 Tick/维护/持久化已有通用调度，这点已改善；但依赖要求注册顺序正确，恢复要求持久化扩展数量完全一致，且没有 required/optional、默认初始化及迁移策略。

**后果**：新增可选扩展也可能让旧快照直接无法恢复；插件作者需要知道隐含注册顺序，重入地修改扩展/世界拓扑也没有完整调度契约。

**建议**：构建依赖图并检查循环；区分必须状态与可缺省状态；按扩展版本迁移。注册/卸载与拓扑变更进入帧边界命令，返回的能力视图只允许阶段内借用。

**请决定**：未知/缺失的可选扩展是阻止恢复，还是明确警告并按默认值启用？推荐必须扩展严格失败、可选扩展显式迁移或初始化。

### A13 · P2：内容定义、运行态和跨端展示仍有多份事实

**位置**：[petals_behavior.h](../src/Server/Game/entities/petals/petals_behavior.h)、[mob_behavior.cpp](../src/Server/Game/entities/mob_behavior.cpp)、[talent.h](../src/Server/Game/talent.h)、[petal_info.js](../web/src/petal_info.js)、[talent_data.js](../web/src/talent_data.js)、[hot_reload_snapshot.cpp](../src/Server/HotReload/hot_reload_snapshot.cpp)。

**根源**：部分 ID 和魔法花瓣数据已生成，但数值公式、描述、状态序列化和资源能力仍分散。新增 State 仍需接入中心保存/恢复分支；基础模板数值、实例随机值和运行时覆盖也尚未完全分层。

**后果**：服务器能运行的新内容，未必能正确显示、热重载或解释生效条件；改平衡表时难以说明旧实体应保留哪些数值。

**建议**：为内容建立稳定 ID、定义版本、资源和状态 codec 清单；静态定义生成双端数据，运行时覆盖与派生值分开。服务器向自己控制的玩家提供有效属性/限制原因，避免客户端猜全部协同数值。逐个迁移典型内容，不引入能做所有事的超大 DSL。

**请决定**：热更新后已有实体保留旧模板数值，还是按新定义重算？推荐保留实例随机/持久状态，定义迁移显式选择，不能按字段碰巧混用。

### A14 · P2：结算时序是各层 Tick 的隐式约定

**位置**：[mob.h](../src/Server/Game/entities/mob.h) `CMob::Tick`；[flower.cpp](../src/Server/Game/entities/flower.cpp) `Tick/RebuildFinalStats`；[gameworld.cpp](../src/Server/Game/gameworld.cpp) Tick 各阶段；状态析构副作用。

**根源**：输入、位移、状态、属性重建、花瓣和世界约束分散执行。普通 dirty stats 仍可能在移动/状态之后重建；复杂派生技能还拥有自己的 windup、位移和恢复状态。

**后果**：添加新区域、引导技能、击退或临时属性时，要猜“这一帧该读新值还是旧值”。不意味着当前单线程就有数据竞争，但会增加顺序依赖和回归成本。

**建议**：明确输入意图、移动提交、区域协调、属性视图、动作/伤害、清理、快照的读写边界；需要即时生效的变更在规定屏障提交。分步整理现有 Tick，不一次重写整个物理系统。

**请决定**：效果在触发当帧还是下一帧生效、无敌/封槽结束与攻击的先后如何定义？这些是需要稳定测试的玩法规则。

### A15 · P2：迁移与保存的成本按全局规模放大

**位置**：[world_transition.cpp](../src/Server/Game/world_transition.cpp) `CWorldCheckpoint`；[account_store.cpp](../src/Server/Persistence/account_store.cpp) `QueueAccountSave`；[gamecontext.h](../src/Server/Game/gamecontext.h)；静态 `CAccountDataStore`、`CServer::GetInstance` 的消费者。

**根源**：迁移复制完整世界索引，保存捕获全账户集合；不少游戏逻辑仍能直接访问全服网络/账户静态服务。当前已有依赖收集优化和不可变快照复用，但不能消除这些规模成本。

**后果**：大量单位、频繁跨世界、多人同时修改经济数据时，主线程预算不易控制；也不便于隔离模式测试。这里是已确认的复杂度成本，尚不能宣称已经测出线上卡顿。

**建议**：先记录阶段 p95/p99、迁移实体数/复制字节、保存 dirty 账户数，再采用增量撤销日志和账户增量提交。通过 GameContext 提供窄的地形、进度和消息接口，逐步移除玩法层的全局访问。

**请决定**：近期目标并发和单房间单位数量是多少？保留单进程固定 tick，先解决可测的瓶颈，不先拆微服务或全面替换 ECS。

## 用户体验与玩法选择

| 体验问题 | 代码依据与根源 | 建议与需要决定的规则 |
|---|---|---|
| 死亡后整理装备 | `loadoutEditingBlocked`、`CPlayer::TryEquipPetal/TryUnequipPetal` 明确拒绝死体操作 | 若希望死亡时间用于准备，允许编辑下一次重生的 loadout，和当前尸体分开；否则 UI 明确解释锁定原因。不能只放开客户端 |
| 传送门有时没反应 | `CPortal::AttractAndTransferPlayer` 用 `CInvincibleState` 直接挡传送；没有独立的 portal 进入冷却契约 | 把战斗保护与传送冷却分开，决定自动吸入/按键确认，以及是否跟随队友进入同一实例。新地图应测试相邻/重叠门与返程出生点 |
| 房间谁能改规则/开始 | `CWaitingController::HandlePlayerCommand/HandleSettingCommand` 没有房主/准备机制；参数主要靠命令 | 决定房主、投票或公开房规则；复用 ParamSchema 做结构化设置界面，服务端执行相同授权 |
| 合成等候与失败反馈 | 网络结果和 0～3.5 圈随机动画共同决定展示时机，watchdog 20 秒；已有动画取消及降低动态效果路径 | 明确“请求被拒绝”和“合成随机失败”；决定保留悬念动画还是允许快速模式，展示投入/返还/产出汇总。真实库存更新不能依赖动画完成 |
| 特殊玩法缺少可发现操作 | EOU 逻辑技能伪装成普通花瓣槽，NPC 技能结果多为通用不可用，规则消息大量英文字符串 | 增加 owner action bar/能力描述、冷却/资源/原因提示；模式名称、角色、临时背包状态在 HUD 明示；系统事件用 key+参数，日志与显示文案分开 |
| 房间进度让人困惑 | 目前主要是正式账户或 sandbox 临时账户两种选择 | 明确哪些模式带正式装备、哪些是预设装备、哪些局内成长、哪些奖励结算到正式进度。推荐 ProgressPolicy/ScopeId，而非继续增加名字前缀判断 |

这些建议没有真人可用性测试作为结论，不能据此宣称已经达到“最好体验”。建议用新玩家首次五分钟、短断线重连、较高 RTT、移动端触控以及高密度战斗这几组场景验证：操作到反馈时间、请求终态覆盖、误操作恢复、规则理解率和帧耗时，而不只看画面是否更复杂。

## 后续玩法如何落地

| 方向 | 先需要的基础能力 | 不建议沿用的捷径 |
|---|---|---|
| 经典探索、配装、掉落成长 | 属性说明与权威内容清单、可靠经济操作、清晰死亡/重生契约 | 客户端另写完整数值公式；靠多条装备包拼一个动作 |
| 独立进度的波次生存/肉鸽房 | RunScope、波次状态、局内奖励、退出/重连/结算策略 | 临时改登录账户名；复用开放世界刷怪再四处加开关 |
| 合作 Boss、副本与事件 | 全服 party 与房间 team 分离、共同目标、角色能力、统一结算 | 用队伍聊天名单字符串当参赛身份；队友各自随机进实例 |
| 对战与观战 | MatchState、Role、VisibilityPolicy、明确的中途加入/重开规则 | 只换 team 数字；把画面遮盖当成服务端隐藏 |
| EOU/建造/可破坏地形 | 世界地形服务、寻路 revision、建筑生命周期、结构化操作栏 | 控制器私有一份地图历史；技能长期假装普通背包花瓣 |
| 密道、天气、区域生态 | 来源化限制、地图角色、天气/区域规则消费接口 | 清空 states 来禁 stats；给可选扩展开放任意全服写权限 |

推荐顺序：先确定 A01/A02/A03 的安全与事务契约，再确定地形和进度归属，随后用一个小型波次玩法验证组合接口。表现优化可以并行，但不应继续扩大模糊的数据边界。

## 验证与交付状态

确定问题的验证已完成：Windows Debug 干净构建通过，运行时回归 **85/85**、基础回归 **90/90**；其中新增架构问题定向测试 **8/8**。Node 脚本 **34/34**、Canvas 像素对比 **152 组**通过，客户端依赖及源码卫生检查通过。包含 MySQL 支持的原生 Visual Studio Debug 构建成功。

测试不启动正式服务，不使用真实账号数据库；本地复现与账户写入均在 `artifacts/` 隔离目录。未验证真实 MySQL 运行、Linux/Release、真人可用性和完整联网游戏体验。

后续状态：用户已认可 A01–A15 的整改方向；这些大范围整改尚未实施，A14 的有序 Tick 阶段与当帧规则需要先具体设计。目前按用户最新优先级处理瓦片裁剪与移动接缝，保留上述整改清单。

备份：`FlorrBt-backups/pre-architecture-review-20261025-022747.zip`，基于审查基线的完整源码归档。未提交、未推送。

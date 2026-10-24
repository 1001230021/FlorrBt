# FlorrBt 当前架构关系图

审查日期：2026-10-24。本文已随边界修复更新，记录运行时主干关系，不是拟议重构方案。
实现与验证状态见 [边界修复记录](boundary-repairs-2026-10-24.md)。

本文包含四张 Mermaid 流程图，可在支持 Mermaid 的 Markdown 预览器中查看，也可直接编辑图的源码。

## 图例与范围

- 实线 `-->`：持有或包含，具体所有权形式见边上的说明。
- 虚线 `-.->`：借用指针、引用、临时查询或句柄关联，不转移所有权。
- 粗线 `==>`：继承，箭头从派生类指向基类。
- 实体句柄包含实体 ID、实体 generation、世界 ID 和世界 instance generation。
- 图中省略具体怪物、花瓣和技能的全部派生类；组合节点表示同类成员的归组，不代表另有一个聚合类。
- EOU 寻路板借用在控制器停用和跨世界迁移时撤销，进入兼容世界后重新绑定。

## 1. 服务器、模块与世界

`CServer` 负责装配。`IWorldModule` 拥有世界，`INetworkModule` 拥有玩家会话；玩家不拥有受控实体。

```mermaid
flowchart TB
    S["CServer"]
    M["m_modules<br/>vector of unique_ptr IModule"]
    WM["IWorldModule"]
    N["INetworkModule"]
    UI["IConsoleModule / IServerGuiModule"]
    C["CConsole"]
    G["CGameContext"]
    R["CExtensionRuntime"]
    D["IServerDataStore"]
    A["CAccountDataStore<br/>静态账号存储与后台保存"]
    U["IUniquePetalRegistry"]
    W["CGameWorld"]
    Map["FlorrBtMap"]
    GC["IGameController"]
    P["CPlayer<br/>登录身份 / 独立进度作用域"]
    Services["CSnapshotService<br/>CPlayerLifecycleService"]
    Workers["认证 / 邮件 / 快照工作线程<br/>调试连接服务"]
    Reports["CReportReviewService<br/>有界举报队列与可 join 工作线程"]
    Transfers["SDeferredWorldTransfer 队列<br/>会话与世界句柄；帧边界提交"]

    S -->|"值成员"| M
    M -->|"unique_ptr"| WM
    M -->|"unique_ptr"| N
    M -->|"unique_ptr"| UI
    S -->|"值成员"| C
    UI -.->|"CConsole 引用"| C

    S -->|"unique_ptr"| G
    S -->|"unique_ptr"| R
    S -->|"unique_ptr"| D
    S -->|"unique_ptr"| Reports
    D -->|"unique_ptr"| U
    D -.->|"调用静态服务"| A

    WM -->|"vector of unique_ptr"| W
    WM -->|"vector 值成员"| Transfers
    W -->|"unique_ptr"| Map
    W -->|"unique_ptr"| GC
    W -.->|"CGameContext 指针"| G

    N -->|"vector of unique_ptr"| P
    N -->|"值成员"| Services
    N -->|"unique_ptr"| Workers
    N -.->|"CServer 引用"| S
    P -.->|"借用世界作为实体解析上下文"| W

    G -.->|"INetworkModule 引用"| N
    G -.->|"事件及世界解析接口引用"| S
    G -.->|"CExtensionRuntime 引用"| R
```

主要定义：[server.h](../src/Server/server.h)、[world_module.h](../src/Server/Module/world_module.h)、[network_module.h](../src/Server/Module/network_module.h)、[gamecontext.h](../src/Server/Game/gamecontext.h)、[gameworld.h](../src/Server/Game/gameworld.h)。

## 2. 扩展的所有权与访问关系

`CExtensionRuntime` 是扩展的唯一所有者。聊天、天气属于全局作用域，社交队伍属于世界作用域。世界保存非持有的 `CExtensionView`，不使用 `shared_ptr` 共同拥有扩展。

```mermaid
flowchart TB
    R["CExtensionRuntime"]
    GS["全局 SScope"]
    WS["世界 SScope<br/>world ID + generation"]
    Chat["CChatExtension<br/>IChatService / 命令能力"]
    Weather["CPersistentWeatherExtension<br/>继承天气模拟；实现持久化能力"]
    Squad["CSquadExtension<br/>查询 / 命令 / 持久化能力"]
    EC["CExtensionContext"]
    N["INetworkModule"]
    W["CGameWorld"]
    V["CExtensionView<br/>Runtime 指针 + 世界句柄"]
    GC["世界控制器 / 奖励 / 快照消费者"]

    R -->|"值成员"| GS
    R -->|"unordered_map 值成员"| WS
    GS -->|"unique_ptr CExtension"| Chat
    GS -->|"unique_ptr CExtension"| Weather
    WS -->|"unique_ptr CExtension"| Squad
    R -->|"unique_ptr"| EC

    EC -.->|"IPlayerDirectory 引用"| N
    EC -.->|"IChatDelivery 引用"| N
    EC -.->|"IChatService 指针"| Chat
    EC -.->|"ISquadDirectory 指针"| R

    W -->|"值成员，不拥有扩展"| V
    V -.->|"按世界身份查询"| R
    GC -.->|"临时查询能力接口"| V
    Squad -.->|"借用聊天服务"| Chat
    Chat -.->|"查询世界队伍"| R
```

主要定义：[extension.h](../src/Server/Game/extension.h)、[extension_runtime.h](../src/Server/Game/extensions/extension_runtime.h)、[extension_services.h](../src/Server/Game/extensions/extension_services.h)。

## 3. 实体、控制权、花瓣与状态

`CGameWorld` 独占实体。生物拥有其行为控制器和状态；花朵拥有槽位，但槽内花瓣实体仍由世界持有。`IGameController` 是世界玩法控制器，`IController` 是单个生物的行为控制器。

```mermaid
flowchart TB
    W["CGameWorld"]
    E["CEntity"]
    Index["live / active / cleanup / spatial 索引"]
    P["CPlayer"]
    Mob["CMobBase"]
    AI["IController<br/>玩家输入或 AI"]
    State["CState"]
    Flower["CFlower"]
    Slot["CPetalSlot"]
    Proto["CPetalPrototype<br/>由原型注册表持有"]
    Projectile["CProjectile"]
    Petal["CPetal"]
    Chain["CChainEntity"]
    Damage["CDamageData<br/>账号 + 奖励进度作用域"]
    Drop["CDrop<br/>账号 + 私有奖励作用域"]

    W -->|"vector of unique_ptr"| E
    W -->|"值成员"| Index
    Index -.->|"裸指针或 ID；不拥有实体"| E
    E -.->|"CGameWorld 指针"| W
    P -.->|"SEntityHandle；不拥有"| E

    Mob ==>|"继承"| E
    Flower ==>|"经模板基类继承"| Mob
    Projectile ==>|"继承"| E
    Petal ==>|"继承"| Projectile
    Chain ==>|"继承"| E
    Drop ==>|"继承"| E

    Mob -->|"unique_ptr"| AI
    Mob -->|"vector of unique_ptr"| State
    Mob -->|"vector 值成员"| Damage
    Damage -.->|"仅解析同进度作用域的会话"| P
    Drop -.->|"领取 / 可见性校验"| P
    State -.->|"借用世界解析上下文"| W
    State -.->|"owner handle"| Mob
    AI -.->|"目标 / 召唤拥有者句柄"| E

    Flower -->|"vector 值成员"| Slot
    Slot -.->|"CPetal 裸指针"| Petal
    Slot -.->|"const 原型指针"| Proto
    Projectile -.->|"owner handle"| E

    Chain -.->|"两个端点 handle"| E
    Chain -.->|"lease handle，经接口通知"| Petal
```

主要定义：[entity.h](../src/Server/Game/entity.h)、[player.h](../src/Server/Game/player.h)、[mob.h](../src/Server/Game/entities/mob.h)、[flower.h](../src/Server/Game/entities/flower.h)、[petal_slot.h](../src/Server/Game/entities/petals/petal_slot.h)、[projectile.h](../src/Server/Game/entities/projectile.h)、[state.h](../src/Server/Game/state.h)、[chain.h](../src/Server/Game/chain.h)。

## 4. EOU 的持有链及借用撤销

EOU 的 `CTeam` 管理玩法蚁群，与扩展中的社交队伍不同。生物由世界持有，寻路板仍属于控制器；控制器停用和实体迁移时撤销借用，目标 EOU 控制器负责重新绑定。

```mermaid
flowchart TB
    W["CGameWorld"]
    AC["CAntKingdomController"]
    Board["CWall / CTileBoard"]
    Team["CTeam"]
    Group["CEouPheromoneGroup"]
    Cells["CCellGroupSet"]
    Mob["EOU 生物实体"]
    Cell["Cell / Egg / Queen 实体"]

    W -->|"unique_ptr 世界控制器"| AC
    W -->|"unique_ptr 实体"| Mob
    W -->|"unique_ptr 实体"| Cell

    AC -->|"值成员"| Board
    AC -->|"unordered_map 值成员"| Team
    Team -->|"vector of unique_ptr"| Group
    Team -->|"值成员"| Cells

    Team -.->|"const CTileBoard 引用"| Board
    Group -.->|"const CTileBoard 引用"| Board
    Group -.->|"成员 handle"| Mob
    Cells -.->|"cell 成员 handle"| Cell
    Team -.->|"queen handle"| Cell

    Mob -.->|"m_board 借用：停用 / 迁移时解绑"| Board
    Board -.->|"借用 CGameWorld 指针"| W
```

主要定义：[ant_kingdom_controller.h](../src/Server/Game/gamecontrollers/ant_kingdom_controller.h)、[eou_team.h](../src/Server/Game/gamecontrollers/eou_team.h)、[eou_mob.h](../src/Server/Game/entities/eou_mob.h)、[wall_board.h](../src/Server/Game/wall_board.h)。

## 5. 迁移与场景初始化的帧边界

世界模拟以外发起的迁移仍同步执行；模拟内请求先返回 `deferred`，不能当作已经提交。
玩家登录身份不变，沙盒只切换独立的游戏进度作用域。

```mermaid
flowchart TB
    Tick["逐世界模拟本帧"]
    Queue["模拟中的迁移请求<br/>保存会话 / 世界 / 实体身份"]
    Fence["全部世界完成本帧模拟"]
    Resolve["帧边界重新解析身份"]
    Move["CWorldTransfer<br/>预检、检查点、搬迁与提交"]
    Scope["同步进度作用域<br/>离场与入场通知"]
    Baseline["临时缓冲区构建完整场景初始化<br/>Welcome → 状态 → 地形基线"]
    Retry["保留重试<br/>暂停该玩家新场景快照"]
    Publish["一次入队<br/>恢复正常快照发送"]
    Client["客户端新场景代数<br/>重建可变地图后回放地形"]

    Tick --> Queue
    Queue --> Fence
    Fence --> Resolve
    Resolve --> Move
    Move --> Scope
    Scope --> Baseline
    Baseline -->|"发送队列空间不足"| Retry
    Retry --> Baseline
    Baseline -->|"完整入队成功"| Publish
    Publish --> Client
```

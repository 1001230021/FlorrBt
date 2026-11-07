# 当前架构与持有关系（2026-10-31）

这是本轮修改后的实际结构。实线表示拥有，虚线表示借用、句柄解析或服务调用；接口不延长被引用对象的生命周期。

## 装配、服务与扩展

```mermaid
flowchart TD
    S["CServer"] -->|"m_modules 中的 unique_ptr"| WM["IWorldModule"]
    S -->|"m_modules 中的 unique_ptr"| NM["INetworkModule"]
    S -->|"unique_ptr"| GC["CGameContext"]
    S -->|"unique_ptr"| ER["CExtensionRuntime"]
    S -->|"unique_ptr"| RR["CReportReviewService"]
    S -->|"unique_ptr"| DS["IServerDataStore 适配层"]

    WM -->|"unique_ptr 集合"| W["CGameWorld"]
    NM -->|"unique_ptr 集合"| P["CPlayer"]
    NM -.->|"非拥有 ID 索引"| P
    GC -.->|"IPlayerDirectory / IPlayerUpdates"| NM
    GC -.->|"IWorldEffects / ISceneTransitions"| NM
    GC -.->|"IGameWorldResolver / IGameEventSink / IUniquePetalAccess"| S
    GC -.->|"引用"| ER
    GC -->|"按值"| POP["CNaturalSpawnPopulation"]
    W -.->|"借用上下文"| GC

    ER -->|"作用域内 unique_ptr"| GE["全局扩展 Chat / Weather / Party"]
    ER -->|"按世界身份索引的 unique_ptr"| WE["世界扩展 CSquadExtension"]
    W -->|"按值"| EV["CExtensionView"]
    EV -.->|"运行时指针 + SWorldHandle"| ER
    WE -.->|"IPartyService"| GE
    GE -.->|"目录 / 投递服务"| NM
    RR -->|"有界队列与可等待线程"| JOB["聊天值副本 / 设置 / SPlayerSessionRef"]
    JOB -.->|"主线程重新校验会话后回填"| P
```

扩展实例由运行时唯一持有，世界保存可失效视图。全局 Party 管跨世界社交关系；世界 Squad 只提供本世界成员视图，比赛阵营由玩法规则负责。它们不是同一个 team 编号。

`IServerDataStore` 适配层仍调用进程级账号存储；上图不能理解为已经支持同进程多个独立账号数据库实例。

## 世界、玩法与实体

```mermaid
flowchart TD
    W["CGameWorld"] -->|"unique_ptr"| MAP["FlorrBtMap"]
    W -->|"按值"| T["CWorldTerrain"]
    T -->|"按值"| WALL["CWall"]
    W -->|"按值"| CE["CCombatEventSystem"]
    W -->|"unique_ptr"| WC["IGameController"]
    W -->|"unique_ptr 集合"| E["CEntity"]
    W -->|"非拥有指针及完整句柄"| IDX["空间索引 / 活跃集合 / 实体索引"]
    IDX -.-> E

    WC -->|"组合成员，以 EOU 为例"| OR["COpenWorldRules"]
    WC -->|"组合成员，以 EOU 为例"| MR["CMatchRules"]
    WC -.->|"受限资源准备与地形操作"| T

    E -->|"生物按 unique_ptr 持有"| AI["IController"]
    E -->|"生物按 unique_ptr 集合持有"| ST["CState"]
    E -.->|"世界借用指针"| W
    E -.->|"EOU 生物借用可撤销的 const CTileBoard 指针"| WALL
    P["CPlayer"] -.->|"m_p_world 借用"| W
    P -.->|"SEntityHandle"| E
    E -.->|"拥有者 / chain 端点 / 多节关系句柄"| E
    CE -->|"事件结束前暂存 unique_ptr"| PS["未附着状态"]
    PS -.->|"发布时移交生物"| ST
    CE -.->|"完整实体句柄 + 来源 ID"| E
```

地形历史、墙板和几何版本随世界存活；控制器切换不会销毁其内部地形。EOU 停用/迁移仍撤销并重绑借用，不能仅依赖更长生命周期而省略清理。

花朵槽位管理装备、装填及花瓣句柄，花瓣实体的最终所有权仍在世界。附身的控制器租约保留原 AI 与装备；释放时恢复原 AI，无默认 AI 或默认玩家控制器则非击杀移除。NPC 使用独立临时背包，预置装备不流入玩家正式账号。

## 事务、场景与快照

```mermaid
flowchart LR
    A[换玩法 / 迁移 / 附身] --> B[校验会话世界及资源]
    B --> C[准备进度和目标资源]
    C --> D[提交归属与控制权]
    D --> E[离开进入及状态通知]
    E --> F[Welcome 场景代数]
    F --> G[地形基线与增量]
    G --> H[允许新世界普通快照]
    B -->|失败| OLD[保留原状态]
    C -->|失败| OLD
    D -->|意外提交失败| REC[显式恢复态]
    E -->|异常| REC
```

恢复态暂停不完整世界的普通模拟和快照；这不是任意回调异常的强回滚保证。普通快照恢复先保存回滚备份，恢复态直接载入受此限制，见 [剩余架构问题](architecture-fixes-2026-10-31.md#仍需审查的架构取舍)。

有序帧与战斗事件流程单列于 [有序 Tick](ordered-tick-design.md)。快照对象按依赖顺序创建，建立全部身份后再修复引用。网络快照身份与热重载存档版本是两套边界，不能互相替代。

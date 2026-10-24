# 聊天、天气与组队扩展重构

后续通用能力调度、场景同步及进度隔离修复见
[boundary-repairs-2026-10-24.md](boundary-repairs-2026-10-24.md)。

## 备份与范围

修改前的源码备份：`D:\VSProject\FlorrBt-backups\pre-extensions-20261024`。
1357 个跟踪/未忽略的未跟踪文件逐个通过 SHA-256 校验，包含此前全部未提交修改。
备份不含被 Git 忽略的构建缓存、日志与本地账号数据；这些数据没有被本轮修改。

本轮保持网络协议 `1.0.4`、天气模拟公式、频道路由规则、组队人数与整队合并规则、奖励分配公式。
不运行真实游戏服务、RCON、外部账号数据库或线上压测，不提交或推送。

## 实际持有关系

```text
CServer
  unique_ptr<CExtensionRuntime>
    server scope
      unique_ptr<CChatExtension>
      unique_ptr<CWeatherExtension>
    world ID + instance generation -> scope
      unique_ptr<CSquadExtension>（按配置启用）
  IWorldModule -> unique_ptr<CGameWorld>
    unique_ptr<IGameController>
    CExtensionView（非持有作用域绑定）
  INetworkModule
    IPlayerDirectory + IChatDelivery（查询与传输适配器）
```

扩展运行时是唯一持有者。世界销毁/解除上下文时撤销世界作用域；旧世界代数的 view 失效。
控制器替换不销毁队伍，修改天气显示/刷怪开关也不重建队伍。
服务器析构先解除世界绑定，再停用运行时，借用的玩家/传输服务在此期间仍有效。

`CExtension` 只规定描述符和 attach/detach；Tick、命令、持久化是独立能力接口。
作用域装配校验依赖和命令冲突，停止时先撤销命令，再按逆序 detach。
目前为编译期内置扩展，不承诺动态加载 DLL 或任意在线卸载。

## 功能归属

- 聊天：`CChatExtension` 持有历史、接收者筛选、普通消息与私聊处理。
  网络只编码并发送给已选会话；聊天 DTO 不再嵌于 `CServer`。
  队伍频道通过 `ISquadDirectory` 查询世界作用域，不依赖世界控制器。
  私聊保留原有不进入公共举报历史的策略；队伍功能缺失时只反馈不可用，绝不回退成全服广播。
- 天气：`CWeatherExtension` 是全服唯一模拟实例。网络/刷怪/闪电只读 `IWeatherDiurnalView`，
  管理命令使用 `IWeatherCommands`。天气数据和接口位于 `weather_view.h`，JSON 持久化单独适配，
  纯天气模拟测试仍不依赖 SFML/Jansson。
- 组队：`CSquadExtension` 维护成员、邀请、合并与离队。
  `COpenController` 不再保存队伍。奖励和快照只读 `ISquadView`，命令通过 `ISquadCommands` 修改。
  查询不再压缩路径、裁剪成员或重建索引；这些维护在主线程完成。
  活跃会话集合未变时复用成员索引，避免每次快照重复重建；根查询不再临时分配集合。
  玩家会话代数防止数字 ID 复用继承旧成员或旧邀请；代数随热重载保存。
- `report.cpp` 保留举报审查和现有世界/房间命令，转发扩展命令，不再实现邀请、离队、队伍聊天和私聊。
  RCON/调试权限仍由原认证边界处理，未作为普通扩展命令暴露。

社交队伍与 PvP 阵营、EOU 蚁群不同；阵营、胜负、刷怪和奖励公式仍由玩法实现负责。

## 调度与配置

共享扩展每个服务器 tick 只更新一次，位置保持在控制台/GUI 之后、世界模拟之前。
网络启动并行快照构建前再完成队伍维护；工作线程只调用只读查询，运行时装配/命令/状态修改仅在游戏线程。
借用的 capability/player 指针不得跨调度阶段保存，跨阶段关联用世界/玩家身份解析。

`SWorldExtensionConfig` 独立记录 `squads`、`weather_visuals`、`weather_spawns`。
静态 garden 启用天气显示与刷怪影响；其他静态地图保留原设置；等待世界不启用组队。
动态创建保留原 `weather` 命令别名，其蓝图组装为普通 open controller + 天气配置。
不把世界内组队改变成跨世界组队，也不把全服天气改成各世界独立天气。

## 删除与兼容

已删除天气专用控制器头/实现文件；旧 weather system 头/实现迁移到 extensions。
删除 Server 聊天历史与 SubmitChat/SubmitServerChat、Network 广播路由、
OpenController 组队数据与方法，以及 report 内对应命令实现。
CMake、原生 VS 项目与 filters 同步，不留旧源码编译入口。

旧 `world_controller.weather_diurnal_open` 字符串只作为旧快照/命令蓝图兼容标识，不再对应旧类。
`tools/test_extensions_architecture.mjs` 检查这些边界，防止旧依赖重新引入。

## 快照

热重载容器升级到 **2**，网络协议不变。
新 `extensions` section 保存一次全服天气和每个世界的配置/组队状态；世界记录也保存显式装配配置。
新快照不再输出旧 `weather_diurnal` section，也不再把 squads 写入控制器状态。
容器 2 缺失扩展 section 会拒绝恢复，旧二进制也会因容器版本拒绝，避免静默丢失扩展状态。

容器 1 的天气 section（含旧版本天气迁移）和 controller.squads 仍可导入。
恢复扩展状态在世界/玩家/控制器创建与恢复后进行，错误沿用完整快照回退机制。
世界记录与扩展记录的装配配置必须一致；缺失/重复/未知实例会报错。
组队状态先在临时容器中检查成员、代数、缺失父节点和循环，验证成功才替换现有状态。
聊天历史与待接受邀请保持原有不跨热重载持久化策略。

## 三项后续任务已完成

2026-10-24 后续实现及最终验证见 [world-transactions.md](world-transactions.md)。

1. **迁移/控制权事务化**：只读预检、实体搬迁检查点回滚、提交后通知与重入保护。
2. **复合实体整体迁移**：根/身体/附着弹药的依赖收集和句柄重映射，删除临时拒绝保护。
3. **主世界引用解耦**：网络/上下文按解析器获取当前主世界，支持选择新主世界及恢复不同主世界地图。

回滚覆盖提交前阶段；提交后通知不能再作为拒绝条件，详见后续记录中的回调契约。

## 验证（Windows，2026-10-24）

- CMake Debug 服务器、运行时测试和标准测试目标构建通过（本机配置关闭 MySQL）。
- 原生 Visual Studio Debug 服务器构建通过（启用 MySQL，仅构建，未连接真实数据库）。
- 最终 CTest：运行时 **43/43**，标准 **89/89**，全部通过，耗时 **286.54 秒**。
- 原有 36 项运行时回归全部保留；新增 7 项覆盖全服天气单次更新、队伍隔离、旧 view 失效、
  只读查询、成员及队长 ID 复用、频道/私聊/队伍隔离、控制器更换、新旧快照迁移、失败回退、
  缺失/循环/失效代数队伍状态的原子拒绝。
- 27 个 `tools/test_*.mjs` 脚本通过，包含新架构约束检查及两个编译生产代码替身的脚本。
- 19 个桥接/客户端 Node 子测试、6 个 Python 离线协议测试通过。
- 客户端依赖检查、`git diff --check` 通过；未引入新的客户端依赖环。
- 天气模拟 `.cpp` 与备份进行归一化逐字对比，仅类名/头文件路径改变，模拟公式未变。
- 本轮未执行 Linux/Release 服务器构建、实际 CI 或真实联网压测，不把离线回归视为线上性能保证。

最终日志：`build/codex-extensions-final-ctest.log`、`build/codex-extensions-verified-build.log`、
`build/codex-extensions-final-msbuild.log`。旧实现的恢复来源仍为本文开头的独立源码备份。

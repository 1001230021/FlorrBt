# 花园密道

## 本次规则

- 仅玩家控制器控制的生物忽略密道墙；普通墙和 AI 生物的碰撞保持原规则。
- 密道内保留基础属性和玩家等级属性，不计入花瓣、天赋提供的 stats 修饰。
- 技能不禁用。所有 CState 照常结算、计时和结束，包括中毒、无敌、Pulse、digging；密道不是免伤或清状态功能。
- 保留已学天赋、槽位、装备和账号数据。离开时天赋 stats 恢复；花瓣进行一次完整重新加载，其属性按原有加载/生效规则恢复。
- 花瓣运行态沿用 digging 的清理资产：NotifyCleared、所有者清理、连接/召唤物清理。已独立释放、未由槽位清理管理的实体保持原有生命周期。

## 地图资源与碰撞

`data/tiles/tileset.tsj` 的 125、126、127 号本地 tile 分别对应 `foliage_bri_0`、`foliage_t_0`、`foliage_tl_0`。

以 256×256 图片上的 128×128 小正方形为单位，轮廓分别覆盖除右下外的三个小格、上方两个小格、左上一个小格。采用合并轮廓，内部不生成碰撞边。花园瓦片为 512×512，因此实际小格为 256×256；沿用加载器的 Tiled 水平、竖直和对角翻转转换。

加载边界把图层名转换为 `EMapLayerRole`，其他系统不读取贴图名称或 GID：

- Terrain：普通物理几何。
- Shortcut：同一份几何同时用于条件墙和区域接触检测。
- ShortcutCover：仅视觉，不提取 dirt 瓦片附带的物理碰撞。

移除了 garden 中未使用的桌面绝对路径 tileset 引用，没有删除贴图或用户绘制的地图格子。

## 所有权与接口

继续使用 CGameWorld 已有的地图和墙网格作为唯一几何源，没有另建一份密道碰撞数据。

- CGameWorld 值持有 CEnvironmentSystem；查询接收 EWallQuery 掩码。
- CMobBase 值持有 SCapabilityState，按 Shortcut、Digging 来源组合限制。
- CShortcutRule 是无状态策略，不持有玩家、实体、世界控制器或地图指针。
- IController 暴露控制来源；穿透资格不依赖“已经进入区域”，也不依赖花朵类型或玩家阵营。
- CFlower 管理花瓣挂起/待重载；世界仍是实际花瓣实体的所有者。
- CPlayer::ApplyTalents 只对属性重建事件检查限制，学习记录、槽位容量和非 stats 事件不受此检查影响。

这次没有把密道塞入 IGameController 或 CExtension；此前“将所有可变地形运行态从玩法控制器上移”的完整迁移仍是独立工作。

## 时序与恢复

普通移动在控制器处理前协调当前环境，在位移后、状态和花瓣更新前再次协调。玩家位移先处理普通墙，避免尚未通过实体墙的预期路径误触发密道。技能入口没有新增禁用检查。

墙体预处理只在 CGameWorld::Tick 的模拟作用域内执行，独立调用实体 Tick 不额外运行世界物理。进入/出生回调可能删除实体，回调后的环境协调重新解析当前实体，不继续使用回调前的裸指针。

接触检测包含扫掠，整帧跨过一条密道也会清理旧花瓣并重新加载。链条/携带等后续位移使用上一次检测位置继续扫掠，不从帧起点重复触发。跨世界提交重置检测上下文，回滚不触发环境退出。

进入时锁存身体碰撞半径，离开前不因装备体型缩小而缩小触发范围，避免边界振荡。失去玩家控制时查询完整墙体并寻找安全落点。

花瓣暂停不递减加载计时，也不扣加载法力。digging 与密道叠加时，只有最后一个限制结束才重新加载。状态仍然运行，digging 析构请求的重载可以延后到离开密道。

热重载保存 `shortcut_contact`、`shortcut_probe_radius`、`petals_suspended` 及原有槽位计时；全部实体和控制器恢复后按当前地图重新协调。记录里没有地图几何裸指针。

## 客户端

绘制顺序为普通地形、shortcut、实体、shortcut_cover、天气光照和 HUD。两种密道图层各自拥有透明缓存，和普通地形共用图块内存预算。概览与直接绘制回退也遵守分层；透明图块覆盖旧缓存时先清理独立图层对应区域，避免重复叠加 alpha。

圆形遮罩只擦除密道独立画布，不擦除场景、实体或普通地形。在服务器确认进入后，圆内 shortcut 保留 50% 原 alpha，cover 保留 5%。半径复用花朵默认照明函数，花园为 512，与 horizon、灯泡、天气开关及缩放无关。

异步换图期间不对旧地图应用新场景的揭示状态；死亡、控制对象更换及地图释放也不会保留旧遮罩。调试观察窗口同样绘制独立 cover 层。

表现状态放在实体快照 `ServerEntityFlag::Shortcut`（bit 9），沿用完整/增量快照、实体代数与场景切换机制。客户端与服务端版本同步为 1.0.5，防止旧客户端仍按 cover 物理墙预测。

## 验证入口

- `FlorrBt_RuntimeTests.exe Shortcut`：资源几何、分类物理、stats/天赋/槽位隔离、states、技能、digging 重叠、扫掠、体型变化、控制权变化、迁移回滚和热重载。
- 标准测试 `ShortcutContactFlagRoundTripsAndInvalidatesDeltaBase`：快照编码和增量差异。
- `node tools/test_shortcuts.mjs`：真实加载器/缓存/合成器的离线调用验证，包括 alpha、圆半径、物理预测、缓存生命周期和场景身份。
- `node tools/test_map_loading.mjs`、`node tools/test_render_pipeline.mjs` 及其余既有回归。

本轮浏览器插件没有可用会话，离线绘制测试不替代实机画面验收。未启动正式游戏服、连接真实账号数据库、部署或推送。

### 最终验证结果

- CMake Debug 服务器、运行时测试、标准测试：构建通过。
- 原生 Visual Studio Debug 服务器（含 MySQL 链接）：构建通过，独立输出到 `build/codex-shortcuts-native/bin`。
- CTest：运行时 77/77，标准 90/90，完整套件耗时 653.41 秒。
- Node 回归脚本 31/31；桥接子测试 16/16；Python 离线协议测试 6/6。
- 花园 1484 个密道轮廓的 2952 个四分之一格内部采样，没有与普通墙内部重叠。
- 核对备份后，garden 的地图格子没有变化；仅清理无用 tileset 引用及补齐文件结尾换行。Tiled 原有空白格式未批量重写。
- 未验证 Linux/Release、线上负载或真实浏览器画面。

最终日志：`build/codex-shortcuts-verified-build.log`、`build/codex-shortcuts-native.log`、`build/codex-shortcuts-ctest.log`、`build/codex-shortcuts-node.log`、`build/codex-shortcuts-web-bridge.log`、`build/codex-shortcuts-python.log`。早期单项测试日志可能保留修正前的失败，最终结果以上述完整套件为准。

备份：`D:\VSProject\FlorrBt-backups\pre-shortcuts-20261024-234836`，1377 个文件逐个 SHA-256 校验。

# 服务端复查：Bubble、生命周期与热点（2026-10-02）

> 后续状态：本文下方保留发现时的审查记录。七项确认缺陷及对应热点已在后续实现中处理，详见 [修复与验证](SERVER_FIXES_2026-10-02.md)。

## 本轮范围

除 Bubble 的有效性保护外，本轮不修改下面发现的问题或性能路径。
没有启动游戏服务器、连接数据库、使用真实玩家账号或改变运行中的世界。
这是对当前工作区的定向审查，不是全服务端无遗漏审计，也不是实测 TPS 报告。

## Bubble：防御足够，攻击不构成否决条件

- 当前前后端都保留独立攻防标志，`CBubbleBehavior::OnTick` 只读取 `m_defending` 和原有装填等待时间，不判断 `!m_attacking`。
- 防御单独按、攻防同时按，均在 Bubble 准备好时触发。仅攻击不触发。键盘、鼠标、触控和持续开关最终走同一输入包。
- 新补保护：已经被打到零血或标记移除的 Bubble 不得再给予推力。此前槽位 Tick 在行为执行后才检查死亡，可能出现“碰撞先打破 Bubble，下一 tick 恰好按防御又获加速”。正常装填周期不变。
- `tools/test_bubble_behavior.mjs` 提取真实 `BubbleReload` 和 `CBubbleBehavior` 定义，在离线实体替身上编译执行；覆盖 12 种品质 × 攻击/防御 × Flower/Moon × 就绪状态，以及死亡/移除，共 196 种情况。
- `tools/test_bubble_input.mjs` 验证真实输入刷新和编码，包含同时按住、松开、手机按钮、debug 输入隔离及发包频率上限。

## 隐藏 bug（未修复）

### P1-1：伤害贡献保留离线玩家的裸指针，热重载可访问已释放内存

9 月 25 日报告中的问题仍在。

链路：攻击存活生物 → 玩家被彻底删除 → 生物保存旧 `CPlayer*` → 保存热重载快照时直接解引用。

- `entities/mob.h:21`：`CDamageData::m_player` 是裸指针。
- `entities/mob.cpp:163`：按指针地址累计贡献。
- `Module/network_module.cpp:2942`：DropPlayer 删除玩家对象，未清理其他生物的伤害记录。
- `HotReload/hot_reload_snapshot.cpp:526`：`record.m_player->GetId()` 没有身份/有效性校验。
- `gamecontrollers/open_reward_service.cpp:30` 的地址存在性检查也无法区分被重新分配到同一地址的另一个账号。

建议：稳定账号标识加会话代次，快照保存身份而非解析旧指针；明确断线重连是否继承贡献。单纯“解引用前查地址”不够。

### P1-2：休眠花朵的花瓣被管理员移除后，槽内指针没有摘除

9 月 25 日报告中的问题仍在。

链路：机械花退出活动范围、不 Tick → debug 移除其花瓣 → Cleanup 释放花瓣 → 机械花重新活动/重算属性 → 槽解引用旧指针。

- `DevTools/dev_debug_service.cpp:603`：直接 MarkForDestroy。
- `Game/gameworld.cpp:3103` / `3122`：先调用 OnBeforeRemoved，再释放对象。
- `entities/petals/petal.h`：CPetal 没有覆盖 OnBeforeRemoved 来摘除所属槽引用。
- `entities/petals/petal.cpp:467` / `502`：直接使用槽内非空指针，没有先验证存活句柄。

建议：统一销毁入口保证双向解除挂接，正确调用清理回调，明确是否回装。不要只修 debug 按钮或改成“扣到零血”，否则其他移除路径仍可能留下悬空引用。

### P2-1：输入的接收顺序正确，执行顺序却可能倒退

- `controllers/player_controller.cpp:48`：sequence 只用于拒绝较旧的收包。
- `:87`：按各自 delay 写入未来槽；`player_controller.h:28` 的待执行记录不保存 sequence。
- `:12`：到期就执行，无“已执行较新状态”的检查。
- `Module/network_module.cpp:1478`：允许不同包指定不同的 1～8 tick 延迟。

离线运行生产队列方法：先接收 sequence100 的按下、延迟8；再接收 sequence101 的松开、延迟1。

```text
tick1: attack=0 defend=0
tick8: attack=1 defend=1  ← 旧按下复活，期望仍为0/0
```

目标 tick 回退时即可触发，不需要 TCP 乱序。后续正常心跳会纠正，但中间仍可能出现误移动、误攻防；不能声称所有“松手不停”都由它造成。

建议：移动和动作待执行状态携带 sequence，并在执行时防倒退；或保证实际排程目标 tick 单调，合并被覆盖的状态。须保留序号回绕及旧 Input/Chores 协议语义。

### P2-2：连续碰撞只补了窄阶段，候选搜索仍只看终点

- `Game/gameworld.cpp:179`：SweptProjectileTouches 可以正确判断整段运动。
- `:3011`：普通候选仍按 `终点 + 自身半径 + 大体型门槛` 查找。
- `:3018`：大生物候选也按终点 x/y 范围筛选。
- `Engine/spatial_hash_grid.h:233`：ForEachInRange 再按中心距离过滤。

离线运行真实扫掠函数，投射物从(-600,0)到(600,0)，半径10；目标在(0,0)，半径20。当前门槛200：

```text
endpoint_candidate=0, swept_hit=1
```

实际影响取决于单 tick 位移/碰撞半径，不能据此说所有普通低速花瓣都漏击；高速或配置调整后会再次暴露。

建议：候选阶段查询前后位置的扫掠 AABB/capsule，双方均考虑移动；候选去重后仍使用现有精确检测。进一步按最早碰撞时间处理一次性投射物，避免遍历顺序决定先命中谁。

### P2-3：新掉落合入临近过期的旧堆，寿命和拾取等待随之丢失

- `entities/drop.cpp:150`：MergeNearbyDrops 只比较 type、rarity、owner 和 ID。
- `:167`：只移动 stack 数量；没有维护寿命、惯性窗口、拾取等待。
- `:93`：旧堆按自己的 timer 到期删除整个 stack。

对生产 MergeNearbyDrops 方法做离线验证：旧堆1张剩0.1秒，新堆5张剩30秒、等待拾取0.2秒。合并后：

```text
旧堆变6张，寿命仍0.1秒，等待仍0；新堆被移除。
```

也就是新5张可在约0.1秒内一起过期，或跳过其本应存在的拾取等待。默认0.25秒合并周期带随机初始相位，因此不能依靠“通常晚于0.2秒才合并”保证安全。

建议：惯性/不可拾取窗口未结束的掉落不合并；为不同出生批次保留寿命，或者明确统一到较晚过期时间。后者会延长旧掉落寿命，属于需要选择的规则，不宜暗改。

### P2-4：私人掉落归属仍绑定可复用的连接 ID

9 月 25 日报告中的问题仍在。

- `entities/drop.cpp:118`：拾取只比较 player_id。
- `systems/snapshot_service.cpp:291`：可见性同样只比较 player_id。
- `Module/network_module.cpp:2962` / `2966`：断开后回收 ID，新连接优先复用。

A 完全退出、B 在旧掉落过期前复用其 ID，B 可以看到并拾取 A 的私人掉落。建议使用稳定归属，统一覆盖可见性、拾取、合堆和热重载，不能只修其中一个入口。

### P2-5：Corruption 状态结束可能恢复跨世界前的队伍

9 月 25 日报告中的问题仍在。

- `states/states.cpp:393`：析构无条件 `Team() = m_old_team`。
- `Game/gameworld.cpp:2243`：直接转世界复用原实体/状态，`:2262` 再调用新 controller 入场逻辑。
- `gamecontrollers/pvp_controller.cpp:96`：为新世界写入新队伍。
- 管理端直接改队也能被状态析构覆盖。

建议：区分基础队伍与临时状态覆盖，或者跨世界/改队时明确撤销、重建状态。正常路径经 waiting 世界可能提前清状态，所以不是每次进入 EOU 都必现。

## 优化优先级（有代码热点依据，没有线上耗时实测）

### 1. 账号保存：后台落库不等于主线程没有保存成本

`Persistence/account_store.cpp:911` 每次 QueueAccountSave 先执行 `snapshot.accounts = g_accounts`，复制所有账号及其背包/槽位/图鉴；`:945` 才覆盖待保存快照。存档批次能合并单次操作里的多个 Save，但大量拾取仍可不断复制整份存档。

MySQL 端已经按账号 diff，不能说它每次重写全库。不过 `mysql_account_backend.cpp:532` 每次新建连接并 EnsureSchema，`:322` 对每个变动账号删掉并重插 items/talents/codex，单次库存数字变化也会带来多张表的全量写入。

建议：按 dirty-account/修订号制作不可变增量快照，在逻辑 tick 或短时间窗口合并；后台复用连接、建表只在初始化/恢复连接时做，细分变动字段。保留 generation、失败重试和 Flush 的持久化保证，不能简单“少保存”冒充优化。

### 2. 生态归属与刷新影响：同一批静止生物反复查全部区域

`gamecontrollers/open_spawn_director.cpp:652` 每个刷新轮遍历所有 mob；GetMobSpawnRarityExcess 调 `Shared/spawn_dominance.h:36`，逐个计算所有区域的多边形距离。`gameworld.cpp:2429` 的清理扫描又重复计算。出生候选还遍历所有 dominant_mobs 求抑制。

复杂度约为“生物数 × 全部区域边数”，加“出生尝试数 × 高阶来源数”。建议区域 AABB/空间索引、按 map revision 与位置缓存归属、刷新轮共享结果、影响源也建立空间索引。缓存必须处理移动/传送/地图更新，不能固定用出生区。

### 3. 碰撞候选和压力链：先减少重复查找，保留真实接触

普通实体统一用约自身半径+200世界单位扫描，大量小实体会捞到许多远于实际接触距离的候选。窄阶段与额外2轮速度传递还会重复建立 collision_pair_context、检查所有权/状态。

建议：先统计每 tick 的候选数、实际接触数、接触链时间，再考虑分半径桶的 broadphase 或局部最大半径，以及在同 tick 缓存稳定字段。不能取消普通 mob 碰撞，也不能缓存会被伤害/状态改变的有效性；额外轮次仍不得重复造成伤害。

## 需要确认的设计，不直接判定为 bug

- `CDamageData::m_reset_timer` 设置为120秒并进入快照，但没有实际递减/清理。需要决定贡献是否随脱战超时。
- 图鉴计数位于 `drops.empty()` 检查之后：有效击杀但没掉落就不计数。源码注释明确说按掉落滚动计数，应先决定产品含义。
- 快照工作池按 world 分组，同世界玩家依然串行。大单世界不会因为线程数增加自动加速；安全的方向是复用只读实体快照，而不是直接并行访问仍可变的世界对象。

## 建议执行次序

1. 修复两个 P1 生命周期问题及私人掉落稳定归属。
2. 修输入执行顺序、完整扫掠 broadphase。
3. 明确合堆寿命政策后修掉落时间字段。
4. 先加耗时/数量统计，再做账号增量保存与刷怪空间缓存。

Bubble 的 Release 编译与196例离线行为检查通过。其余发现没有被悄悄修掉；队列、扫掠和合堆用提取的生产方法配合最小实体替身复现，指针与队伍问题是完整调用链静态审查结果。

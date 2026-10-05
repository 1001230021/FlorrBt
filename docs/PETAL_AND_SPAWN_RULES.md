# Moon、Wax、掉落与区域生态调整

## Moon

外圈固定为 **2 世界坐标单位**，不随 Moon 稀有度或自身半径增粗，**仅随镜头缩放**变化（画布 DPR 正常应用，不重复计入）。纹理以原 Mythic Moon 的可见世界半径 **134.4** 为基准：坑纹半径约 10.6～27.8 世界单位，坑纹描边 0.9 世界单位。小 Moon 裁出较少坑纹，大 Moon 显露更多坑纹，不会将同一组坑纹随圆盘一起拉伸。几何图案缓存复用，缩放不生成新位图。

初始圆心在出生位置右侧一个自身半径处，轨道半径为 Moon 当前碰撞半径。默认角速度 `0.2 rad/s`，约 31.4 秒一圈，不读取花朵花瓣转速、攻击范围或 Yin-Yang。无外力时圆心不移动；Bubble/碰撞给予的外部速度会平移圆心，按 `moon_slowdown_time` 指定的时间常数指数衰减。轨道分量与外部速度分开计算，墙体/接触修正不会被下个 tick 拉回。

2026-10-03 数值调整：`moon_slowdown_time` 默认 **0.1 秒**（之前1秒），约0.3秒衰减95%的外来速度；Bubble 对 Moon 的独立推力倍率 `bubble_moon_boost_multiplier` 默认 **0.625**（此前0.25，增加2.5倍）。Bubble 推 Flower 的基础速度不变，Moon 自身慢速绕圈也不受这两个参数影响。持续追加推力仍可推动 Moon，此处描述单次推力停止后的衰减。

2026-10-03 轨道调整：每个花瓣保存一个运行时环绕者引用。Moon 自身的环绕者是 Flower；其他花瓣在有效 Moon 存在时以 Moon 为环绕者，否则以 Flower 为环绕者。花瓣生成位置、轨道中心和基准轨道半径都使用同一个引用，避免生成在 Flower 周围后再跳到 Moon 周围。

除 Heavy 外，花瓣在防御模式都应用 `petal_defend_offset` 的负 reach；Heavy 保持自己的防御轨道规则。Sawblade 在攻击模式只应用 Flower 的基础攻击 reach 偏移 `petal_attack_offset`，不读取最终 `reach`，所以不包含 Third Eye 或天赋附加 reach；防御模式仍应用防御负偏移。

圆心、相位、上次轨道速度和初始化状态随热重载存取；旧快照缺少轨道速度时兼容为原来的纯轨道状态。攻防输入现在保持两个独立按住状态，Shift+Space 或移动端 A+B 同按时 Bubble 也能看到防御，仍按原装填周期触发。已经被打破或标记移除的 Bubble 不会再补发推力。EOU 自己的投掷模式继续只选择一个动作，不同时执行建造和指挥。

```text
set moon_orbit_speed 0.2
set moon_slowdown_time 0.1
set bubble_moon_boost_multiplier 0.625
```

## Wax

生成时沿所属槽位应到位置的方向，先做一小段有墙体检查的偏移，再带初速度滑行。默认总设计距离 24 游戏单位（48 服务端坐标单位），不继承花朵移动方向。

Wax 的质量为 `wax_mass × GetMobMassScaleForLevel(GetLevel(rarity))`，与普通 mob 共用质量增长曲线，ET/Unique 同数值等级。和非 Flower mob 的碰撞先按双方质量以及 `mob_contact_mass_exponent` 计算权重。

被 mob 主动撞到时，额外对 Wax/mob 的质量权重分别乘 `wax_passive_push_fraction` 和 `1 - fraction` 后归一化。默认值 0.08 意味着**等质量时**仍为 8%／92%；实际质量不同就不再强行使用固定比例。Wax 主动撞 mob 使用正常质量权重，不再当作无限质量的推土机；静止障碍保持不动。Wax 之后仍按原有阻尼减速。

**与 Flower 的分支不使用 Wax 质量**：Flower 保持位置，Wax 让路，和原来相同。

```text
set wax_spawn_slide_distance 48
set wax_passive_push_fraction 0.08
```

## 切叶兵蚁

仅切叶兵蚁在原普通兵蚁基础属性上应用倍率，再按原稀有度曲线增长：health ×1.25、mass ×1.25、radius ×1.15、damage ×1.5。普通兵蚁和 LeafPiece 本身不变。四项倍率可用 `mob_leafcutter_soldier_{health,mass,radius,damage}_multiplier` 配置。

## 数量与 Unique 掉落

数量为 1 的卡片不显示右上角数量，包含背包、主侧槽、合成、合成结果和 forge。保留式合成格子在 1 ↔ 多张切换时只更新／隐藏数量节点，不重建卡片。花瓣自身的充能等功能数值不属于卡片数量，不受影响。

Unique 指 **Unique 生物的重复掉落结算**：全部抽取完成后，按同花瓣类型、同稀有度合并成尽量大的 stack，再生成掉落实体。每个玩家独立结算，保持总数量、掉落稀有度、拾取归属和掉落惯性动画不变；超过 uint16 上限时分包。普通生物的掉落生成和既有地面定期合并逻辑不变，Unique 花瓣的唯一持有登记不变。

## 高阶生物保留与刷新抑制

- 根据生物**当前位置**所在刷怪多边形的难度计算 `floor(difficulty / open_rarity_difficulty_scale)`，最小等级为 Common；使用现有数值等级体系（Eternal/Unique 同为 9）。
- 位于多个区域时取最高难度；不在任何区域时，按距离多边形边界最近的区域判断，不按区域中心距离。没有有效刷怪区时不启用新规则。
- 生物高出区域至少 **2 级**，不会因离开玩家视野而被定时清理，并清零离屏计时；只高 1 级不获得这项保护。进入更高区域后重新判断，并非永久免清理标记。
- 玩家及召唤物不获得该保护／生态影响。原有死亡、手动移除、关世界以及 Eternal 等原有激活规则不改。
- 每轮自然刷新收集一次高阶生物影响，休眠生物也参与，但不强制唤醒它们。每个候选出生点独立计算距离衰减后的最强影响，不改变整张地图的刷新参数。
- 默认等级差 2 时，中心附近的刷新接受率约 **57%**，品质分布中心下移 **0.75 级**。更大等级差影响更强、范围更大；边界连续衰减到零。刷新接受率最低 15%，品质中心最多下移 3 级。
- 普通自然生成和远区 Eternal/Unique 生成都受影响；被抑制的尝试直接消耗当轮预算，不在同轮重新抽取规避。原有 ET/Q 同类型全服唯一约束仍保留。

```text
set open_dominant_mob_range 4096
set open_dominant_mob_rate_strength 12
set open_dominant_mob_min_rate 0.02
set open_dominant_mob_level_penalty 1.5
```

例如 Super（8）进入 Mythic（6）区，等级差为 2，获得新增保留和局部抑制；Ultra（7）进入相同区域，不获得它们。

## 检查

独立 C++ 用例覆盖轨道/热重载连续性、不同 tick 率的 Bubble 冲量保留、Wax 质量/静态障碍响应、切叶兵蚁倍率、Unique 数量守恒与上限、向下取整、最近多边形、距离衰减和品质抽样单调性。客户端用例：`node tools/test_moon_cards.mjs`、`node tools/test_bubble_input.mjs`、`node tools/test_craft_matrix.mjs`。仅做离线验证，实机观感／平衡由用户测试。

此次包含服务端修改，需重编服务端；前端需刷新。

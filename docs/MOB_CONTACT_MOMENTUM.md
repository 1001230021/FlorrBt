# 生物碰撞与群体推力

## 原因

普通非 flower 生物此前通过 `ApplyWeakOverlapCollision` 分离，但每次接触都分别扣除双方朝碰撞方向速度的 35%，不考虑质量，也没有把损失的速度传给对方。同向等速跟随也会被扣速。一个 Common 因而可以持续刹住 Primordial。

另外，倒数质量之和被 `epsilon` 抬高，两个超大质量的生物可能无法正常分离；转向代码每帧重写整个速度向量，超速减速代码又直接按受 Web/Pincer 影响的速度上限制动，进一步消掉了外来的推动。

## 新规则

- 接触按有效质量 `mass^exponent` 分配位移和法向冲量，默认指数为 2。10:1 的质量比对应 100:1 的响应差距，100:1 对应 10000:1。只改变碰撞权重，不修改基础质量、Web 的质量判定或碰撞伤害。
- 仅消除双方相向接近的相对法向速度，并将冲量分配给双方；同向等速、正在分离和切向运动不被扣速。不加入弹性反弹或超额能量。
- 普通 mob 保留 0.60 的柔和位置分离，flower 接触保留完整分离。固定障碍和 Wax 的特殊碰撞规则保留。
- 默认在首次碰撞后再正反交替处理两遍接触速度，使压力沿群体传递。额外轮次不重复伤害、状态、生成或位置修正；死亡、转世界及失效的接触会重新过滤。
- 普通 mob、Bee、Firefly 的移动控制通过有限加速度趋近目标速度，不再将外来推力立即压回减速后的上限，也不把整个速度强行旋转到朝向。Web/Pincer 仍限制自身驱动，而非把生物固定在原地。
- 玩家 flower 的原有输入制动行为保留，避免改变客户端预测契约。碰撞后的分离位移额外检测墙体，防止大质量差把小生物直接挤穿墙。

## 可调参数

服务端重新编译并使用新版本后，可通过服务端控制台或 RCON 的 `set` 调整：

```text
set mob_contact_mass_exponent 2
set mob_contact_velocity_passes 3
```

`mob_contact_mass_exponent` 有效范围为 1–4，1 为线性质量权重，越大越突出质量差。`mob_contact_velocity_passes` 有效范围为 1–8，1 为单次接触处理，增加轮数会增加接触链计算量。每帧最多缓存 65536 对用于额外传递的接触，超出后仍进行原本的首次碰撞和伤害处理。

旧参数 `world_mob_overlap_velocity_retention` 现在仅控制陷阱／友方召唤物软接触的相对速度传递比例，不再是每只普通 mob 的扣速率。

## 验证

扩展 `tests/test_circle_contact.cpp`，覆盖质量极差、等质量超大数值、锁定实体、同向与分离速度、有效动量守恒、不增加动能、速度轮次不重复位移、接触链双向遍历，以及 20/60/120Hz 下减速生物保留外来推动。独立测试工程无需启动服务器或访问账户数据：

```powershell
cmake -S tests -B build/contact-tests
cmake --build build/contact-tests --config Release
ctest --test-dir build/contact-tests -C Release --output-on-failure
```

实机平衡仍需观察：同种 Common 与 Primordial 迎面相撞、同向密集队列推动被 Web/Pincer 减速的前排、狭窄墙边拥挤，以及 Dummy／Wax 接触。

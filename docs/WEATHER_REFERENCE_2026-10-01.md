# 雨天表现参照与前端改造

## 参照来源

本次先找了实际截图，再改实现：

- [Steam 截图：Thank you rain for stopping the fires](https://steamcommunity.com/sharedfiles/filedetails/?id=2851926461)：右下角明确显示 `Rain`，用于观察远景雨幕和整体环境压暗。
- [Steam 截图：My campfire is unaffected by the rain](https://steamcommunity.com/sharedfiles/filedetails/?id=2552716888)：右下角明确显示 `Rain`，近景能看到细、短、方向一致的雨线。
- [Steam 截图：circular base under thunderstorm](https://steamcommunity.com/sharedfiles/filedetails/?id=3323867190)：右下角显示雷暴相关信息，仅用于确认雷暴画面仍维持低对比度；不把雷电当成普通雨层。

前两张是 Steam 社区截图，且至少一张明确带有模组环境，所以只提取能稳定确认的视觉特征：

- 雨线细、短、低对比度，整体方向统一，略微倾斜；
- 雨层不会用大块蓝色遮罩盖住地图、实体和 UI；
- 雨天环境略微压暗、去饱和，但地形仍保持可读；
- 截图不能可靠证明水花、地面涟漪或雨滴落点，因此本轮不添加这些元素。

## 实现变化

`web/src/weather_rain.js` 现在维护固定种子的屏幕雨场：

- 一次生成有限数量的远景/近景雨线，运行时只推进时间，不逐帧创建和删除粒子；
- 所有雨线共享一个基础斜率，风向只对整片雨幕施加偏移，不再按屏幕左右扇形发散；
- 远景雨线更短、更淡，近景雨线略长，但两者都保持细线和 `butt` 端点；
- 雨量只控制密度，强度变化由既有的平滑天气值控制；
- 天气重置时清空时间和天气平滑值，重连后不会残留上一局雨幕。

`game.js` 仅负责天气强度、风向和画布尺寸的接入；服务端天气规则、网络包和天气类型没有修改。天气层仍在 HUD 前绘制，所以左上角、卡槽、聊天、血条和小地图不被雨线覆盖。

另外加入了 `web/src/weather_fog.js`：雾使用有限分辨率的块网格绘制，玩家所在屏幕位置作为淡化中心；一定范围内越靠近玩家越透明，远处按雾强度变浓。逻辑网格最多 48×32，放大时关闭平滑，避免把块雾变成模糊全屏滤镜。

## 离线验证

```text
node tools/test_weather_rain.mjs
node tools/test_weather_fog.mjs
```

覆盖固定雨场、雨量密度、统一斜率、风向、30/60fps 等价推进和重置。未启动游戏或服务端；实机仍需确认不同地图缩放、120Hz 和强雨量下的观感。

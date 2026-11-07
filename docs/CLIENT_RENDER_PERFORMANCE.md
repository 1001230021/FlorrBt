# 合成与大视野性能调整

本轮仅修改客户端，保留此前的前端布局、换卡修复和服务端碰撞修改。不以隐藏小实体、降低分辨率、取消生物动画或减少服务器实体数据作为优化手段。

## 代码中确认的重复工作

- 合成矩阵每个格子调用 `getInventoryCount`，间接复制并扫描整份背包及槽位事务；每次刷新又重建全部卡片、重新 append 全部格子。
- debug 绘制时绕过屏幕裁剪，且为每个 mob、花瓣、掉落物都计算 flower 表情；每帧还建立新的实体 Map。
- 缩小时，即使已有更清晰的贴图缓存，也会因为没有完全匹配的尺寸桶而回退到 SVG，等待另一个尺寸缓存完成。
- 同一帧反复使用同一张贴图时不断移动缓存淘汰队列；每次坐标转换重复计算相同的视野比例。
- 地图每帧重新创建、排序分块任务，即使相机还在同一组网格里，所有精确缓存都已经命中。
- 掉帧后的追赶步骤重复更新整份实体列表最多 8 遍，进一步加重卡顿。

## 实现

`craft_matrix.js` 保留格子和已创建的卡片。亮起状态由实际数量判断，数字仍然为实际数量减去暂存数量。零数量的消失动画与合成选择规则不变。使用 IntersectionObserver 预备可视范围和上下 160px 内的卡片，每帧最多创建 12 张，时间片预算 2ms；关闭面板会暂停。已显示卡片的数量直接更新，不重建 SVG 和文字。合成和 forge 共用同一机制。

debug 屏幕裁剪带有贴图外沿余量，完整记录保留以供选择和操作；仅 flower 类实体计算表情。地图与实体使用相同 DPR。视觉副本不再把血祭粒子写入普通游戏的共享粒子池，血祭实际玩法和外观不变。

地图分块计划只在网格覆盖、分辨率或地形版本变化时重建；缓存被淘汰后仍会重新准备。正常命中时不扫描、排序全部旧缓存。精灵缩小优先复用分辨率足够的已有 bitmap，不放大低分辨率缓存；超过缓存尺寸上限时仍使用 SVG。

相同帧的缓存访问、相机比例和渲染数组复用。2026-11-01 起已删除本地预测、速度外推与提前排帧；玩家、远端实体、步态和淡出按完整帧时间推进一次，位置只插值到已收到的权威快照。

## 离线验证

```powershell
node tools/test_craft_matrix.mjs
node tools/test_render_workload.mjs
node tools/test_render_pipeline.mjs
node tools/test_dev_debug.mjs
node tools/test_craft_talent_ui.mjs
node tools/check_client_deps.mjs
```

这些检查验证工作量和正确性，不代表真实设备帧率：630 格合成矩阵重复打开不重建或重排；固定卡片准备数量；消失动画与补货竞态；8000 级实体列表复用；同一贴图 10000 次访问只做一次 LRU 更新；相机参数不变时 10000 次转换只计算一次比例；debug 不提交屏幕外实体但保留屏幕内微小实体。未启动浏览器游戏或服务器进行 FPS/观感测试。

## 2026-10-25：瓦片裁剪与移动接缝回归

确认了两个独立问题：

1. `drawRasterImage` 用 SVG 的 `naturalWidth/naturalHeight` 作为九参数 `drawImage` 的源裁剪范围。只有 `viewBox` 的真实素材在 Chromium 中报告 150×150 默认尺寸，这个裁剪范围与目标 SVG 视口不一致，会放大局部、截掉边缘或留下空白。改为五参数完整图像绘制，保留连续的小数坐标变换；地图文件和碰撞数据没有修改。
2. 缓存未命中的路径直接把每张瓦片画到屏幕小数坐标。仅把裁剪边界对齐到物理像素，无法补回图像外缘抗锯齿留下的半透明像素。改为复用一张最大 1028×1028 的临时画布，先按缓存分辨率合成相同图层和邻接像素，再走统一的分块绘制路径。每个缓存实例最多保留一张临时画布，销毁时释放。

原来的 Canvas 位图测试未覆盖 Chromium 的 SVG 解码/绘制差异；加入真实浏览器测试，通过实际素材、生产绘制函数以及完整花园加载器验证：

```powershell
npm --prefix build/render-qa install --no-save @napi-rs/canvas playwright-core
node tools/test_map_seams.mjs --pixels --browser
node tools/test_render_pipeline.mjs
node tools/test_map_loading.mjs
node tools/test_shortcuts.mjs
```

浏览器测试默认使用已安装的 Edge/Chrome，也可设置 `FLORRBT_BROWSER_EXECUTABLE`。运行于独立 headless 浏览器，所有请求拦截到本地测试资源，不连接游戏服务器或账号。覆盖真实 SVG 的八种翻转、32–512 像素尺寸、0.75–4 倍 DPR、小数位移、缓存命中/未命中、缩放旧缓存、半透明叠层和纹理连续性。结果和花园边界截图写入忽略目录 `artifacts/map-render/`。这些检查不代替真实设备的帧率测试。

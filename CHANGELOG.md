# CHANGELOG

本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## v0.3.0 — OpenRA 对齐（2026-10）

### 修复
- **子格精度投影**（`ProjectSub`）：修复单位移动"跳格子"——`snapshot.h` 之前用 `ProjectCell(pos.ToCell())` 把子格坐标取整到格子，渲染位置只有格子精度。这是跳格/顿挫的根因。
- **坦克只有炮台**：RA 的坦克是 `idle`（帧 0-31 车体）+ `turret`（帧 32-63 炮塔）分离 SHP，图集生成时合成为完整坦克。
- **矿车斜向被裁剪**：改用完整 SHP 画布（`ShpTDSprite.Size` 语义），不再猜测裁剪框。
- **黑影/黑点**：去掉尸体渲染（之前画成黑色半透明剪影/血渍）。
- **走/站切换闪烁**：战斗状态（Attack）保持 run 帧，不切 stand。

### 改进
- 地形：菱形 tile（OpenRA `TerrainSpriteLayer`）+ 按屏幕 Y（x+y 深度）排序。
- 相机：`ClampToBounds`（OpenRA `Viewport.mapBounds`）+ zoom 1.8。
- 移动：先转向后移动（`MoveFirstHalf/MoveSecondHalf`），转向 180°/tick（`TurnSpeed 512`）。
- 单位速度：步兵 160 / 矿车 120 / 坦克 100 子格/tick。
- AI：集结 → 推进（`SquadManagerBotModule` 语义），4 秒集结后整队冲锋。
- 资源加载：工作目录自动定位到含 `assets/` 的目录（任意路径启动可用）。

### 新增
- `.github/workflows/ci.yml`（Windows + Linux 矩阵：构建/测试/lint/autotest）
- `tools/autotest.cpp`（AI 对战回归 + 种子对称性）
- `tools/autotest_visual.cpp`（观战可视化 + 游戏内截图）
- `openra-reference` skill（参考 OpenRA 源码的工作流）

## v0.2.0 — 内容与联机（2026-09）

- M18 交互反馈：选择框、小地图点击、攻击特效、命令标记
- M19 内容：防御塔（自动攻击）、科技升级（攻/防/速）
- M20 战役：6 关、选关界面、结算统计、N/R/ESC 导航
- M21 存档：`World::SaveToText/LoadFromText`，F5/F9
- M22 联机：`rts_game --host/--client` 人类对战（锁步）

## v0.1.0 — 引擎地基（2026-08）

- M0 地基：Q16.16 定点、Xoshiro256\*\*、2D 格子层、CMake/Ninja
- M1 世界模型：地图（地形/资源/迷雾）、RTSMAP 序列化、等距投影（精确往返）
- M2 模拟循环：组件式实体、活动状态机、命令队列、回放
- M3 渲染：Win32 + OpenGL 1.1、精灵批处理、相机、快照（65k 精灵 @120FPS）
- M4 寻路：A*（octile）、路径跟随、分离（1000 单位 0.8ms/tick）
- M5 数据驱动：YAML 规则、采矿经济、建造/生产/战斗
- M6 AI：确定性命令驱动 AI
- M7 联机：Winsock TCP 锁步、命令同步、哈希校验（5000 tick 一致）
- M8 工具链：地图编辑器、规则 lint、CI
- M9 可玩 Demo：玩家 vs AI、选择/命令、小地图、Apache-2.0

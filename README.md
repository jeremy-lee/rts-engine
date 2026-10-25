# RTS Engine

开源 2D 实时战略（RTS）游戏引擎。**C++20**，确定性模拟，渲染与逻辑彻底解耦。

- 许可证：Apache-2.0
- 平台：Windows（渲染/联机）/ 全平台（headless 模拟）
- 状态：**M0–M9 全部里程碑完成**

## 状态

**M0–M15 全部完成**（116/116 测试通过）。

| 里程碑 | 内容 |
|---|---|
| M0 地基 | 定点数/随机/格子层/CMake/CI |
| M1 世界模型 | 2D 网格地图、地形/资源/迷雾、等距投影 |
| M2 模拟循环 | 组件式实体、活动状态机、命令队列、回放 |
| M3 渲染 | Win32+OpenGL、精灵批处理、相机、快照 |
| M4 寻路 | A*（octile）、路径跟随、单位分离 |
| M5 数据驱动 | YAML 规则、采矿经济、建造/生产、战斗 |
| M6 AI | 确定性命令驱动 AI |
| M7 联机 | Winsock TCP 锁步、命令同步、哈希校验 |
| M8 工具链 | 地图编辑器、规则 lint、GitHub Actions CI |
| M9 可玩 Demo | 玩家 vs AI、点选/框选、小地图 |
| M10 HUD/UI | 位图字体、命令面板、资源条、血条、进度条 |
| M11 建筑占格 | 占用层、4×4 验证、寻路避让、摧毁释放 |
| M12 战争迷雾 | 三态可见性、视野揭示、敌方隐藏 |
| M13 音效 | miniaudio、合成音效、BGM、静音 |
| M14 渲染后端 | 软件光栅化后端、F1 切换 |
| M15 战役关卡 | 关卡 YAML、波次、胜负、结算 |

## 快速开始

```bat
build.bat

:: 可玩 Demo（玩家 vs AI）
build\rts_game.exe

:: 全部测试
build\rts_tests.exe

:: 可视化 AI vs AI
build\rts_win_app.exe

:: 地图编辑器
build\rts_editor.exe

:: 数据校验
build\rts_lint.exe data\rules.yaml

:: AI 自动对战测试（完整对局 + 确定性）
build\rts_autotest.exe --runs 3
```

### 可玩 Demo 操作

| 操作 | 功能 |
|---|---|
| 左键点选 / 拖拽框选 | 选择己方单位 |
| 右键 | 移动（空地）/ 攻击（点中敌方） |
| `1` / `2` / `3` | 全选步兵 / 坦克 / 采矿车 |
| `B` | 在相机中心建造兵营（300 资源） |
| `P` | 所有空闲兵营生产步兵（50/个） |
| `WASD`/方向键 | 平移相机 |
| 滚轮 | 缩放 |
| 右下角 | 小地图 |

### 联机

```bat
:: 主机（AI 玩家 0）
build\rts_net.exe host 5000

:: 客户端（另一台机器）
build\rts_net.exe client 127.0.0.1 5000
```

双端最终哈希必须一致 —— 这是锁步正确性的证明。

## 设计原则

1. **逻辑层禁止浮点**：坐标 `int32` 子格（1 格 = 1024 子格），速度 Q16.16 定点
2. **固定步长 30Hz**：渲染可变帧率 + 插值
3. **确定性**：Xoshiro256** PRNG、命令队列、状态哈希（联机/回放校验）
4. **渲染可替换**：`render/renderer.h` 纯虚接口；`NullRenderer` 为默认 headless 后端
5. **依赖方向**：`core ← sim ← app`；`render` 只读快照
6. **数据驱动**：单位/建筑/武器定义在 `data/rules.yaml`，代码只写机制

## 目录

```
core/       零依赖：定点数、数学、随机、tick、格子层
sim/        世界、地图、实体、活动、寻路、经济、AI、回放
render/     渲染接口、投影、快照、精灵批处理、OpenGL 后端
platform/   Win32 窗口
net/        Winsock TCP + 锁步会话
tools/      地图编辑器、规则 lint
app/        可玩 Demo / AI 对战 / 联机 / headless 验收
tests/      87 个单元测试（含确定性/联机回归）
data/       rules.yaml（玩法数据）、map_export.map（编辑器导出）
docs/adr/   架构决策记录
```

## 文档

- `ENGINE-DESIGN.md` — 权威设计文档（不变量/模块边界/路线图）
- `CONTRIBUTING.md` — 贡献指南（确定性红线）
- `docs/adr/` — 架构决策记录

## 路线图（后续）

下一轮（M10–M15）的完整计划见 **ROADMAP.md**：

- M10 HUD 与 UI 系统 
- M11 建筑占格与寻路避让 
- M12 战争迷雾与视野 
- M13 音效与音乐 
- M14 渲染后端升级（The Forge / Sokol）
- M15 战役/关卡与内容管线 

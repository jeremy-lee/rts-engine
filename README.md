# RTS Engine

开源 2D 实时战略（RTS）游戏引擎。C++20，确定性模拟，渲染与逻辑解耦。

## 状态

**M2 — 模拟循环**（完成）。M0 地基 + M1 世界模型已验收；M2 增加组件式实体存储（EntityStore）、活动状态机（移动/攻击/待命）、命令分发、确定性回放。

测试：47/47 通过。

## 快速开始

```bat
build.bat
build\rts_app.exe        :: headless 确定性自检
build\rts_tests.exe      :: 单元测试
```

## 设计原则

1. **逻辑层禁止浮点**：坐标用 int32 子格（1 格 = 1024 子格），速度用 Q16.16 定点。
2. **固定步长**：逻辑 30 Hz，渲染可变 + 插值。
3. **确定性**：Xoshiro256** PRNG，命令队列，状态哈希可校验。
4. **渲染可替换**：`render/renderer.h` 纯虚接口；`NullRenderer` 是默认 headless 后端。
5. **依赖方向**：`core ← sim ← app`；`render` 只向上读数据。

## 目录

- `core/` — 零依赖：定点数、数学、随机、格子层、tick
- `sim/` — 世界、命令、固定步长模拟
- `render/` — 渲染抽象接口 + NullRenderer
- `app/` — headless 演示程序
- `tests/` — 单元测试与确定性回归
- `docs/adr/` — 架构决策记录
- `ENGINE-DESIGN.md` — 权威设计文档

## 路线图

M0 地基 → M1 世界模型 → M2 模拟循环 → M3 渲染后端 → M4 寻路 → M5 玩法 → M6 AI → M7 联机 → M8 工具链 → M9 发布

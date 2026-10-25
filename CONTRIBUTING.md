# 贡献指南（CONTRIBUTING）

感谢你愿意为 RTS Engine 做贡献！

## 开发环境

- **C++20** 编译器：MSVC 2022（Windows）/ GCC 13+ / Clang 16+
- **CMake** ≥ 3.20
- **Ninja**（推荐）

## 构建

```bat
:: Windows（自动检测 VS 安装路径）
build.bat

:: 或手动
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物：
| 程序 | 说明 |
|---|---|
| `rts_app` | headless 验收（确定性/经济/AI 对局） |
| `rts_tests` | 全部单元测试（87+） |
| `rts_win_app` | 可视化 AI vs AI |
| `rts_game` | **可玩 Demo（玩家 vs AI）** |
| `rts_net` | 联机（host/client） |
| `rts_editor` | 地图编辑器 |
| `rts_lint` | 数据校验 |

## 代码规范

- 命名：`PascalCase` 类型、`camelCase` 函数/变量、`k` 前缀常量、`m_` 前缀成员
- 头文件 `#pragma once`；包含顺序：本项目 → 标准库
- **逻辑层（core/sim）禁止浮点**：坐标 int32 子格、速度 Q16.16、数学整数化
- 逻辑层禁止 `new/delete`、`rand()`、wall-clock
- 提交前：`rts_tests` 全过 + `rts_lint data/rules.yaml` 通过

## 提交规范

```
M<N>: <简短描述>

- 要点 1
- 要点 2
```

示例：`M4: pathfinding - A* with octile heuristic`

## 新增内容

- **新单位/建筑**：改 `data/rules.yaml`（无需改代码），跑 `rts_lint`
- **新机制**：加到 `sim/`，必须配单元测试
- **新地图**：用 `rts_editor` 编辑并导出 `RTSMAP` 格式

## 确定性红线

任何影响模拟的逻辑改动，必须保证：
1. 整数运算（或严格同序浮点）
2. 遍历顺序固定
3. `StateHash` 覆盖新状态
4. 测试：同输入两次运行哈希一致（参考 `test_determinism.cpp`）

破坏确定性的 PR 会被拒绝。

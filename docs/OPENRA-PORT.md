# OpenRA 基础移植记录

本项目以 [OpenRA](https://github.com/OpenRA/OpenRA)（GPL-3.0）为参考实现，逐阶段移植其**经过验证的基础设施**。
所有移植遵循两条原则：

1. **语义一致**：类型/算法与 OpenRA 源码一致（引用具体文件）。
2. **确定性优先**：不使用浮点三角函数、不使用不确定的容器遍历。

## 阶段总览

| 阶段 | 内容 | 状态 | 测试 |
|---|---|---|---|
| Phase 1 | 32 朝向精灵 + WAngle facing | ✅ 完成 | 6 |
| Phase 2 | WDist / WVec 世界距离与向量 | ✅ 完成 | 6 |
| Phase 3 | Sheet / SpriteRect（图集与 UV 语义） | ✅ 完成 | 4 |
| Phase 4 | Trait / Activity 架构评估 | ✅ 评估完成（不重构） | — |
| Phase 5 | 文档 + 全量验证 | ✅ 完成 | 145 总计 |

---

## Phase 1：32 朝向精灵 + WAngle facing

### 参考
- `OpenRA.Game/WAngle.cs` — 角度类型（1024 = 360°）
- `mods/ra/sequences/vehicles.yaml` — 坦克 `idle: Facings 32` / `turret: Start 32, Facings 32`

### 实现
- `core/wangle.h`：`WAngle`（`angle % 1024`，`FromFacing(f)=f*4`，`Sin/Cos` 查表，`Lerp` 处理环绕）
- `sim/entity.h`：`Movable.facing` 从 `uint8_t`（8 方向）改为 `WAngle`
- `sim/activity.cpp`：转向按 `WAngle` 最短路径，每 tick 最多 512（180°）
- `assets/cnc_atlas.png`：**5120×2176**（64 列 × 34 行，格子 80×64）
  - 行 0/1：坦克 32 朝向 × 2 阵营（**车体帧 0-31 + 炮塔帧 32-63 合成**）
  - 行 2-15：步兵 8 朝向 × 7 帧 × 2 阵营（RA 步兵原生 8 朝向）
  - 行 16-29：矿车 8 朝向 × 7 帧 × 2 阵营
  - 行 30-33：建筑
- `render/cnc_assets.h`：`GetUV(id, team, WAngle facing, frame, ...)`
  - 坦克：`col = (32 - angle/32) % 32 + team*32`
  - 步兵/矿车：`col = (8 - angle/128) % 8 + team*8`，`row += frame*2 + team`

### 关键经验
- **RA 的 SHP 是分离的**：`2tnk.shp` 帧 0-31 = 车体，32-63 = 炮塔；只画车体看起来"只有炮台"。
- **步兵素材只有 8 朝向**，32 朝向仅坦克/炮塔可用。

---

## Phase 2：WDist / WVec

### 参考
- `OpenRA.Game/WDist.cs` — 1D 距离，1024 = 1 格
- `OpenRA.Game/WVec.cs` — 3D 世界向量

### 实现
- `core/wdist.h`：`WDist { length }`、`FromCells`/`ToCells`/`LengthSquared`、比较与算术
- `core/wvec.h`：`WVec { x, y, z }`、`FromCell`/`ToCell`、`Length()`（整数 Isqrt，确定性）、`Dot`

### 与现有类型的关系
| OpenRA | 本项目 | 说明 |
|---|---|---|
| `CPos` | `CellCoord` | 格子坐标（int32） |
| `WPos` | `SubCoord` | 世界位置（1024/格） |
| `WDist` | `WDist`（新增） | 类型安全的距离 |
| `WVec` | `WVec`（新增） | 类型安全的向量 |
| `WAngle` | `WAngle`（新增） | 1024 = 360° |

---

## Phase 3：Sheet / SpriteRect

### 参考
- `OpenRA.Game/Graphics/Sprite.cs` — `Sprite`（Bounds + Offset + Size + inset UV）
- `OpenRA.Game/Graphics/SheetBuilder.cs` — 图集打包

### 实现
- `render/sheet.h`：
  - `Sheet { texture, width, height }`
  - `SpriteRect { x, y, w, h, u0, v0, u1, v1 }`
  - `MakeSpriteRect(sheet, x, y, w, h)` — **内含 1/128 像素 inset**（与 OpenRA `Sprite` 构造函数一致，避免 GPU 采样越界）
- `render/cnc_assets.h`：`GetUV` 改用 `MakeSpriteRect`（UV 预计算含 inset）

### 为什么重要
`GL_NEAREST` 采样时，UV 若正好落在图集格子边界，可能采到相邻格的首/末像素（出现 1px 杂色）。
OpenRA 用 1/128 像素的 inset 规避，本项目采用同一策略。

---

## Phase 4：Trait / Activity 架构评估

### OpenRA 架构
```
Actor
 ├─ Trait（能力，接口 + 组合）
 │   ├─ Mobile（移动）
 │   ├─ Health（生命）
 │   ├─ Armament（武器）
 │   ├─ Production（生产）
 │   └─ ...
 └─ Activity（状态机，基类 + Tick）
     ├─ Move
     ├─ Attack
     └─ Harvest
```

### 本项目现状
| 方面 | OpenRA | 本项目 | 评估 |
|---|---|---|---|
| 实体 | Actor + Trait 组合 | `Actor` 结构体 + 组件 flags + 字段 | **可接受**（字段少，直接结构更简单） |
| 能力扩展 | 自定义 Trait | 修改 `Actor` 结构体 | **差距**（但当前 6 种能力足够） |
| 活动 | `Activity` 基类，`QueueChild` 组合 | `ActivityType` 枚举 + switch | **可接受**（状态少：Idle/MoveTo/Attack/Harvest） |
| 配置 | `TraitInfo`（YAML 反射） | `UnitDef`/`BuildingDef`（手写解析） | **可接受**（规则字段少） |

### 结论：**不重构**
理由：
1. 当前 `Actor` 结构体只有 ~40 个字段，**字段访问零开销**；Trait 组合会引入虚函数/查找开销。
2. `ActivityType` 枚举只有 4 个状态，`switch` 比虚函数更直观且**可预测**。
3. 重构会触及 **145 个测试** 与所有存档/回放兼容性，**风险远大于收益**。

### 若未来需要扩展
建议的**渐进路径**（不破坏现有）：
1. 新能力 → 加 `kComp*` flag + 字段（当前模式）。
2. 活动 → 加 `ActivityType` 枚举值 + switch 分支。
3. 规则 → 扩展 `UnitDef` 字段（YAML 解析已支持）。
4. 只有当**动态能力组合**（运行时增删能力）成为需求时，才引入 Trait 系统。

---

## Phase 5：验证

```
build.bat          → BUILD OK（零错误）
rts_tests          → 145/145 passed
rts_autotest       → determinism/economy/production PASS
rts_autotest_visual → 游戏内截图验证（32 朝向坦克、菱形地形、平滑移动）
```

## 后续可移植项（按价值排序）

| 项 | OpenRA 参考 | 价值 | 风险 |
|---|---|---|---|
| 迷雾/视野 | `Traits/RevealsShroud.cs`、`Shroud.cs` | 高 | 中 |
| 建造动画 | `Traits/Buildable.cs`、`ProductionQueue` | 中 | 低 |
| 音效系统 | `OpenRA.Game/Sound/` | 中 | 低 |
| 路径平滑 | `Pathfinder/` + `Move` 的弧线 | 中 | 中 |
| Trait 系统 | `TraitsInterfaces.cs` | 低（当前） | 高 |
| 完整 SHP 加载器 | `ShpTDLoader.cs` | 低（图集已预生成） | 中 |

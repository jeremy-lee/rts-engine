# 资源导入流程（配置驱动）

引擎的美术资源只有两个文件：

| 文件 | 内容 |
|---|---|
| `assets/cnc_atlas.bmp` | 图集（32 位 RGBA，stb_image 可读） |
| `assets/cnc_atlas.txt` | 元数据（每个精灵的 row / bbox / 朝向数 / 帧数） |

两者都由 **`rts_import`** 从帧 PNG + 配置生成，**不需要改代码**。

## 一键导入

```bat
build\rts_import.exe <framesDir> assets\sprites.txt assets\cnc_atlas.bmp assets\cnc_atlas.txt
```

## 配置格式（assets/sprites.txt）

```
cell 80 64
cols 64
team 2
team0 70 90 255
team1 235 60 60

sprite <name> <prefix> <facings> <frames> <mode> <standStart> <runStart> <turretStart> <row>
```

### 帧索引约定（RA SHP）

```
stand = standStart + facing
run   = runStart + facing*6 + (frame-1)
turret= turretStart + facing
```

### RA 单位参考值

| 精灵 | prefix | facings | frames | mode | stand | run | turret | row |
|---|---|---|---|---|---|---|---|---|
| Infantry | e1 | 8 | 7 | unit8 | 0 | 16 | -1 | 2 |
| Tank | 2tnk | 32 | 1 | tank32 | 0 | -1 | 32 | 0 |
| LightTank | 1tnk | 32 | 1 | tank32 | 0 | -1 | 32 | 1 |
| Harvester | harv | 8 | 7 | unit8 | 0 | 16 | -1 | 16 |
| Barracks | tent | 0 | 1 | building | 0 | -1 | -1 | 30 |
| Factory | weap | 0 | 1 | building | 0 | -1 | -1 | 32 |

## 从 RA 的 SHP 提取帧

```bat
OpenRA.Utility.exe ra --png <name>.shp temperat.pal
```

导入工具会自动：裁剪到内容 bbox（OpenRA TrimmedFrame 语义）、阵营色替换、坦克车体+炮塔合成、居中入格、输出元数据。

## 加新素材的步骤

1. 帧 PNG 放进目录（命名 `<prefix>-<%04d>.png`）
2. 在 `assets/sprites.txt` 加一行 `sprite ...`
3. 运行 `rts_import`
4. 重启游戏/编辑器（无需重新编译）

## 注意事项

- 元数据里的 w/h 是**所有帧的最大 bbox**——不要手改小，否则斜向/动画帧会被裁。
- BMP 无压缩（约 43MB）。可转 PNG 后用 .png（stb_image 两种都支持）。
- 帧缺失时导入工具打印 `[import] missing <path>` 并跳过。
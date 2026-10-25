# C&C 素材来源说明

本目录的 SHP/调色板文件从 OpenRA（C&C 泰伯利亚黎明）提取，仅作**占位美术**。

## 重新生成图集

`powershell
# 1. 提取（需要 OpenRA.Utility.exe）
cd assets_raw
OpenRA.Utility.exe cnc --extract conquer.mix e1.shp e3.shp mtnk.shp ltnk.shp harv.shp pyle.shp fact.shp
OpenRA.Utility.exe cnc --extract temperat.mix temperat.pal

# 2. SHP → PNG（多帧）
foreach ($n in @('e1','e3','mtnk','ltnk','harv','pyle','fact')) {
    OpenRA.Utility.exe cnc --png "$n.shp" temperat.pal
}

# 3. 取 stand 首帧拼图集（80x64 格，8 列）
#    帧 0000 = stand 朝向 0
#    见 tools/build_atlas.ps1
`

## 图集布局（assets/cnc_atlas.png）

| 格 | 精灵 | 帧 | 尺寸 |
|---|---|---|---|
| 0 | 步兵 e1 | 0000 | 50×39 |
| 1 | 火箭兵 e3 | 0000 | 50×39 |
| 2 | 中型坦克 mtnk | 0000 | 36×36 |
| 3 | 轻型坦克 ltnk | 0000 | 24×24 |
| 4 | 采矿车 harv | 0000 | 48×48 |
| 5 | 兵营 pyle | 0000 | 48×48 |
| 6 | 建造厂 fact | 0000 | 72×48 |

## 版权说明

素材版权归 EA（C&C）所有，OpenRA 以免费分发方式提供。
本项目仅作技术演示占位；正式发布需替换为原创/授权美术。

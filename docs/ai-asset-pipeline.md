# AI 生成资产 → 引擎（闭环）

这条链路已经验证通：ComfyUI 出的图能变成引擎里能显示的 sprite。

## 前提

ComfyUI 服务（Desktop 版，端口 8189）：

```powershell
$env:PYTHONIOENCODING = "utf-8"
$env:PYTHONUTF8 = "1"
cd "D:\Comfy-Desktop\ComfyUI-Installs\Minimax\ComfyUI"
.\.venv\Scripts\python.exe main.py --port 8189 --listen 127.0.0.1 `
    > D:\rts-engine\comfy.log 2> D:\rts-engine\comfy.err
```

**两个关键点**（踩过坑）：

1. 必须有 `PYTHONIOENCODING=utf-8`，否则非 ASCII 输出会 `UnicodeEncodeError`
2. **stdout 和 stderr 都要重定向到文件**。Hidden 进程没有真控制台，tqdm 写进度条会抛 `OSError [Errno 22]`，直接让 KSampler 崩掉

## 步骤

### 1. 生成

```powershell
.\tools\comfy_test.ps1 `
    -Ckpt "v1-5-pruned-emaonly-fp16.safetensors" `
    -Prompt "pixel art, isometric military barracks building, Red Alert style, red walls with metal trim, single building centered in frame, plain solid magenta background, RTS game asset, limited palette, sharp pixels" `
    -Negative "photorealistic, 3d, blurry, anti-aliasing, smooth gradients, trees, grass, terrain, water, multiple buildings, text, watermark" `
    -Out "gen_barracks" -Width 512 -Height 512 -Steps 30 -Cfg 8.0
```

**提示词要点**（这是效果差距最大的地方）：

- `(pixel art:1.4)` 之类**权重语法**必须用
- `isometric` 不是 `top-down`
- `Command and Conquer Red Alert 2 style` 锚定风格
- 负面要排除 `anti-aliasing` / `smooth gradients`（像素风不能有）
- **`plain solid background`** —— 抠图的前提

### 2. 转成 RA sprite

```powershell
.\.venv\Scripts\python.exe .\tools\gen_to_sprite.py `
    gen_barracks.png `
    assets_raw\ra\temperat.pal `
    out\barracks_48.png `
    --size 48x48 --tol 45
```

做的事：检测四角背景色 → 抠成透明 → 裁主体 → 等比缩放到画布 → 量化到 RA 6-bit 调色板。

### 3. 替换素材 + 重建图集

```powershell
# 备份
Copy-Item assets_raw\ra\tent-0000.png assets_raw\ra\tent-0000.orig.png

# 替换（兵营只有 1 帧，只吃 tent-0000.png）
Copy-Item out\barracks_48.png assets_raw\ra\tent-0000.png -Force

# 重建
cd build
.\rts_import.exe D:\rts-engine\assets_raw\ra `
    D:\rts-engine\assets\sprites.txt `
    D:\rts-engine\assets\cnc_atlas.bmp `
    D:\rts-engine\assets\cnc_atlas.txt
```

**注意**：`rts_import.exe` 改了源码后必须重新 `build.bat`，否则会静默用旧版本产出旧图集。

### 4. 验证

```powershell
build\rts_game.exe --map D:\rts-engine\grid_map.map `
    --rules D:\dosbox\game\OpenRA\mods\ra\rules --autoshot
```

`--autoshot` 下相机是完全锁定的（不响应 WASD / 滚轮 / 边缘滚动），所以截到的就是开局视角。

## 结果

生成 → 抠图 → 48×48 → 引擎显示，全程没改引擎代码。截图见 `artifacts/closed_loop.png`。

## 已知限制

| 问题 | 说明 |
|---|---|
| **颜色偏亮** | AI 出的图比 RA 原画亮，量化后仍偏亮。`--brightness` 参数可以压，但没细调 |
| **尺寸损失** | 512×512 缩到 48×48 细节会丢，但建筑这种大件还能接受 |
| **tile 不能这么干** | 地形 24×24 用同样流程会塌成噪声（实测 119448 色 → 4 色），必须**切块**而不是缩小 |
| **多朝向没有** | 单帧建筑可以，单位的多方向动画不行 |

## 相关脚本

| 文件 | 用途 |
|---|---|
| `tools/comfy_test.ps1` | 提交工作流到 ComfyUI，带 LoRA 支持，会检查 `status.error` |
| `tools/gen_to_sprite.py` | 抠图 + 缩放 + 量化到 RA 调色板 |
| `tools/tile_quantize.py` | 地形 tile 量化（缩图方案，效果差） |
| `tools/tile_crop_test.py` | 对比缩图 vs 切块（结论：切块） |

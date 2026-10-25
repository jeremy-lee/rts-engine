# 从 SHP 帧 PNG 拼装图集（80x64 格，8 列 × 1 行）
# 用法：先按 assets_raw/README.md 生成帧 PNG，再运行本脚本
Add-Type -AssemblyName System.Drawing

$raw = Join-Path $PSScriptRoot '..\assets_raw'
$out = Join-Path $PSScriptRoot '..\assets\cnc_atlas.png'
$cellW = 80; $cellH = 64; $cols = 8

$frames = @(
    'e1-0000.png',   # 0 步兵
    'e3-0000.png',   # 1 火箭兵
    'mtnk-0000.png', # 2 坦克
    'ltnk-0000.png', # 3 轻坦
    'harv-0000.png', # 4 采矿车
    'pyle-0000.png', # 5 兵营
    'fact-0000.png'  # 6 建造厂
)

$atlas = New-Object System.Drawing.Bitmap(($cellW * $cols), $cellH)
$g = [System.Drawing.Graphics]::FromImage($atlas)
$g.Clear([System.Drawing.Color]::Transparent)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor

$i = 0
foreach ($f in $frames) {
    $path = Join-Path $raw $f
    if (-not (Test-Path $path)) { Write-Warning "缺少 $f"; $i++; continue }
    $img = [System.Drawing.Image]::FromFile($path)
    $x = ($i % $cols) * $cellW + [int](($cellW - $img.Width) / 2)
    $y = [int](($cellH - $img.Height) / 2)
    $g.DrawImage($img, $x, $y, $img.Width, $img.Height)
    $img.Dispose()
    $i++
}
$g.Dispose()
New-Item -ItemType Directory -Path (Split-Path $out) -Force | Out-Null
$atlas.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$atlas.Dispose()
Write-Host "图集已保存: $out"

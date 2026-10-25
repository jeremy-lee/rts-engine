# 生成 32 朝向图集（Phase 1: OpenRA 对齐）
# 布局（宽 64 列 x 80px = 5120）：
#   行 0: 2tnk 32 朝向 x 2 阵营（col = facing + team*32），车体(0-31)+炮塔(32-63) 合成
#   行 1: 1tnk 32 朝向 x 2 阵营（同上）
#   行 2-15: e1  8 朝向 x 7 帧 x 2 阵营（col = f + team*8, row = 2 + k*2 + team）
#   行 16-29: harv 8 朝向 x 7 帧 x 2 阵营
#   行 30-33: tent/weap x 2 阵营
Add-Type -AssemblyName System.Drawing

$srcDir = "D:\rts-engine\assets_raw\ra"
$outPath = "D:\rts-engine\assets\cnc_atlas.png"
$cellW = 80; $cellH = 64; $cols = 64; $rows = 34

function Load-Png([string]$name, [int]$frame) {
    $p = Join-Path $srcDir ("{0}-{1:D4}.png" -f $name, $frame)
    if (-not (Test-Path $p)) { return $null }
    return [System.Drawing.Bitmap]::FromFile($p)
}

function Crop-BBox($img) {
    if ($null -eq $img) { return $null }
    $minX = $img.Width; $maxX = -1; $minY = $img.Height; $maxY = -1
    for ($y = 0; $y -lt $img.Height; $y++) {
        for ($x = 0; $x -lt $img.Width; $x++) {
            if ($img.GetPixel($x, $y).A -gt 50) {
                if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($maxX -lt 0) { return $null }
    $w = $maxX - $minX + 1; $h = $maxY - $minY + 1
    $out = New-Object System.Drawing.Bitmap($w, $h)
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) { $out.SetPixel($x, $y, $img.GetPixel($minX + $x, $minY + $y)) }
    }
    return $out
}

function Composite($body, $turret) {
    if ($null -eq $body) { return $null }
    if ($null -eq $turret) { return $body }
    $out = New-Object System.Drawing.Bitmap($body.Width, $body.Height)
    for ($y = 0; $y -lt $body.Height; $y++) {
        for ($x = 0; $x -lt $body.Width; $x++) {
            $tc = $turret.GetPixel($x, $y)
            if ($tc.A -gt 50) { $out.SetPixel($x, $y, $tc) } else { $out.SetPixel($x, $y, $body.GetPixel($x, $y)) }
        }
    }
    return $out
}

function SetTeam($img, $team) {
    if ($null -eq $img) { return $null }
    $out = New-Object System.Drawing.Bitmap($img.Width, $img.Height)
    for ($y = 0; $y -lt $img.Height; $y++) {
        for ($x = 0; $x -lt $img.Width; $x++) {
            $c = $img.GetPixel($x, $y)
            if ($c.A -gt 0 -and $c.R -gt 60 -and $c.R -lt 120 -and $c.G -gt 200 -and $c.B -gt 60 -and $c.B -lt 120) {
                $out.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($c.A, $team.r, $team.g, $team.b))
            } else { $out.SetPixel($x, $y, $c) }
        }
    }
    return $out
}

function Place($g, $sprite, $col, $row) {
    if ($null -eq $sprite) { return }
    $dx = $col * $cellW + [int](($cellW - $sprite.Width) / 2)
    $dy = $row * $cellH + [int](($cellH - $sprite.Height) / 2)
    $g.DrawImage($sprite, $dx, $dy, $sprite.Width, $sprite.Height)
}

$teams = @(@{r=70;g=90;b=255}, @{r=235;g=60;b=60})
$atlas = New-Object System.Drawing.Bitmap(($cellW * $cols), ($cellH * $rows))
$g = [System.Drawing.Graphics]::FromImage($atlas)
$g.Clear([System.Drawing.Color]::Transparent)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor

# --- 坦克：32 朝向（idle 帧 0-31 车体 + 32-63 炮塔 合成），行 0=2tnk, 行 1=1tnk ---
$tanks = @("2tnk", "1tnk")
for ($ti = 0; $ti -lt 2; $ti++) {
    $name = $tanks[$ti]
    $row = $ti
    for ($team = 0; $team -lt 2; $team++) {
        for ($f = 0; $f -lt 32; $f++) {
            $body = Load-Png $name $f
            $turret = Load-Png $name (32 + $f)
            $composite = Composite $body $turret
            if ($null -ne $body) { $body.Dispose() }
            if ($null -ne $turret) { $turret.Dispose() }
            $crop = Crop-BBox $composite
            if ($null -ne $composite) { $composite.Dispose() }
            $teamImg = SetTeam $crop $teams[$team]
            if ($null -ne $crop) { $crop.Dispose() }
            Place $g $teamImg ($f + $team * 32) $row
            if ($null -ne $teamImg) { $teamImg.Dispose() }
        }
    }
    Write-Host "  坦克 $name (32 朝向) 完成"
}

# --- 步兵/矿车：8 朝向 x 7 帧 x 2 阵营 ---
$units = @("e1", "harv")
for ($ui = 0; $ui -lt 2; $ui++) {
    $name = $units[$ui]
    $baseRow = 2 + $ui * 14
    for ($k = 0; $k -lt 7; $k++) {
        for ($team = 0; $team -lt 2; $team++) {
            $row = $baseRow + $k * 2 + $team
            for ($f = 0; $f -lt 8; $f++) {
                $bodyFrame = if ($k -eq 0) { $f } else { 16 + $f * 6 + ($k - 1) }
                $img = Load-Png $name $bodyFrame
                $crop = Crop-BBox $img
                if ($null -ne $img) { $img.Dispose() }
                $teamImg = SetTeam $crop $teams[$team]
                if ($null -ne $crop) { $crop.Dispose() }
                Place $g $teamImg ($f + $team * 8) $row
                if ($null -ne $teamImg) { $teamImg.Dispose() }
            }
        }
    }
    Write-Host "  $name (8 朝向 x 7 帧) 完成"
}

# --- 建筑：行 30-33 ---
$bnames = @("tent", "weap")
for ($team = 0; $team -lt 2; $team++) {
    for ($b = 0; $b -lt 2; $b++) {
        $img = Load-Png $bnames[$b] 0
        $crop = Crop-BBox $img
        if ($null -ne $img) { $img.Dispose() }
        $teamImg = SetTeam $crop $teams[$team]
        if ($null -ne $crop) { $crop.Dispose() }
        Place $g $teamImg 0 (30 + $b * 2 + $team)
        if ($null -ne $teamImg) { $teamImg.Dispose() }
    }
}

$g.Dispose()
$atlas.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
$atlas.Dispose()
$kb = [Math]::Round((Get-Item $outPath).Length / 1KB)`nWrite-Host ("图集已生成: " + $outPath + " (" + $kb + " KB, " + $cols + " 列 x " + $rows + " 行)")

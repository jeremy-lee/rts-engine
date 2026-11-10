# Batch terrain tiles: generate a seamless texture per terrain type, then cut native
# 24x24 tiles out of it (never downscale - that collapses to noise).
#
# Usage: .\batch_terrain.ps1 [-Steps 25] [-Count 4]
param(
    [int]$Steps = 25,
    [int]$Count = 4
)

$ErrorActionPreference = "Continue"
$py = "D:\Comfy-Desktop\ComfyUI-Installs\Minimax\ComfyUI\.venv\Scripts\python.exe"
$comfyOut = "D:\Comfy-Desktop\ComfyUI-Installs\Minimax\ComfyUI\output"
$pal = "D:\rts-engine\assets_raw\ra\temperat.pal"
$outDir = "D:\rts-engine\artifacts\terrain"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

# name, prompt fragment, brightness, saturation
$kinds = @(
    @{ N = "grass";  P = "dark green grass, sparse dry patches, top-down";           B = 0.55; S = 0.65 },
    @{ N = "dirt";   P = "packed brown dirt and gravel, top-down";                    B = 0.6;  S = 0.55 },
    @{ N = "sand";   P = "pale desert sand with small stones, top-down";              B = 0.65; S = 0.4 },
    @{ N = "rock";   P = "grey rocky ground, cracked stone, top-down";                B = 0.6;  S = 0.35 },
    @{ N = "water";  P = "deep blue water surface with subtle ripples, top-down";     B = 0.7;  S = 0.75 },
    @{ N = "snow";   P = "white snow with faint blue shadows, top-down";              B = 0.9;  S = 0.25 }
)

$neg = "grid, lines, tiles, seams, pattern, objects, buildings, characters, shadows, gradients, 3d, photo, text, watermark, horizon, perspective"
Write-Host "terrain kinds: $($kinds.Count)"

$done = 0
foreach ($k in $kinds) {
    $name = $k.N
    Write-Host "--- $name ---"
    # NOTE: do NOT ask for "seamless tileable" - SD1.5 answers that with a faint grid.
    $prompt = "pixel art, " + $k.P + ", top-down view, 8-bit retro RTS game terrain, soft natural colour variation, flat lighting"
    $gen = & "D:\rts-engine\tools\comfy_test.ps1" -Ckpt "v1-5-pruned-emaonly-fp16.safetensors" `
        -Prompt $prompt -Negative $neg -Out ("ter_" + $name) `
        -Width 512 -Height 512 -Steps $Steps -Cfg 7.5 2>&1
    $imgLine = ($gen | Select-String -Pattern "IMAGE:") | Select-Object -First 1
    if (-not $imgLine) { Write-Host "  generation failed"; continue }
    $fname = ($imgLine.Line -replace '^IMAGE: ', '' -split ' ')[0]
    $src = Join-Path $comfyOut $fname
    if (-not (Test-Path $src)) { Write-Host "  output missing"; continue }

    $conv = & $py "D:\rts-engine\tools\terrain_tiles.py" $src $pal "$outDir\$name" `
        --tile 24 --count $Count --brightness $k.B --saturation $k.S --centre-only --flatten 2>&1
    $conv | Select-String -Pattern "tiles of|tile\d|wrote" | ForEach-Object { "  " + $_.Line }
    $done++
}

Write-Host ""
Write-Host "terrain sets built: $done / $($kinds.Count)"

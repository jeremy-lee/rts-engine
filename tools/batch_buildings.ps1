# Batch-generate C&C style buildings through the whole loop:
#   ComfyUI render -> gen_to_sprite.py -> RA-sized quantised sprite -> contact sheet
#
# Usage: .\batch_buildings.ps1 [-Steps 30] [-Cfg 8.0] [-Only "barracks,refinery"]
param(
    [int]$Steps = 30,
    [double]$Cfg = 8.0,
    [string]$Only = ""
)

$ErrorActionPreference = "Continue"

$py = "D:\Comfy-Desktop\ComfyUI-Installs\Minimax\ComfyUI\.venv\Scripts\python.exe"
$comfyOut = "D:\Comfy-Desktop\ComfyUI-Installs\Minimax\ComfyUI\output"
$outDir = "D:\rts-engine\artifacts\batch"
$pal = "D:\rts-engine\assets_raw\ra\temperat.pal"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

# name, description fed into the prompt, target canvas
$buildings = @(
    @{ N = "barracks";  D = "military barracks, red walls with metal trim and a low roof";       S = "48x48" },
    @{ N = "warfactory"; D = "heavy war factory, wide red building with a large vehicle door";     S = "72x48" },
    @{ N = "powerplant"; D = "power plant, red building with two tall cooling towers";             S = "48x48" },
    @{ N = "radardome"; D = "radar station with a large rotating dish on top";                     S = "48x48" },
    @{ N = "refinery";  D = "ore refinery, red industrial building with pipes and a loading dock"; S = "72x48" },
    @{ N = "conyard";   D = "construction yard, massive red building with a crane on top";         S = "72x48" },
    @{ N = "repairbay"; D = "vehicle repair bay, red building with an open service bay";           S = "48x48" },
    @{ N = "helipad";   D = "helipad, flat platform with an H marking and red border";             S = "48x48" },
    @{ N = "tower";     D = "defence gun tower, tall red turret with twin cannons";                S = "32x48" },
    @{ N = "silo";      D = "resource storage silo, round red tank with a metal cap";              S = "32x48" }
)

$sel = @()
if ($Only -ne "") {
    $want = $Only.Split(",") | ForEach-Object { $_.Trim() }
    foreach ($b in $buildings) { if ($want -contains $b.N) { $sel += $b } }
} else {
    $sel = $buildings
}

Write-Host "building $($sel.Count) assets"

$neg = "photorealistic, 3d render, blurry, anti-aliasing, smooth gradients, trees, grass, terrain, water, people, vehicles, multiple buildings, text, watermark, sky"
$results = @()

foreach ($b in $sel) {
    $name = $b.N
    Write-Host "--- $name ---"
    $prompt = "pixel art, isometric " + $b.D + ", Command and Conquer Red Alert style, single building centered in frame, plain solid magenta background, RTS game asset, limited palette, sharp pixels, crisp edges"

    $gen = & "D:\rts-engine\tools\comfy_test.ps1" -Ckpt "v1-5-pruned-emaonly-fp16.safetensors" `
        -Prompt $prompt -Negative $neg -Out ("batch_" + $name) `
        -Width 512 -Height 512 -Steps $Steps -Cfg $Cfg 2>&1

    $imgLine = ($gen | Select-String -Pattern "IMAGE:") | Select-Object -First 1
    if (-not $imgLine) {
        Write-Host "  GENERATION FAILED"
        $results += @{ N = $name; Ok = $false }
        continue
    }
    $fname = ($imgLine.Line -replace '^IMAGE: ', '' -split ' ')[0]
    $src = Join-Path $comfyOut $fname
    if (-not (Test-Path $src)) {
        Write-Host "  output missing: $src"
        $results += @{ N = $name; Ok = $false }
        continue
    }

    $dst = Join-Path $outDir ($name + "_sprite.png")
    $conv = & $py "D:\rts-engine\tools\gen_to_sprite.py" $src $pal $dst --size $b.S --tol 45 2>&1
    $okConv = Test-Path $dst
    $conv | Select-String -Pattern "background|bbox|wrote" | ForEach-Object { "  " + $_.Line }
    $results += @{ N = $name; Ok = $okConv; Path = $dst }
}

$okCount = ($results | Where-Object { $_.Ok }).Count
Write-Host ""
Write-Host "converted $okCount / $($sel.Count)"
$results | ForEach-Object { if (-not $_.Ok) { Write-Host "  FAILED: $($_.N)" } }

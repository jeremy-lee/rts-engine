# ComfyUI text-to-image test harness
# Usage: .\comfy_test.ps1 -Ckpt "model.safetensors" -Prompt "..." -Out "prefix" [-Width 512] [-Height 512] [-Steps 20] [-Cfg 7]
param(
    [Parameter(Mandatory=$true)][string]$Ckpt,
    [Parameter(Mandatory=$true)][string]$Prompt,
    [Parameter(Mandatory=$true)][string]$Out,
    [int]$Width = 512,
    [int]$Height = 512,
    [int]$Steps = 20,
    [double]$Cfg = 7.0,
    [int]$Seed = 42,
    [string]$Negative = "blurry, low quality, watermark, text, 3d render, photo",
    [string]$Server = "http://127.0.0.1:8189"
)

$ErrorActionPreference = "Stop"

# Minimal graph: CheckpointLoader -> 2x CLIPTextEncode -> EmptyLatent -> KSampler -> VAEDecode -> SaveImage
$json = @"
{
  "prompt": {
    "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "$Ckpt"}},
    "5": {"class_type": "EmptyLatentImage", "inputs": {"width": $Width, "height": $Height, "batch_size": 1}},
    "6": {"class_type": "CLIPTextEncode", "inputs": {"text": "$Prompt", "clip": ["4", 1]}},
    "7": {"class_type": "CLIPTextEncode", "inputs": {"text": "$Negative", "clip": ["4", 1]}},
    "3": {"class_type": "KSampler", "inputs": {
        "seed": $Seed, "steps": $Steps, "cfg": $Cfg,
        "sampler_name": "euler", "scheduler": "normal", "denoise": 1.0,
        "model": ["4", 0], "positive": ["6", 0], "negative": ["7", 0], "latent_image": ["5", 0]
    }},
    "8": {"class_type": "VAEDecode", "inputs": {"samples": ["3", 0], "vae": ["4", 2]}},
    "9": {"class_type": "SaveImage", "inputs": {"images": ["8", 0], "filename_prefix": "$Out"}}
  },
  "client_id": "rts-test"
}
"@

$resp = Invoke-RestMethod -Uri "$Server/prompt" -Method Post -Body $json -ContentType "application/json" -TimeoutSec 60
$promptId = $resp.prompt_id
Write-Host "submitted prompt_id=$promptId"

$deadline = (Get-Date).AddMinutes(8)
$done = $false
$failed = $false
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 3
    try {
        $h = Invoke-RestMethod -Uri "$Server/history/$promptId" -TimeoutSec 20
        if ($h.PSObject.Properties.Name -contains $promptId) {
            $entry = $h.$promptId
            # A prompt can appear in /history with status error and no images at all.
            # Checking only for images made this script print DONE for failures.
            $statusStr = $entry.status.status_str
            if ($statusStr -eq "error") {
                $failed = $true
                $errs = @($entry.status.messages | Where-Object { $_[0] -eq "execution_error" })
                foreach ($e in $errs) {
                    Write-Host "ERROR: $($e[1].exception_type): $($e[1].exception_message)"
                }
                break
            }
            $done = $true
            $outs = $entry.outputs
            foreach ($nodeId in $outs.PSObject.Properties.Name) {
                $imgs = $outs.$nodeId.images
                if ($imgs) {
                    foreach ($im in $imgs) {
                        Write-Host "IMAGE: $($im.filename) subfolder=$($im.subfolder) type=$($im.type)"
                    }
                }
            }
            break
        }
    } catch { }
}
if ($failed) { Write-Host "FAILED"; exit 1 }
if (-not $done) { Write-Host "TIMEOUT after 8 minutes"; exit 1 }
Write-Host "DONE"

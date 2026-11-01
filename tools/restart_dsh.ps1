# 重启 DSH —— 由 dsh 会话在后台触发
# 1. 等待几秒，让 agent 的回复先发出去
# 2. 关掉所有 DeepSeek Harness 进程（GUI + web server 都在里面）
# 3. 重新启动应用
# 日志写文件，便于排查启动失败

$log = "$env:TEMP\dsh-restart.log"
function Log($m) { "$(Get-Date -Format 'HH:mm:ss')  $m" | Out-File -FilePath $log -Append -Encoding utf8 }

Log "=== restart requested ==="
Start-Sleep -Seconds 4

$procs = Get-Process | Where-Object { $_.ProcessName -eq 'DeepSeek Harness' }
Log "found $($procs.Count) DeepSeek Harness process(es)"
$procs | ForEach-Object { Log "  pid=$($_.Id)" }
$procs | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

$exe = 'D:\DeepSeek Harness\DeepSeek Harness.exe'
if (Test-Path $exe) {
    Log "starting $exe"
    Start-Process -FilePath $exe
    Log "started"
} else {
    Log "ERROR: exe not found at $exe"
}

Log "=== done ==="

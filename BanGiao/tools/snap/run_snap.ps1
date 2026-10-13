param([string]$Out, [string]$Only = '', [string]$Model = 'DEFAULT', [int]$W = 1280, [int]$H = 976)
# Chạy chương trình vẽ rồi dọn đúng các tiến trình VisionMaster CON của nó (vServerApp, AwakenGpuTool) —
# không dọn thì chúng treo lại và giữ ShieldCanAssy.Wpf.exe trong bin.
$env:SNAP_MODEL = $Model
$exe = Join-Path $PSScriptRoot 'bin2\Snap.exe'
$argList = @("$W", "$H", "`"$Out`"")
if ($Only) { $argList += $Only }
$p = Start-Process -FilePath $exe -ArgumentList $argList -PassThru
$done = $p.WaitForExit(170000)
$snapId = $p.Id
$kids = Get-CimInstance Win32_Process | Where-Object { $_.ParentProcessId -eq $snapId -and ($_.Name -eq 'vServerApp.exe' -or $_.Name -eq 'AwakenGpuTool.exe') }
foreach ($k in $kids) { Stop-Process -Id $k.ProcessId -Force -Confirm:$false -ErrorAction SilentlyContinue; "dọn $($k.ProcessId) $($k.Name)" }
if (-not $done) { 'TIMEOUT'; Stop-Process -Id $snapId -Force -ErrorAction SilentlyContinue } else { 'exit ' + $p.ExitCode }

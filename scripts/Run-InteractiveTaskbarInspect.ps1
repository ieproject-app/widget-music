$ErrorActionPreference = 'Continue'

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$diagDir = Join-Path $root 'out\diagnostics'
New-Item -ItemType Directory -Path $diagDir -Force | Out-Null

$log = Join-Path $diagDir 'interactive-taskbar-inspect.txt'
$marker = Join-Path $diagDir 'interactive-taskbar-inspect.done'
$inspect = Join-Path $root 'scripts\Inspect-WidgetMusicTaskbar.ps1'

Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue

$header = [pscustomobject]@{
  Timestamp = Get-Date
  User = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
  SessionId = (Get-Process -Id $PID).SessionId
  Pid = $PID
  Root = $root
}

@(
  '=== SnipTune 10 interactive taskbar inspect ==='
  ($header | Format-List | Out-String)
  '=== Inspect output ==='
) | Set-Content -LiteralPath $log -Encoding UTF8

try {
  & $inspect *>&1 | Out-String | Add-Content -LiteralPath $log -Encoding UTF8
} catch {
  ('Inspect failed: ' + $_.Exception.Message) | Add-Content -LiteralPath $log -Encoding UTF8
}

('=== Done: ' + (Get-Date).ToString('o') + ' ===') | Add-Content -LiteralPath $log -Encoding UTF8
Set-Content -LiteralPath $marker -Value (Get-Date).ToString('o') -Encoding UTF8

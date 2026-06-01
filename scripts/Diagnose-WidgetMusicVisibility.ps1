param(
  [string]$Configuration = 'Release',
  [switch]$RunRegisterRestart
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$registerCmd = Join-Path $root 'scripts\Register-WidgetMusic.cmd'
$enableScript = Join-Path $root 'scripts\Enable-WidgetMusicTaskbar.ps1'
$inspectScript = Join-Path $root 'scripts\Inspect-WidgetMusicTaskbar.ps1'
$clsid = '{0E716D1F-3D3D-4A57-878D-A7DFC29D9115}'
$dllPath = Join-Path $root "out\$Configuration\x64\WidgetMusicDeskband.dll"

function Write-Section {
  param([string]$Title)
  Write-Host ''
  Write-Host "=== $Title ==="
}

Write-Host 'Widget Music Visibility Diagnostic'
Write-Host ("Root: " + $root)
Write-Host ("Configuration: " + $Configuration)

if ($RunRegisterRestart) {
  Write-Section 'Register Restart'
  if (-not (Test-Path -LiteralPath $registerCmd)) {
    Write-Host "[FAIL] Missing register script: $registerCmd"
  } else {
    cmd /c """$registerCmd"" $Configuration restart"
    Write-Host ("[INFO] Register exit code: " + $LASTEXITCODE)
  }
}

Write-Section 'Registration Check'
if (Test-Path -LiteralPath $dllPath) {
  Write-Host ("[OK] DLL exists: " + $dllPath)
} else {
  Write-Host ("[FAIL] DLL missing: " + $dllPath)
}

$clsidKey = "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32"
if (Test-Path -LiteralPath $clsidKey) {
  $inproc = (Get-ItemProperty -LiteralPath $clsidKey -ErrorAction SilentlyContinue).'(default)'
  if (-not $inproc) {
    $inproc = (Get-ItemProperty -LiteralPath $clsidKey -ErrorAction SilentlyContinue).PSObject.Properties['(default)'].Value
  }
  Write-Host ("[OK] CLSID registered: " + $clsid)
  if ($inproc) {
    Write-Host ("[INFO] InprocServer32: " + $inproc)
  }
} else {
  Write-Host ("[FAIL] CLSID key missing: " + $clsidKey)
}

Write-Section 'Explorer / Host Process'
$procs = Get-Process explorer, WidgetMusicHost -ErrorAction SilentlyContinue
if (-not $procs) {
  Write-Host '[FAIL] explorer / WidgetMusicHost process not found.'
} else {
  $procs | Select-Object ProcessName, Id, StartTime, Path | Format-Table -AutoSize
}

Write-Section 'Explorer Module Check'
try {
  $mods = Get-Process explorer -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Modules } |
    Where-Object { $_.ModuleName -match 'WidgetMusic|explorerframe|twinui' } |
    Select-Object ModuleName, FileName
  if ($mods) {
    $mods | Format-Table -AutoSize
  } else {
    Write-Host '[WARN] No related modules found in explorer module list.'
  }
} catch {
  Write-Host ("[WARN] Could not enumerate explorer modules: " + $_.Exception.Message)
}

Write-Section 'Enable Script Probe'
if (-not (Test-Path -LiteralPath $enableScript)) {
  Write-Host ("[FAIL] Missing enable script: " + $enableScript)
} else {
  & powershell -NoProfile -ExecutionPolicy Bypass -File $enableScript
  Write-Host ("[INFO] Enable script exit code: " + $LASTEXITCODE)
}

Write-Section 'Taskbar Inspect Probe'
if (-not (Test-Path -LiteralPath $inspectScript)) {
  Write-Host ("[WARN] Missing inspect script: " + $inspectScript)
} else {
  & powershell -NoProfile -ExecutionPolicy Bypass -File $inspectScript
  Write-Host ("[INFO] Inspect script exit code: " + $LASTEXITCODE)
}

Write-Section 'Done'
Write-Host 'Diagnostic complete.'

param(
  [string[]]$Files
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $Files -or $Files.Count -eq 0) {
  $dist = Join-Path $root 'out\dist\WidgetMusic'
  $Files = @(
    (Join-Path $dist 'WidgetMusicDeskband.dll'),
    (Join-Path $dist 'WidgetMusicHost.exe')
  )
}

function Find-DumpBin {
  if ($env:WIDGETMUSIC_DUMPBIN -and (Test-Path -LiteralPath $env:WIDGETMUSIC_DUMPBIN -PathType Leaf)) {
    return (Resolve-Path -LiteralPath $env:WIDGETMUSIC_DUMPBIN).Path
  }

  $fromPath = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
  if ($fromPath) {
    return $fromPath.Source
  }

  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'dumpbin.exe not found. Install Visual Studio Build Tools 2022 with Desktop development with C++, or set WIDGETMUSIC_DUMPBIN.'
  }

  $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vsInstall) {
    throw 'Visual Studio C++ tools not found. Install Visual Studio Build Tools 2022 with Desktop development with C++.'
  }

  $toolsRoot = Join-Path $vsInstall 'VC\Tools\MSVC'
  $preferred = Get-ChildItem -LiteralPath $toolsRoot -Recurse -Filter dumpbin.exe |
    Where-Object { $_.FullName -like '*\bin\Hostx64\x64\dumpbin.exe' } |
    Select-Object -First 1

  if ($preferred) {
    return $preferred.FullName
  }

  $fallback = Get-ChildItem -LiteralPath $toolsRoot -Recurse -Filter dumpbin.exe | Select-Object -First 1
  if ($fallback) {
    return $fallback.FullName
  }

  throw 'dumpbin.exe not found under the Visual Studio C++ tools installation.'
}

$dumpbin = Find-DumpBin
$blocked = @('MSVCP140.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll')
$failed = $false

foreach ($file in $Files) {
  $resolved = (Resolve-Path -LiteralPath $file).Path
  $output = & $dumpbin /dependents $resolved 2>&1
  if ($LASTEXITCODE -ne 0) {
    $output | ForEach-Object { Write-Host $_ }
    throw "dumpbin.exe failed for $resolved."
  }

  $found = @()
  foreach ($dll in $blocked) {
    if ($output -match [regex]::Escape($dll)) {
      $found += $dll
    }
  }

  if ($found.Count -gt 0) {
    $failed = $true
    Write-Host ("[Dependencies] FAIL: {0} imports {1}" -f $resolved, ($found -join ', '))
  } else {
    Write-Host ("[Dependencies] OK: {0} has no dynamic VC++ runtime imports." -f $resolved)
  }
}

if ($failed) {
  throw 'Runtime dependency check failed. Release binaries must not import MSVCP140.dll or VCRUNTIME140*.dll.'
}

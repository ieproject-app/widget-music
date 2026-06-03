param(
  [ValidateSet('Install', 'Uninstall', 'Start')]
  [string]$Action = 'Install',
  [string]$HostPath = '',
  [int]$StartupDelayMs = 15000
)

$ErrorActionPreference = 'Stop'

$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$valueName = 'SnipTune10Prewarm'
$sessionId = (Get-Process -Id $PID).SessionId

function Stop-SessionHost {
  Get-Process WidgetMusicHost -ErrorAction SilentlyContinue |
    Where-Object { $_.SessionId -eq $sessionId } |
    Stop-Process -Force -ErrorAction SilentlyContinue
}

function Resolve-HostPath {
  param([string]$Path)

  if (-not $Path) {
    throw 'HostPath is required.'
  }
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "WidgetMusicHost.exe was not found: $Path"
  }

  return (Resolve-Path -LiteralPath $Path).Path
}

function Start-PrewarmHost {
  param(
    [string]$Path,
    [int]$DelayMs
  )

  Start-Process -FilePath $Path `
    -ArgumentList @('--prewarm', "--startup-delay-ms=$DelayMs") `
    -WindowStyle Hidden `
    -ErrorAction SilentlyContinue
}

if ($StartupDelayMs -lt 0) {
  $StartupDelayMs = 0
}

switch ($Action) {
  'Install' {
    $resolvedHost = Resolve-HostPath -Path $HostPath
    New-Item -Path $runKey -Force | Out-Null
    $command = '"{0}" --prewarm --startup-delay-ms={1}' -f $resolvedHost, $StartupDelayMs
    New-ItemProperty -Path $runKey -Name $valueName -Value $command -PropertyType String -Force | Out-Null
    Stop-SessionHost
    Start-PrewarmHost -Path $resolvedHost -DelayMs 0
  }
  'Start' {
    $resolvedHost = Resolve-HostPath -Path $HostPath
    Start-PrewarmHost -Path $resolvedHost -DelayMs 0
  }
  'Uninstall' {
    if (Test-Path -LiteralPath $runKey) {
      Remove-ItemProperty -Path $runKey -Name $valueName -ErrorAction SilentlyContinue
    }
    Stop-SessionHost
  }
}

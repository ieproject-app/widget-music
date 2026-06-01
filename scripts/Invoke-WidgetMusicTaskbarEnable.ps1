param(
  [string]$EnableScriptPath = (Join-Path $PSScriptRoot 'Enable-WidgetMusicTaskbar.ps1'),
  [int]$TimeoutSeconds = 8
)

$ErrorActionPreference = 'Stop'
$WarningPreference = 'SilentlyContinue'

if ($TimeoutSeconds -lt 1) {
  $TimeoutSeconds = 1
}

if (-not (Test-Path -LiteralPath $EnableScriptPath)) {
  Write-Host ("[Enable] Missing enable script: " + $EnableScriptPath)
  exit 1
}

$psExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
if (Test-Path (Join-Path $env:SystemRoot 'Sysnative\WindowsPowerShell\v1.0\powershell.exe')) {
  $psExe = Join-Path $env:SystemRoot 'Sysnative\WindowsPowerShell\v1.0\powershell.exe'
}

try {
  $job = Start-Job -ScriptBlock {
    param(
      [string]$WorkerPsExe,
      [string]$WorkerScriptPath
    )

    $lines = @()
    $exitCode = 1
    try {
      $lines = & $WorkerPsExe -NoProfile -ExecutionPolicy Bypass -File $WorkerScriptPath 2>&1 | ForEach-Object { $_.ToString() }
      $exitCode = $LASTEXITCODE
    } catch {
      $lines += ("[Enable] Worker failed: " + $_.Exception.Message)
    }

    [PSCustomObject]@{
      ExitCode = $exitCode
      Lines = $lines
    }
  } -ArgumentList $psExe, $EnableScriptPath

  $completed = Wait-Job -Id $job.Id -Timeout $TimeoutSeconds
  if (-not $completed) {
    Stop-Job -Id $job.Id -ErrorAction SilentlyContinue
    Remove-Job -Id $job.Id -Force -ErrorAction SilentlyContinue
    Write-Host ("[Enable] Timed out after " + $TimeoutSeconds + "s while waiting for taskbar confirmation. Continuing without blocking.")
    exit 2
  }

  $result = Receive-Job -Id $job.Id -ErrorAction SilentlyContinue | Select-Object -Last 1
  Remove-Job -Id $job.Id -Force -ErrorAction SilentlyContinue

  if ($result -and $result.Lines) {
    foreach ($line in $result.Lines) {
      Write-Host $line
    }
  }

  if (-not $result) {
    Write-Host '[Enable] Wrapper failed: empty worker result.'
    exit 1
  }

  exit ([int]$result.ExitCode)
} catch {
  Write-Host ("[Enable] Wrapper failed: " + $_.Exception.Message)
  exit 1
}

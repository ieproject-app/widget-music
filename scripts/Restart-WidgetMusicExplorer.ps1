param(
  [string]$RegistrationCommand = '',
  [string]$RegistrationArgumentsText = '',
  [switch]$StopHost
)

$ErrorActionPreference = 'Stop'
$sessionId = (Get-Process -Id $PID).SessionId
$exitCode = 0
$killer = $null

function Stop-SessionProcess {
  param([string]$Name)

  Get-Process $Name -ErrorAction SilentlyContinue |
    Where-Object { $_.SessionId -eq $sessionId } |
    Stop-Process -Force -ErrorAction SilentlyContinue
}

function Ensure-SessionExplorer {
  if ($sessionId -eq 0) {
    return
  }

  for ($attempt = 0; $attempt -lt 24; $attempt++) {
    if (Get-Process explorer -ErrorAction SilentlyContinue |
        Where-Object { $_.SessionId -eq $sessionId }) {
      return
    }
    Start-Process explorer.exe -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 350
  }

  Start-Process explorer.exe -ErrorAction SilentlyContinue
}

try {
  if ($sessionId -ne 0) {
    $killer = Start-Job -ArgumentList $sessionId, $StopHost.IsPresent -ScriptBlock {
      param($targetSessionId, $stopHostProcess)
      while ($true) {
        Get-Process explorer -ErrorAction SilentlyContinue |
          Where-Object { $_.SessionId -eq $targetSessionId } |
          Stop-Process -Force -ErrorAction SilentlyContinue
        if ($stopHostProcess) {
          Get-Process WidgetMusicHost -ErrorAction SilentlyContinue |
            Where-Object { $_.SessionId -eq $targetSessionId } |
            Stop-Process -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Milliseconds 100
      }
    }
  }

  if ($StopHost) {
    Stop-SessionProcess -Name 'WidgetMusicHost'
  }
  Start-Sleep -Milliseconds 500

  if ($RegistrationCommand) {
    [string[]]$arguments = @()
    if ($RegistrationArgumentsText) {
      $arguments = [string[]]($RegistrationArgumentsText -split '\|')
    }
    & $RegistrationCommand @arguments
    if ($null -ne $LASTEXITCODE) {
      $exitCode = $LASTEXITCODE
    }
  }
} catch {
  Write-Error $_
  $exitCode = 1
} finally {
  if ($killer) {
    Stop-Job $killer -ErrorAction SilentlyContinue
    Remove-Job $killer -Force -ErrorAction SilentlyContinue
  }
  Ensure-SessionExplorer
}

exit $exitCode

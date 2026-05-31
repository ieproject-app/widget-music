param(
  [int]$Tail = 80
)

$ErrorActionPreference = 'Stop'

function Read-WidgetMusicLog {
  param(
    [string]$Name,
    [int]$Tail
  )

  $path = Join-Path $env:TEMP $Name
  Write-Host "--- $Name ---"
  if (-not (Test-Path -LiteralPath $path)) {
    Write-Host "Missing: $path"
    return
  }

  $bytes = [System.IO.File]::ReadAllBytes($path)
  if ($bytes.Length -eq 0) {
    Write-Host 'Empty log.'
    return
  }

  $text = [System.Text.Encoding]::Unicode.GetString($bytes)
  $lines = [System.Text.RegularExpressions.Regex]::Split($text, "\r?\n")
  $lines | Where-Object { $_ -ne '' } | Select-Object -Last $Tail
}

Read-WidgetMusicLog -Name 'WidgetMusicDeskband.log' -Tail $Tail
Read-WidgetMusicLog -Name 'WidgetMusicHost.log' -Tail $Tail

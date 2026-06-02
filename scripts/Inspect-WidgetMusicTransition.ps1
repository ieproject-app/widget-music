param(
  [int]$SampleIntervalMs = 12,
  [int]$ObserveMs = 360
)

$ErrorActionPreference = 'Stop'

if (-not ('WidgetMusicTransitionWin32' -as [type])) {
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class WidgetMusicTransitionWin32 {
  public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);

  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern IntPtr FindWindow(string className, string windowName);

  [DllImport("user32.dll")]
  public static extern bool EnumChildWindows(IntPtr hwndParent, EnumProc callback, IntPtr lParam);

  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetClassName(IntPtr hwnd, StringBuilder className, int maxCount);

  [DllImport("user32.dll")]
  public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

  [DllImport("user32.dll")]
  public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern bool SystemParametersInfo(uint action, uint uiParam, out bool enabled, uint flags);

  [StructLayout(LayoutKind.Sequential)]
  public struct RECT {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
  }
}
'@
}

$compactWidth = 132
$fullWidth = 300
$wmCommand = 0x0111
$menuViewCompact = 0x5101
$menuViewFull = 0x5102
$spiGetClientAreaAnimation = 0x1042

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$diagDir = Join-Path $root 'out\diagnostics'
New-Item -ItemType Directory -Path $diagDir -Force | Out-Null
$samplesPath = Join-Path $diagDir 'widget-transition-samples.csv'
$summaryPath = Join-Path $diagDir 'widget-transition-summary.txt'

$tray = [WidgetMusicTransitionWin32]::FindWindow('Shell_TrayWnd', $null)
if ($tray -eq [IntPtr]::Zero) {
  throw 'Shell_TrayWnd was not found.'
}

$script:widget = [IntPtr]::Zero
[WidgetMusicTransitionWin32]::EnumChildWindows($tray, {
  param($hwnd, $lParam)
  $className = New-Object System.Text.StringBuilder 128
  [void][WidgetMusicTransitionWin32]::GetClassName($hwnd, $className, $className.Capacity)
  if ($className.ToString() -eq 'WidgetMusicDeskbandWindow') {
    $script:widget = $hwnd
    return $false
  }
  return $true
}, [IntPtr]::Zero) | Out-Null

if ($script:widget -eq [IntPtr]::Zero) {
  throw 'WidgetMusicDeskbandWindow was not found. Enable SnipTune 10 from the taskbar Toolbars menu first.'
}

$animationEnabled = $true
if (-not [WidgetMusicTransitionWin32]::SystemParametersInfo(
    $spiGetClientAreaAnimation, 0, [ref]$animationEnabled, 0)) {
  $animationEnabled = $true
}

$samples = New-Object System.Collections.Generic.List[object]

function Send-WidgetMode {
  param([int]$Command)
  [void][WidgetMusicTransitionWin32]::SendMessage($script:widget, $wmCommand, [IntPtr]$Command, [IntPtr]::Zero)
}

function Add-WidgetSample {
  param(
    [string]$Phase,
    [long]$ElapsedMs
  )
  $rect = New-Object WidgetMusicTransitionWin32+RECT
  if (-not [WidgetMusicTransitionWin32]::GetWindowRect($script:widget, [ref]$rect)) {
    throw 'Could not read the widget rectangle.'
  }
  $samples.Add([pscustomobject]@{
    Phase = $Phase
    ElapsedMs = $ElapsedMs
    Left = $rect.Left
    Right = $rect.Right
    Width = $rect.Right - $rect.Left
    Height = $rect.Bottom - $rect.Top
  })
}

function Capture-WidgetSamples {
  param(
    [string]$Phase,
    [int]$DurationMs
  )
  $watch = [Diagnostics.Stopwatch]::StartNew()
  while ($watch.ElapsedMilliseconds -le $DurationMs) {
    Add-WidgetSample -Phase $Phase -ElapsedMs $watch.ElapsedMilliseconds
    Start-Sleep -Milliseconds $SampleIntervalMs
  }
  Add-WidgetSample -Phase $Phase -ElapsedMs $watch.ElapsedMilliseconds
}

Send-WidgetMode -Command $menuViewCompact
Start-Sleep -Milliseconds ($ObserveMs + 80)
Add-WidgetSample -Phase 'CompactBaseline' -ElapsedMs 0

Send-WidgetMode -Command $menuViewFull
Capture-WidgetSamples -Phase 'Expand' -DurationMs $ObserveMs

Send-WidgetMode -Command $menuViewCompact
Capture-WidgetSamples -Phase 'Collapse' -DurationMs $ObserveMs

Send-WidgetMode -Command $menuViewFull
Capture-WidgetSamples -Phase 'ReverseOutbound' -DurationMs 72
Add-WidgetSample -Phase 'ReverseBefore' -ElapsedMs 0
$beforeReverse = $samples[$samples.Count - 1]
Send-WidgetMode -Command $menuViewCompact
Add-WidgetSample -Phase 'ReverseAfter' -ElapsedMs 0
$afterReverse = $samples[$samples.Count - 1]
Capture-WidgetSamples -Phase 'ReverseReturn' -DurationMs $ObserveMs

$samples | Export-Csv -LiteralPath $samplesPath -NoTypeInformation -Encoding UTF8

$expandWidths = @($samples | Where-Object Phase -eq 'Expand' | Select-Object -ExpandProperty Width -Unique)
$collapseWidths = @($samples | Where-Object Phase -eq 'Collapse' | Select-Object -ExpandProperty Width -Unique)
$intermediateExpand = @($expandWidths | Where-Object { $_ -gt $compactWidth -and $_ -lt $fullWidth })
$intermediateCollapse = @($collapseWidths | Where-Object { $_ -gt $compactWidth -and $_ -lt $fullWidth })
$reverseReturnWidths = @($samples | Where-Object Phase -eq 'ReverseReturn' | Select-Object -ExpandProperty Width)
$rightEdges = @($samples | Select-Object -ExpandProperty Right)
$rightEdgeDrift = ($rightEdges | Measure-Object -Maximum).Maximum - ($rightEdges | Measure-Object -Minimum).Minimum
$fullReached = $expandWidths -contains $fullWidth
$fullSettled = $samples |
               Where-Object Phase -eq 'Expand' |
               Select-Object -Last 1 -ExpandProperty Width
$fullSettled = $fullSettled -eq $fullWidth
$compactReached = $collapseWidths -contains $compactWidth
$reverseStartsBetweenEndpoints = $afterReverse -and
                                  $afterReverse.Width -gt $compactWidth -and
                                  $afterReverse.Width -lt $fullWidth
$reverseMovesTowardCompact = $reverseReturnWidths.Count -gt 0 -and
                             @($reverseReturnWidths | Where-Object { $_ -gt $afterReverse.Width }).Count -eq 0 -and
                             $reverseReturnWidths -contains $compactWidth
$reverseNoSnap = $reverseStartsBetweenEndpoints -and $reverseMovesTowardCompact
$hasIntermediateFrames = $intermediateExpand.Count -gt 0 -and $intermediateCollapse.Count -gt 0
$passed = $fullReached -and $fullSettled -and $compactReached -and ($rightEdgeDrift -le 1) -and
          ((-not $animationEnabled) -or ($hasIntermediateFrames -and $reverseNoSnap))

@(
  '=== SnipTune 10 compact/full transition inspect ==='
  ('Timestamp=' + (Get-Date).ToString('o'))
  ('AnimationEnabled=' + $animationEnabled)
  ('FullReached=' + $fullReached)
  ('FullSettled=' + $fullSettled)
  ('CompactReached=' + $compactReached)
  ('IntermediateExpandWidths=' + ($intermediateExpand -join ','))
  ('IntermediateCollapseWidths=' + ($intermediateCollapse -join ','))
  ('RightEdgeDriftPx=' + $rightEdgeDrift)
  ('ReverseBeforeWidth=' + $beforeReverse.Width)
  ('ReverseAfterWidth=' + $afterReverse.Width)
  ('ReverseReturnWidths=' + (($reverseReturnWidths | Select-Object -Unique) -join ','))
  ('ReverseNoSnap=' + $reverseNoSnap)
  ('SamplesCsv=' + $samplesPath)
  ('Passed=' + $passed)
) | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Get-Content -LiteralPath $summaryPath
if (-not $passed) {
  exit 1
}

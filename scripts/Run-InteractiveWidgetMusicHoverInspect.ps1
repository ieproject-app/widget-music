$ErrorActionPreference = 'Continue'

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$src = @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class WidgetMusicHoverWin32 {
  public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);

  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern IntPtr FindWindow(string className, string windowName);

  [DllImport("user32.dll")]
  public static extern bool EnumChildWindows(IntPtr hwndParent, EnumProc callback, IntPtr lParam);

  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetClassName(IntPtr hwnd, StringBuilder className, int maxCount);

  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int maxCount);

  [DllImport("user32.dll")]
  public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

  [DllImport("user32.dll")]
  public static extern bool IsWindowVisible(IntPtr hwnd);

  [DllImport("user32.dll")]
  public static extern bool SetCursorPos(int x, int y);

  [StructLayout(LayoutKind.Sequential)]
  public struct RECT {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
  }
}
'@

Add-Type $src

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$diagDir = Join-Path $root 'out\diagnostics'
New-Item -ItemType Directory -Path $diagDir -Force | Out-Null

$log = Join-Path $diagDir 'interactive-widget-hover-inspect.txt'
$marker = Join-Path $diagDir 'interactive-widget-hover-inspect.done'
Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue

function Get-ClassName {
  param([IntPtr]$Hwnd)
  $sb = New-Object System.Text.StringBuilder 256
  [void][WidgetMusicHoverWin32]::GetClassName($Hwnd, $sb, $sb.Capacity)
  $sb.ToString()
}

function Get-WindowTextValue {
  param([IntPtr]$Hwnd)
  $sb = New-Object System.Text.StringBuilder 256
  [void][WidgetMusicHoverWin32]::GetWindowText($Hwnd, $sb, $sb.Capacity)
  $sb.ToString()
}

function Get-ChildRows {
  param([IntPtr]$Parent)
  $rows = New-Object System.Collections.Generic.List[object]
  [WidgetMusicHoverWin32]::EnumChildWindows($Parent, {
    param($hwnd, $lParam)
    $class = Get-ClassName -Hwnd $hwnd
    if ($class -match 'Widget|ReBar|Tray|Toolbar|MSTask|Notify|Clock|Input|ShowDesktop') {
      $rect = New-Object WidgetMusicHoverWin32+RECT
      [void][WidgetMusicHoverWin32]::GetWindowRect($hwnd, [ref]$rect)
      $rows.Add([pscustomobject]@{
        Class = $class
        Hwnd = $hwnd
        Visible = [WidgetMusicHoverWin32]::IsWindowVisible($hwnd)
        Rect = "$($rect.Left),$($rect.Top),$($rect.Right),$($rect.Bottom)"
        Size = "$($rect.Right - $rect.Left)x$($rect.Bottom - $rect.Top)"
        Text = Get-WindowTextValue -Hwnd $hwnd
      })
    }
    return $true
  }, [IntPtr]::Zero) | Out-Null
  $rows
}

function Find-WidgetRow {
  param($Rows)
  $Rows | Where-Object { $_.Class -eq 'WidgetMusicDeskbandWindow' } | Select-Object -First 1
}

function Save-TaskbarShot {
  param([string]$Name)
  $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $taskbarHeight = [Math]::Min(96, [Math]::Max(40, [int]($bounds.Height * 0.12)))
  $captureRect = [System.Drawing.Rectangle]::new($bounds.Left, $bounds.Bottom - $taskbarHeight, $bounds.Width, $taskbarHeight)
  $bmp = [System.Drawing.Bitmap]::new($captureRect.Width, $captureRect.Height)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($captureRect.Location, [System.Drawing.Point]::Empty, $captureRect.Size)
  $g.Dispose()
  $path = Join-Path $diagDir $Name
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  $path
}

@(
  '=== Widget Music interactive hover inspect ==='
  ('Timestamp=' + (Get-Date).ToString('o'))
  ('User=' + [System.Security.Principal.WindowsIdentity]::GetCurrent().Name)
  ('SessionId=' + (Get-Process -Id $PID).SessionId)
) | Set-Content -LiteralPath $log -Encoding UTF8

$tray = [WidgetMusicHoverWin32]::FindWindow('Shell_TrayWnd', $null)
("Shell_TrayWnd=$tray") | Add-Content -LiteralPath $log -Encoding UTF8

if ($tray -eq [IntPtr]::Zero) {
  'Shell_TrayWnd not found.' | Add-Content -LiteralPath $log -Encoding UTF8
  Set-Content -LiteralPath $marker -Value (Get-Date).ToString('o') -Encoding UTF8
  exit 2
}

$beforeRows = Get-ChildRows -Parent $tray
'--- Before hover ---' | Add-Content -LiteralPath $log -Encoding UTF8
($beforeRows | Sort-Object Class, Rect | Format-Table -AutoSize | Out-String) | Add-Content -LiteralPath $log -Encoding UTF8
$beforeShot = Save-TaskbarShot -Name 'taskbar-widget-before-hover.png'
("BeforeScreenshot=$beforeShot") | Add-Content -LiteralPath $log -Encoding UTF8

$widget = Find-WidgetRow -Rows $beforeRows
if ($widget) {
  $parts = $widget.Rect -split ','
  $left = [int]$parts[0]
  $top = [int]$parts[1]
  $right = [int]$parts[2]
  $bottom = [int]$parts[3]
  $cx = [int](($left + $right) / 2)
  $cy = [int](($top + $bottom) / 2)
  $titleX = $left + 4
  $titleY = $top + 2
  ("MovingCursorToTitleZone=$titleX,$titleY") | Add-Content -LiteralPath $log -Encoding UTF8
  [void][WidgetMusicHoverWin32]::SetCursorPos($titleX, $titleY)
  Start-Sleep -Milliseconds 1600
  $titleRows = Get-ChildRows -Parent $tray
  '--- After title popup hover ---' | Add-Content -LiteralPath $log -Encoding UTF8
  ($titleRows | Sort-Object Class, Rect | Format-Table -AutoSize | Out-String) | Add-Content -LiteralPath $log -Encoding UTF8
  $titleShot = Save-TaskbarShot -Name 'taskbar-widget-after-title-popup.png'
  ("TitlePopupScreenshot=$titleShot") | Add-Content -LiteralPath $log -Encoding UTF8

  ("MovingCursorToPlayButton=$cx,$cy") | Add-Content -LiteralPath $log -Encoding UTF8
  [void][WidgetMusicHoverWin32]::SetCursorPos($cx, $cy)
  Start-Sleep -Milliseconds 1100
}

$afterRows = Get-ChildRows -Parent $tray
'--- After play button tooltip hover ---' | Add-Content -LiteralPath $log -Encoding UTF8
($afterRows | Sort-Object Class, Rect | Format-Table -AutoSize | Out-String) | Add-Content -LiteralPath $log -Encoding UTF8
$afterShot = Save-TaskbarShot -Name 'taskbar-widget-after-button-tooltip.png'
("ButtonTooltipScreenshot=$afterShot") | Add-Content -LiteralPath $log -Encoding UTF8

[void][WidgetMusicHoverWin32]::SetCursorPos(12, 12)
Start-Sleep -Seconds 10
$collapsedRows = Get-ChildRows -Parent $tray
'--- After idle away ---' | Add-Content -LiteralPath $log -Encoding UTF8
($collapsedRows | Sort-Object Class, Rect | Format-Table -AutoSize | Out-String) | Add-Content -LiteralPath $log -Encoding UTF8
$idleShot = Save-TaskbarShot -Name 'taskbar-widget-after-idle-away.png'
("IdleScreenshot=$idleShot") | Add-Content -LiteralPath $log -Encoding UTF8

('=== Done: ' + (Get-Date).ToString('o') + ' ===') | Add-Content -LiteralPath $log -Encoding UTF8
Set-Content -LiteralPath $marker -Value (Get-Date).ToString('o') -Encoding UTF8

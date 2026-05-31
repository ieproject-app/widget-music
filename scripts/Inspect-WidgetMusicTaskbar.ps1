Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$src = @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class WidgetMusicWin32 {
  public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);

  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern IntPtr FindWindow(string className, string windowName);

  [DllImport("user32.dll")]
  public static extern bool EnumWindows(EnumProc callback, IntPtr lParam);

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

$tray = [WidgetMusicWin32]::FindWindow('Shell_TrayWnd', $null)
Write-Host "Shell_TrayWnd=$tray"

$rows = New-Object System.Collections.Generic.List[object]

function Add-InterestingWindowRow {
  param(
    [IntPtr]$Hwnd,
    [string]$Scope
  )

  $className = New-Object System.Text.StringBuilder 256
  $text = New-Object System.Text.StringBuilder 256
  [void][WidgetMusicWin32]::GetClassName($Hwnd, $className, $className.Capacity)
  [void][WidgetMusicWin32]::GetWindowText($Hwnd, $text, $text.Capacity)
  $rect = New-Object WidgetMusicWin32+RECT
  [void][WidgetMusicWin32]::GetWindowRect($Hwnd, [ref]$rect)
  $name = $className.ToString()
  if ($name -match 'Widget|Deskband|Toolbar|ReBar|Tray|Notify|MSTask|Worker|Band') {
    $rows.Add([pscustomobject]@{
      Scope = $Scope
      Class = $name
      Hwnd = $Hwnd
      Visible = [WidgetMusicWin32]::IsWindowVisible($Hwnd)
      Rect = "$($rect.Left),$($rect.Top),$($rect.Right),$($rect.Bottom)"
      Size = "$($rect.Right - $rect.Left)x$($rect.Bottom - $rect.Top)"
      Text = $text.ToString()
    })
  }
}

function Add-InterestingChildRows {
  param(
    [IntPtr]$Parent,
    [string]$Scope
  )

  [WidgetMusicWin32]::EnumChildWindows($Parent, {
    param($hwnd, $lParam)
    Add-InterestingWindowRow -Hwnd $hwnd -Scope $Scope
    return $true
  }, [IntPtr]::Zero) | Out-Null
}

if ($tray -ne [IntPtr]::Zero) {
  Add-InterestingChildRows -Parent $tray -Scope 'Shell_TrayWnd'
} else {
  [WidgetMusicWin32]::EnumWindows({
    param($hwnd, $lParam)
    Add-InterestingWindowRow -Hwnd $hwnd -Scope 'TopLevel'
    Add-InterestingChildRows -Parent $hwnd -Scope 'TopLevelChild'
    return $true
  }, [IntPtr]::Zero) | Out-Null
}

if ($rows.Count -gt 0) {
  $rows | Sort-Object Class, Rect | Format-Table -AutoSize
} else {
  Write-Host 'No taskbar/widget windows found from this automation session.'
}

$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$taskbarHeight = [Math]::Min(96, [Math]::Max(40, [int]($bounds.Height * 0.12)))
$captureRect = [System.Drawing.Rectangle]::new($bounds.Left, $bounds.Bottom - $taskbarHeight, $bounds.Width, $taskbarHeight)
try {
  $bmp = [System.Drawing.Bitmap]::new($captureRect.Width, $captureRect.Height)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($captureRect.Location, [System.Drawing.Point]::Empty, $captureRect.Size)
  $g.Dispose()
  $shot = Join-Path $diagDir 'taskbar-crop.png'
  $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  Write-Host "Screenshot=$shot"
} catch {
  Write-Host "Screenshot unavailable: $($_.Exception.Message)"
}

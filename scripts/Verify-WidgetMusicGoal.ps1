param(
  [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$deskbandPath = Join-Path $root 'WidgetMusicDeskband\src\Deskband.cpp'
$hostPath = Join-Path $root 'WidgetMusicHost\src\main.cpp'
$deskbandDll = Join-Path $root "out\$Configuration\x64\WidgetMusicDeskband.dll"
$hostExe = Join-Path $root "out\$Configuration\x64\WidgetMusicHost.exe"
$distDir = Join-Path $root 'out\dist\WidgetMusic'
$distDll = Join-Path $distDir 'WidgetMusicDeskband.dll'
$distHost = Join-Path $distDir 'WidgetMusicHost.exe'
$distRegister = Join-Path $distDir 'Register-WidgetMusic.cmd'
$distUnregister = Join-Path $distDir 'Unregister-WidgetMusic.cmd'

$deskband = Get-Content -Raw -Path $deskbandPath
$hostSource = Get-Content -Raw -Path $hostPath
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure {
  param([string]$Message)
  $script:failures.Add($Message)
}

function Assert-MatchText {
  param(
    [string]$Name,
    [string]$Text,
    [string]$Pattern
  )

  if ($Text -notmatch $Pattern) {
    Add-Failure $Name
  } else {
    Write-Host "[OK] $Name"
  }
}

function Assert-NotMatchText {
  param(
    [string]$Name,
    [string]$Text,
    [string]$Pattern
  )

  if ($Text -match $Pattern) {
    Add-Failure $Name
  } else {
    Write-Host "[OK] $Name"
  }
}

function Assert-FileExists {
  param([string]$Name, [string]$Path)

  if (-not (Test-Path -LiteralPath $Path)) {
    Add-Failure "$Name missing: $Path"
    return
  }

  $item = Get-Item -LiteralPath $Path
  Write-Host ("[OK] {0}: {1} bytes, {2}" -f $Name, $item.Length, $item.LastWriteTime)
}

function Assert-Condition {
  param([string]$Name, [bool]$Condition)

  if (-not $Condition) {
    Add-Failure $Name
  } else {
    Write-Host "[OK] $Name"
  }
}

Assert-FileExists 'Deskband DLL output' $deskbandDll
Assert-FileExists 'Host EXE output' $hostExe
Assert-FileExists 'Clean package deskband DLL' $distDll
Assert-FileExists 'Clean package host EXE' $distHost
Assert-FileExists 'Clean package register script' $distRegister
Assert-FileExists 'Clean package unregister script' $distUnregister

if (Test-Path -LiteralPath $distDir) {
  $distFiles = Get-ChildItem -LiteralPath $distDir -Recurse -File
  $distSize = ($distFiles | Measure-Object Length -Sum).Sum
  Assert-Condition 'clean package is below 1 MB' ($distSize -lt 1MB)
  Assert-Condition 'clean package does not include PDB files' (-not ($distFiles | Where-Object { $_.Extension -ieq '.pdb' }))
  Assert-Condition 'clean package does not include intermediate files' (-not (Test-Path -LiteralPath (Join-Path $distDir 'intermediate')))
}

Assert-MatchText 'compact mode width is taskbar-toolbar sized' $deskband 'constexpr\s+int\s+kBandCompactWidth\s*=\s*132;'
Assert-MatchText 'compact title reveal duration is defined' $deskband 'constexpr\s+DWORD\s+kCompactTitleRevealMs\s*=\s*3200;'
Assert-MatchText 'startup host delay is 7 seconds' $deskband 'constexpr\s+DWORD\s+kStartupPipeDelayMs\s*=\s*7000;'
Assert-MatchText 'round control size is defined' $deskband 'constexpr\s+int\s+kRoundButtonSize\s*=\s*32;'
Assert-MatchText 'play visual circle is smaller than hit target' $deskband 'constexpr\s+int\s+kPlayVisualSize\s*=\s*28;'
Assert-MatchText 'play ring is visually lighter' $deskband 'constexpr\s+float\s+kPlayRingWidth\s*=\s*1\.5f;'
Assert-MatchText 'side glyphs use compact vector size' $deskband 'constexpr\s+int\s+kSideGlyphSize\s*=\s*19;'
Assert-MatchText 'marquee uses speed-based native timing' $deskband 'constexpr\s+int\s+kMarqueeSpeedPxPerSec\s*=\s*40;'
Assert-MatchText 'marquee caps delayed frames' $deskband 'constexpr\s+DWORD\s+kMarqueeMaxFrameMs\s*=\s*48;'
Assert-NotMatchText 'old fixed-pixel marquee tick removed' $deskband 'kMarqueePixelsPerTick'
Assert-NotMatchText 'old auto-hide animation constants removed' $deskband 'kAutoHide|AutoHide|AnimationProgressPermille|Collapsing|Expanding'
Assert-MatchText 'startup erase paints taskbar background immediately' $deskband '(?s)case\s+WM_ERASEBKGND:.*?PaintImmediateBackground\(hwnd,\s*reinterpret_cast<HDC>\(wp\)\)'
Assert-MatchText 'deskband starts in compact mode by default' $deskband '_bandMode\s*=\s*BandDisplayMode::Compact'
Assert-MatchText 'display mode update uses official band info notification' $deskband '(?s)void\s+SetDisplayMode\(BandDisplayMode\s+nextMode\).*?_bandMode\s*=\s*nextMode;.*?NotifyBandInfoChanged\(\).*?ApplyCurrentBandSize\(\)'
Assert-MatchText 'right-click context menu opens display mode menu' $deskband '(?s)case\s+WM_RBUTTONUP:.*?ShowModeContextMenu\(pt\.x,\s*pt\.y\).*?case\s+WM_CONTEXTMENU:.*?ShowModeContextMenu\(sx,\s*sy\)'
Assert-MatchText 'context menu exposes compact and full entries' $deskband '(?s)void\s+ShowModeContextMenu\(.*?AppendMenuW\(menu,\s*compactFlags,\s*kMenuViewCompact,\s*L"Compact view"\).*?AppendMenuW\(menu,\s*fullFlags,\s*kMenuViewFull,\s*L"Full view"\)'
Assert-MatchText 'resize keeps right edge anchored' $deskband '(?s)void\s+ApplyCurrentBandSize\(\).*?MapWindowPoints\(HWND_DESKTOP,\s*parent,\s*pts,\s*2\).*?pts\[1\]\.x\s*-\s*targetWidth'
Assert-MatchText 'compact mode can reveal track title on change' $deskband '(?s)void\s+OnStateUpdated\(\).*?IsCompactMode\(\).*?primary\s*!=\s*_lastPrimaryText.*?StartCompactTitleReveal\(primary\)'
Assert-MatchText 'compact title reveal uses animated custom title card' $deskband '(?s)void\s+ShowCompactTitlePopup\(.*?SplitTitleCardText.*?StartTitleCardAnimation\(232\)'
Assert-MatchText 'title popup is suppressed after click to avoid blocking controls' $deskband 'kTitleSuppressAfterClickMs'
Assert-MatchText 'full mode renders seek track and hover thumb' $deskband '(?s)IsFullMode\(\)\s*&&\s*_seekRc\.right\s*>\s*_seekRc\.left.*?_seekHover'
Assert-NotMatchText 'mode chevron button removed from deskband surface' $deskband '_btnMode|drawModeGlyph|kModeGlyphSize|Switch compact/full view'
Assert-MatchText 'deskband controls require an actionable session' $deskband '(?s)const\s+bool\s+actionableMedia\s*=\s*s\.connected\s*&&\s*s\.has_session;.*?_btnPlayPause\.enabled\s*=\s*actionableMedia\s*&&\s*s\.can_play_pause'
Assert-MatchText 'optimistic play/pause is blocked without actionable media' $deskband '(?s)std::string\s+OptimisticPlayPauseTarget\(\).*?!_state\.connected\s*\|\|\s*!_state\.has_session\s*\|\|\s*!_state\.can_play_pause'
Assert-MatchText 'hide path stops pipe client' $deskband '(?s)IFACEMETHODIMP\s+ShowDW\(BOOL\s+fShow\).*?else\s*\{.*?StopPipeClient\(true\)'
Assert-NotMatchText 'old collapsed visual path removed' $deskband 'Collapsed|collapsed|StartCollapse|ExpandFromUser|drawRevealButton|IsRevealOnly'

Assert-MatchText 'host no-client timeout is 8 seconds' $hostSource 'constexpr\s+DWORD\s+kPipeNoClientTimeoutMs\s*=\s*8000;'
Assert-MatchText 'host exits when no deskband connects' $hostSource '(?s)WAIT_TIMEOUT.*?Pipe connect timeout; host exiting.*?SetEvent\(_stopEvent\)'
Assert-MatchText 'host exits when pipe client disconnects' $hostSource '(?s)Pipe client disconnected.*?SetEvent\(_stopEvent\)'
Assert-MatchText 'Media Player window fallback does not enable fake controls' $hostSource '(?s)auto\s+tryMediaPlayerWindowFallback.*?out\.can_prev\s*=\s*false;.*?out\.can_next\s*=\s*false;.*?out\.can_play_pause\s*=\s*false;'
Assert-MatchText 'host fallback media keys require an actionable target' $hostSource '(?s)allowFallbackMediaKey.*?TryReadMediaPlayerNowPlayingFromUIA.*?if\s*\(allowFallbackMediaKey\s*&&\s*\(IsTrackCommand\(name\)\s*\|\|\s*allowPlaybackFallback\)\)'

if ($failures.Count -gt 0) {
  Write-Host ''
  Write-Host 'Verification failed:'
  foreach ($failure in $failures) {
    Write-Host " - $failure"
  }
  exit 1
}

Write-Host ''
Write-Host 'Widget Music goal invariants passed.'

param(
  [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$failures = New-Object System.Collections.Generic.List[string]

function Read-Source {
  param([string]$RelativePath)
  Get-Content -Raw -LiteralPath (Join-Path $root $RelativePath)
}

function Add-Failure {
  param([string]$Message)
  $script:failures.Add($Message)
}

function Assert-Condition {
  param([string]$Name, [bool]$Condition)
  if ($Condition) {
    Write-Host "[OK] $Name"
  } else {
    Add-Failure $Name
  }
}

function Assert-File {
  param([string]$Name, [string]$Path)
  Assert-Condition "$Name exists" (Test-Path -LiteralPath $Path -PathType Leaf)
}

function Assert-Match {
  param([string]$Name, [string]$Text, [string]$Pattern)
  Assert-Condition $Name ($Text -match $Pattern)
}

function Assert-NoMatch {
  param([string]$Name, [string]$Text, [string]$Pattern)
  Assert-Condition $Name ($Text -notmatch $Pattern)
}

function Assert-SameHash {
  param([string]$Name, [string]$Source, [string]$Packaged)
  if (-not (Test-Path -LiteralPath $Source -PathType Leaf) -or
      -not (Test-Path -LiteralPath $Packaged -PathType Leaf)) {
    Add-Failure "$Name cannot be compared because a file is missing"
    return
  }
  $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Source).Hash
  $packagedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Packaged).Hash
  Assert-Condition "$Name source and dist hashes match" ($sourceHash -eq $packagedHash)
}

$deskband = Read-Source 'WidgetMusicDeskband\src\Deskband.cpp'
$accessibility = Read-Source 'WidgetMusicDeskband\src\Accessibility.h'
$hostSource = Read-Source 'WidgetMusicHost\src\main.cpp'
$protocol = Read-Source 'shared\WidgetMusicProtocol.h'
$visual = Read-Source 'shared\WidgetMusicVisual.h'
$package = Read-Source 'scripts\Package-WidgetMusic.cmd'
$restart = Read-Source 'scripts\Restart-WidgetMusicExplorer.ps1'
$register = Read-Source 'scripts\Register-WidgetMusic.cmd'
$install = Read-Source 'scripts\Install-WidgetMusic.cmd'
$unregister = Read-Source 'scripts\Unregister-WidgetMusic.cmd'
$uninstall = Read-Source 'scripts\Uninstall-WidgetMusic.cmd'
$deskbandProject = Read-Source 'WidgetMusicDeskband\WidgetMusicDeskband.vcxproj'
$hostProjectFile = Read-Source 'WidgetMusicHost\WidgetMusicHost.vcxproj'
$installer = Read-Source 'installer\WidgetMusic.iss'
$buildInstaller = Read-Source 'scripts\Build-Installer.cmd'
$dependencyCheck = Read-Source 'scripts\Check-RuntimeDependencies.ps1'
$workflow = Read-Source '.github\workflows\windows-ci.yml'
$releaseWorkflow = Read-Source '.github\workflows\windows-release.yml'

$buildDir = Join-Path $root "out\$Configuration\x64"
$distDir = Join-Path $root 'out\dist\SnipTune10'
$dll = Join-Path $buildDir 'WidgetMusicDeskband.dll'
$hostExe = Join-Path $buildDir 'WidgetMusicHost.exe'
$distDll = Join-Path $distDir 'WidgetMusicDeskband.dll'
$distHost = Join-Path $distDir 'WidgetMusicHost.exe'
$sums = Join-Path $distDir 'SHA256SUMS.txt'

Assert-File 'Deskband build DLL' $dll
Assert-File 'Host build EXE' $hostExe
Assert-File 'Packaged deskband DLL' $distDll
Assert-File 'Packaged host EXE' $distHost
Assert-File 'Package checksum manifest' $sums
Assert-File 'Package version marker' (Join-Path $distDir 'VERSION.txt')
Assert-File 'Packaged restart helper' (Join-Path $distDir 'Restart-WidgetMusicExplorer.ps1')
Assert-SameHash 'Deskband DLL' $dll $distDll
Assert-SameHash 'Host EXE' $hostExe $distHost

if (Test-Path -LiteralPath $distDir -PathType Container) {
  $distFiles = Get-ChildItem -LiteralPath $distDir -Recurse -File
  $distBytes = ($distFiles | Measure-Object Length -Sum).Sum
  Assert-Condition 'runtime package stays below 1 MB' ($distBytes -lt 1MB)
  Assert-Condition 'runtime package excludes PDB files' (-not ($distFiles | Where-Object Extension -ieq '.pdb'))
  Assert-Condition 'runtime package excludes intermediate output' (-not (Test-Path -LiteralPath (Join-Path $distDir 'intermediate')))
}

if (Test-Path -LiteralPath $sums -PathType Leaf) {
  $sumLines = Get-Content -LiteralPath $sums
  $expectedFiles = Get-ChildItem -LiteralPath $distDir -File | Where-Object Name -ne 'SHA256SUMS.txt'
  Assert-Condition 'checksum manifest covers every packaged file' ($sumLines.Count -eq $expectedFiles.Count)
  foreach ($file in $expectedFiles) {
    $expected = '{0}  {1}' -f (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant(), $file.Name
    Assert-Condition "checksum matches $($file.Name)" ($sumLines -contains $expected)
  }
}

if (Test-Path -LiteralPath $dll -PathType Leaf) {
  Assert-Condition 'Deskband binary version is 1.0.1.0' ((Get-Item -LiteralPath $dll).VersionInfo.FileVersion -eq '1.0.1.0')
}
if (Test-Path -LiteralPath $hostExe -PathType Leaf) {
  Assert-Condition 'Host binary version is 1.0.1.0' ((Get-Item -LiteralPath $hostExe).VersionInfo.FileVersion -eq '1.0.1.0')
}

Assert-Match 'full mode remains progress-first' $deskband '(?s)BuildPrimaryText\(const BandState& s\).*?IsFullMode\(\).*?BuildProgressText\(s,\s*now\).*?return progress'
Assert-Match 'progress timer repaints text and seek union' $deskband '(?s)OnProgressTimer\(\).*?RECT dirty = _seekRc;.*?UnionRect\(&dirty,\s*&dirty,\s*&_textRc\)'
Assert-Match 'taskbar surface sampling is preferred before DWM fallback' $deskband '(?s)COLORREF sampled = SampleAdjacentTaskbarColor\(hwnd,\s*CLR_INVALID\);.*?COLORREF dwmColor = GetTaskbarColorViaDWM\(\);.*?sampled != CLR_INVALID \? sampled'
Assert-Match 'taskbar surface uses robust median color' $deskband 'widgetmusic::MedianColor\(samples,\s*fallback\)'
Assert-NoMatch 'dormant marquee path is removed' $deskband '(?i)marquee'
Assert-NoMatch 'display-only progress bar has no seek hover affordance' $deskband '(?i)seekHover'
Assert-Match 'title popup clamps to active monitor' $deskband '(?s)MonitorFromWindow\(_hwnd,\s*MONITOR_DEFAULTTONEAREST\).*?const int above.*?const int below'
Assert-Match 'keyboard path handles arrows and activation keys' $deskband '(?s)case WM_KEYDOWN:.*?OnKeyDown.*?VK_LEFT.*?VK_RIGHT.*?VK_RETURN.*?VK_SPACE'
Assert-Match 'deskband publishes MSAA through WM_GETOBJECT' $deskband '(?s)case WM_GETOBJECT:.*?OBJID_CLIENT.*?LresultFromObject\(IID_IAccessible'
Assert-Match 'deskband emits accessibility state events' $deskband 'NotifyWinEvent\(EVENT_OBJECT_STATECHANGE'
Assert-Match 'focus ring uses Windows focus drawing' $deskband 'DrawFocusRect\(mem,\s*&focusRc\)'
Assert-Match 'mouse activation suppresses visual focus ring' $deskband '(?s)OnMouseDown.*?FocusAccessibleButton\(focused,\s*false\).*?OnMouseUp.*?InvokeButton\(widgetmusic::kAccessiblePlayPause,\s*false\)'
Assert-Match 'keyboard activation keeps visual focus ring' $deskband '(?s)OnKeyDown.*?FocusAccessibleButton\(next,\s*true\).*?InvokeButton\(_focusedButton,\s*true\)'
Assert-Match 'MSAA exposes three virtual children' $accessibility 'kAccessibleButtonCount = 3'
Assert-Match 'MSAA exposes push-button roles' $accessibility 'ROLE_SYSTEM_PUSHBUTTON'

Assert-Match 'pipe path is session scoped' $protocol 'WidgetMusic\.Pipe\.v1\.Session\.'
Assert-Match 'shared payload cap is defined' $protocol 'kMaxPipeMessageBytes = 16 \* 1024'
Assert-Match 'shared metadata caps are defined' $protocol 'kMaxTitleChars = 256'
Assert-Match 'host uses logon SID group for pipe ACL' $hostSource 'SE_GROUP_LOGON_ID'
Assert-Match 'pipe ACL creation fails closed' $hostSource '(?s)if\s*\(!MakePipeSecurity\(&sa,\s*&sd\)\).*?refusing insecure fallback.*?return INVALID_HANDLE_VALUE'
Assert-Match 'pipe rejects remote clients' $hostSource 'PIPE_REJECT_REMOTE_CLIENTS'
Assert-Match 'host session pointer has dedicated mutex' $hostSource 'std::mutex _sessionMu'
Assert-Match 'command path snapshots session under mutex' $hostSource '(?s)ExecuteCommand\(.*?lock\(_sessionMu\).*?session = _session'
Assert-Match 'session replacement occurs under mutex' $hostSource '(?s)SetSession\(.*?lock\(_sessionMu\).*?_session = s'
Assert-Match 'teardown clears pending command queue' $hostSource '(?s)void Stop\(\).*?_commandQueue\.clear\(\)'
Assert-Match 'deskband requires protocol hello before state' $deskband '(?s)if\s*\(!_helloValidated\).*?Pipe state rejected before hello handshake'
Assert-Match 'deskband validates protocol version' $deskband 'IsSupportedProtocolVersion\(version\)'
Assert-Match 'host clamps state metadata' $hostSource 'ClampProtocolText'
Assert-Match 'deskband clamps received metadata defensively' $deskband 'ClampProtocolText'
Assert-Match 'deskband rotates logs above 512 KB' $deskband 'kMaxLogBytes = 512 \* 1024'
Assert-Match 'host rotates logs above 512 KB' $hostSource 'kMaxLogBytes = 512 \* 1024'

Assert-Match 'packager copies scoped Explorer restart helper' $package 'Restart-WidgetMusicExplorer\.ps1'
Assert-Match 'packager writes VERSION.txt' $package 'VERSION\.txt'
Assert-Match 'packager writes SHA256SUMS.txt' $package 'SHA256SUMS\.txt'
Assert-Match 'deskband Release links static VC runtime' $deskbandProject '(?s)Release\|x64.*?<RuntimeLibrary>MultiThreaded</RuntimeLibrary>'
Assert-Match 'host Release links static VC runtime' $hostProjectFile '(?s)Release\|x64.*?<RuntimeLibrary>MultiThreaded</RuntimeLibrary>'
Assert-Match 'installer targets per-user LocalAppData' $installer 'DefaultDirName=\{localappdata\}\\SnipGeek\\SnipTune 10'
Assert-Match 'installer links publisher to SnipGeek website' $installer 'AppPublisherURL=https://snipgeek\.com'
Assert-Match 'installer is limited to x64 Windows' $installer 'ArchitecturesAllowed=x64os'
Assert-Match 'installer registers deskband after install' $installer 'Register-WidgetMusic\.cmd"; Parameters: "restart auto"'
Assert-Match 'installer unregisters deskband during uninstall' $installer 'Unregister-WidgetMusic\.cmd"; Parameters: "restart"'
Assert-Match 'installer unregisters existing install before update' $installer '(?s)PrepareToInstall.*?Unregister-WidgetMusic\.cmd.*?Exec\(UnregisterScript,\s*''restart'''
Assert-Match 'installer output filename is stable' $installer 'OutputBaseFilename=SnipTune10Setup-\{#MyAppVersion\}-x64'
Assert-Match 'installer build invokes runtime packager' $buildInstaller 'Package-WidgetMusic\.cmd'
Assert-Match 'installer build checks runtime dependencies' $buildInstaller 'Check-RuntimeDependencies\.ps1'
Assert-Match 'installer build reports missing Inno Setup' $buildInstaller 'Inno Setup 6 was not found'
Assert-Match 'dependency checker defaults to branded runtime package' $dependencyCheck 'out\\dist\\SnipTune10'
Assert-Match 'dependency checker blocks MSVCP140' $dependencyCheck 'MSVCP140\.dll'
Assert-Match 'dependency checker blocks VCRUNTIME140' $dependencyCheck 'VCRUNTIME140_1?\.dll'
Assert-Match 'workflow checks runtime dependencies' $workflow 'Check-RuntimeDependencies\.ps1'
Assert-Match 'workflow can upload installer artifact' $workflow 'SnipTune10Setup-\*\.exe'
Assert-Match 'release workflow runs on version tags' $releaseWorkflow 'tags:\s*(?s).*?v\*\.\*\.\*'
Assert-Match 'release workflow installs Inno Setup' $releaseWorkflow 'choco install innosetup'
Assert-Match 'release workflow builds installer' $releaseWorkflow 'Build-Installer\.cmd Release'
Assert-Match 'release workflow creates runtime zip' $releaseWorkflow 'SnipTune10-\$version-runtime\.zip'
Assert-Match 'release workflow creates checksum asset' $releaseWorkflow 'SHA256SUMS\.txt'
Assert-Match 'release workflow publishes draft release' $releaseWorkflow '(?s)gh release create.*?--draft'
Assert-Match 'restart helper scopes Explorer operations by session' $restart '(?s)\$sessionId = \(Get-Process -Id \$PID\)\.SessionId.*?Where-Object \{ \$_.SessionId -eq \$sessionId \}'
Assert-Match 'restart helper scopes host shutdown by session' $restart 'Stop-SessionProcess -Name ''WidgetMusicHost'''
foreach ($script in @(
    @{ Name = 'register'; Text = $register },
    @{ Name = 'install'; Text = $install },
    @{ Name = 'unregister'; Text = $unregister },
    @{ Name = 'uninstall'; Text = $uninstall }
  )) {
  Assert-Match "$($script.Name) uses scoped restart helper" $script.Text 'Restart-WidgetMusicExplorer\.ps1'
  Assert-NoMatch "$($script.Name) contains no global Explorer stop" $script.Text 'Stop-Process\s+-Name\s+explorer'
}

Assert-File '.gitattributes' (Join-Path $root '.gitattributes')
Assert-File 'Windows CI workflow' (Join-Path $root '.github\workflows\windows-ci.yml')
Assert-File 'Windows release workflow' (Join-Path $root '.github\workflows\windows-release.yml')
Assert-File 'lightweight tests executable' (Join-Path $root "out\$Configuration\x64\WidgetMusicTests.exe")
Assert-File 'canonical final audit' (Join-Path $root 'docs\Audit-Final-1-Juni-2026.md')

if ($failures.Count -gt 0) {
  Write-Host ''
  Write-Host 'Verification failed:'
  foreach ($failure in $failures) {
    Write-Host " - $failure"
  }
  exit 1
}

Write-Host ''
Write-Host 'SnipTune 10 final invariants passed.'

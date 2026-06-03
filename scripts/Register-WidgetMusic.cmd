@echo off
setlocal enableextensions

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "ACTION=%~2"

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul || exit /b 1

set "PS=%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"
if exist "%SystemRoot%\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe" set "PS=%SystemRoot%\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe"
set "RESTART_HELPER=%ROOT%\scripts\Restart-WidgetMusicExplorer.ps1"
set "PREWARM_HELPER=%ROOT%\scripts\Configure-WidgetMusicPrewarm.ps1"

if /i "%ACTION%"=="restart" (
  "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%RESTART_HELPER%" -RegistrationCommand "%~f0" -RegistrationArgumentsText "%CONFIG%|norestart" -StopHost
  set "ERR=%ERRORLEVEL%"
  popd >nul
  exit /b %ERR%
)

call "%ROOT%\\scripts\\Build.cmd" %CONFIG%
if errorlevel 1 (
  echo [Register] Build failed.
  popd >nul
  exit /b 1
)

set "OUTDIR=%ROOT%\\out\\%CONFIG%\\x64"
set "DLL=%OUTDIR%\\WidgetMusicDeskband.dll"
set "HOST=%OUTDIR%\\WidgetMusicHost.exe"

if not exist "%DLL%" (
  echo [Register] Deskband DLL not found: "%DLL%"
  popd >nul
  exit /b 1
)
if not exist "%HOST%" (
  echo [Register] Host EXE not found: "%HOST%"
  popd >nul
  exit /b 1
)

set "REGSVR=%SystemRoot%\\System32\\regsvr32.exe"
if exist "%SystemRoot%\\Sysnative\\regsvr32.exe" set "REGSVR=%SystemRoot%\\Sysnative\\regsvr32.exe"

echo [Register] Registering "%DLL%" (per-user)...
"%REGSVR%" /s "%DLL%"
if errorlevel 1 (
  echo [Register] regsvr32 failed.
  popd >nul
  exit /b 1
)

echo [Register] Enabling SnipTune 10 prewarm for this user...
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%PREWARM_HELPER%" -Action Install -HostPath "%HOST%" -StartupDelayMs 15000
if errorlevel 1 (
  echo [Register] prewarm setup failed.
  popd >nul
  exit /b 1
)

echo [Register] Done.
echo [Register] Enable manually from Taskbar ^> Toolbars ^> SnipTune 10.
popd >nul
exit /b 0

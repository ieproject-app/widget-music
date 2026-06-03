@echo off
setlocal enableextensions

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "ACTION=%~2"

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul || exit /b 1

set "OUTDIR=%ROOT%\\out\\%CONFIG%\\x64"
set "DLL=%OUTDIR%\\WidgetMusicDeskband.dll"

if not exist "%DLL%" (
  echo [Unregister] Deskband DLL not found: "%DLL%"
  popd >nul
  exit /b 1
)

set "REGSVR=%SystemRoot%\\System32\\regsvr32.exe"
if exist "%SystemRoot%\\Sysnative\\regsvr32.exe" set "REGSVR=%SystemRoot%\\Sysnative\\regsvr32.exe"

set "PS=%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"
if exist "%SystemRoot%\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe" set "PS=%SystemRoot%\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe"
set "RESTART_HELPER=%ROOT%\\scripts\\Restart-WidgetMusicExplorer.ps1"
set "PREWARM_HELPER=%ROOT%\\scripts\\Configure-WidgetMusicPrewarm.ps1"

echo [Unregister] Unregistering "%DLL%" (per-user)...
"%REGSVR%" /s /u "%DLL%"
if errorlevel 1 (
  echo [Unregister] regsvr32 failed.
  popd >nul
  exit /b 1
)

if exist "%PREWARM_HELPER%" (
  echo [Unregister] Disabling SnipTune 10 prewarm...
  "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%PREWARM_HELPER%" -Action Uninstall >nul 2>nul
)

if /i "%ACTION%"=="restart" (
  echo [Unregister] Restarting Explorer...
  "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%RESTART_HELPER%" -StopHost >nul 2>nul
)

echo [Unregister] Done.
popd >nul
exit /b 0
